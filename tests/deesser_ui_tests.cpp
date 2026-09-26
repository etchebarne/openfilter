#include "Editor.hpp"
#include "ReadoutTest.hpp"
#include "Test.hpp"
#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <iostream>
using namespace openfilter::deesser;
void readoutTests() {
    for (unsigned i = 0; i < parameterCount; ++i) {
        const auto p = parameter(i);
        if (p.stepped)
            continue;
        const double initial = denormalized(i, .35), value = denormalized(i, .45);
        readoutTest<Editor, EditorState, MeterTap>(
            i, initial, std::to_string(value), value, p.initial,
            [i](Editor &e, EditorState &s, double &t) {
                (void)i;
                (void)e;
                (void)s;
                (void)t;
            },
            [i](Editor &e) {
                const auto r = e.controlBounds(i);
                if (r.h <= 28)
                    return r;
                return openfilter::ui::Rect{r.x, r.y + r.h - 23, r.w, 20};
            });
    }
}
int main() {
    try {
        readoutTests();
        {
            MeterTap tap;
            tap.reset(1000); // Five samples per waveform slice.
            tap.sample(.3, -.7, 2, .2, -.4);
            tap.sample(-.5, .6, 0, .9, -.9); // No reduction: no highlight.
            tap.sample(.1, -.1, 2, .05, -.05);
            tap.sample(0, 0, 0, 0, 0);
            MeterFrame f;
            CHECK(!tap.frames.pop(f));
            tap.sample(0, 0, 0, 0, 0);
            CHECK(tap.frames.pop(f));
            near(f.positive, .6);
            near(f.negative, -.7);
            near(f.detectedPositive, .2);
            near(f.detectedNegative, -.4);
            for (unsigned n = 0; n < 5; ++n)
                tap.sample(.4, -.4, 0, 1, -1);
            CHECK(tap.frames.pop(f));
            near(f.positive, .4);
            near(f.negative, -.4); // Stereo never cancels.
            near(f.detectedPositive, 0);
            near(f.detectedNegative, 0);
        }
        {
            // Coherent 8.2 kHz stereo tone must appear in its frequency bucket,
            // even with opposite channel polarity and a coarse logarithmic pixel.
            MeterTap tap;
            DetectionSpectrum spectrum;
            tap.reset(48000);
            for (unsigned n = 0; n < 32768; ++n) {
                const double tone = .5 * std::sin(2 * std::acos(-1.) * 700 * n / 4096.);
                tap.sample(0, 0, 0, tone, -tone);
                if (n % 1024 == 1023)
                    spectrum.update(tap.spectrum.frames.read());
            }
            const double peak = spectrum.level(8100, 8300);
            near(peak, 20 * std::log10(.5), .02);
            CHECK(spectrum.level(3000, 4000) < -90);
            CHECK(spectrum.level(25000, 30000) == -120);
            spectrum.decay(.5);
            near(spectrum.level(8100, 8300), peak - 24, .02);
            tap.reset(96000);
            for (unsigned n = 0; n < 32768; ++n) {
                tap.sample(0, 0, 0, 0, 0);
                if (n % 1024 == 1023)
                    spectrum.update(tap.spectrum.frames.read());
            }
            CHECK(spectrum.level(8100, 8300) < -110);
        }
        {
            EditorState state;
            MeterTap tap;
            std::array<int, parameterCount> depth{};
            unsigned gestures = 0;
            Editor e([&] { return state; },
                     [&](UiKind k, unsigned i, double v) {
                         if (k == UiKind::Begin) {
                             CHECK(++depth[i] == 1);
                             ++gestures;
                         }
                         if (k == UiKind::Value) {
                             CHECK(depth[i] == 1);
                             state.values[i] = state.effective[i] = v;
                         }
                         if (k == UiKind::End)
                             CHECK(--depth[i] == 0);
                     },
                     tap);
            double time = 1;
            auto click = [&](unsigned i, double y = 40, unsigned button = 0, unsigned mods = 0) {
                const auto r = e.controlBounds(i);
                e.press(r.x + r.w / 2, r.y + std::min(y, r.h / 2), button, mods, time);
                e.release(0, 0);
                time += .6;
            };
            // Every control resets from non-default values, including selectors/readouts.
            for (unsigned i = 0; i < parameterCount; ++i) {
                state.values[i] = state.effective[i] =
                    parameter(i).initial == parameter(i).max ? parameter(i).min : parameter(i).max;
                e.tick();
                const auto r = e.controlBounds(i);
                const double x = r.x + r.w / 2, y = r.y + r.h / 2;
                e.press(x, y, 0, 0, time);
                e.release(x, y);
                e.press(x, y, 0, 0, time + .12);
                e.release(x, y);
                time += 1;
                near(state.values[i], parameter(i).initial);
                e.key(PUGL_KEY_ESCAPE, 0);
            }
            click(Attack, 40, 1);
            e.input("2.5 ms");
            e.key(PUGL_KEY_ENTER, 0);
            near(state.values[Attack], 2.5);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Attack], .5);
            e.key('z', PUGL_MOD_CTRL | PUGL_MOD_SHIFT);
            near(state.values[Attack], 2.5);
            click(Threshold, 40, 1);
            e.input("nonsense");
            e.key(PUGL_KEY_ENTER, 0);
            near(state.values[Threshold], -30);
            e.key(PUGL_KEY_ESCAPE, 0);
            // Relative drag switches to fine mode without jumping and forms one undo step.
            auto r = e.controlBounds(Range);
            const auto before = state.values[Range];
            const unsigned count = gestures;
            e.press(r.x + r.w / 2, r.y + 55, 0, 0, time);
            e.motion(r.x + r.w / 2, r.y + 45, 0);
            const auto moved = state.values[Range];
            e.motion(r.x + r.w / 2, r.y + 45, PUGL_MOD_SHIFT);
            near(state.values[Range], moved);
            e.motion(r.x + r.w / 2, r.y + 35, PUGL_MOD_SHIFT);
            e.release(0, 0);
            CHECK(gestures == count + 1);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Range], before);
            // A/B keeps independent edits, and Copy explicitly overwrites the other slot.
            auto command = [&](unsigned i) {
                const auto button = e.headerBounds(i);
                e.press(button.x + 10, button.y + 10, 0, 0, time);
                e.release(0, 0);
                time += .6;
            };
            click(Threshold, 40, 1);
            e.input("-27 dB");
            e.key(PUGL_KEY_ENTER, 0);
            command(4);
            near(state.values[Threshold], -30);
            click(Threshold, 40, 1);
            e.input("-36 dB");
            e.key(PUGL_KEY_ENTER, 0);
            command(3);
            near(state.values[Threshold], -27);
            command(5);
            command(4);
            near(state.values[Threshold], -27);
            // The waveform is a read-only amplitude display, not a dB threshold axis.
            const auto graph = e.graphBounds();
            const auto waveformValues = state.values;
            const auto waveformGestures = gestures;
            e.press(graph.x + 20, graph.y + 20, 0, 0, time);
            e.motion(graph.x + 20, graph.y + 40, 0);
            e.release(0, 0);
            e.press(graph.x + 20, graph.y + 20, 0, 0, time + .12);
            e.release(0, 0);
            CHECK(state.values == waveformValues && gestures == waveformGestures);
            time += .6;
            // Starting-points menu applies a complete preset and is undoable.
            const auto saved = state.values;
            command(0);
            const auto menu = e.headerBounds(0);
            e.press(menu.x + 20, menu.y + menu.h + 8 + 36 + 15, 0, 0, time);
            e.release(0, 0);
            time += .6;
            near(state.values[Range], 4);
            near(state.values[Low], 4500);
            e.key('z', PUGL_MOD_CTRL);
            CHECK(state.values == saved);
            // State load invalidates local history and A/B state.
            state.values[Threshold] = -33;
            ++state.stateSerial;
            e.tick();
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Threshold], -33);
            for (const auto d : depth)
                CHECK(d == 0);
            // Detection handles change frequency with balanced gestures and undo.
            const auto band = e.bandBounds();
            const auto lowBefore = state.values[Low];
            const double handle = band.x + band.w * std::log(lowBefore / 1000) / std::log(22.);
            e.press(handle, band.y + 10, 0, 0, time);
            e.motion(handle - 30, band.y + 10, 0);
            e.release(0, 0);
            time += .6;
            CHECK(state.values[Low] < lowBefore);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Low], lowBefore);
            // Each segmented choice selects the clicked half rather than cycling.
            for (unsigned i : {Detection, Processing, Sidechain}) {
                r = e.controlBounds(i);
                e.press(r.x + r.w - 10, r.y + 10, 0, 0, time);
                e.release(0, 0);
                time += .6;
                near(state.values[i], 1);
                e.press(r.x + 10, r.y + 10, 0, 0, time);
                e.release(0, 0);
                time += .6;
                near(state.values[i], 0);
            }
            // Hit targets and rendering survive compact and HiDPI dimensions.
            for (double scale : {.75, 1., 2., 3.}) {
                CHECK(e.setScale(scale));
                CHECK(e.resize(static_cast<unsigned>(900 * scale),
                               static_cast<unsigned>(600 * scale)));
                for (unsigned i = 0; i < parameterCount; ++i) {
                    r = e.controlBounds(i);
                    CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= 900 && r.y + r.h <= 600);
                }
                auto *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, e.width(), e.height());
                auto *c = cairo_create(s);
                e.paint(c, 900, 600);
                CHECK(cairo_status(c) == CAIRO_STATUS_SUCCESS);
                cairo_destroy(c);
                cairo_surface_destroy(s);
            }
        }
        cairo_debug_reset_static_data();
        FcFini();
        std::cout << "Deesser UI: all defaults, exact entry, undo/redo, fine drag, state reset "
                     "and scaled layouts passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
