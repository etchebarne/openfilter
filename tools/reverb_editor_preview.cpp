#include "Editor.hpp"
#include "Engine.hpp"
#include <filesystem>
#include <iostream>
#include <memory>
using namespace openfilter::reverb;
int main(int argc, char **argv) {
    const std::filesystem::path dir = argc > 1 ? argv[1] : "reports/reverb-ui";
    std::filesystem::create_directories(dir);
    EditorState s;
    openfilter::ui::AnalysisTap tap;
    for (bool post : {false, true})
        for (unsigned b = 0; b < 3; ++b) {
            const auto i = bandIndex(post, b, 0);
            s.values[i + Enabled] = 1;
            s.values[i + Frequency] = b == 0 ? 150 : b == 1 ? 1400 : 7000;
            s.values[i + Amount] = post ? (b == 0   ? -8
                                           : b == 1 ? -3
                                                    : 2)
                                        : (b == 0   ? 140
                                           : b == 1 ? 75
                                                    : 45);
            s.values[i + Type] = b == 0 ? 1 : b == 2 ? 2 : 0;
        }
    s.effective = s.values;
    auto engine = std::make_unique<Engine>();
    engine->prepare(48000, s.values);
    Editor e([&] { return s; },
             [&](UiKind k, unsigned i, double v) {
                 if (k == UiKind::Value)
                     s.values[i] = s.effective[i] = v;
             },
             tap);
    uint32_t rng = 1;
    // Deterministic musical bursts excite real early reflections and a pitched tail.
    // Every ribbon in the preview comes through the same audio tap as the plugin.
    for (unsigned n = 0; n < 168960; ++n) {
        rng = rng * 1664525 + 1013904223;
        const double t = n / 48000., beat = std::fmod(t, .72);
        double in = 0;
        if (beat < .24) {
            const double envelope = std::min(1., beat / .004) * std::exp(-beat * 16);
            for (unsigned h = 1; h <= 18; ++h)
                in += std::sin(2 * std::numbers::pi * (147 + 73.5 * (unsigned(t / .72) % 3)) * h *
                               t) *
                      .19 * envelope / h;
            in += (double(rng) / 4294967296. - .5) * .15 * envelope;
        }
        double l = in, r = in * .8;
        engine->sample(l, r);
        tap.sample(engine->wet(0), engine->wet(1), l, r);
        if ((n + 1) % 1024 == 0)
            e.tick();
    }
    s.peaks = {.34, .31, .23, .19};
    e.tick();
    auto render = [&](const char *name, unsigned w, unsigned h) {
        e.resize(w, h);
        auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        auto *cr = cairo_create(surface);
        e.paint(cr, w / e.scale(), h / e.scale());
        cairo_destroy(cr);
        const auto path = dir / name;
        const auto status = cairo_surface_write_to_png(surface, path.c_str());
        cairo_surface_destroy(surface);
        std::cout << path << '\n';
        return status == CAIRO_STATUS_SUCCESS;
    };
    if (!render("editor.png", 1120, 720) || !render("compact.png", 900, 600))
        return 1;
    e.setScale(2);
    if (!render("hidpi.png", 1800, 1200))
        return 1;
    e.setScale(1);
    e.resize(1120, 720);
    auto r = e.headerBounds(0);
    e.press(r.x + 10, r.y + 10, 0, 0, 1);
    e.release(0, 0);
    if (!render("menu.png", 1120, 720))
        return 1;
    e.key(PUGL_KEY_ESCAPE, 0);
    r = e.valueBounds(Space);
    e.press(r.x + 30, r.y + 15, 0, 0, 2);
    e.input("3.20 s");
    if (!render("entry.png", 1120, 720))
        return 1;
    e.key(PUGL_KEY_ESCAPE, 0);
    auto g = e.graphBounds();
    e.press(g.x + g.w * .55, g.y + g.h * .25, 0, 0, 3);
    e.release(0, 0);
    if (!render("selected.png", 1120, 720) || !render("selected-compact.png", 900, 600))
        return 1;
    e.key(PUGL_KEY_ESCAPE, 0);
    r = e.headerBounds(6);
    e.press(r.x + 10, r.y + 10, 0, 0, 4);
    e.release(0, 0);
    if (!render("help.png", 900, 600))
        return 1;
    e.key(PUGL_KEY_ESCAPE, 0);
    for (bool post : {false, true})
        for (unsigned b = 0; b < bands; ++b)
            s.values[bandIndex(post, b, Enabled)] = 1;
    s.effective = s.values;
    e.tick();
    if (!render("all-bands.png", 1120, 720))
        return 1;
    r = e.viewBounds(1);
    e.press(r.x + 12, r.y + 12, 0, 0, 5);
    e.release(0, 0);
    if (!render("post-eq.png", 1120, 720))
        return 1;
    const auto start = std::chrono::steady_clock::now();
    auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1120, 720);
    auto *cr = cairo_create(surface);
    for (unsigned frame = 0; frame < 60; ++frame)
        e.paint(cr, 1120, 720);
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    std::cout << "Dense EQ + wet-history paint: " << elapsed / 60 << " ms/frame (60-frame mean)\n";
    Editor empty([&] { return EditorState{}; }, [](UiKind, unsigned, double) {}, tap);
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1120, 720);
    cr = cairo_create(surface);
    empty.paint(cr, 1120, 720);
    cairo_destroy(cr);
    cairo_surface_write_to_png(surface, (dir / "empty.png").c_str());
    cairo_surface_destroy(surface);
}
