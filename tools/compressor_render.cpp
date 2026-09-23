#include "Engine.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string_view>
using namespace openfilter::compressor;
// Binary little-endian doubles: input L/R, sidechain L/R, output L/R, GR.
int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
        Engine engine;
        auto v = defaults();
        v[Lookahead] = 5;
        v[AutoRelease] = 1;
        v[SidechainHP] = 120;
        engine.prepare(48000, v);
        double sum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (unsigned n = 0; n < 480000; ++n) {
            double l = .5 * std::sin(n * .13), r = .3 * std::cos(n * .07);
            engine.sample(l, r);
            sum += l + r;
        }
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "480 k stereo frames at 48 kHz: " << elapsed << " s, " << 100 * elapsed / 10
                  << "% of real time; checksum " << sum << "\n";
        return 0;
    }
    if (argc < 4)
        return 1;
    const double rate = std::atof(argv[1]);
    const unsigned count = std::atoi(argv[2]);
    const std::string_view signal(argv[3]);
    auto values = defaults();
    for (int a = 4; a + 1 < argc; a += 2) {
        const unsigned i = std::atoi(argv[a]);
        if (i >= parameterCount)
            return 2;
        values[i] = std::atof(argv[a + 1]);
    }
    Engine engine;
    engine.prepare(rate, values);
    for (unsigned n = 0; n < count; ++n) {
        const double t = n / rate;
        double l, r, sc;
        if (signal == "step") {
            l = t < .2 ? .01 : t < .6 ? .8 : .01;
            r = l * .5;
            sc = l;
        } else if (signal == "dc") {
            l = .5;
            r = .125;
            sc = .8;
        } else if (signal == "impulse") {
            l = n == 0 ? 1 : 0;
            r = l;
            sc = l;
        } else {
            l = .5 * std::sin(2 * std::acos(-1.) * (signal == "bass" ? 55 : 997) * t);
            r = .3 * std::cos(2 * std::acos(-1.) * 137 * t);
            sc = .8 * std::sin(2 * std::acos(-1.) * 83 * t);
        }
        const double inL = l, inR = r;
        engine.sample(l, r, false, sc, sc);
        const double row[]{inL, inR, sc, sc, l, r, engine.gainReduction()};
        if (std::fwrite(row, sizeof(double), 7, stdout) != 7)
            return 3;
    }
}
