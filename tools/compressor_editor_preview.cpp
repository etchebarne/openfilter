#include "Editor.hpp"
#include "Engine.hpp"
#include <filesystem>
#include <iostream>
using namespace openfilter::compressor;
int main(int argc, char **argv) {
    const std::filesystem::path dir = argc > 1 ? argv[1] : "reports/compressor-ui";
    std::filesystem::create_directories(dir);
    EditorState s;
    MeterTap tap;
    Engine engine;
    engine.prepare(48000, s.values);
    Editor editor([&] { return s; },
                  [&](UiKind k, unsigned i, double v) {
                      if (k == UiKind::Value)
                          s.values[i] = s.effective[i] = v;
                  },
                  tap);
    for (unsigned n = 0; n < 288000; ++n) {
        const double env = .2 + .6 * std::exp(-std::fmod(n / 48000., .48) * 12);
        double l = env * std::sin(n * .13), r = l * .8;
        const double in = l;
        engine.sample(l, r);
        tap.sample(in, l, engine.gainReduction());
        if (n % 1024 == 0)
            editor.tick();
    }
    s.peaks = {.64, .56, .28, .24};
    s.reduction = 6.8;
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
    r = editor.controlBounds(Attack);
    editor.press(r.x + 30, r.y + 125, 0, 0, 2);
    editor.release(0, 0);
    editor.input("12.5 ms");
    if (!render("entry.png", 1120, 720))
        return 1;
    editor.key(PUGL_KEY_ESCAPE, 0);
    r = editor.viewBounds(1);
    editor.press(r.x + 12, r.y + 12, 0, 0, 3);
    editor.release(0, 0);
    if (!render("sidechain.png", 1120, 720) || !render("sidechain-compact.png", 900, 600))
        return 1;
    r = editor.controlBounds(Sidechain);
    editor.press(r.x + 12, r.y + 12, 0, 0, 4);
    editor.release(0, 0);
    if (!render("sidechain-menu.png", 900, 600))
        return 1;
    editor.key(PUGL_KEY_ESCAPE, 0);
    r = editor.viewBounds(1);
    editor.press(r.x + 12, r.y + 12, 0, 0, 5);
    editor.release(0, 0);
    r = editor.viewBounds(0);
    editor.press(r.x + 12, r.y + 12, 0, 0, 6);
    editor.release(0, 0);
    if (!render("history-only.png", 1120, 720))
        return 1;
    editor.press(r.x + 12, r.y + 12, 0, 0, 7);
    editor.release(0, 0);
    editor.key(PUGL_KEY_ESCAPE, 0);
    r = editor.headerBounds(6);
    editor.press(r.x + 10, r.y + 10, 0, 0, 3);
    editor.release(0, 0);
    if (!render("help.png", 1120, 720))
        return 1;
}
