#pragma once
#include "Test.hpp"
#include <array>
#include <pugl/pugl.h>
#include <string>

// Exercise actual editors, not just the shared pointer helper. Locate returns
// the painted numeric field; setup exposes drawers/bands for that editor.
template <class Editor, class State, class Tap, class Setup, class Locate>
void readoutTest(unsigned id, double initial, const std::string &typed, double expected,
                 double reset, Setup setup, Locate locate) {
    for (double scale : {.75, 1., 2.}) {
        State state;
        state.values[id] = initial;
        state.effective = state.values;
        Tap tap;
        std::array<int, std::tuple_size_v<decltype(state.values)>> depth{};
        unsigned begins = 0, values = 0, ends = 0;
        Editor e([&] { return state; },
                 [&](auto kind, unsigned i, double v) {
                     using Kind = decltype(kind);
                     if (kind == Kind::Begin) {
                         CHECK(++depth[i] == 1);
                         ++begins;
                     }
                     if (kind == Kind::Value) {
                         CHECK(depth[i] == 1);
                         state.values[i] = state.effective[i] = v;
                         ++values;
                     }
                     if (kind == Kind::End) {
                         CHECK(--depth[i] == 0);
                         ++ends;
                     }
                 },
                 tap);
        CHECK(e.setScale(scale));
        const unsigned width = scale == 1 ? 1120 : 900;
        const unsigned height = scale == 1 ? 720 : 600;
        CHECK(e.resize(unsigned(width * scale), unsigned(height * scale)));
        double time = 10;
        setup(e, state, time);
        auto point = [&] {
            auto r = locate(e);
            CHECK(r.w > 0 && r.h > 0);
            return std::pair{r.x + r.w / 2, r.y + r.h / 2};
        };
        auto [x, y] = point();
        const auto original = state.values;
        begins = values = ends = 0;
        e.press(x, y, 0, 0, time);
        e.motion(x + 1, y - 2, 0); // Jitter is neither a drag nor a host gesture.
        CHECK(state.values == original && begins == 0 && values == 0);
        e.motion(x, y - 20, 0);
        CHECK(state.values[id] > initial && begins == 1 && ends == 0);
        const double moved = state.values[id];
        e.motion(x, y - 20, PUGL_MOD_SHIFT);
        near(state.values[id], moved);
        e.motion(x, y - 30, PUGL_MOD_SHIFT);
        CHECK(state.values[id] > moved && state.values[id] - moved < (moved - initial) * .5);
        const auto after = state.values;
        e.release(x, y - 30);
        CHECK(begins == 1 && ends == 1);
        e.input(typed); // Drag-release must not open entry.
        CHECK(state.values == after);
        e.key('z', PUGL_MOD_CTRL);
        CHECK(state.values == original); // One drag, one undo.
        e.key('z', PUGL_MOD_CTRL | PUGL_MOD_SHIFT);
        CHECK(state.values == after);
        e.key('z', PUGL_MOD_CTRL);
        time += 1;
        std::tie(x, y) = point();
        begins = values = ends = 0;
        e.press(x, y, 0, 0, time);
        e.motion(x + 1, y + 1, 0);
        e.release(x, y);
        CHECK(begins == 0 && state.values == original);
        e.input(typed);
        e.key(PUGL_KEY_ENTER, 0);
        near(state.values[id], expected, 1e-6);
        CHECK(begins == 1 && ends == 1);
        time += 1;
        std::tie(x, y) = point();
        e.press(x, y, 0, 0, time);
        e.release(x, y);
        e.press(x, y, 0, 0, time + .12);
        e.release(x, y);
        near(state.values[id], reset, 1e-9);
        e.input(typed); // Reset must not leave a text edit active.
        near(state.values[id], reset, 1e-9);
        time += 1;
        std::tie(x, y) = point();
        const auto canceled = state.values;
        begins = values = ends = 0;
        e.press(x, y, 0, 0, time);
        e.finishGesture(); // Focus loss/hide cancels an unstarted click.
        e.release(x, y);
        e.input(typed);
        CHECK(state.values == canceled && begins == 0 && ends == 0);
        for (auto d : depth)
            CHECK(d == 0);
    }
}
