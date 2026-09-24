#include "Engine.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
using namespace openfilter::reverb;
int main(int argc, char **argv) {
    if (argc < 5) {
        std::cerr << "reverb_render path rate seconds "
                     "impulse|burst|tone|file:path|space-sweep|predelay-sweep|shape-switch [id "
                     "value ...] [--legacy]\n";
        return 1;
    }
    const bool legacy = std::string_view(argv[argc - 1]) == "--legacy";
    if (legacy)
        --argc;
    const double rate = std::stod(argv[2]), seconds = std::stod(argv[3]);
    if (rate < 8000 || rate > 192000 || seconds <= 0 || seconds > 120)
        return 1;
    auto v = defaults();
    for (int i = 5; i + 1 < argc; i += 2) {
        const auto id = std::stoul(argv[i]);
        if (id >= parameterCount)
            return 1;
        v[id] = parameter(id).constrain(std::stod(argv[i + 1]));
    }
    auto e = std::make_unique<Engine>();
    e->prepare(rate, v, legacy ? 1 : 2);
    std::ofstream f(argv[1], std::ios::binary);
    if (!f)
        return 1;
    const std::string_view kind = argv[4];
    std::ifstream source;
    if (kind.starts_with("file:")) {
        source.open(std::string(kind.substr(5)), std::ios::binary);
        if (!source)
            return 1;
    }
    uint32_t rng = 1;
    for (unsigned n = 0; n < unsigned(rate * seconds); ++n) {
        rng = rng * 1664525 + 1013904223;

        double l = kind == "tone" ? .1 * std::sin(2 * std::numbers::pi * 997 * n / rate)
                   : (kind == "burst" || kind == "freeze")
                       ? (n < rate * .25 ? (double(rng) / 4294967296. - .5) * .2 : 0.)
                       : (n == 0 ? 1. : 0.);
        double r = l;
        if (source.is_open()) {
            double pair[2]{};
            source.read(reinterpret_cast<char *>(pair), sizeof(pair));
            l = pair[0];
            r = pair[1];
        }
        if (kind == "space-sweep" || kind == "predelay-sweep" || kind == "shape-switch") {
            l = r = .1 * std::sin(2 * std::numbers::pi * 997 * n / rate);
            if (n == unsigned(rate)) {
                if (kind == "space-sweep")
                    e->set(Space, 8);
                if (kind == "predelay-sweep")
                    e->set(Predelay, 100);
                if (kind == "shape-switch")
                    e->set(bandIndex(true, 0, Type), 3);
            }
        }
        if (kind == "freeze" && n == unsigned(rate * .5))
            e->set(Freeze, 1);
        e->sample(l, r);
        const double pair[]{l, r};
        f.write(reinterpret_cast<const char *>(pair), sizeof(pair));
    }
    return f ? 0 : 1;
}
