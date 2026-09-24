#include "Engine.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>
using namespace openfilter::limiter;
// Binary native doubles: input L/R, output L/R, reduction. Linux first target.
int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
        auto storage = std::make_unique<Engine>();
        auto &engine = *storage;
        auto v = defaults();
        v[Gain] = 12;
        v[AutoRelease] = 1;
        engine.prepare(48000, v);
        double sum = 0;
        const auto start = std::chrono::steady_clock::now();
        std::vector<double> callbacks, cpuCallbacks;
        callbacks.reserve(7500);
        cpuCallbacks.reserve(7500);
        for (unsigned block = 0; block < 7500; ++block) {
            const auto callbackStart = std::chrono::steady_clock::now();
            const auto cpuStart = std::clock();
            for (unsigned j = 0; j < 64; ++j) {
                const auto n = block * 64 + j;
                double l = .5 * std::sin(n * .13), r = .3 * std::cos(n * .07);
                engine.sample(l, r);
                sum += l + r;
            }
            cpuCallbacks.push_back(1e6 * double(std::clock() - cpuStart) / CLOCKS_PER_SEC);
            callbacks.push_back(std::chrono::duration<double, std::micro>(
                                    std::chrono::steady_clock::now() - callbackStart)
                                    .count());
        }
        const auto seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const auto worstWall =
            std::max_element(callbacks.begin(), callbacks.end()) - callbacks.begin();
        const double correspondingCpu = cpuCallbacks[worstWall];
        std::sort(callbacks.begin(), callbacks.end());
        std::sort(cpuCallbacks.begin(), cpuCallbacks.end());
        std::cout << "480 k stereo frames: " << seconds << " s, " << 10 * seconds
                  << "% real time; checksum " << sum << '\n'
                  << "64-frame callbacks (1333.33 us budget): median " << callbacks[3750]
                  << " us, p99 " << callbacks[7425] << " us, max " << callbacks.back() << " us\n"
                  << "CPU time: p99 " << cpuCallbacks[7425] << " us, max " << cpuCallbacks.back()
                  << " us; CPU time during slowest wall callback " << correspondingCpu << " us\n"
                  << "Instance storage: " << sizeof(Engine) << " bytes\n";
        return 0;
    }
    if (argc < 4)
        return 1;
    const double rate = std::atof(argv[1]);
    const unsigned count = std::atoi(argv[2]);
    const std::string_view signal(argv[3]);
    auto v = defaults();
    for (int a = 4; a + 1 < argc; a += 2) {
        const unsigned i = std::atoi(argv[a]);
        if (i >= parameterCount)
            return 2;
        v[i] = std::atof(argv[a + 1]);
    }
    auto storage = std::make_unique<Engine>();
    auto &engine = *storage;
    engine.prepare(rate, v);
    std::unique_ptr<ModernEngine> core;
    if (signal == "core") {
        if (!std::isfinite(rate) || rate < 1000 || rate > 768000)
            return 5;
        core = std::make_unique<ModernEngine>();
        core->prepare(rate, static_cast<unsigned>(std::ceil(rate * .005)), v);
    }
    for (unsigned n = 0; n < count; ++n) {
        const double t = n / rate;
        double l = 0, r = 0;
        if (signal == "stdin" || signal == "meter" || signal == "core") {
            double row[2];
            if (std::fread(row, sizeof(double), 2, stdin) != 2)
                return 4;
            l = row[0];
            r = row[1];
        } else if (signal == "step") {
            l = t < .2 ? .1 : t < .6 ? 2 : .1;
            r = l * .25;
        } else if (signal == "impulse") {
            l = n == 1000 ? 2 : 0;
            r = l;
        } else {
            l = .8 * std::sin(2 * std::acos(-1.) * (signal == "bass" ? 55 : 997) * t);
            r = .5 * l;
        }
        const double il = l, ir = r;
        if (core) {
            l *= std::pow(10., v[Gain] / 20);
            r *= std::pow(10., v[Gain] / 20);
            core->sample(l, r);
            l *= std::pow(10., v[Ceiling] / 20);
            r *= std::pow(10., v[Ceiling] / 20);
        } else
            engine.sample(l, r);
        const double row[]{il, ir, l, r,
                           signal == "meter" ? std::max(engine.outputPeak(0), engine.outputPeak(1))
                           : core            ? core->gainReduction()
                                             : engine.gainReduction()};
        if (std::fwrite(row, sizeof(double), 5, stdout) != 5)
            return 3;
    }
}
