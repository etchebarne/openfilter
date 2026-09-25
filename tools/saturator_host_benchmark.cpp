#include "Parameters.hpp"
#include <algorithm>
#include <chrono>
#include <clap/clap.h>
#include <cmath>
#include <ctime>
#include <dlfcn.h>
#include <iostream>
#include <memory>
#include <vector>
using namespace openfilter::saturator;
extern thread_local bool realtime;
static double cpuTime() {
    timespec t{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
struct Events {
    std::array<clap_event_param_value, parameterCount> values{};
    unsigned count = 0;
    clap_input_events input{
        this, [](const clap_input_events *e) { return static_cast<Events *>(e->ctx)->count; },
        [](const clap_input_events *e, uint32_t i) -> const clap_event_header * {
            auto &s = *static_cast<Events *>(e->ctx);
            return i < s.count ? &s.values[i].header : nullptr;
        }};
    void add(unsigned id, double value, unsigned time = 0) {
        auto &e = values[count++];
        e = {};
        e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = id;
        e.value = value;
        e.note_id = e.port_index = e.channel = e.key = -1;
    }
};
int main(int argc, char **argv) {
    if (argc < 2)
        return 1;
    const unsigned rate = argc > 2 ? std::stoul(argv[2]) : 48000;
    const unsigned block = argc > 3 ? std::stoul(argv[3]) : 64;
    const unsigned seconds = argc > 4 ? std::stoul(argv[4]) : 30;
    const unsigned instances = argc > 5 ? std::stoul(argv[5]) : 1;
    const std::string mode = argc > 6 ? argv[6] : "default";
    if (rate < 1000 || rate > 768000 || !block || block > 4096 || !seconds || seconds > 600 ||
        !instances || instances > 16)
        return 1;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        std::cerr << dlerror() << '\n';
        return 2;
    }
    const auto *entry = static_cast<const clap_plugin_entry *>(dlsym(library, "clap_entry"));
    if (!entry || !entry->init(argv[1]))
        return 3;
    const auto *factory =
        static_cast<const clap_plugin_factory *>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory)
        return 4;
    clap_host host{CLAP_VERSION,
                   nullptr,
                   "Saturator callback measurement",
                   "OpenFilter",
                   "",
                   "1",
                   [](const clap_host *, const char *) -> const void * { return nullptr; },
                   [](const clap_host *) {},
                   [](const clap_host *) {},
                   [](const clap_host *) {}};
    std::vector<const clap_plugin *> plugins;
    for (unsigned n = 0; n < instances; ++n) {
        const auto *p = factory->create_plugin(factory, &host, "org.openfilter.saturator");
        if (!p || !p->init(p))
            return 5;
        const auto *params =
            static_cast<const clap_plugin_params *>(p->get_extension(p, CLAP_EXT_PARAMS));
        Events initial;
        if (mode == "dense" || mode == "automated")
            for (unsigned b = 0; b < 3; ++b) {
                initial.add(band(b, Drive), 24);
                initial.add(band(b, Dynamics), 60);
                initial.add(band(b, Bass), 6);
                initial.add(band(b, Presence), -3);
            }
        params->flush(p, &initial.input, nullptr);
        if (mode == "mono") {
            const auto *config = static_cast<const clap_plugin_audio_ports_config *>(
                p->get_extension(p, CLAP_EXT_AUDIO_PORTS_CONFIG));
            if (!config || !config->select(p, 1))
                return 6;
        }
        if (!p->activate(p, rate, 1, block) || !p->start_processing(p))
            return 6;
        plugins.push_back(p);
    }
    std::array<double, 4096> left{}, right{};
    double *channels[]{left.data(), right.data()};
    clap_audio_buffer bus{nullptr, channels, mode == "mono" ? 1u : 2u, 0, 0};
    clap_output_events output{
        nullptr, [](const clap_output_events *, const clap_event_header *) { return true; }};
    Events events;
    clap_process process{};
    process.audio_inputs = process.audio_outputs = &bus;
    process.audio_inputs_count = process.audio_outputs_count = 1;
    process.in_events = &events.input;
    process.out_events = &output;
    std::vector<double> wall, cpu;
    wall.reserve(size_t(rate) * seconds / block + 1);
    cpu.reserve(wall.capacity());
    double checksum = 0;
    for (unsigned at = 0; at < rate * seconds; at += block) {
        process.steady_time = at;
        process.frames_count = std::min(block, rate * seconds - at);
        events.count = 0;
        if (mode == "automated") {
            const double phase = std::sin(at / double(rate));
            const unsigned offset = process.frames_count / 2;
            events.add(Input, phase * 12, offset);
            events.add(CrossoverLow, 250 + phase * 150, offset);
            events.add(CrossoverHigh, 6000 + phase * 3000, offset);
            for (unsigned b = 0; b < 3; ++b) {
                events.add(band(b, Drive), 18 + phase * 15, offset);
                events.add(band(b, Style), (at / block) % 4, offset);
                events.add(band(b, Dynamics), phase * 90, offset);
                for (unsigned t = 0; t < 4; ++t)
                    events.add(band(b, Bass + t), phase * 10, offset);
            }
        }
        double blockCpu = 0, blockWall = 0;
        for (const auto *p : plugins) {
            for (unsigned n = 0; n < process.frames_count; ++n) {
                left[n] = mode == "silence"
                              ? 0
                              : .2 * std::sin((at + n) * .071) + .1 * std::sin((at + n) * .173);
                right[n] = mode == "silence" ? 0 : .2 * std::cos((at + n) * .121);
            }
            const auto before = std::chrono::steady_clock::now();
            const double cpuBefore = cpuTime();
            realtime = true;
            const auto status = p->process(p, &process);
            realtime = false;
            blockCpu += (cpuTime() - cpuBefore) * 1e6;
            blockWall +=
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - before)
                    .count();
            if (status == CLAP_PROCESS_ERROR)
                return 7;
            for (unsigned n = 0; n < process.frames_count; ++n) {
                if (!std::isfinite(left[n]) || !std::isfinite(right[n]))
                    return 8;
                checksum += left[n] * left[n] + (mode == "mono" ? 0 : right[n] * right[n]);
            }
        }
        wall.push_back(blockWall);
        cpu.push_back(blockCpu);
    }
    double total = 0;
    for (auto t : cpu)
        total += t;
    std::sort(wall.begin(), wall.end());
    std::sort(cpu.begin(), cpu.end());
    std::cout << "{\"rate\":" << rate << ",\"block\":" << block << ",\"seconds\":" << seconds
              << ",\"instances\":" << instances << ",\"mode\":\"" << mode
              << "\",\"cpu_percent\":" << total / (seconds * 10000.)
              << ",\"cpu_p99_us\":" << cpu[size_t(cpu.size() * .99)]
              << ",\"cpu_max_us\":" << cpu.back()
              << ",\"wall_p99_us\":" << wall[size_t(wall.size() * .99)]
              << ",\"wall_max_us\":" << wall.back() << ",\"wall_deadline_exceedances\":"
              << std::count_if(wall.begin(), wall.end(),
                               [&](double t) { return t > 1e6 * block / rate; })
              << ",\"checksum\":" << checksum << "}\n";
    for (const auto *p : plugins) {
        p->stop_processing(p);
        p->deactivate(p);
        p->destroy(p);
    }
    entry->deinit();
    dlclose(library);
}
