#pragma once
#include "Parameters.hpp"
#include <openfilter/dsp/Filter.hpp>

namespace openfilter::limiter {
class LegacyEngine {
  public:
    void prepare(double rate, const Values &values) noexcept;
    void reset(const Values &values) noexcept { prepare(rate_, values); }
    void set(unsigned i, double value) noexcept;
    void sample(double &l, double &r, bool mono = false) noexcept;
    uint32_t latency() const noexcept { return latency_; }
    double gainReduction() const noexcept { return reduction_; }
    double inputPeak() const noexcept { return inputPeak_; }

  private:
    static constexpr unsigned capacity = 3842, peakCapacity = 11523;
    struct Minimum {
        std::array<double, peakCapacity> value{};
        std::array<uint64_t, peakCapacity> time{};
        unsigned head = 0, size = 0;
        double push(double v, uint64_t n, unsigned window) noexcept;
    };
    std::array<Minimum, 2> minimum_{};
    std::array<std::array<double, capacity>, 2> dry_{}, driven_{}, gains_{};
    std::array<double, capacity> drive_{};
    std::array<dsp::Ramp, parameterCount> ramps_{};
    std::array<double, 2> envelope_{1, 1}, slow_{};
    std::array<long double, 2> sum_{};
    uint64_t samples_ = 0;
    unsigned position_ = 0, latency_ = 240, hold_ = 480, smoothing_ = 480;
    double rate_ = 48000, slowDecay_ = 0, reduction_ = 0, inputPeak_ = 0;
};
} // namespace openfilter::limiter
