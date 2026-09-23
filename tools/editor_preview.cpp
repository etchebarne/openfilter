#include "Editor.hpp"
#include "Engine.hpp"
#include <cairo.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>

using namespace openfilter;
int main(int argc, char **argv) {
    const std::filesystem::path destination = argc > 1 ? argv[1] : "reports/ui";
    std::filesystem::create_directories(destination);
    eq::EditorState state;
    ui::AnalysisTap tap;
    auto send = [&](eq::UiKind kind, unsigned i, double value) {
        if (kind == eq::UiKind::Value) {
            state.values[i] = state.effective[i] = value;
        }
    };
    auto render = [&](eq::Editor &editor, const char *name, unsigned w, unsigned h) {
        editor.resize(w, h);
        auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        auto *cr = cairo_create(surface);
        editor.paint(cr, w / editor.scale(), h / editor.scale());
        cairo_destroy(cr);
        const auto path = destination / name;
        if (cairo_surface_write_to_png(surface, path.c_str()) != CAIRO_STATUS_SUCCESS)
            return false;
        cairo_surface_destroy(surface);
        std::cout << path << '\n';
        return true;
    };
    eq::Editor empty([&] { return state; }, send, tap);
    render(empty, "empty.png", 1120, 720);
    const double frequencies[]{70, 240, 1600, 8500};
    const double gains[]{0, -3.5, 2, 3.5};
    const int types[]{3, 0, 0, 2};
    for (unsigned b = 0; b < 4; ++b) {
        state.values[eq::index(b, eq::Enabled)] = 1;
        state.values[eq::index(b, eq::Frequency)] = std::log2(frequencies[b]);
        state.values[eq::index(b, eq::Gain)] = gains[b];
        state.values[eq::index(b, eq::Type)] = types[b];
    }
    state.values[eq::index(0, eq::Slope)] = 1;
    state.values[eq::index(1, eq::Q)] = std::log2(1.5);
    state.effective = state.values;
    auto analyze = [&] {
        eq::Engine engine;
        engine.prepare(48000, state.values);
        std::mt19937 rng(3456);
        std::uniform_real_distribution<double> noise(-1, 1);
        double slow = 0;
        for (unsigned n = 0; n < 65536; ++n) {
            slow = .97 * slow + .03 * noise(rng);
            double l = slow * .6 + noise(rng) * .012 + .04 * std::sin(n * .028), r = l * .82;
            const double il = l, ir = r;
            engine.sample(l, r);
            tap.sample(il, ir, l, r);
        }
    };
    analyze();
    state.peaks = {.48, .43, .62, .55};
    eq::Editor demo([&] { return state; }, send, tap);
    demo.tick();
    const auto graph = demo.graphBounds();
    const double bandX = graph.x + graph.w * std::log(240. / 10) / std::log(3000.);
    const double bandY = graph.y + graph.h * (.5 + 3.5 / 24);
    demo.key(PUGL_KEY_ESCAPE, 0); // Reveal the node before selecting it through a floating panel.
    demo.press(bandX, bandY, 0, 0, 1);
    demo.release(bandX, bandY);
    render(demo, "editor.png", 1120, 720);
    render(demo, "compact.png", 900, 600);
    demo.setScale(2);
    render(demo, "hidpi.png", 2240, 1440);
    demo.setScale(1);
    demo.resize(1120, 720);
    const auto shape = demo.controlBounds(0);
    demo.press(shape.x + 40, shape.y + 40, 0, 0, 2);
    demo.release(shape.x + 40, shape.y + 40);
    render(demo, "menu.png", 1120, 720);
    demo.press(1, 1, 0, 0, 3);
    demo.release(1, 1);
    demo.press(bandX, bandY, 0, PUGL_MOD_ALT, 4);
    demo.release(bandX, bandY);
    render(demo, "disabled.png", 1120, 720);
    demo.press(bandX, bandY, 0, PUGL_MOD_ALT, 5);
    demo.release(bandX, bandY);
    auto entry = demo.controlBounds(1);
    demo.press(entry.x + entry.w / 2, entry.y + 40, 1, 0, 6);
    demo.release(entry.x + entry.w / 2, entry.y + 40);
    demo.input("240 Hz");
    render(demo, "entry.png", 1120, 720);
    demo.key(PUGL_KEY_ESCAPE, 0);
    demo.press(1060, 25, 0, 0, 7);
    demo.release(1060, 25);
    render(demo, "help.png", 1120, 720);
    demo.key(PUGL_KEY_ESCAPE, 0);
    for (unsigned b = 0; b < eq::bands; ++b) {
        state.values[eq::index(b, eq::Enabled)] = 1;
        state.values[eq::index(b, eq::Frequency)] = std::log2(25. * std::pow(800., b / 23.));
        state.values[eq::index(b, eq::Gain)] = (b % 2 ? -1 : 1) * 2.;
    }
    state.effective = state.values;
    // A realistic low-cut example for comparing the Brickwall workflow and layout.
    const auto denseState = state;
    state.values = eq::defaults();
    state.values[eq::index(0, eq::Enabled)] = 1;
    state.values[eq::index(0, eq::Type)] = 3;
    state.values[eq::index(0, eq::Slope)] = 3;
    state.values[eq::index(0, eq::Frequency)] = std::log2(146.9);
    state.effective = state.values;
    analyze();
    eq::Editor brickwall([&] { return state; }, send, tap);
    brickwall.tick();
    render(brickwall, "brickwall.png", 1120, 720);
    render(brickwall, "brickwall-compact.png", 900, 600);
    auto slope = brickwall.controlBounds(0);
    brickwall.press(slope.x + 50, slope.y + 132, 0, 0, 10);
    brickwall.release(slope.x + 50, slope.y + 132);
    render(brickwall, "brickwall-menu.png", 900, 600);
    state.values[eq::index(0, eq::Type)] = 4;
    state.values[eq::index(0, eq::Frequency)] = std::log2(8000.);
    state.effective = state.values;
    analyze();
    eq::Editor highcut([&] { return state; }, send, tap);
    highcut.tick();
    render(highcut, "brickwall-highcut.png", 1120, 720);
    state = denseState;
    analyze();
    eq::Editor stress([&] { return state; }, send, tap);
    render(stress, "24-bands.png", 900, 600);
    auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1120, 720);
    auto *cr = cairo_create(surface);
    const auto start = std::chrono::steady_clock::now();
    for (unsigned n = 0; n < 60; ++n)
        stress.paint(cr, 1120, 720);
    std::cout << "24-band software rendering: "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                         .count() /
                     60
              << " ms/frame (mean)\n";
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}
