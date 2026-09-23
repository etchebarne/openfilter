#include "Engine.hpp"
#include "Parameters.hpp"
#include "Test.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cairo.h>
#include <clap/clap.h>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <new>
#include <openfilter/plugin/Stream.hpp>
#include <openfilter/plugin/TripleBuffer.hpp>
#include <thread>
#include <vector>

extern thread_local bool realtime;

using namespace openfilter;
using Event = clap_event_param_value;
struct Events {
    std::vector<Event> values;
    clap_input_events input{
        this,
        [](const clap_input_events *e) -> uint32_t {
            return static_cast<uint32_t>(static_cast<Events *>(e->ctx)->values.size());
        },
        [](const clap_input_events *e, uint32_t i) -> const clap_event_header * {
            auto &v = static_cast<Events *>(e->ctx)->values;
            return i < v.size() ? &v[i].header : nullptr;
        }};
    void add(uint32_t time, uint32_t id, double value, bool mod = false) {
        Event e{};
        e.header = {sizeof(Event), time, CLAP_CORE_EVENT_SPACE_ID,
                    static_cast<uint16_t>(mod ? CLAP_EVENT_PARAM_MOD : CLAP_EVENT_PARAM_VALUE), 0};
        e.param_id = id;
        e.note_id = e.port_index = e.channel = e.key = -1;
        e.value = value;
        values.push_back(e);
    }
};
struct Stream {
    std::vector<uint8_t> bytes;
    size_t position = 0;
    size_t chunk = 7;
    Stream() = default;
    Stream(const Stream &other)
        : bytes(other.bytes), position(other.position), chunk(other.chunk) {}
    Stream(Stream &&other) noexcept
        : bytes(std::move(other.bytes)), position(other.position), chunk(other.chunk) {}
    clap_ostream out{this, [](const clap_ostream *s, const void *data, uint64_t size) -> int64_t {
                         auto &self = *static_cast<Stream *>(s->ctx);
                         const auto n = std::min<uint64_t>(self.chunk, size);
                         const auto *p = static_cast<const uint8_t *>(data);
                         self.bytes.insert(self.bytes.end(), p, p + n);
                         return static_cast<int64_t>(n);
                     }};
    clap_istream in{this, [](const clap_istream *s, void *data, uint64_t size) -> int64_t {
                        auto &self = *static_cast<Stream *>(s->ctx);
                        const auto n = std::min({self.chunk, static_cast<size_t>(size),
                                                 self.bytes.size() - self.position});
                        std::memcpy(data, self.bytes.data() + self.position, n);
                        self.position += n;
                        return static_cast<int64_t>(n);
                    }};
};
struct Fixture {
    clap_host host{CLAP_VERSION,
                   this,
                   "OpenFilter tests",
                   "OpenFilter",
                   "",
                   "1",
                   [](const clap_host *, const char *) -> const void * { return nullptr; },
                   [](const clap_host *) {},
                   [](const clap_host *) {},
                   [](const clap_host *) {}};
    const clap_plugin *p;
    const clap_plugin_params *params;
    const clap_plugin_state *state;
    bool active = false;
    explicit Fixture(const clap_plugin_factory *factory)
        : p(factory->create_plugin(factory, &host, "org.openfilter.compressor")) {
        CHECK(p && p->init(p));
        params = static_cast<const clap_plugin_params *>(p->get_extension(p, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state *>(p->get_extension(p, CLAP_EXT_STATE));
        CHECK(params && state);
    }
    ~Fixture() {
        stop();
        p->destroy(p);
    }
    void start(double rate = 48000) {
        CHECK(p->activate(p, rate, 1, 4096));
        CHECK(p->start_processing(p));
        active = true;
    }
    void stop() {
        if (active) {
            p->stop_processing(p);
            p->deactivate(p);
            active = false;
        }
    }
    void flush(Events &e) {
        realtime = active;
        params->flush(p, &e.input, nullptr);
        realtime = false;
    }
    double value(uint32_t id) {
        double v = 0;
        CHECK(params->get_value(p, id, &v));
        return v;
    }
    void process(std::vector<double> &l, std::vector<double> &r, Events &events,
                 bool mono = false) {
        double *pointers[]{l.data(), r.data()};
        clap_audio_buffer bus{nullptr, pointers, mono ? 1u : 2u, 0, 0};
        clap_process proc{};
        proc.steady_time = -1;
        proc.frames_count = static_cast<uint32_t>(l.size());
        proc.audio_inputs = &bus;
        proc.audio_outputs = &bus;
        proc.audio_inputs_count = proc.audio_outputs_count = 1;
        proc.in_events = &events.input;
        realtime = true;
        const auto status = p->process(p, &proc);
        realtime = false;
        CHECK(status != CLAP_PROCESS_ERROR);
    }
    Stream save() {
        Stream s;
        CHECK(state->save(p, &s.out));
        return s;
    }
    bool load(Stream &s) {
        s.position = 0;
        return state->load(p, &s.in);
    }
};
using namespace openfilter::compressor;
void metadata(const clap_plugin_factory *factory) {
    Fixture f(factory);
    CHECK(f.params->count(f.p) == parameterCount);
    for (unsigned i = 0; i < parameterCount; ++i) {
        clap_param_info info{};
        CHECK(f.params->get_info(f.p, i, &info));
        CHECK(info.id == i);
        near(f.value(i), parameter(i).initial);
        char text[64];
        double value = 0;
        CHECK(f.params->value_to_text(f.p, i, info.default_value, text, sizeof(text)));
        CHECK(f.params->text_to_value(f.p, i, text, &value));
        near(value, info.default_value);
    }
    const auto *ports =
        static_cast<const clap_plugin_audio_ports *>(f.p->get_extension(f.p, CLAP_EXT_AUDIO_PORTS));
    CHECK(ports->count(f.p, true) == 2 && ports->count(f.p, false) == 1);
    clap_audio_port_info sc{};
    CHECK(ports->get(f.p, 1, true, &sc));
    CHECK(!(sc.flags & CLAP_AUDIO_PORT_IS_MAIN));
    CHECK(sc.in_place_pair == CLAP_INVALID_ID);
    const auto *latency =
        static_cast<const clap_plugin_latency *>(f.p->get_extension(f.p, CLAP_EXT_LATENCY));
    f.start(44100);
    CHECK(latency->get(f.p) == 441);
    f.stop();
    f.start(96000);
    CHECK(latency->get(f.p) == 960);
}
std::vector<double> automated(const clap_plugin_factory *factory, unsigned block) {
    Fixture f(factory);
    f.start();
    Events timeline;
    timeline.add(127, Threshold, -30);
    timeline.add(301, Attack, .2);
    timeline.add(613, Threshold, 8, true);
    timeline.add(799, Sidechain, 1);
    timeline.add(999, Lookahead, 10);
    timeline.add(1301, Mix, 40);
    timeline.add(1703, Bypass, 1);
    timeline.add(2201, Bypass, 0);
    timeline.add(2500, Threshold, 0, true);
    timeline.add(3007, Detector, 1);
    timeline.add(4001, AutoRelease, 1);
    timeline.add(6007, Output, 6);
    std::vector<double> output;
    for (unsigned at = 0; at < 8192; at += block) {
        unsigned n = std::min(block, 8192 - at);
        std::vector<double> l(n), r(n);
        Events events;
        for (unsigned j = 0; j < n; ++j) {
            l[j] = .6 * std::sin((at + j) * .13);
            r[j] = .3 * std::cos((at + j) * .037);
        }
        for (const auto &event : timeline.values)
            if (event.header.time >= at && event.header.time < at + n) {
                events.values.push_back(event);
                events.values.back().header.time -= at;
            }
        f.process(l, r, events);
        output.insert(output.end(), l.begin(), l.end());
        output.insert(output.end(), r.begin(), r.end());
    }
    // Canonical channel ordering for comparison across partitions.
    std::vector<double> canonical(16384);
    unsigned src = 0;
    for (unsigned at = 0; at < 8192; at += block) {
        unsigned n = std::min(block, 8192 - at);
        std::copy_n(output.begin() + src, n, canonical.begin() + at);
        src += n;
        std::copy_n(output.begin() + src, n, canonical.begin() + 8192 + at);
        src += n;
    }
    near(f.value(Threshold), -30);
    return canonical;
}
void states(const clap_plugin_factory *factory) {
    Fixture f(factory);
    auto original = f.save();
    Events e;
    e.add(0, Threshold, -32);
    e.add(0, Sidechain, 1);
    f.flush(e);
    auto changed = f.save();
    CHECK(f.load(original));
    near(f.value(Threshold), -18);
    CHECK(f.load(changed));
    near(f.value(Threshold), -32);
    auto bad = changed;
    bad.bytes[24] ^= 0x7f;
    CHECK(!f.load(bad));
    near(f.value(Threshold), -32);
    for (size_t n : {0u, 8u, 16u, 100u}) {
        Stream shortState;
        shortState.bytes.assign(changed.bytes.begin(), changed.bytes.begin() + n);
        CHECK(!f.load(shortState));
    }
    f.start();
    CHECK(f.load(original));
    near(f.value(Threshold), -18);
    Events mod;
    mod.add(0, Threshold, 12, true);
    f.flush(mod);
    near(f.value(Threshold), -18);
    auto saved = f.save();
    Fixture other(factory);
    CHECK(other.load(saved));
    near(other.value(Threshold), -18);
    // Bounded pending loads reject overflow without partially replacing accepted state.
    unsigned accepted = 0;
    for (unsigned n = 0; n < 12; ++n)
        if (f.load(changed))
            ++accepted;
    CHECK(accepted > 0 && accepted < 12);
    Events empty;
    std::vector<double> l(1024, .5), r = l;
    f.process(l, r, empty);
    near(f.value(Threshold), -32);
}
void audio(const clap_plugin_factory *factory) {
    for (bool mono : {false, true})
        for (bool floats : {false, true}) {
            Fixture f(factory);
            auto *config = static_cast<const clap_plugin_audio_ports_config *>(
                f.p->get_extension(f.p, CLAP_EXT_AUDIO_PORTS_CONFIG));
            CHECK(config->select(f.p, mono ? 1 : 0));
            Events setup;
            setup.add(0, Sidechain, 1);
            setup.add(0, Lookahead, 5);
            setup.add(0, SidechainHP, 120);
            f.flush(setup);
            f.start();
            auto v = defaults();
            v[Sidechain] = 1;
            v[Lookahead] = 5;
            v[SidechainHP] = 120;
            Engine reference;
            reference.prepare(48000, v);
            std::array<double, 1024> l{}, r{}, sl{}, sr{};
            std::array<float, 1024> lf{}, rf{}, slf{}, srf{};
            double *main64[]{l.data(), r.data()}, *side64[]{sl.data(), sr.data()};
            float *main32[]{lf.data(), rf.data()}, *side32[]{slf.data(), srf.data()};
            clap_audio_buffer in[2]{
                {floats ? main32 : nullptr, floats ? nullptr : main64, mono ? 1u : 2u, 0, 0},
                {floats ? side32 : nullptr, floats ? nullptr : side64, mono ? 1u : 2u, 0, 0}};
            clap_audio_buffer out = in[0];
            clap_process proc{};
            proc.frames_count = 1024;
            proc.audio_inputs = in;
            proc.audio_outputs = &out;
            proc.audio_inputs_count = 2;
            proc.audio_outputs_count = 1;
            for (unsigned b = 0; b < 12; ++b) {
                for (unsigned n = 0; n < 1024; ++n) {
                    l[n] = .3 * std::sin((n + b * 1024) * .13);
                    r[n] = -.25 * l[n];
                    sl[n] = .8 * std::cos((n + b * 1024) * .09);
                    sr[n] = sl[n] * .4;
                    lf[n] = l[n];
                    rf[n] = r[n];
                    slf[n] = sl[n];
                    srf[n] = sr[n];
                }
                std::array<double, 1024> el{}, er{};
                for (unsigned n = 0; n < 1024; ++n) {
                    el[n] = floats ? lf[n] : l[n];
                    er[n] = floats ? rf[n] : r[n];
                    reference.sample(el[n], er[n], mono, floats ? slf[n] : sl[n],
                                     floats ? srf[n] : sr[n]);
                }
                realtime = true;
                auto status = f.p->process(f.p, &proc);
                realtime = false;
                CHECK(status != CLAP_PROCESS_ERROR);
                for (unsigned n = 0; n < 1024; ++n) {
                    near(floats ? lf[n] : l[n], el[n], floats ? 2e-8 : 1e-12);
                    if (!mono)
                        near(floats ? rf[n] : r[n], er[n], floats ? 2e-8 : 1e-12);
                }
            }
        }
    // Exact event offset: output trim starts a 480-sample ramp at frame 701.
    Fixture f(factory);
    Events setup;
    setup.add(0, Ratio, 1);
    f.flush(setup);
    f.start();
    Events change;
    change.add(701, Output, 6);
    std::vector<double> l(2048, .1), r = l;
    f.process(l, r, change);
    for (unsigned n = 480; n < 701; ++n)
        near(l[n], .1, 1e-15);
    CHECK(l[701] > .1);
    near(l[1181], .1 * std::pow(10., 6. / 20), 1e-12);
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!library)
            throw std::runtime_error(dlerror());
        const auto *entry = static_cast<const clap_plugin_entry *>(dlsym(library, "clap_entry"));
        CHECK(entry && entry->init(argv[1]));
        const auto *factory =
            static_cast<const clap_plugin_factory *>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        CHECK(factory && factory->get_plugin_count(factory) == 1);
        metadata(factory);
        states(factory);
        audio(factory);
        const auto reference = automated(factory, 1);
        for (unsigned n : {17u, 64u, 257u, 1024u, 4096u})
            CHECK(automated(factory, n) == reference);
        entry->deinit();
        dlclose(library);
        cairo_debug_reset_static_data();
        std::cout << "Compressor CLAP: offsets, bit-identical partitions, mono/stereo float/double "
                     "external SC, state, modulation and allocation guards passed\n";
    } catch (const std::exception &e) {
        realtime = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
