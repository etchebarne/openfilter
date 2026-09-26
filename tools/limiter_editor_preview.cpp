#include "Editor.hpp"
#include "Engine.hpp"
#include <filesystem>
#include <iostream>
#include <memory>
using namespace openfilter::limiter;
int main(int argc, char **argv) {
    const std::filesystem::path dir = argc > 1 ? argv[1] : "reports/limiter-ui";
    std::filesystem::create_directories(dir);
    EditorState s;
    s.values[Gain] = s.effective[Gain] = 8;
    MeterTap tap;
    auto storage = std::make_unique<Engine>();
    auto &engine = *storage;
    engine.prepare(48000, s.values);
    Editor editor([&] { return s; },
                  [&](UiKind k, unsigned i, double v) {
                      if (k == UiKind::Value)
                          s.values[i] = s.effective[i] = v;
                  },
                  tap);
    for (unsigned n = 0; n < 288000; ++n) {
        const double t = n / 48000.;
        const double beat = std::fmod(t, .48), offbeat = std::fmod(t + .12, .31);
        const double env = .008 + .4 * std::exp(-beat * 28) + .14 * std::exp(-offbeat * 65) +
                           .035 * std::pow(std::sin(t * 2.7), 8);
        double l = env * (std::sin(n * .071) + .31 * std::sin(n * .193) + .16 * std::sin(n * .037)),
               r = l * .8;
        engine.sample(l, r);
        tap.sample(engine.inputPeak(), l, engine.gainReduction());
        if (n % 1024 == 0)
            editor.tick();
    }
    s.peaks = {.8, .64, .891, .713};
    s.reduction = engine.gainReduction();
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
    if (!render("hidpi.png", 1800, 1200))
        return 1;
    editor.setScale(1);
    editor.resize(1120, 720);
    auto r = editor.headerBounds(0);
    editor.press(r.x + 12, r.y + 12, 0, 0, 1);
    editor.release(0, 0);
    if (!render("menu.png", 1120, 720))
        return 1;
    editor.key(PUGL_KEY_ESCAPE, 0);
    r = editor.controlBounds(Style);
    editor.press(r.x + 30, r.y + 12, 0, 0, 1.6);
    editor.release(0, 0);
    if (!render("style-menu.png", 1120, 720))
        return 1;
    editor.key(PUGL_KEY_ESCAPE, 0);
    s.values[Style] = s.effective[Style] = Legacy;
    editor.tick();
    if (!render("legacy.png", 900, 600))
        return 1;
    s.values[Style] = s.effective[Style] = Clean;
    editor.tick();
    editor.resize(1120, 720);

    r = editor.controlBounds(Release);
    editor.press(r.x + 30, r.y + 108, 0, 0, 2);
    editor.release(0, 0);
    editor.input("250 ms");
    if (!render("entry.png", 1120, 720))
        return 1;
    editor.key(PUGL_KEY_ESCAPE, 0);
    r = editor.viewBounds(0);
    editor.press(r.x + 12, r.y + 12, 0, 0, 3);
    editor.release(0, 0);
    if (!render("history-only.png", 1120, 720) || !render("history-compact.png", 900, 600))
        return 1;
    editor.resize(1120, 720);
    r = editor.controlBounds(Bypass);
    editor.press(r.x + 10, r.y + 10, 1, 0, 4);
    if (!render("toggle-entry.png", 900, 600))
        return 1;
    editor.key(PUGL_KEY_ESCAPE, 0);
    editor.resize(1120, 720);
    r = editor.headerBounds(6);
    editor.press(r.x + 10, r.y + 10, 0, 0, 5);
    editor.release(0, 0);
    if (!render("help.png", 1120, 720))
        return 1;
}
