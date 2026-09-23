#include "Engine.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
using namespace openfilter;
int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) {
        for (bool brickwall : {false, true})
            for (bool automate : {false, true}) {
                auto v = eq::defaults();
                for (unsigned b = 0; b < eq::bands; ++b) {
                    v[eq::index(b, eq::Enabled)] = 1;
                    v[eq::index(b, eq::Gain)] = (b % 2 ? 3 : -3);
                    v[eq::index(b, eq::Frequency)] = std::log2(40. * std::pow(400., b / 23.));
                    if (brickwall) {
                        v[eq::index(b, eq::Type)] = b % 2 ? 3 : 4;
                        v[eq::index(b, eq::Slope)] = 3;
                    }
                }
                eq::Engine e;
                e.prepare(48000, v);
                double sum = 0;
                const auto start = std::chrono::steady_clock::now();
                for (int n = 0; n < 480000; ++n) {
                    if (automate && n % 64 == 0) {
                        for (unsigned b = 0; b < eq::bands; ++b) {
                            if (brickwall)
                                e.set(eq::index(b, eq::Frequency),
                                      v[eq::index(b, eq::Frequency)] + std::sin(n * .0001) * .5);
                            else
                                e.set(eq::index(b, eq::Gain), std::sin(n * .0001) * 6);
                        }
                    }
                    double l = std::sin(n * .1) * .1, r = l;
                    e.sample(l, r);
                    sum += l;
                }
                const double seconds =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                std::cout << (brickwall ? "Brickwall " : "Bell ")
                          << (automate ? "automated" : "static") << " 24 bands: " << seconds
                          << " s for 10 s stereo audio; realtime ratio " << seconds / 10
                          << "; checksum " << sum << '\n';
            }
        return 0;
    }
    if (argc != 8) {
        std::cerr
            << "Usage: eq_render rate shape frequency gain Q slope-index frames > impulse.f64\n";
        return 1;
    }
    const double rate = std::atof(argv[1]), hz = std::atof(argv[3]), gain = std::atof(argv[4]),
                 q = std::atof(argv[5]);
    const int type = std::atoi(argv[2]), slope = std::atoi(argv[6]), frames = std::atoi(argv[7]);
    if (!std::isfinite(rate) || rate < 1000 || rate > 768000 || !std::isfinite(hz) || hz < 10 ||
        hz > 30000 || !std::isfinite(gain) || std::abs(gain) > 24 || !std::isfinite(q) || q < .1 ||
        q > 40 || type < 0 || type > 5 || slope < 0 || slope > 3 || frames < 1 || frames > 4000000)
        return 2;
    auto v = eq::defaults();
    v[eq::index(0, eq::Enabled)] = 1;
    v[eq::index(0, eq::Type)] = type;
    v[eq::index(0, eq::Frequency)] = std::log2(hz);
    v[eq::index(0, eq::Gain)] = gain;
    v[eq::index(0, eq::Q)] = std::log2(q);
    v[eq::index(0, eq::Slope)] = slope;
    eq::Engine e;
    e.prepare(rate, v);
    std::vector<double> out(static_cast<size_t>(frames));
    for (int n = 0; n < frames; ++n) {
        double l = n == 0 ? 1 : 0, r = l;
        e.sample(l, r);
        out[n] = l;
    }
    std::cout.write(reinterpret_cast<const char *>(out.data()),
                    static_cast<std::streamsize>(out.size() * sizeof(double)));
}
