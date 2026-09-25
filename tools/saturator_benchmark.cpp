#include "Engine.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>
#include <memory>
#include <vector>
using namespace openfilter::saturator;
double cpuTime() {
    timespec t{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return double(t.tv_sec) + t.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
    const unsigned rate = argc > 1 ? std::stoul(argv[1]) : 48000;
    const unsigned blockSize = argc > 2 ? std::stoul(argv[2]) : 64;
    const unsigned seconds = argc > 3 ? std::stoul(argv[3]) : 20;
    const std::string mode = argc > 4 ? argv[4] : "default";
    const unsigned instances = argc > 5 ? std::stoul(argv[5]) : 1;
    if (!instances || instances > 16 || rate < 1000 || rate > 768000 || !blockSize ||
        blockSize > 4096 || !seconds || seconds > 300)
        return 1;
    auto v = defaults();
    for (unsigned b = 0; b < 3; ++b) {
        if (mode == "dense" || mode == "automated") {
            v[band(b, Drive)] = 24;
            v[band(b, Dynamics)] = 60;
            v[band(b, Bass)] = 6;
            v[band(b, Presence)] = -3;
        }
        if (mode == "rounded")
            v[band(b, Style)] = 1;
        if (mode == "asymmetric")
            v[band(b, Style)] = 3;
    }
    std::vector<std::unique_ptr<Engine>> engines;
    for (unsigned i = 0; i < instances; ++i) {
        auto engine = std::make_unique<Engine>();
        engine->prepare(rate, v);
        engines.push_back(std::move(engine));
    }
    std::array<double, 8192> input{};
    for (unsigned n = 0; n < input.size(); ++n)
        input[n] = .1 * std::sin(n * .071) + .07 * std::sin(n * .173);
    std::vector<double> times, cpuTimes;
    times.reserve(size_t(rate) * seconds / blockSize + 1);
    cpuTimes.reserve(times.capacity());
    double checksum = 0;
    const double start = cpuTime();
    for (unsigned at = 0; at < rate * seconds; at += blockSize) {
        const auto before = std::chrono::steady_clock::now();
        const double cpuBefore = cpuTime();
        if (mode == "automated") {
            for (auto &e : engines) {
                const double phase = std::sin(at / double(rate));
                e->set(Input, phase * 12);
                e->set(CrossoverLow, 250 + phase * 150);
                e->set(CrossoverHigh, 6000 + phase * 3000);
                for (unsigned b = 0; b < 3; ++b) {
                    e->set(band(b, Drive), 18 + phase * 15);
                    e->set(band(b, Style), (at / blockSize) % 4);
                    e->set(band(b, Dynamics), phase * 90);
                    for (unsigned t = 0; t < 4; ++t)
                        e->set(band(b, Bass + t), phase * 10);
                }
            }
        }
        // Hosts normally process each plugin's complete block, not alternate
        // between plugin instances after every individual sample.
        for (auto &e : engines) {
            for (unsigned n = at; n < std::min(at + blockSize, rate * seconds); ++n) {
                double l = input[n % input.size()], r = input[(n + 371) % input.size()];
                if (mode == "silence" || (mode == "tail" && n > rate / 10))
                    l = r = 0;
                e->sample(l, r, mode == "mono");
                checksum += l * l + r * r;
            }
        }
        cpuTimes.push_back((cpuTime() - cpuBefore) * 1e6);
        times.push_back(
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - before)
                .count());
    }
    const double cpu = cpuTime() - start;
    std::sort(times.begin(), times.end());
    std::sort(cpuTimes.begin(), cpuTimes.end());
    std::cout << "{\"rate\":" << rate << ",\"block\":" << blockSize << ",\"seconds\":" << seconds
              << ",\"instances\":" << instances << ",\"engine_bytes\":" << sizeof(Engine)
              << ",\"mode\":\"" << mode << "\",\"cpu_seconds\":" << cpu
              << ",\"cpu_percent_of_realtime\":" << cpu / seconds * 100
              << ",\"cpu_p99_us\":" << cpuTimes[size_t(cpuTimes.size() * .99)]
              << ",\"cpu_max_us\":" << cpuTimes.back()
              << ",\"wall_p99_us\":" << times[size_t(times.size() * .99)]
              << ",\"wall_max_us\":" << times.back() << ",\"wall_deadline_exceedances\":"
              << std::count_if(times.begin(), times.end(),
                               [&](double x) { return x > 1e6 * blockSize / rate; })
              << ",\"checksum\":" << checksum << "}\n";
}
