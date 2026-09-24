#pragma once
#include <array>
#include <cstdint>
namespace openfilter::limiter {
// Maximum window: 31 ms at 768 kHz plus its inclusive endpoint.
struct Minimum {
    static constexpr unsigned peakCapacity = 24580;
    std::array<double, peakCapacity> value{};
    std::array<uint32_t, peakCapacity> time{};
    unsigned head = 0, size = 0;
    double push(double v, uint64_t n, unsigned window) noexcept;
    void reset() noexcept { head = size = 0; }
};
inline double Minimum::push(double v, uint64_t n, unsigned window) noexcept {
    while (size && static_cast<uint32_t>(n) - time[head] > window) {
        head = (head + 1) % peakCapacity;
        --size;
    }
    // A new minimum invalidates the whole tail in constant time. For a
    // partial tail replacement, cap linear work and binary-search the sorted
    // remainder. A single peak cannot cause thousands of audio-thread pops.
    if (size && v <= value[head])
        size = 0;
    for (unsigned popped = 0; popped < 4 && size && value[(head + size - 1) % peakCapacity] >= v;
         ++popped)
        --size;
    if (size && value[(head + size - 1) % peakCapacity] >= v) {
        unsigned lo = 0, hi = size;
        while (lo < hi) {
            const unsigned mid = lo + (hi - lo) / 2;
            if (value[(head + mid) % peakCapacity] < v)
                lo = mid + 1;
            else
                hi = mid;
        }
        size = lo;
    }
    const auto tail = (head + size++) % peakCapacity;
    value[tail] = v;
    time[tail] = static_cast<uint32_t>(n);
    return value[head];
}
} // namespace openfilter::limiter
