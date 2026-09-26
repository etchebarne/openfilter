#include "Editor.hpp"
#include "ReadoutTest.hpp"
#include "Test.hpp"
#include <fontconfig/fontconfig.h>
#include <iostream>
using namespace openfilter::limiter;
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
                if (i == Gain)
                    return e.gainReadoutBounds();
                if (i == Ceiling)
                    return r;
                return openfilter::ui::Rect{r.x, r.y + r.h - 20, r.w, 18};
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
            auto click = [&](unsigned i, unsigned button = 0) {
                auto r = e.controlBounds(i);
                e.press(r.x + r.w / 2, r.y + r.h / 2, button, 0, time);
                e.release(0, 0);
                time += .6;
            };
            for (unsigned i = 0; i < parameterCount; ++i) {
                state.values[i] = state.effective[i] =
                    parameter(i).initial == parameter(i).max ? parameter(i).min : parameter(i).max;
                e.tick();
                auto r = e.controlBounds(i);
                for (double t : {time, time + .12}) {
                    e.press(r.x + r.w / 2, r.y + r.h / 2, 0, 0, t);
                    e.release(0, 0);
                }
                time += 1;
                near(state.values[i], parameter(i).initial);
                e.key(PUGL_KEY_ESCAPE, 0);
            }
            // Style menu selects real engine modes; Legacy disables only the new
            // timing/link controls while preserving their stored values.
            click(Style);
            auto style = e.controlBounds(Style);
            e.press(style.x + 30, style.y - 152 + 18, 0, 0, time);
            e.release(0, 0);
            time += .6;
            near(state.values[Style], Legacy);
            const auto legacyValues = state.values;
            for (unsigned i : {Lookahead, Attack, ReleaseLink}) {
                const auto b = e.controlBounds(i);
                e.scroll(b.x + 30, b.y + 40, 5, 0);
                e.press(b.x + 30, b.y + 40, 1, 0, time);
                e.release(0, 0);
                time += .6;
                e.input("123");
                e.key(PUGL_KEY_ESCAPE, 0);
            }
            CHECK(state.values == legacyValues);
            click(Style);
            e.press(style.x + 30, style.y - 152 + 3 * 36 + 18, 0, 0, time);
            e.release(0, 0);
            time += .6;
            near(state.values[Style], Dense);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Style], Legacy);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Style], Clean);
            click(Release, 1);
            e.input("350 ms");
            e.key(PUGL_KEY_ENTER, 0);
            near(state.values[Release], 350);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Release], 200);
            e.key('z', PUGL_MOD_CTRL | PUGL_MOD_SHIFT);
            near(state.values[Release], 350);
            click(Ceiling, 1);
            e.input("nonsense");
            e.key(PUGL_KEY_ENTER, 0);
            near(state.values[Ceiling], -1);
            e.key(PUGL_KEY_ESCAPE, 0);
            auto r = e.controlBounds(Gain);
            const auto count = gestures;
            e.press(r.x + 40, r.y + 60, 0, 0, time);
            e.motion(r.x + 40, r.y + 40, 0);
            const double moved = state.values[Gain];
            e.motion(r.x + 40, r.y + 40, PUGL_MOD_SHIFT);
            near(state.values[Gain], moved);
            e.motion(r.x + 40, r.y + 30, PUGL_MOD_SHIFT);
            e.release(0, 0);
            time += .6;
            CHECK(gestures == count + 1);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Gain], 0);
            // The integrated readout is both a drag handle and a click-to-edit value.
            r = e.gainReadoutBounds();
            e.press(r.x + 20, r.y + 12, 0, 0, time);
            e.release(0, 0);
            time += .6;
            e.input("10.5 dB");
            e.key(PUGL_KEY_ENTER, 0);
            near(state.values[Gain], 10.5);
            r = e.gainReadoutBounds();
            e.press(r.x + 20, r.y + 12, 0, 0, time);
            e.motion(r.x + 20, r.y - 13, 0);
            e.release(0, 0);
            time += .6;
            CHECK(state.values[Gain] > 10.5);
            const auto dragged = state.values[Gain];
            e.input("1 dB");
            e.key(PUGL_KEY_ESCAPE, 0);
            near(state.values[Gain], dragged);
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Gain], 10.5);
            r = e.gainReadoutBounds();
            for (double t : {time, time + .12}) {
                e.press(r.x + 20, r.y + 12, 0, 0, t);
                e.release(0, 0);
            }
            time += .6;
            near(state.values[Gain], 0);
            auto command = [&](unsigned i) {
                const auto b = e.headerBounds(i);
                e.press(b.x + 10, b.y + 10, 0, 0, time);
                e.release(0, 0);
                time += .6;
            };
            click(Gain, 1);
            e.input("6 dB");
            e.key(PUGL_KEY_ENTER, 0);
            command(4);
            near(state.values[Gain], 0);
            click(Gain, 1);
            e.input("9 dB");
            e.key(PUGL_KEY_ENTER, 0);
            command(3);
            near(state.values[Gain], 6);
            command(5);
            command(4);
            near(state.values[Gain], 6);
            const auto saved = state.values;
            command(0);
            auto menu = e.headerBounds(0);
            e.press(menu.x + 20, menu.y + menu.h + 8 + 36 + 15, 0, 0, time);
            e.release(0, 0);
            time += .6;
            near(state.values[Gain], 3);
            near(state.values[AutoRelease], 1);
            e.key('z', PUGL_MOD_CTRL);
            CHECK(state.values == saved);
            state.values[Gain] = 7;
            ++state.stateSerial;
            e.tick();
            e.key('z', PUGL_MOD_CTRL);
            near(state.values[Gain], 7);
            const auto viewValues = state.values;
            const auto viewGestures = gestures;
            auto view = e.viewBounds(0);
            e.press(view.x + 10, view.y + 10, 0, 0, time);
            e.release(0, 0);
            time += .6;
            r = e.controlBounds(Release);
            e.scroll(r.x + 20, r.y + 40, 4, 0);
            e.press(r.x + 20, r.y + 40, 0, 0, time);
            e.release(0, 0);
            time += .6;
            CHECK(state.values == viewValues && gestures == viewGestures);
            e.press(view.x + 10, view.y + 10, 0, 0, time);
            e.release(0, 0);
            for (double scale : {.75, 1., 2., 3.}) {
                CHECK(e.setScale(scale));
                CHECK(e.resize(static_cast<unsigned>(900 * scale),
                               static_cast<unsigned>(600 * scale)));
                for (unsigned i = 0; i < parameterCount; ++i) {
                    r = e.controlBounds(i);
                    CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= 900 && r.y + r.h <= 600);
                }
                auto *surface =
                    cairo_image_surface_create(CAIRO_FORMAT_ARGB32, e.width(), e.height());
                auto *c = cairo_create(surface);
                e.paint(c, 900, 600);
                CHECK(cairo_status(c) == CAIRO_STATUS_SUCCESS);
                cairo_destroy(c);
                cairo_surface_destroy(surface);
            }
            for (auto d : depth)
                CHECK(d == 0);
        }
        cairo_debug_reset_static_data();
        FcFini();
        std::cout << "Limiter UI: resets, entry, gestures, A/B, undo, presets, drawer and scaling "
                     "passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
