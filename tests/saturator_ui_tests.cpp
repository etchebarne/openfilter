#include "Editor.hpp"
#include "ReadoutTest.hpp"
#include "Test.hpp"
#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <iostream>
using namespace openfilter::saturator;
void readoutTests() {
    for (unsigned i = 0; i < parameterCount; ++i) {
        const auto p = parameter(i);
        if (p.stepped)
            continue;
        const double initial = denormalized(i, .35), value = denormalized(i, .45);
        readoutTest<Editor, EditorState, openfilter::ui::AnalysisTap>(
            i, initial, std::to_string(value), value, p.initial,
            [i](Editor &e, EditorState &s, double &t) {
                (void)i;
                (void)e;
                (void)s;
                (void)t;
                if (i >= globals && i < legacyParameterCount) {
                    auto r = e.viewBounds((i - globals) / stride);
                    e.press(r.x + 10, r.y + 10, 0, 0, t);
                    e.release(0, 0);
                    t += 1;
                }
            },
            [i](Editor &e) {
                const auto r = e.controlBounds(i);
                if (i < globals)
                    return r;
                return openfilter::ui::Rect{r.x, r.y + r.h - 23, r.w, 20};
            });
    }
}
int main() {
    try {
        readoutTests();
        {
            EditorState state;
            openfilter::ui::AnalysisTap tap;
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
                if (i >= globals && i < legacyParameterCount) {
                    const auto tab = e.viewBounds((i - globals) / stride);
                    e.press(tab.x + 10, tab.y + 10, 0, 0, time);
                    e.release(0, 0);
                    time += .6;
                }
                state.values[i] = state.effective[i] =
                    parameter(i).initial == parameter(i).max
                        ? parameter(i).min
                        : (i == CrossoverLow ? 1000 : parameter(i).max);
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
            auto select = [&](unsigned b) {
                const auto r = e.viewBounds(b);
                e.press(r.x + 10, r.y + 10, 0, 0, time);
                e.release(0, 0);
                time += .6;
            };
            select(1);
            // Last row must be reachable in both normal and compact layouts.
            click(band(1, Style));
            const auto styleControl = e.controlBounds(band(1, Style));
            e.press(styleControl.x + 20, styleControl.y - 42 + 18, 0, 0, time);
            e.release(0, 0);
            time += .6;
            near(state.values[band(1, Style)], 5);
            click(band(1, Drive), 40, 1);
            e.input("12.5 dB");
            e.key(PUGL_KEY_ENTER, 0);
            near(state.values[band(1, Drive)], 12.5);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[band(1, Drive)], 6);
            e.key('z', PUGL_MOD_CTRL | PUGL_MOD_SHIFT);
            near(state.values[band(1, Drive)], 12.5);
            click(band(1, Drive), 40, 1);
            e.input("bad");
            e.key(PUGL_KEY_ENTER, 0);
            near(state.values[band(1, Drive)], 12.5);
            e.key(PUGL_KEY_ESCAPE, 0);
            auto r = e.controlBounds(band(1, Drive));
            const auto before = state.values;
            const unsigned count = gestures;
            e.press(r.x + r.w / 2, r.y + 55, 0, 0, time);
            e.motion(r.x + r.w / 2, r.y + 45, 0);
            const auto moved = state.values[band(1, Drive)];
            e.motion(r.x + r.w / 2, r.y + 45, PUGL_MOD_SHIFT);
            near(state.values[band(1, Drive)], moved);
            e.motion(r.x + r.w / 2, r.y + 35, PUGL_MOD_SHIFT);
            e.release(0, 0);
            time += .6;
            CHECK(gestures == count + 1);
            e.key('z', PUGL_MOD_CTRL);
            CHECK(state.values == before);
            // Crossover graph hit regions produce horizontal frequency gestures.
            r = e.controlBounds(CrossoverLow);
            const double cx = r.x + r.w / 2, cy = e.graphBounds().y + 75;
            const double oldCross = state.values[CrossoverLow];
            e.press(cx, cy, 0, 0, time);
            e.motion(cx + 30, cy, 0);
            e.release(0, 0);
            time += .6;
            CHECK(state.values[CrossoverLow] > oldCross);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[CrossoverLow], oldCross);
            // Crossover readouts support single-click exact entry, like other values.
            click(CrossoverLow);
            e.input("320 Hz");
            e.key(PUGL_KEY_ENTER, 0);
            near(state.values[CrossoverLow], 320);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[CrossoverLow], oldCross);
            // Switching visible bands is a UI action, not a parameter edit.
            const auto previous = state.values;
            const auto previousGestures = gestures;
            select(0);
            select(2);
            CHECK(state.values == previous && gestures == previousGestures);
            // A/B isolates edits and Copy overwrites the destination explicitly.
            auto command = [&](unsigned i) {
                const auto r = e.headerBounds(i);
                e.press(r.x + 10, r.y + 10, 0, 0, time);
                e.release(0, 0);
                time += .6;
            };
            select(1);
            click(band(1, Drive), 40, 1);
            e.input("19 dB");
            e.key(PUGL_KEY_ENTER, 0);
            command(4);
            near(state.values[band(1, Drive)], 6);
            command(3);
            near(state.values[band(1, Drive)], 19);
            command(5);
            command(4);
            near(state.values[band(1, Drive)], 19);
            // Presets are whole-state edits and undo as one operation.
            const auto saved = state.values;
            command(0);
            const auto menu = e.headerBounds(0);
            e.press(menu.x + 20, menu.y + menu.h + 8 + 36 + 15, 0, 0, time);
            e.release(0, 0);
            time += .6;
            near(state.values[Mix], 40);
            near(state.values[band(1, Style)], 3);
            e.key('z', PUGL_MOD_CTRL);
            CHECK(state.values == saved);
            // Crossing host automation must still expose disjoint readout hit targets.
            state.values[CrossoverLow] = 4000;
            state.values[CrossoverHigh] = 200;
            state.effective = state.values;
            e.tick();
            const auto low = e.controlBounds(CrossoverLow), high = e.controlBounds(CrossoverHigh);
            CHECK(low.x + low.w <= high.x);
            state.values[CrossoverLow] = 250;
            state.values[CrossoverHigh] = 4000;
            state.effective = state.values;
            e.tick();
            // Host state invalidates local undo.
            state.values[band(2, Drive)] = 17;
            ++state.stateSerial;
            e.tick();
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[band(2, Drive)], 17);
            for (auto d : depth)
                CHECK(d == 0);
            // Hit targets and rendering survive compact and HiDPI dimensions.
            for (double scale : {.75, 1., 2., 3.}) {
                CHECK(e.setScale(scale));
                CHECK(e.resize(static_cast<unsigned>(900 * scale),
                               static_cast<unsigned>(600 * scale)));
                for (unsigned i = 0; i < parameterCount; ++i) {
                    r = e.controlBounds(i);
                    CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= 900 && r.y + r.h <= 600);
                }
                // The floating controls retain independent targets at every scale.
                // Exercise primary-click readouts and real drags, not paint-only bounds.
                for (unsigned b = 0; b < 3; ++b) {
                    select(b);
                    for (unsigned f :
                         {unsigned(BandMix), unsigned(Dynamics), unsigned(Drive), unsigned(Level),
                          unsigned(Bass), unsigned(Mid), unsigned(Treble), unsigned(Presence)}) {
                        const unsigned id = band(b, f);
                        const auto control = e.controlBounds(id);
                        const double x = control.x + control.w / 2;
                        e.press(x, control.y + control.h - 12, 0, 0, time);
                        e.release(0, 0);
                        time += .6;
                        const double value = f == BandMix ? 73 : 3;
                        e.input(std::to_string(value));
                        e.key(PUGL_KEY_ENTER, 0);
                        near(state.values[id], value);
                        const auto beforeDrag = state.values;
                        e.press(x, control.y + 30, 0, 0, time);
                        e.motion(x, control.y + 20, 0);
                        e.release(0, 0);
                        time += .6;
                        CHECK(state.values[id] > beforeDrag[id]);
                        for (unsigned other = 0; other < parameterCount; ++other)
                            if (other != id)
                                near(state.values[other], beforeDrag[other]);
                    }
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
        std::cout << "Saturator UI: all defaults, exact entry, undo/redo, fine drag, state reset "
                     "and scaled layouts passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
