#include "Editor.hpp"
#include "Engine.hpp"
#include <filesystem>
#include <iostream>
#include <random>
using namespace openfilter::deesser;
int main(int argc, char **argv) {
    const std::filesystem::path dir = argc > 1 ? argv[1] : "reports/deesser-ui";
    std::filesystem::create_directories(dir);
    EditorState s;
    MeterTap tap;
    Engine engine;
    engine.prepare(48000, s.values);
    Editor e([&] { return s; },
             [&](UiKind k, unsigned i, double v) {
                 if (k == UiKind::Value)
                     s.values[i] = s.effective[i] = v;
             },
             tap);
    std::mt19937 random(723);
    std::uniform_real_distribution<double> noise(-1, 1);
    for (unsigned n = 0; n < 288000; ++n) {
        const double t = n / 48000.;
        auto pulse = [&](double at, double width, double level) {
            const double x = (t - at) / width;
            return level * std::exp(-x * x * 2);
        };
        const double env = pulse(.42, .24, .34) + pulse(.91, .15, .46) + pulse(1.45, .32, .27) +
                           pulse(2.28, .21, .37) + pulse(2.69, .3, .25) + pulse(3.35, .18, .50) +
                           pulse(4.02, .26, .38) + pulse(4.69, .3, .66) + pulse(5.32, .19, .35);
        const double ess = pulse(1.02, .09, .38) + pulse(3.61, .08, .5) + pulse(4.1, .12, .46) +
                           pulse(5.98, .18, .45);
        double l = env * (std::sin(n * .021) + .27 * std::sin(n * .067)) + ess * noise(random),
               r = l * .86;
        engine.sample(l, r);
        tap.sample(engine.displayInput(0), engine.displayInput(1), engine.gainReduction(),
                   engine.displayDetector(0), engine.displayDetector(1));
        if (n % 1024 == 0)
            e.tick();
    }
    s.peaks = {.64, .56, .43, .38};
    s.reduction = 4.8;
    e.tick();
    auto render = [&](const char *name, unsigned w, unsigned h) {
        if (!e.resize(w, h))
            return false;
        auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        auto *cr = cairo_create(surface);
        e.paint(cr, w / e.scale(), h / e.scale());
        cairo_destroy(cr);
        const auto status = cairo_surface_write_to_png(surface, (dir / name).c_str());
        cairo_surface_destroy(surface);
        std::cout << (dir / name) << '\n';
        return status == CAIRO_STATUS_SUCCESS;
    };
    if (!render("editor.png", 1120, 720) || !render("compact.png", 900, 600))
        return 1;
    e.setScale(2);
    if (!render("hidpi.png", 2240, 1440))
        return 1;
    e.setScale(1);
    e.resize(1120, 720);
    auto r = e.headerBounds(0);
    e.press(r.x + 12, r.y + 12, 0, 0, 1);
    e.release(0, 0);
    if (!render("menu.png", 1120, 720))
        return 1;
    e.key(PUGL_KEY_ESCAPE, 0);
    r = e.controlBounds(Low);
    e.press(r.x + 30, r.y + 12, 0, 0, 2);
    e.release(0, 0);
    e.input("5.5 kHz");
    if (!render("entry.png", 1120, 720))
        return 1;
    e.key(PUGL_KEY_ESCAPE, 0);
    r = e.headerBounds(6);
    e.press(r.x + 10, r.y + 10, 0, 0, 3);
    e.release(0, 0);
    if (!render("help.png", 1120, 720))
        return 1;
}
