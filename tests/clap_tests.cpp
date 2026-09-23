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
        : p(factory->create_plugin(factory, &host, "org.openfilter.eq")) {
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
void metadata(const clap_plugin_factory *factory) {
    Fixture f(factory);
    CHECK(f.params->count(f.p) == eq::parameterCount);
    std::vector<uint32_t> ids;
    for (unsigned i = 0; i < eq::parameterCount; ++i) {
        clap_param_info p{};
        CHECK(f.params->get_info(f.p, i, &p));
        ids.push_back(p.id);
        CHECK(p.min_value <= p.default_value && p.default_value <= p.max_value);
        near(f.value(p.id), p.default_value);
        char text[128]{};
        double parsed = 0;
        CHECK(f.params->value_to_text(f.p, p.id, p.default_value, text, sizeof(text)));
        CHECK(f.params->text_to_value(f.p, p.id, text, &parsed));
        near(parsed, p.default_value, .0001);
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    double v = 0;
    CHECK(!f.params->get_value(f.p, 999999, &v));
    CHECK(!f.params->text_to_value(f.p, 1, "garbage", &v));
    CHECK(!f.params->text_to_value(f.p, 1, "nan", &v));
    CHECK(f.params->text_to_value(f.p, eq::bandId(0, eq::Frequency), "2.5 kHz", &v));
    near(std::exp2(v), 2500);
    char slopeLabel[32]{};
    CHECK(
        f.params->value_to_text(f.p, eq::bandId(0, eq::Slope), 3, slopeLabel, sizeof(slopeLabel)));
    CHECK(std::string_view(slopeLabel) == "Brickwall");
    CHECK(f.params->text_to_value(f.p, eq::bandId(0, eq::Slope), "Brickwall", &v) && v == 3);
    const auto *remote = static_cast<const clap_plugin_remote_controls *>(
        f.p->get_extension(f.p, CLAP_EXT_REMOTE_CONTROLS));
    CHECK(remote && remote->count(f.p) == 25);
    clap_remote_controls_page page{};
    CHECK(remote->get(f.p, 1, &page));
    CHECK(page.param_ids[0] == eq::bandId(0, eq::Frequency));
}
std::vector<double> automated(const clap_plugin_factory *factory, unsigned block,
                              bool brickwall = false) {
    Fixture f(factory);
    Events initial;
    initial.add(0, eq::bandId(0, eq::Enabled), 1);
    initial.add(0, eq::bandId(0, eq::Gain), 6);
    if (brickwall) {
        initial.add(0, eq::bandId(0, eq::Type), 3);
        initial.add(0, eq::bandId(0, eq::Slope), 3);
    }
    f.flush(initial);
    f.start();
    Events timeline;
    timeline.add(127, eq::bandId(0, eq::Frequency), std::log2(8000.));
    timeline.add(301, eq::bandId(0, eq::Gain), -12);
    timeline.add(613, eq::bandId(0, eq::Q), std::log2(4.));
    timeline.add(799, eq::bandId(0, eq::Type), brickwall ? 4 : 2);
    timeline.add(999, eq::bandId(0, eq::Routing), 3);
    timeline.add(1301, 0, 1);
    timeline.add(1703, 0, 0);
    if (brickwall) {
        timeline.add(2001, eq::bandId(0, eq::Slope), 2);
        timeline.add(3007, eq::bandId(0, eq::Slope), 3);
    }
    std::vector<double> output;
    for (unsigned at = 0; at < 4096; at += block) {
        const unsigned n = std::min(block, 4096 - at);
        std::vector<double> l(n), r(n);
        Events local;
        for (unsigned j = 0; j < n; ++j) {
            l[j] = std::sin((at + j) * .13) * .25;
            r[j] = std::cos((at + j) * .09) * .1;
        }
        for (const auto &event : timeline.values)
            if (event.header.time >= at && event.header.time < at + n) {
                local.values.push_back(event);
                local.values.back().header.time -= at;
            }
        f.process(l, r, local);
        output.insert(output.end(), l.begin(), l.end());
    }
    return output;
}
void eventTiming(const clap_plugin_factory *factory) {
    for (bool brickwall : {false, true}) {
        const auto reference = automated(factory, 1, brickwall);
        for (unsigned n : {17u, 64u, 257u, 1024u})
            CHECK(automated(factory, n, brickwall) == reference);
    }
    Fixture f(factory);
    f.start();
    std::vector<double> l(1024, 1), r = l;
    Events e;
    e.add(127, 1, 6);
    f.process(l, r, e);
    for (unsigned n = 0; n < 127; ++n)
        CHECK(l[n] == 1);
    CHECK(l[127] > 1 && l[127] < 1.01);
    near(l.back(), std::pow(10., 6. / 20));
    e.values.clear();
    e.add(0, 1, 6, true);
    std::fill(l.begin(), l.end(), 1);
    r = l;
    f.process(l, r, e);
    near(l.back(), std::pow(10., 12. / 20));
    CHECK(f.value(1) == 6);
    e.values.clear();
    e.add(0, 1, 0, true);
    std::fill(l.begin(), l.end(), 1);
    r = l;
    f.process(l, r, e);
    near(l.back(), std::pow(10., 6. / 20));
    CHECK(f.value(1) == 6);
    f.p->reset(f.p);
    e.values.clear();
    std::fill(l.begin(), l.end(), 1);
    r = l;
    f.process(l, r, e);
    near(l[0], std::pow(10., 6. / 20));
}
void states(const clap_plugin_factory *factory) {
    Fixture a(factory), b(factory);
    Events e;
    e.add(0, 1, -3);
    e.add(0, eq::bandId(7, eq::Enabled), 1);
    e.add(0, eq::bandId(7, eq::Type), 3);
    e.add(0, eq::bandId(7, eq::Slope), 3);
    e.add(0, eq::bandId(7, eq::Frequency), std::log2(12345.));
    a.flush(e);
    auto original = a.save();
    CHECK(b.load(original));
    auto restored = b.save();
    CHECK(original.bytes == restored.bytes);
    auto truncated = a.save();
    truncated.bytes.resize(100);
    CHECK(!b.load(truncated));
    CHECK(b.save().bytes == original.bytes);
    auto corrupt = a.save();
    corrupt.bytes[100] ^= 1;
    CHECK(!b.load(corrupt));
    CHECK(b.save().bytes == original.bytes);
    auto nan = a.save();
    plugin::put64(nan.bytes.data() + 20,
                  std::bit_cast<uint64_t>(std::numeric_limits<double>::quiet_NaN()));
    plugin::put32(nan.bytes.data() + nan.bytes.size() - 4,
                  plugin::checksum(nan.bytes.data(), nan.bytes.size() - 4));
    CHECK(!b.load(nan));
    CHECK(b.save().bytes == original.bytes);
    auto version = a.save();
    plugin::put32(version.bytes.data() + 8, 999);
    plugin::put32(version.bytes.data() + version.bytes.size() - 4,
                  plugin::checksum(version.bytes.data(), version.bytes.size() - 4));
    CHECK(!b.load(version));
    b.start();
    Events change;
    change.add(0, 1, 12);
    b.flush(change);
    CHECK(b.value(1) == 12);
    CHECK(b.load(original));
    CHECK(b.value(1) == -3);
    CHECK(b.save().bytes == original.bytes);
    std::vector<double> l(1024, .2), r = l;
    Events empty;
    b.process(l, r, empty);
    CHECK(b.value(1) == -3);
    // Bounded state handoff refuses overflow and leaves the accepted state intact.
    for (unsigned i = 0; i < 7; ++i) {
        CHECK(b.load(original));
    }
    CHECK(!b.load(original));
    b.process(l, r, empty);
    CHECK(b.load(original));
}
void formats(const clap_plugin_factory *factory) {
    Fixture f(factory);
    for (double rate : {44100., 48000., 88200., 96000., 176400., 192000.}) {
        f.start(rate);
        std::vector<float> l(257), r(257);
        for (unsigned n = 0; n < l.size(); ++n) {
            l[n] = std::sin(n * .1f);
            r[n] = -l[n];
        }
        const auto copy = l;
        float *pointers[]{l.data(), r.data()};
        clap_audio_buffer bus{pointers, nullptr, 2, 0, 0};
        Events empty;
        clap_process p{};
        p.steady_time = -1;
        p.frames_count = 257;
        p.audio_inputs = &bus;
        p.audio_outputs = &bus;
        p.audio_inputs_count = p.audio_outputs_count = 1;
        p.in_events = &empty.input;
        realtime = true;
        const auto status = f.p->process(f.p, &p);
        realtime = false;
        CHECK(status != CLAP_PROCESS_ERROR);
        CHECK(l == copy);
        // Subnormal inputs and underflow at the host precision are flushed.
        std::fill(l.begin(), l.end(), std::numeric_limits<float>::denorm_min());
        std::fill(r.begin(), r.end(), -std::numeric_limits<float>::denorm_min());
        CHECK(f.p->process(f.p, &p) != CLAP_PROCESS_ERROR);
        for (auto x : l)
            CHECK(x == 0);
        for (auto x : r)
            CHECK(x == 0);
        f.stop();
    }
    const auto *config = static_cast<const clap_plugin_audio_ports_config *>(
        f.p->get_extension(f.p, CLAP_EXT_AUDIO_PORTS_CONFIG));
    CHECK(config && config->select(f.p, 1));
    f.start();
    std::vector<double> l(17, .25), r(17, 0);
    Events empty;
    f.process(l, r, empty, true);
    for (auto x : l)
        CHECK(x == .25);
    std::fill(l.begin(), l.end(), std::numeric_limits<double>::denorm_min());
    f.process(l, r, empty, true);
    for (auto x : l)
        CHECK(x == 0);
}
void concurrentState(const clap_plugin_factory *factory) {
    Fixture f(factory);
    auto preset = f.save();
    f.start();
    std::atomic<bool> run{true};
    std::atomic<unsigned> processed{0};
    std::thread audio([&] {
        std::vector<double> l(64), r(64);
        Events empty;
        while (run.load()) {
            std::fill(l.begin(), l.end(), .1);
            std::fill(r.begin(), r.end(), .2);
            f.process(l, r, empty);
            ++processed;
        }
    });
    for (unsigned n = 0; n < 1000; ++n) {
        preset.position = 0;
        f.state->load(f.p, &preset.in);
        Stream output;
        CHECK(f.state->save(f.p, &output.out));
        double value = 0;
        CHECK(f.params->get_value(f.p, 1, &value));
        CHECK(std::isfinite(value));
    }
    run = false;
    audio.join();
    CHECK(processed > 0);
}
void snapshotConcurrency() {
    plugin::TripleBuffer<std::array<uint64_t, 170>> snapshots;
    std::atomic<bool> finished{false};
    std::thread writer([&] {
        std::array<uint64_t, 170> values{};
        for (uint64_t n = 1; n <= 100000; ++n) {
            values.fill(n);
            snapshots.publish(values);
        }
        finished.store(true, std::memory_order_release);
    });
    bool coherent = true;
    uint64_t latest = 0;
    while (!finished.load(std::memory_order_acquire)) {
        const auto &values = snapshots.read();
        if (values[0] < latest)
            coherent = false;
        latest = values[0];
        for (auto value : values)
            if (value != latest)
                coherent = false;
    }
    writer.join();
    const auto &values = snapshots.read();
    CHECK(coherent && values[0] == 100000);
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!library)
            throw std::runtime_error(dlerror());
        const auto *entry = static_cast<const clap_plugin_entry *>(dlsym(library, "clap_entry"));
        CHECK(entry && entry->init(argv[1]));
        CHECK(entry->init(argv[1]));
        const auto *factory =
            static_cast<const clap_plugin_factory *>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        CHECK(factory && factory->get_plugin_count(factory) == 1);
        CHECK(!factory->get_plugin_descriptor(factory, 1));
        metadata(factory);
        eventTiming(factory);
        states(factory);
        formats(factory);
        concurrentState(factory);
        snapshotConcurrency();
        entry->deinit();
        entry->deinit();
        dlclose(library);
        // Standalone host cleanup, never plugin teardown. Linking Cairo into the host also
        // keeps its dependencies resident: Ubuntu's graphics initialization allocations leak when
        // Cairo is repeatedly dlopened/dlclosed, even without any plugin code.
        cairo_debug_reset_static_data();
        std::cout << "CLAP: metadata, sample offsets, block partition invariance, modulation, "
                     "state integrity/concurrency, mono/stereo and 32/64-bit buffers passed; no "
                     "C++ allocations in guarded callbacks\n";
    } catch (const std::exception &e) {
        realtime = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
