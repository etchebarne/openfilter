#include "Engine.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>
#include <memory>
#include <random>
#include <string_view>
#include <thread>
#if defined(__SSE2__)
#include <xmmintrin.h>
#endif
#include <vector>
using namespace openfilter::limiter;
static double cpuTime() {
    timespec t{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return double(t.tv_sec) * 1e6 + double(t.tv_nsec) * .001;
}
int main(int argc, char **argv) {
    const unsigned seconds = argc > 1 ? std::stoul(argv[1]) : 120;
    const unsigned instances = argc > 2 ? std::stoul(argv[2]) : 1;
    const unsigned rate = argc > 3 ? std::stoul(argv[3]) : 48000;
    const unsigned block = argc > 4 ? std::stoul(argv[4]) : 64;
    const bool control = argc > 5 && std::string_view(argv[5]) == "control";
    const bool paced = argc > 5 && std::string_view(argv[5]) == "paced";
#if defined(__SSE2__)
    if (argc > 5 && std::string_view(argv[5]) == "ftz")
        _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
    if (!seconds || seconds > 3600 || !instances || instances > 32 || rate < 1000 ||
        rate > 768000 || !block || block > 4096)
        return 1;
    std::vector<std::unique_ptr<Engine>> engines;
    auto v = defaults();
    v[Gain] = 12;
    v[AutoRelease] = 1;
    for (unsigned i = 0; i < instances; ++i) {
        auto e = std::make_unique<Engine>();
        e->prepare(rate, v);
        engines.push_back(std::move(e));
    }
    std::mt19937 generator(1231);
    std::vector<std::array<double, 2>> source(65536);
    for (unsigned n = 0; n < source.size(); ++n) {
        // Four repeatable stresses: tones, independent overload noise, silence,
        // and gradually falling magnitudes interrupted by an isolated peak.
        switch (n / 16384) {
        case 0:
            source[n] = {.8 * std::sin(n * .731), .6 * std::cos(n * .419)};
            break;
        case 1:
            source[n] = {4. * generator() / generator.max() - 2,
                         4. * generator() / generator.max() - 2};
            break;
        case 2:
            source[n] = {0, 0};
            break;
        default: {
            const double x = n % 8192 == 8191 ? 1e6 : 2. - double(n % 8192) / 8192;
            source[n] = {x, -.25 * x};
            break;
        }
        }
    }
    const unsigned callbacks = static_cast<uint64_t>(seconds) * rate / block;
    const double budget = 1e6 * block / rate;
    std::vector<double> wall(callbacks), cpu(callbacks);
    double checksum = 0;
    unsigned lateWall = 0, lateCpu = 0, lateSchedule = 0;
    const auto scheduleStart = std::chrono::steady_clock::now();
    for (unsigned b = 0; b < callbacks; ++b) {
        const auto scheduled =
            scheduleStart + std::chrono::nanoseconds(uint64_t(b) * block * 1000000000 / rate);
        if (paced)
            std::this_thread::sleep_until(scheduled);
        const auto begin = std::chrono::steady_clock::now();
        const double startCpu = cpuTime();
        if (!control && b % 73 == 0)
            for (auto &e : engines) {
                e->set(Lookahead, (b % 11) * .5);
                e->set(Style, 1 + b % 3);
                e->set(Attack, 1 + b % 999);
                e->set(Release, 20 + b % 700);
                e->set(StereoLink, b % 101);
                e->set(ReleaseLink, 100 - b % 101);
                e->set(TruePeak, (b / 73) % 2);
            }
        if (control) {
            // Fixed arithmetic workload, independent of audio/DSP/queue state.
            // Comparable callback duration helps distinguish machine timing
            // disturbances from data-dependent limiter work.
            double x = .1 + double(b % 101) * .001;
            for (unsigned j = 0; j < 65536; ++j)
                x = x * .999999 + .000001;
            checksum += x;
        } else
            for (auto &e : engines)
                for (unsigned j = 0; j < block; ++j) {
                    const auto x = source[(static_cast<uint64_t>(b) * block + j) % source.size()];
                    double l = x[0], r = x[1];
                    e->sample(l, r);
                    checksum += l + r;
                }
        cpu[b] = cpuTime() - startCpu;
        wall[b] =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin)
                .count();
        lateWall += wall[b] > budget;
        lateCpu += cpu[b] > budget;
        if (paced)
            lateSchedule +=
                std::chrono::duration<double, std::micro>(begin - scheduled).count() + wall[b] >
                budget;
    }
    const double totalCpu = std::accumulate(cpu.begin(), cpu.end(), 0.);
    std::vector<unsigned> order(callbacks);
    std::iota(order.begin(), order.end(), 0);
    std::partial_sort(order.begin(), order.begin() + std::min(10u, callbacks), order.end(),
                      [&](unsigned a, unsigned b) { return cpu[a] > cpu[b]; });
    std::cout << "{\"slowest\":[";
    for (unsigned i = 0; i < std::min(10u, callbacks); ++i) {
        const unsigned b = order[i];
        if (i)
            std::cout << ",";
        std::cout << "{\"callback\":" << b
                  << ",\"source_frame\":" << (uint64_t(b) * block) % source.size()
                  << ",\"cpu_us\":" << cpu[b] << ",\"wall_us\":" << wall[b] << "}";
    }
    std::cout << "],";
    std::sort(cpu.begin(), cpu.end());
    std::sort(wall.begin(), wall.end());
    auto stats = [&](const auto &v) {
        std::cout << "{\"median_us\":" << v[callbacks / 2]
                  << ",\"p99_us\":" << v[callbacks * 99 / 100]
                  << ",\"p999_us\":" << v[callbacks * 999 / 1000] << ",\"max_us\":" << v.back()
                  << "}";
    };
    std::cout << "\"paced\":" << (paced ? "true" : "false") << ",\"late_schedule\":" << lateSchedule
              << ",\"control_workload\":" << (control ? "true" : "false")
              << ",\"seconds_audio\":" << seconds << ",\"instances\":" << instances
              << ",\"rate\":" << rate << ",\"block\":" << block << ",\"callbacks\":" << callbacks
              << ",\"budget_us\":" << budget << ",\"cpu_percent\":" << totalCpu / (seconds * 1e4)
              << ",\"wall\":";
    stats(wall);
    std::cout << ",\"cpu\":";
    stats(cpu);
    std::cout << ",\"late_wall\":" << lateWall << ",\"late_cpu\":" << lateCpu
              << ",\"checksum\":" << checksum << ",\"instance_bytes\":" << sizeof(Engine) << "}\n";
}
