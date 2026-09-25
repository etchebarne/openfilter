#include "Editor.hpp"
#include "Engine.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
using namespace openfilter::saturator;
int main(int argc, char **argv) {
    const std::filesystem::path dir = argc > 1 ? argv[1] : "reports/saturator-ui";
    std::filesystem::create_directories(dir);
    EditorState s;
    openfilter::ui::AnalysisTap tap;
    Engine engine;
    engine.prepare(48000, s.values);
    Editor editor([&] { return s; },
                  [&](UiKind k, unsigned i, double v) {
                      if (k == UiKind::Value)
                          s.values[i] = s.effective[i] = v;
                  },
                  tap);
    tap.reset(48000);
    uint32_t seed = 0x51a7u;
    double noiseLow = 0;
    for (unsigned n = 0; n < 48000; ++n) {
        double l = 0, r = 0;
        for (unsigned harmonic = 1; harmonic < 50; ++harmonic) {
            l += .16 / harmonic * std::sin(n * 2 * std::numbers::pi * 110 * harmonic / 48000);
            r += .15 / harmonic * std::sin(n * 2 * std::numbers::pi * 147 * harmonic / 48000);
        }
        // Deterministic broadband content, fed through the actual DSP and analyzer.
        seed = seed * 1664525u + 1013904223u;
        const double noise = double(seed) / 2147483648. - 1;
        noiseLow += .22 * (noise - noiseLow);
        l += .48 * noiseLow;
        r += .43 * noiseLow;
        const double il = l, ir = r;
        engine.sample(l, r);
        tap.sample(il, ir, l, r);
        if (n % 1024 == 0)
            editor.tick();
    }
    s.peaks = {.64, .56, .28, .24};
    editor.tick();
    auto render = [&](const char *name, unsigned w, unsigned h) {
        editor.resize(w, h);
        auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        auto *cr = cairo_create(surface);
        editor.paint(cr, w / editor.scale(), h / editor.scale());
        cairo_destroy(cr);
        const auto path = dir / name;
        const auto status = cairo_surface_write_to_png(surface, path.c_str());
        cairo_surface_destroy(surface);
        std::cout << path << '\n';
        return status == CAIRO_STATUS_SUCCESS;
    };
    if (!render("editor.png", 1120, 720) || !render("compact.png", 900, 600))
        return 1;
    editor.setScale(2);
    if (!render("hidpi.png", 2240, 1440))
        return 1;
    editor.setScale(1);
    editor.resize(1120, 720);
    auto r = editor.headerBounds(0);
    editor.press(r.x + 12, r.y + 12, 0, 0, 1);
    editor.release(0, 0);
    if (!render("menu.png", 1120, 720))
        return 1;
    editor.key(PUGL_KEY_ESCAPE, 0);
    r = editor.controlBounds(band(1, Drive));
    editor.press(r.x + 30, r.y + r.h - 12, 0, 0, 2);
    editor.input("12.5 dB");
    if (!render("entry.png", 1120, 720))
        return 1;
    editor.key(PUGL_KEY_ESCAPE, 0);
    r = editor.controlBounds(band(1, Style));
    editor.press(r.x + 12, r.y + 12, 0, 0, 4);
    editor.release(0, 0);
    if (!render("style-menu.png", 1120, 720))
        return 1;
    editor.key(PUGL_KEY_ESCAPE, 0);
    r = editor.viewBounds(0);
    editor.press(r.x + 10, r.y + 10, 0, 0, 6);
    editor.release(0, 0);
    if (!render("low-band.png", 1120, 720))
        return 1;
    s.values[band(0, Enabled)] = 0;
    s.effective = s.values;
    editor.tick();
    if (!render("disabled.png", 1120, 720))
        return 1;
    s.values[CrossoverLow] = 4000;
    s.values[CrossoverHigh] = 200;
    s.effective = s.values;
    editor.tick();
    if (!render("crossed-automation.png", 900, 600))
        return 1;
    s.values[CrossoverLow] = 250;
    s.values[CrossoverHigh] = 4000;
    s.effective = s.values;
    editor.tick();
    editor.resize(1120, 720);
    r = editor.headerBounds(6);
    editor.press(r.x + 10, r.y + 10, 0, 0, 3);
    editor.release(0, 0);
    if (!render("help.png", 1120, 720))
        return 1;
    editor.key(PUGL_KEY_ESCAPE, 0);
    auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1120, 720);
    auto *cr = cairo_create(surface);
    const auto start = std::chrono::steady_clock::now();
    for (unsigned frame = 0; frame < 60; ++frame)
        editor.paint(cr, 1120, 720);
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    std::cout << "Saturator software rendering: " << elapsed / 60 << " ms/frame (60-frame mean)\n";
}
