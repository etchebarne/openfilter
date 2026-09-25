#include "Engine.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string_view>
using namespace openfilter::saturator;
int main(int argc, char **argv) {
    Engine engine;
    auto v = defaults();
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
        engine.prepare(48000, v);
        double sum = 0;
        auto start = std::chrono::steady_clock::now();
        for (unsigned n = 0; n < 480000; ++n) {
            double l = .5 * std::sin(n * .13), r = .3 * std::cos(n * .07);
            engine.sample(l, r);
            sum += l + r;
        }
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "10 seconds stereo: " << seconds << " s (" << seconds * 10
                  << "% real time), checksum " << sum << '\n';
        return 0;
    }
    if (argc < 2)
        return 1;
    for (int a = 2; a + 1 < argc; a += 2) {
        const unsigned i = std::atoi(argv[a]);
        if (i >= parameterCount)
            return 2;
        v[i] = std::atof(argv[a + 1]);
    }
    engine.prepare(std::atof(argv[1]), v);
    double row[2];
    while (std::fread(row, sizeof(double), 2, stdin) == 2) {
        engine.sample(row[0], row[1]);
        if (std::fwrite(row, sizeof(double), 2, stdout) != 2)
            return 3;
    }
}
