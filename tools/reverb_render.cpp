#include "Engine.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
using namespace openfilter::reverb;
int main(int argc, char **argv) {
    if (argc < 5) {
        std::cerr << "reverb_render path rate seconds impulse|burst|tone [id value ...]\n";
        return 1;
    }
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
    e->prepare(rate, v);
    std::ofstream f(argv[1], std::ios::binary);
    if (!f)
        return 1;
    uint32_t rng = 1;
    for (unsigned n = 0; n < unsigned(rate * seconds); ++n) {
        rng = rng * 1664525 + 1013904223;
        const std::string_view kind = argv[4];
        double l = kind == "tone"    ? .1 * std::sin(2 * std::numbers::pi * 997 * n / rate)
                   : kind == "burst" ? (n < rate * .25 ? (double(rng) / 4294967296. - .5) * .2 : 0.)
                                     : (n == 0 ? 1. : 0.);
        double r = l;
        e->sample(l, r);
        const double pair[]{l, r};
        f.write(reinterpret_cast<const char *>(pair), sizeof(pair));
    }
    return f ? 0 : 1;
}
