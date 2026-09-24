#include "Engine.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string_view>
using namespace openfilter::deesser;
// stdin: interleaved little-endian double main L/R and sidechain L/R.
// stdout: input L/R, sidechain L/R, output L/R, reduction, detector RMS.
int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
        Engine e;
        e.prepare(48000, defaults());
        double sum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (unsigned n = 0; n < 480000; ++n) {
            double l = .5 * std::sin(n * .91), r = .3 * std::cos(n * .79);
            e.sample(l, r);
            sum += l + r;
        }
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "10 s stereo / 48 kHz: " << elapsed << " s CPU wall, " << elapsed * 10
                  << "% realtime; checksum " << sum << '\n';
        return 0;
    }
    if (argc < 2)
        return 1;
    auto v = defaults();
    for (int a = 2; a + 1 < argc; a += 2) {
        unsigned i = std::atoi(argv[a]);
        if (i >= parameterCount)
            return 2;
        v[i] = std::atof(argv[a + 1]);
    }
    Engine e;
    e.prepare(std::atof(argv[1]), v);
    double in[4];
    while (std::fread(in, sizeof(double), 4, stdin) == 4) {
        double l = in[0], r = in[1];
        e.sample(l, r, false, in[2], in[3]);
        const double row[]{in[0], in[1], in[2], in[3], l, r, e.gainReduction(), e.detectorLevel()};
        if (std::fwrite(row, sizeof(double), 8, stdout) != 8)
            return 3;
    }
    return std::ferror(stdin) ? 4 : 0;
}
