#include "Parameters.hpp"
#include "Test.hpp"
#include <X11/Xlib.h>
#include <array>
#include <cairo.h>
#include <chrono>
#include <clap/clap.h>
#include <cstring>
#include <dlfcn.h>
#include <fontconfig/fontconfig.h>
#include <iostream>
#include <thread>
extern thread_local bool realtime;
using namespace openfilter;
struct Host {
    bool registered = false, flushRequested = false;
    clap_host_timer_support timers{[](const clap_host *h, uint32_t ms, clap_id *id) {
                                       auto &s = *static_cast<Host *>(h->host_data);
                                       CHECK(ms > 0 && !s.registered);
                                       s.registered = true;
                                       *id = 42;
                                       return true;
                                   },
                                   [](const clap_host *h, clap_id id) {
                                       auto &s = *static_cast<Host *>(h->host_data);
                                       CHECK(id == 42 && s.registered);
                                       s.registered = false;
                                       return true;
                                   }};
    clap_host_params params{
        [](const clap_host *, uint32_t) {}, [](const clap_host *, clap_id, uint32_t) {},
        [](const clap_host *h) { static_cast<Host *>(h->host_data)->flushRequested = true; }};
    clap_host host{CLAP_VERSION,
                   this,
                   "OpenFilter GUI tests",
                   "OpenFilter",
                   "",
                   "1",
                   [](const clap_host *h, const char *id) -> const void * {
                       auto &s = *static_cast<Host *>(h->host_data);
                       if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT))
                           return &s.timers;
                       if (!std::strcmp(id, CLAP_EXT_PARAMS))
                           return &s.params;
                       return nullptr;
                   },
                   [](const clap_host *) {},
                   [](const clap_host *) {},
                   [](const clap_host *) {}};
};
struct MemoryState {
    std::array<char, 4096> data{};
    size_t size = 0, position = 0;
    clap_ostream out{this, [](const clap_ostream *s, const void *data, uint64_t n) -> int64_t {
                         auto &m = *static_cast<MemoryState *>(s->ctx);
                         if (m.size + n > m.data.size())
                             return -1;
                         std::memcpy(m.data.data() + m.size, data, n);
                         m.size += n;
                         return n;
                     }};
    clap_istream in{this, [](const clap_istream *s, void *data, uint64_t n) -> int64_t {
                        auto &m = *static_cast<MemoryState *>(s->ctx);
                        n = std::min<uint64_t>(n, m.size - m.position);
                        std::memcpy(data, m.data.data() + m.position, n);
                        m.position += n;
                        return n;
                    }};
};
struct Output {
    unsigned count = 0, capacity = 512;
    std::array<clap_event_param_value, 512> events{};
    clap_output_events out{this, [](const clap_output_events *out, const clap_event_header *e) {
                               auto &s = *static_cast<Output *>(out->ctx);
                               if (s.count >= s.capacity)
                                   return false;
                               CHECK(e->size <= sizeof(clap_event_param_value));
                               std::memcpy(&s.events[s.count++], e, e->size);
                               return true;
                           }};
    void balanced() {
        std::array<int, compressor::parameterCount> depth{};
        for (unsigned n = 0; n < count; ++n) {
            auto &e = events[n];
            const int i = compressor::indexForId(e.param_id);
            CHECK(i >= 0);
            if (e.header.type == CLAP_EVENT_PARAM_GESTURE_BEGIN)
                CHECK(++depth[i] == 1);
            else if (e.header.type == CLAP_EVENT_PARAM_GESTURE_END)
                CHECK(--depth[i] == 0);
            else
                CHECK(e.header.type == CLAP_EVENT_PARAM_VALUE && depth[i] == 1);
        }
        for (auto d : depth)
            CHECK(d == 0);
    }
};
int main(int argc, char **argv) {
    Display *display = XOpenDisplay(nullptr);
    if (!display) {
        std::cout << "No X11 display; run with xvfb-run\n";
        return 77;
    }
    try {
        CHECK(argc == 2);
        void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!library)
            throw std::runtime_error(dlerror());
        const auto *entry = static_cast<const clap_plugin_entry *>(dlsym(library, "clap_entry"));
        CHECK(entry && entry->init(argv[1]));
        const auto *factory =
            static_cast<const clap_plugin_factory *>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        Host host;
        const auto *p = factory->create_plugin(factory, &host.host, "org.openfilter.compressor");
        CHECK(p && p->init(p));
        const auto *gui = static_cast<const clap_plugin_gui *>(p->get_extension(p, CLAP_EXT_GUI));
        const auto *timer = static_cast<const clap_plugin_timer_support *>(
            p->get_extension(p, CLAP_EXT_TIMER_SUPPORT));
        const auto *params =
            static_cast<const clap_plugin_params *>(p->get_extension(p, CLAP_EXT_PARAMS));
        CHECK(gui && timer && params);
        CHECK(gui->is_api_supported(p, CLAP_WINDOW_API_X11, false));
        CHECK(!gui->is_api_supported(p, CLAP_WINDOW_API_X11, true));
        CHECK(p->activate(p, 48000, 1, 1024) && p->start_processing(p));
        // This parent belongs only to the test and remains unmapped on the user's desktop.
        Window parent =
            XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 2400, 1600, 0, 0, 0);
        XSync(display, False);
        auto tick = [&] {
            XSync(display, False);
            timer->on_timer(p, 42);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            timer->on_timer(p, 42);
        };
        for (unsigned cycle = 0; cycle < 5; ++cycle) {
            CHECK(gui->create(p, CLAP_WINDOW_API_X11, false));
            CHECK(host.registered);
            uint32_t w = 0, h = 0;
            CHECK(gui->get_size(p, &w, &h) && w == 1120 && h == 720);
            CHECK(gui->set_scale(p, 2));
            CHECK(gui->get_size(p, &w, &h) && w == 2240 && h == 1440);
            w = 1;
            h = 1;
            CHECK(gui->adjust_size(p, &w, &h) && w == 1800 && h == 1200);
            CHECK(gui->set_scale(p, 1));
            clap_window window{};
            window.api = CLAP_WINDOW_API_X11;
            window.x11 = parent;
            CHECK(gui->set_parent(p, &window));
            CHECK(gui->show(p));
            tick();
            Window root, returnedParent, *children = nullptr;
            unsigned count = 0;
            CHECK(XQueryTree(display, parent, &root, &returnedParent, &children, &count) &&
                  count == 1);
            const Window child = children[0];
            XFree(children);
            unsigned long time = 1000 + cycle * 2000;
            auto button = [&](int type, int x, int y) {
                XEvent event{};
                event.xbutton.type = type;
                event.xbutton.display = display;
                event.xbutton.window = child;
                event.xbutton.root = root;
                event.xbutton.x = x;
                event.xbutton.y = y;
                event.xbutton.button = Button1;
                event.xbutton.same_screen = True;
                event.xbutton.time = time;
                CHECK(XSendEvent(display, child, False,
                                 type == ButtonPress ? ButtonPressMask : ButtonReleaseMask,
                                 &event));
                tick();
            };
            // Auto release uses a real X11 event -> Pugl -> editor -> CLAP event queue.
            button(ButtonPress, 416, 597);
            button(ButtonRelease, 416, 597);
            CHECK(host.flushRequested);
            Output out;
            out.capacity = 0;
            realtime = true;
            params->flush(p, nullptr, &out.out);
            realtime = false;
            CHECK(out.count == 0);
            double enabled = 0;
            CHECK(params->get_value(p, compressor::AutoRelease, &enabled) &&
                  enabled ==
                      (cycle % 2 == 0 ? 1 : 0)); // Queries reflect pending UI edits immediately.
            const auto *state =
                static_cast<const clap_plugin_state *>(p->get_extension(p, CLAP_EXT_STATE));
            MemoryState pendingState;
            CHECK(state->save(p, &pendingState.out));
            const auto *other =
                factory->create_plugin(factory, &host.host, "org.openfilter.compressor");
            CHECK(other && other->init(other));
            const auto *otherState =
                static_cast<const clap_plugin_state *>(other->get_extension(other, CLAP_EXT_STATE));
            const auto *otherParams = static_cast<const clap_plugin_params *>(
                other->get_extension(other, CLAP_EXT_PARAMS));
            CHECK(otherState->load(other, &pendingState.in));
            CHECK(otherParams->get_value(other, compressor::AutoRelease, &enabled) &&
                  enabled == (cycle % 2 == 0 ? 1 : 0));
            other->destroy(other);
            out.capacity = 1;
            realtime = true;
            params->flush(p, nullptr, &out.out);
            realtime = false;
            CHECK(out.count == 1 && out.events[0].header.type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
            out.capacity = 512;
            realtime = true;
            params->flush(p, nullptr, &out.out);
            realtime = false;
            CHECK(params->get_value(p, compressor::AutoRelease, &enabled) &&
                  enabled == (cycle % 2 == 0 ? 1 : 0));
            out.balanced();
            tick();
            if (cycle == 0) {
                // A state load supersedes older UI commands still awaiting a callback.
                out.count = 0;
                time += 500;
                button(ButtonPress, 1020, 686);
                button(ButtonRelease, 1020, 686);
                pendingState.position = 0;
                CHECK(state->load(p, &pendingState.in));
                realtime = true;
                params->flush(p, nullptr, &out.out);
                realtime = false;
                CHECK(params->get_value(p, compressor::AutoRelease, &enabled) &&
                      enabled == (cycle % 2 == 0 ? 1 : 0));
                out.balanced();
                tick();
            }
            // Host automation updates the knob; native double-click resets its base value.
            clap_event_param_value automated{};
            automated.header = {sizeof(automated), 0, CLAP_CORE_EVENT_SPACE_ID,
                                CLAP_EVENT_PARAM_VALUE, 0};
            automated.param_id = compressor::Attack;
            automated.value = 55.;
            automated.note_id = automated.port_index = automated.channel = automated.key = -1;
            clap_input_events input{
                &automated, [](const clap_input_events *) -> uint32_t { return 1; },
                [](const clap_input_events *e, uint32_t) -> const clap_event_header * {
                    return &static_cast<clap_event_param_value *>(e->ctx)->header;
                }};
            realtime = true;
            params->flush(p, &input, &out.out);
            realtime = false;
            tick();
            out.count = 0;
            time += 500;
            button(ButtonPress, 552, 507);
            button(ButtonRelease, 552, 507);
            time += 120;
            button(ButtonPress, 552, 507);
            button(ButtonRelease, 552, 507);
            realtime = true;
            params->flush(p, nullptr, &out.out);
            realtime = false;
            double resetValue = 0;
            CHECK(params->get_value(p, compressor::Attack, &resetValue));
            near(resetValue, 10.);
            out.balanced();
            tick();
            // Drag an actual numeric readout while the host rejects output
            // events. Pointer jitter emits no gesture; retry preserves B/V/E.
            {
                out.count = 0;
                out.capacity = 0;
                time += 500;
                constexpr unsigned readoutId = compressor::Output;
                double beforeReadout = 0, afterReadout = 0;
                CHECK(params->get_value(p, readoutId, &beforeReadout));
                auto move = [&](int x, int y) {
                    XEvent event{};
                    event.xmotion.type = MotionNotify;
                    event.xmotion.display = display;
                    event.xmotion.window = child;
                    event.xmotion.root = root;
                    event.xmotion.x = x;
                    event.xmotion.y = y;
                    event.xmotion.state = Button1Mask;
                    event.xmotion.same_screen = True;
                    event.xmotion.time = time + 20;
                    CHECK(XSendEvent(display, child, False, PointerMotionMask, &event));
                    tick();
                };
                button(ButtonPress, 890, 696);
                move(890 + 1, 698);
                out.capacity = 512;
                realtime = true;
                params->flush(p, nullptr, &out.out);
                realtime = false;
                CHECK(out.count == 0);
                out.capacity = 0;
                move(890, 708);
                button(ButtonRelease, 890, 708);
                realtime = true;
                params->flush(p, nullptr, &out.out);
                realtime = false;
                CHECK(out.count == 0);
                CHECK(params->get_value(p, readoutId, &afterReadout));
                CHECK(afterReadout < beforeReadout);
                out.capacity = 1;
                realtime = true;
                params->flush(p, nullptr, &out.out);
                realtime = false;
                CHECK(out.count == 1 &&
                      out.events[0].header.type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
                out.capacity = 512;
                realtime = true;
                params->flush(p, nullptr, &out.out);
                realtime = false;
                CHECK(out.count == 3 && out.events[1].header.type == CLAP_EVENT_PARAM_VALUE &&
                      out.events[2].header.type == CLAP_EVENT_PARAM_GESTURE_END);
                for (unsigned n = 0; n < out.count; ++n)
                    CHECK(out.events[n].param_id == readoutId);
                out.balanced();
                tick();
            }
            // Close with an unfinished numeric drag: the host must still receive its end.
            out.count = 0;
            time += 500;
            button(ButtonPress, 552, 507);
            realtime = true;
            params->flush(p, nullptr, &out.out);
            realtime = false;
            CHECK(out.count == 1 && out.events[0].header.type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
            // Exercise audio publication with the analyzer enabled, allocation guard active.
            std::array<double, 1024> l{}, r{};
            double *buffers[]{l.data(), r.data()};
            clap_audio_buffer bus{nullptr, buffers, 2, 0, 0};
            clap_process process{};
            process.steady_time = -1;
            process.frames_count = 1024;
            process.audio_inputs = process.audio_outputs = &bus;
            process.audio_inputs_count = process.audio_outputs_count = 1;
            process.out_events = &out.out;
            for (unsigned block = 0; block < 8; ++block) {
                l.fill(.2);
                r.fill(-.2);
                realtime = true;
                const auto status = p->process(p, &process);
                realtime = false;
                CHECK(status != CLAP_PROCESS_ERROR);
                tick();
            }
            CHECK(gui->set_size(p, 900, 600));
            tick();
            CHECK(gui->hide(p));
            CHECK(gui->show(p));
            tick();
            gui->destroy(p);
            CHECK(!host.registered);
            realtime = true;
            params->flush(p, nullptr, &out.out);
            realtime = false;
            out.balanced();
        }
        XDestroyWindow(display, parent);
        p->stop_processing(p);
        p->deactivate(p);
        p->destroy(p);
        entry->deinit();
        dlclose(library);
        XCloseDisplay(display);
        // Test-process teardown only: Cairo/Fontconfig keep process-wide caches.
        // Never clear these caches from a plugin sharing a host with other editors.
        cairo_debug_reset_static_data();
        FcFini();
        std::cout << "Native X11 CLAP: embedding, resizing, 2x scaling, five reopen cycles, real "
                     "pointer events, host backpressure, balanced gestures, analyzer audio "
                     "allocation guard passed\n";
    } catch (const std::exception &e) {
        realtime = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
