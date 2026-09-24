#include "Editor.hpp"
#include "Test.hpp"
#include <fontconfig/fontconfig.h>
#include <iostream>
using namespace openfilter::reverb;
void historyTests() {
    DecayHistory h;
    openfilter::ui::AudioFrame frame;
    frame.rate = 48000;
    for (unsigned n = 0; n < openfilter::ui::fftSize; ++n) {
        const float sample =
            .5f * std::sin(2 * std::numbers::pi * 85 * n / openfilter::ui::fftSize);
        frame.samples[0][n] = sample;
        frame.samples[1][n] = -sample;
        frame.samples[2][n] = sample * .5;
        frame.samples[3][n] = sample * .5;
    }
    for (unsigned n = 0; n < 100; ++n) {
        ++frame.serial;
        CHECK(h.update(frame));
    }
    const auto wetPeak = *std::max_element(h.wet.begin(), h.wet.end());
    const auto outPeak = *std::max_element(h.output.begin(), h.output.end());
    near(h.spectrum.at(0, 85 * 48000. / openfilter::ui::fftSize), 20 * std::log10(.5), 1e-6);
    CHECK(wetPeak > -18 && wetPeak < -5);
    near(wetPeak - outPeak, 6.0206, .001);
    const unsigned count = h.count();
    CHECK(count > 40 && count <= DecayHistory::capacity);
    CHECK(!h.update(frame) && h.count() == count);
    const auto stored = h.slice(0).wet;
    // Captured spectra remain data, while age changes and stopped audio fades.
    h.idle(DecayHistory::duration + .1);
    CHECK(h.age(h.slice(count - 1)) > DecayHistory::duration);
    CHECK(h.slice(0).wet == stored);
    CHECK(*std::max_element(h.output.begin(), h.output.end()) < -100);
    frame.rate = 96000;
    ++frame.serial;
    CHECK(h.update(frame) && h.count() == 1);
    h.clear();
    CHECK(h.count() == 0);
    frame.samples = {};
    frame.serial = 1;
    CHECK(h.update(frame));
    for (double value : h.wet)
        near(value, -120);
    const auto first = h.slice(0);
    h.idle(.01);
    ++frame.serial;
    CHECK(h.update(frame));
    near(h.age(first), 1024. / frame.rate, 1e-12);
}
int main() {
    try {
        historyTests();
        {
            EditorState state;
            openfilter::ui::AnalysisTap tap;
            std::array<int, parameterCount> depth{};
            Editor e([&] { return state; },
                     [&](UiKind k, unsigned i, double v) {
                         if (k == UiKind::Begin)
                             CHECK(++depth[i] == 1);
                         if (k == UiKind::Value) {
                             CHECK(depth[i] == 1);
                             state.values[i] = state.effective[i] = v;
                         }
                         if (k == UiKind::End)
                             CHECK(--depth[i] == 0);
                     },
                     tap);
            double time = 1;
            auto click = [&](unsigned i, unsigned button = 0) {
                auto r = e.controlBounds(i);
                e.press(r.x + r.w / 2, r.y + r.h / 2, button, 0, time);
                e.release(0, 0);
                time += 1;
            };
            for (unsigned i = 0; i < globals; ++i) {
                if (i == PredelayOffset)
                    state.values[PredelaySync] = state.effective[PredelaySync] = 1;
                state.values[i] = state.effective[i] =
                    parameter(i).initial == parameter(i).max ? parameter(i).min : parameter(i).max;
                e.tick();
                auto r = e.controlBounds(i);
                const double x = r.x + r.w / 2, y = r.y + r.h / 2;
                e.press(x, y, 0, 0, time);
                e.release(x, y);
                e.press(x, y, 0, 0, time + .12);
                e.release(x, y);
                time += 1;
                near(state.values[i], parameter(i).initial);
                e.key(PUGL_KEY_ESCAPE, 0);
            }
            click(Predelay, 1);
            e.input("42.5 ms");
            e.key(PUGL_KEY_ENTER, 0);
            near(state.values[Predelay], 42.5);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Predelay], 20);
            e.key('z', PUGL_MOD_CTRL | PUGL_MOD_SHIFT);
            near(state.values[Predelay], 42.5);
            click(Space, 1);
            e.input("invalid");
            e.key(PUGL_KEY_ENTER, 0);
            near(state.values[Space], 2.5);
            e.key(PUGL_KEY_ESCAPE, 0);
            auto r = e.controlBounds(Character);
            const auto old = state.values[Character];
            e.press(r.x + r.w / 2, r.y + 40, 0, 0, time);
            e.motion(r.x + r.w / 2, r.y + 30, 0);
            const auto moved = state.values[Character];
            e.motion(r.x + r.w / 2, r.y + 30, PUGL_MOD_SHIFT);
            near(state.values[Character], moved);
            e.release(0, 0);
            time += 1;
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Character], old);
            // Opening/closing an exact field must not quantize a host's value.
            state.values[Space] = state.effective[Space] = 2.543219;
            e.tick();
            click(Space, 1);
            e.key(PUGL_KEY_ENTER, 0);
            near(state.values[Space], 2.543219);
            state.values[Space] = state.effective[Space] = 2.5;
            e.tick();
            // Both graph layers create/edit independent, balanced frequency+amount gestures.
            for (bool post : {false, true}) {
                r = e.viewBounds(post);
                e.press(r.x + 20, r.y + 12, 0, 0, time);
                e.release(0, 0);
                time += 1;
                auto g = e.graphBounds();
                e.press(g.x + g.w * .4, g.y + g.h * .4, 0, 0, time);
                e.motion(g.x + g.w * .5, g.y + g.h * .35, 0);
                e.release(0, 0);
                time += 1;
                const auto i = bandIndex(post, 0, 0);
                near(state.values[i + Enabled], 1);
                near(state.values[i + Frequency], 20 * std::sqrt(1000.), 1e-9);
                near(state.values[i + Amount], post ? -24 + 48 * .65 : 25 * std::pow(16., .65),
                     1e-9);
                e.key('z', PUGL_MOD_CTRL);
                near(state.values[i + Enabled], 0);
                e.key('z', PUGL_MOD_CTRL | PUGL_MOD_SHIFT);
                near(state.values[i + Enabled], 1);
                for (unsigned f = 0; f < fields; ++f) {
                    state.values[i + f] = state.effective[i + f] = parameter(i + f).max;
                    e.tick();
                    r = e.controlBounds(i + f);
                    double x = r.x + r.w / 2, y = r.y + r.h / 2;
                    e.press(x, y, 0, 0, time);
                    e.release(x, y);
                    e.press(x, y, 0, 0, time + .12);
                    e.release(x, y);
                    time += 1;
                    near(state.values[i + f], parameter(i + f).initial);
                }
                e.key(PUGL_KEY_ESCAPE, 0);
            }
            // Cut nodes edit frequency only and never emit duplicate frequency gestures.
            const auto cut = bandIndex(true, 0, 0);
            state.values[cut + Enabled] = 1;
            state.values[cut + Type] = 4;
            state.values[cut + Amount] = 7;
            state.effective = state.values;
            e.tick();
            auto graph = e.graphBounds();
            const double cx =
                graph.x + graph.w * std::log(state.values[cut + Frequency] / 20) / std::log(1000.);
            const double cy = graph.y + graph.h * .5;
            e.press(cx, cy, 0, 0, time);
            e.motion(cx + 20, cy - 30, 0);
            e.release(0, 0);
            time += 1;
            near(state.values[cut + Amount], 7);
            CHECK(state.values[cut + Frequency] > 100);
            e.key(PUGL_KEY_ESCAPE, 0);
            // Mix lock affects presets, and presets remain a single undo action.
            click(Mix, 1);
            e.input("73 %");
            e.key(PUGL_KEY_ENTER, 0);
            click(MixLock);
            r = e.headerBounds(0);
            e.press(r.x + 10, r.y + 10, 0, 0, time);
            e.release(0, 0);
            time += 1;
            e.press(r.x + 20, r.y + r.h + 8 + 32 + 15, 0, 0, time);
            e.release(0, 0);
            time += 1;
            near(state.values[Space], .65);
            near(state.values[Mix], 73);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Space], 2.5);
            for (auto d : depth)
                CHECK(d == 0);
            auto overlaps = [](openfilter::ui::Rect a, openfilter::ui::Rect b) {
                return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
            };
            constexpr unsigned knobs[]{Predelay,   Character, Thickness, Distance, Space,
                                       Brightness, Width,     Ducking,   Mix};
            for (double scale : {.75, 1., 2., 3.})
                for (unsigned width : {900u, 1120u, 1600u}) {
                    CHECK(e.setScale(scale));
                    CHECK(e.resize(unsigned(width * scale), unsigned(600 * scale)));
                    for (unsigned i = 0; i < globals; ++i) {
                        r = e.controlBounds(i);
                        CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= width && r.y + r.h <= 600);
                    }
                    for (unsigned i : knobs) {
                        const auto cap = e.captionBounds(i), value = e.valueBounds(i),
                                   dial = e.dialBounds(i);
                        CHECK(cap.y >= dial.y + dial.h + 1);
                        CHECK(!overlaps(cap, value));
                        for (unsigned j : knobs) {
                            CHECK(!overlaps(cap, e.dialBounds(j)));
                            if (i != Space || j != Space)
                                CHECK(!overlaps(value, e.dialBounds(j)));
                        }
                        for (unsigned j :
                             {unsigned(Style), unsigned(PredelaySync), unsigned(PredelayOffset),
                              unsigned(DecayRate), unsigned(AutoGate), unsigned(GateHold),
                              unsigned(Freeze), unsigned(MixLock)})
                            CHECK(!overlaps(dial, e.controlBounds(j)));
                    }
                    auto *surface =
                        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, e.width(), e.height());
                    auto *cr = cairo_create(surface);
                    e.paint(cr, width, 600);
                    CHECK(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
                    cairo_destroy(cr);
                    cairo_surface_destroy(surface);
                }
        }
        cairo_debug_reset_static_data();
        FcFini();
        std::cout << "Reverb UI: resets, entry, undo, fine drag, two EQ layers, mix lock and "
                     "scaled layouts passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
