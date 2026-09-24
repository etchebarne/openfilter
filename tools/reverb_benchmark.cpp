#include "Engine.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>
#include <memory>
#include <vector>
using namespace openfilter::reverb;
double cpuTime() {
    timespec t{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return double(t.tv_sec) + t.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
    const unsigned rate = argc > 1 ? std::stoul(argv[1]) : 48000;
    const unsigned block = argc > 2 ? std::stoul(argv[2]) : 64;
    const unsigned seconds = argc > 3 ? std::stoul(argv[3]) : 20;
    const std::string mode = argc > 4 ? argv[4] : "default";
    const unsigned instances = argc > 5 ? std::stoul(argv[5]) : 1;
    if (!instances || instances > 16 || rate < 1000 || rate > 768000 || !block || block > 4096 ||
        !seconds || seconds > 300)
        return 1;
    auto v = defaults();
    if (mode != "default")
        for (bool post : {false, true})
            for (unsigned b = 0; b < bands; ++b) {
                v[bandIndex(post, b, Enabled)] = 1;
                v[bandIndex(post, b, Amount)] = post ? -3 : 70;
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
    times.reserve(size_t(rate) * seconds / block + 1);
    cpuTimes.reserve(times.capacity());
    double checksum = 0;
    const double start = cpuTime();
    for (unsigned at = 0; at < rate * seconds; at += block) {
        const auto before = std::chrono::steady_clock::now();
        const double cpuBefore = cpuTime();
        if (mode == "automated") {
            for (auto &e : engines) {
                e->set(Space, .5 + 3 * (.5 + .5 * std::sin(at / double(rate))));
                e->set(Brightness, 50 + 40 * std::sin(at / double(rate)));
            }
        }
        // Hosts normally process each plugin's complete block, not alternate
        // between plugin instances after every individual sample.
        for (auto &e : engines) {
            for (unsigned n = at; n < std::min(at + block, rate * seconds); ++n) {
                double l = input[n % input.size()], r = input[(n + 371) % input.size()];
                e->sample(l, r);
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
    std::cout << "{\"rate\":" << rate << ",\"block\":" << block << ",\"seconds\":" << seconds
              << ",\"instances\":" << instances << ",\"engine_bytes\":" << sizeof(Engine)
              << ",\"mode\":\"" << mode << "\",\"cpu_seconds\":" << cpu
              << ",\"cpu_percent_of_realtime\":" << cpu / seconds * 100
              << ",\"cpu_p99_us\":" << cpuTimes[size_t(cpuTimes.size() * .99)]
              << ",\"cpu_max_us\":" << cpuTimes.back()
              << ",\"wall_p99_us\":" << times[size_t(times.size() * .99)]
              << ",\"wall_max_us\":" << times.back() << ",\"wall_deadline_exceedances\":"
              << std::count_if(times.begin(), times.end(),
                               [&](double x) { return x > 1e6 * block / rate; })
              << ",\"checksum\":" << checksum << "}\n";
}
