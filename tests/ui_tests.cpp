#include "Editor.hpp"
#include "Engine.hpp"
#include "Response.hpp"
#include "Test.hpp"
#include <iostream>
#include <vector>

using namespace openfilter;
void responseTest() {
    auto values = eq::defaults();
    for (unsigned b = 0; b < 5; ++b) {
        values[eq::index(b, eq::Enabled)] = 1;
        values[eq::index(b, eq::Type)] = b % 3;
        values[eq::index(b, eq::Routing)] = b;
        values[eq::index(b, eq::Frequency)] = std::log2(150. * std::pow(3., b));
        values[eq::index(b, eq::Gain)] = b % 2 ? -4 : 3;
    }
    constexpr unsigned size = 32768;
    const double hz = 1500, rate = 48000;
    for (unsigned channel = 0; channel < 2; ++channel) {
        eq::Engine engine;
        engine.prepare(rate, values);
        std::complex<double> lResponse = 0, rResponse = 0;
        for (unsigned n = 0; n < size; ++n) {
            double l = n == 0 && channel == 0 ? 1 : 0, r = n == 0 && channel == 1 ? 1 : 0;
            engine.sample(l, r);
            const auto phase = std::polar(1., -2 * std::numbers::pi * hz * n / rate);
            lResponse += l * phase;
            rResponse += r * phase;
        }
        const auto predicted = eq::totalTransfer(values, hz, rate);
        CHECK(std::abs(lResponse - (channel == 0 ? predicted.ll : predicted.lr)) < 1e-9);
        CHECK(std::abs(rResponse - (channel == 0 ? predicted.rl : predicted.rr)) < 1e-9);
    }
    for (unsigned shape = 0; shape < 6; ++shape)
        for (unsigned slope = 0; slope < 3; ++slope) {
            values = eq::defaults();
            values[eq::index(0, eq::Enabled)] = 1;
            values[eq::index(0, eq::Type)] = shape;
            values[eq::index(0, eq::Slope)] = slope;
            values[eq::index(0, eq::Gain)] = 9;
            eq::Engine engine;
            engine.prepare(rate, values);
            eq::Complex measured = 0;
            for (unsigned n = 0; n < size; ++n) {
                double l = n == 0 ? 1 : 0, r = 0;
                engine.sample(l, r);
                measured += l * std::polar(1., -2 * std::numbers::pi * hz * n / rate);
            }
            CHECK(std::abs(measured - eq::bandTransfer(values, 0, hz, rate)) < 1e-9);
        }
}
void brickwallResponse() {
    auto v = eq::defaults();
    v[eq::index(0, eq::Enabled)] = 1;
    v[eq::index(0, eq::Slope)] = 3;
    for (int type : {3, 4}) {
        v[eq::index(0, eq::Type)] = type;
        eq::Engine engine;
        engine.prepare(48000, v);
        constexpr std::array<double, 3> frequencies{900, 1000, 1100};
        std::array<eq::Complex, 3> measured{};
        // Let the steep filter's ringing decay before comparing a steady-state curve.
        for (unsigned n = 0; n < 131072; ++n) {
            double l = n == 0 ? 1 : 0, r = l;
            engine.sample(l, r);
            for (unsigned i = 0; i < frequencies.size(); ++i)
                measured[i] +=
                    l * std::polar(1., -2 * std::numbers::pi * frequencies[i] * n / 48000);
        }
        for (unsigned i = 0; i < frequencies.size(); ++i)
            CHECK(std::abs(measured[i] - eq::bandTransfer(v, 0, frequencies[i], 48000)) < 1e-8);
        CHECK(std::abs(measured[type == 3 ? 0 : 2]) < 1.01e-5);
        CHECK(std::abs(measured[type == 3 ? 2 : 0]) > .998);
    }
    ui::AnalysisTap tap;
    eq::EditorState state;
    state.values = state.effective = v;
    eq::Editor editor([&] { return state; },
                      [&](eq::UiKind k, unsigned i, double value) {
                          if (k == eq::UiKind::Value)
                              state.values[i] = state.effective[i] = value;
                      },
                      tap);
    const auto q = editor.controlBounds(3);
    editor.press(q.x + 20, q.y + 40, 1, 0, 1);
    editor.input("4");
    editor.key(PUGL_KEY_ENTER, 0);
    editor.scroll(q.x + 20, q.y + 40, 2, 0);
    CHECK(state.values[eq::index(0, eq::Q)] == eq::defaults()[eq::index(0, eq::Q)]);
    // Slope remains a normal parameter: two clicks reset to the original 12 dB/oct.
    auto shape = editor.controlBounds(0);
    for (double time : {2., 2.15}) {
        editor.press(shape.x + 50, shape.y + 132, 0, 0, time);
        editor.release(shape.x + 50, shape.y + 132);
    }
    CHECK(state.values[eq::index(0, eq::Slope)] == 0);
    editor.key('z', PUGL_MOD_CTRL);
    CHECK(state.values[eq::index(0, eq::Slope)] == 3);
}
void spectrumTest() {
    ui::AudioFrame frame;
    frame.serial = 1;
    frame.rate = 48000;
    constexpr unsigned bin = 85;
    for (unsigned n = 0; n < ui::fftSize; ++n) {
        const float x = .5f * std::sin(2 * std::numbers::pi * bin * n / ui::fftSize);
        frame.samples[0][n] = x;
        frame.samples[1][n] = -x;
        frame.samples[2][n] = frame.samples[3][n] = x;
    }
    ui::Spectrum s;
    for (unsigned i = 0; i < 20; ++i) {
        ++frame.serial;
        s.update(frame);
    }
    near(s.db[0][bin], 20 * std::log10(.5), 1e-5);
    near(s.db[0][bin], s.db[1][bin], 1e-6);
    CHECK(s.db[0][bin + 8] < -100); // anti-phase stereo input must not disappear
}
void interactions() {
    eq::EditorState state;
    ui::AnalysisTap tap;
    std::vector<eq::UiMessage> messages;
    eq::Editor editor([&] { return state; },
                      [&](eq::UiKind k, unsigned i, double v) {
                          messages.push_back({k, i, v});
                          if (k == eq::UiKind::Value)
                              state.values[i] = state.effective[i] = v;
                      },
                      tap);
    // One click creates a band; a double click on an existing value resets it.
    editor.press(540, 280, 0, 0, 1);
    editor.release(540, 280);
    CHECK(state.values[eq::index(0, eq::Enabled)] == 1);
    const auto start = state.values;
    editor.key('z', PUGL_MOD_CTRL);
    CHECK(editor.selectedBand() == -1);
    editor.key('z', PUGL_MOD_CTRL | PUGL_MOD_SHIFT);
    CHECK(state.values == start);
    editor.press(540, 280, 0, 0, 2);
    editor.motion(620, 250, 0);
    const auto dragged = state.values;
    editor.motion(620, 250, PUGL_MOD_SHIFT);
    CHECK(state.values == dragged);
    editor.release(620, 250);
    CHECK(state.values[eq::index(0, eq::Frequency)] > start[eq::index(0, eq::Frequency)]);
    editor.scroll(620, 250, 1, 0);
    CHECK(state.values[eq::index(0, eq::Q)] > start[eq::index(0, eq::Q)]);
    double time = 4;
    auto click = [&](ui::Rect r, unsigned button = 0) {
        editor.press(r.x + r.w / 2, r.y + r.h / 2, button, 0, time);
        editor.release(r.x + r.w / 2, r.y + r.h / 2);
        time += 1;
    };
    auto reset = [&](ui::Rect r) {
        editor.press(r.x + r.w / 2, r.y + r.h / 2, 0, 0, time);
        editor.release(r.x + r.w / 2, r.y + r.h / 2);
        editor.press(r.x + r.w / 2, r.y + r.h / 2, 0, 0, time + .15);
        editor.release(r.x + r.w / 2, r.y + r.h / 2);
        time += 1;
    };
    // Secondary click preserves exact entry, including units.
    click(editor.controlBounds(1), 1);
    editor.input("2.5 kHz");
    editor.key(PUGL_KEY_ENTER, 0);
    near(std::exp2(state.values[eq::index(0, eq::Frequency)]), 2500);
    const auto beforeReset = state.values;
    reset(editor.controlBounds(1));
    near(state.values[eq::index(0, eq::Frequency)],
         eq::parameter(eq::index(0, eq::Frequency)).initial);
    editor.key('z', PUGL_MOD_CTRL);
    CHECK(state.values == beforeReset);
    for (unsigned f = 1; f <= 3; ++f) {
        reset(editor.controlBounds(f));
        const auto i = eq::index(0, f == 1 ? eq::Frequency : f == 2 ? eq::Gain : eq::Q);
        near(state.values[i], eq::parameter(i).initial);
    }
    // Readout click opens text entry; another quick primary click resets it.
    auto r = editor.controlBounds(2);
    r.y += 120;
    r.h = 28;
    click(r);
    editor.input("8 dB");
    editor.key(PUGL_KEY_ENTER, 0);
    near(state.values[eq::index(0, eq::Gain)], 8);
    reset(r);
    near(state.values[eq::index(0, eq::Gain)], 0);
    // Type selector: choose Low Cut, then double-click selector to restore Bell.
    auto shape = editor.controlBounds(0);
    ui::Rect shapeButton{shape.x + 4, shape.y + 48, shape.w - 8, 32};
    click(shapeButton);
    click({shape.x + 20, shape.y - 214 + 5 + 3 * 34, 120, 32});
    CHECK(state.values[eq::index(0, eq::Type)] == 3);
    reset(shapeButton);
    CHECK(state.values[eq::index(0, eq::Type)] == 0);
    // Routing and enabled toggles reset to descriptor defaults too.
    auto route = editor.controlBounds(4);
    click(route);
    click({route.x + 20, route.y - 180 + 5 + 3 * 34, 120, 32});
    CHECK(state.values[eq::index(0, eq::Routing)] == 3);
    reset(route);
    CHECK(state.values[eq::index(0, eq::Routing)] == 0);
    auto p = editor.panelBounds();
    reset({p.x + 8, p.y + 5, 28, 24});
    CHECK(state.values[eq::index(0, eq::Enabled)] == 0);
    click({p.x + 8, p.y + 5, 28, 24});
    CHECK(state.values[eq::index(0, eq::Enabled)] == 1);
    // Bypass and output obey the same reset convention.
    click(editor.headerBounds(6));
    CHECK(state.values[0] == 1);
    reset(editor.headerBounds(6));
    CHECK(state.values[0] == 0);
    ui::Rect output{921, 689, 180, 26};
    click(output, 1);
    editor.input("-7 dB");
    editor.key(PUGL_KEY_ENTER, 0);
    CHECK(state.values[1] == -7);
    reset(output);
    CHECK(state.values[1] == 0);
    // Floating panel must intercept empty areas rather than create a band behind it.
    auto panel = editor.panelBounds();
    click({panel.x + 200, panel.y + 2, 50, 20});
    CHECK(state.values[eq::index(1, eq::Enabled)] == 0);
    const auto comparison = state.values;
    click(editor.headerBounds(3));
    CHECK(state.values == eq::defaults() && editor.selectedBand() == -1);
    click(editor.headerBounds(2));
    CHECK(state.values == comparison);
    editor.tick();
    editor.key(PUGL_KEY_DELETE, 0);
    CHECK(state.values[eq::index(0, eq::Enabled)] == 0);
    editor.key('z', PUGL_MOD_CTRL);
    CHECK(state.values[eq::index(0, eq::Enabled)] == 1);
    std::array<int, eq::parameterCount> gestures{};
    for (const auto &m : messages) {
        if (m.kind == eq::UiKind::Begin)
            ++gestures[m.index];
        if (m.kind == eq::UiKind::End)
            --gestures[m.index];
        CHECK(gestures[m.index] >= 0);
    }
    for (auto n : gestures)
        CHECK(n == 0);
    // Reset an existing graph node without deleting its band or changing its type.
    editor.key(PUGL_KEY_ESCAPE, 0);
    auto g = editor.graphBounds();
    const double nx =
        g.x +
        g.w * std::log(std::exp2(state.values[eq::index(0, eq::Frequency)]) / 10) / std::log(3000.);
    const double ny = g.y + g.h * (.5 - state.values[eq::index(0, eq::Gain)] / 24);
    reset({nx - 1, ny - 1, 2, 2});
    for (auto f : {eq::Frequency, eq::Gain, eq::Q})
        near(state.values[eq::index(0, f)], eq::parameter(eq::index(0, f)).initial);
    CHECK(state.values[eq::index(0, eq::Enabled)] == 1);
    ui::ClickTracker clicks;
    CHECK(!clicks.press(1, 10, 10, 0, 1));
    CHECK(!clicks.press(2, 10, 10, 0, 1.1));
    CHECK(!clicks.press(2, 20, 10, 0, 1.2));
    clicks.motion(30, 10);
    CHECK(!clicks.press(2, 20, 10, 0, 1.3));
    CHECK(clicks.press(2, 20, 10, 0, 1.4));
    editor.key(PUGL_KEY_ESCAPE, 0);
    click({798, 518, 4, 4});
    CHECK(state.values[eq::index(1, eq::Enabled)] == 1);
    CHECK(editor.panelBounds().y + editor.panelBounds().h < 520); // Deep cuts remain reachable.
    click({1031, 11, 60, 28});                                    // Help overlays the graph.
    click({760, 180, 4, 4});
    CHECK(state.values[eq::index(2, eq::Enabled)] == 0);
    CHECK(editor.resize(900, 600));
    CHECK(!editor.resize(100, 100));
    CHECK(editor.setScale(2));
    CHECK(editor.width() == 1800);
}
void panelFollowsNodeDrag() {
    for (const auto size : {std::pair{1120u, 720u}, std::pair{900u, 600u}}) {
        eq::EditorState state;
        state.values[eq::index(0, eq::Enabled)] = 1;
        state.effective = state.values;
        ui::AnalysisTap tap;
        std::vector<eq::UiMessage> messages;
        eq::Editor editor([&] { return state; },
                          [&](eq::UiKind kind, unsigned i, double value) {
                              messages.push_back({kind, i, value});
                              if (kind == eq::UiKind::Value)
                                  state.values[i] = state.effective[i] = value;
                          },
                          tap);
        CHECK(editor.resize(size.first, size.second));
        const auto original = state.values;
        const auto g = editor.graphBounds();
        const double x = g.x + g.w * std::log(1000. / 10) / std::log(3000.);
        editor.press(x, g.y + g.h / 2, 0, 0, 1);
        const auto lower = editor.panelBounds();
        editor.motion(x + 10, g.y + g.h - 8, 0);
        const auto upper = editor.panelBounds();
        CHECK(upper.y < lower.y); // Moves before the mouse is released.
        CHECK(upper.y + upper.h < g.y + g.h - 8);
        for (const auto &message : messages)
            CHECK(message.kind != eq::UiKind::End); // Repositioning must not split the gesture.
        editor.motion(x + 20, g.y + 8, 0);
        const auto back = editor.panelBounds();
        CHECK(back.y == lower.y);
        editor.release(x + 20, g.y + 8);
        CHECK(editor.panelBounds().x == back.x && editor.panelBounds().y == back.y);
        std::array<int, eq::parameterCount> begins{}, ends{};
        for (const auto &message : messages) {
            if (message.kind == eq::UiKind::Begin)
                ++begins[message.index];
            if (message.kind == eq::UiKind::End)
                ++ends[message.index];
        }
        CHECK(begins == ends);
        CHECK(begins[eq::index(0, eq::Frequency)] == 1);
        CHECK(begins[eq::index(0, eq::Gain)] == 1);
        editor.key('z', PUGL_MOD_CTRL);
        CHECK(state.values == original); // The complete drag remains one undo operation.
        // Knob edits keep the controls under the pointer, even when gain crosses the boundary.
        const auto beforeKnob = editor.panelBounds();
        const auto knob = editor.controlBounds(2);
        editor.press(knob.x + knob.w / 2, knob.y + 64, 0, 0, 3);
        editor.motion(knob.x + knob.w / 2, knob.y + 240, 0);
        CHECK(state.values[eq::index(0, eq::Gain)] < -10);
        CHECK(editor.panelBounds().x == beforeKnob.x && editor.panelBounds().y == beforeKnob.y);
        editor.release(knob.x + knob.w / 2, knob.y + 240);
        CHECK(editor.panelBounds().y == beforeKnob.y);
    }
}
int main() {
    try {
        responseTest();
        spectrumTest();
        brickwallResponse();
        interactions();
        panelFollowsNodeDrag();
        std::cout << "UI: response matrix, stereo FFT calibration, gestures, graph editing, "
                     "numeric entry, menus, undo and scaling passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
