#include "Editor.hpp"
#include "ReadoutTest.hpp"
#include "Test.hpp"
#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <iostream>
using namespace openfilter::gate;
void readoutTests() {
    for (unsigned i = 0; i < parameterCount; ++i) {
        const auto p = parameter(i);
        if (p.stepped)
            continue;
        const double initial = denormalized(i, .35), value = denormalized(i, .45);
        readoutTest<Editor, EditorState, MeterTap>(
            i, initial, std::to_string(value), value, p.initial,
            [](Editor &, EditorState &, double &) {},
            [i](Editor &e) {
                const auto r = e.controlBounds(i);
                if (r.h == 54)
                    return openfilter::ui::Rect{r.x + r.w * .5, r.y + 3, r.w * .4, 20};
                if (r.h == 28)
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
            e.input("12.5 ms");
            e.key(PUGL_KEY_ENTER, 0);
            near(state.values[Attack], 12.5);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Attack], 1);
            e.key('z', PUGL_MOD_CTRL | PUGL_MOD_SHIFT);
            near(state.values[Attack], 12.5);
            click(Threshold, 40, 1);
            e.input("nonsense");
            e.key(PUGL_KEY_ENTER, 0);
            near(state.values[Threshold], -36);
            e.key(PUGL_KEY_ESCAPE, 0);
            // Relative drag switches to fine mode without jumping and forms one undo step.
            auto r = e.controlBounds(Ratio);
            const auto before = state.values[Ratio];
            const unsigned count = gestures;
            e.press(r.x + r.w / 2, r.y + 55, 0, 0, time);
            e.motion(r.x + r.w / 2, r.y + 45, 0);
            const auto moved = state.values[Ratio];
            e.motion(r.x + r.w / 2, r.y + 45, PUGL_MOD_SHIFT);
            near(state.values[Ratio], moved);
            e.motion(r.x + r.w / 2, r.y + 35, PUGL_MOD_SHIFT);
            e.release(0, 0);
            CHECK(gestures == count + 1);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Ratio], before);
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
            near(state.values[Threshold], -36);
            click(Threshold, 40, 1);
            e.input("-36 dB");
            e.key(PUGL_KEY_ENTER, 0);
            command(3);
            near(state.values[Threshold], -27);
            command(5);
            command(4);
            near(state.values[Threshold], -27);
            // The transfer graph edits threshold in one gesture; its points share dB units.
            const auto graph = e.graphBounds();
            e.press(graph.x + graph.w * .5, graph.y + 10, 0, 0, time);
            e.motion(graph.x + graph.w * .25, graph.y + 20, 0);
            e.release(0, 0);
            time += .6;
            near(state.values[Threshold], -60);
            // Double-clicking the exposed knee follows the suite's default-reset rule.
            e.press(graph.x + graph.w * .5, graph.y + 12, 0, 0, time);
            e.release(0, 0);
            e.press(graph.x + graph.w * .5, graph.y + 12, 0, 0, time + .12);
            e.release(0, 0);
            time += .6;
            near(state.values[Threshold], parameter(Threshold).initial);
            // Starting-points menu applies a complete preset and is undoable.
            const auto saved = state.values;
            command(0);
            const auto menu = e.headerBounds(0);
            e.press(menu.x + 20, menu.y + menu.h + 8 + 36 + 15, 0, 0, time);
            e.release(0, 0);
            time += .6;
            near(state.values[Ratio], 2);
            near(state.values[Detector], 1);
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
            // View changes never send audio parameters, and hidden sidechain controls
            // cannot receive pointer or keyboard edits through the graph.
            auto toggleView = [&](unsigned i) {
                const auto r = e.viewBounds(i);
                e.press(r.x + 10, r.y + 10, 0, 0, time);
                e.release(0, 0);
                time += .6;
            };
            const auto viewValues = state.values;
            const unsigned viewGestures = gestures;
            toggleView(1);
            r = e.controlBounds(SidechainHP);
            e.scroll(r.x + 20, r.y + 10, 2, 0);
            e.press(r.x + 20, r.y + 10, 0, 0, time);
            e.release(0, 0);
            time += .6;
            CHECK(state.values == viewValues && gestures == viewGestures);
            toggleView(0);
            const auto hiddenGraph = e.graphBounds();
            e.press(hiddenGraph.x + 20, hiddenGraph.y + 20, 0, 0, time);
            e.release(0, 0);
            time += .6;
            CHECK(state.values == viewValues && gestures == viewGestures);
            toggleView(0);
            toggleView(1);
            // Sidechain sliders use horizontal fine dragging as one balanced gesture.
            r = e.controlBounds(SidechainHP);
            e.press(r.x + 10, r.y + 39, 0, 0, time);
            e.motion(r.x + 30, r.y + 39, 0);
            e.release(0, 0);
            time += .6;
            CHECK(state.values[SidechainHP] > viewValues[SidechainHP]);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[SidechainHP], viewValues[SidechainHP]);
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
        std::cout << "Gate UI: all defaults, exact entry, undo/redo, fine drag, state reset "
                     "and scaled layouts passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
