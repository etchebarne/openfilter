#pragma once
#include "Parameters.hpp"
#include <openfilter/dsp/Filter.hpp>

namespace openfilter::deesser {
class Engine {
  public:
    void prepare(double rate, const Values &values) noexcept;
    void reset(const Values &values) noexcept { prepare(rate_, values); }
    void set(unsigned i, double value) noexcept;
    void sample(double &l, double &r, bool mono = false, double scL = 0, double scR = 0) noexcept;
    uint32_t latency() const noexcept { return latency_; }
    // Conservative filter decay allowance, including lowest supported sample rate.
    uint32_t tail() const noexcept { return latency_ + static_cast<uint32_t>(rate_); }
    double gainReduction() const noexcept { return reduction_; }
    double detectorLevel() const noexcept { return detectorLevel_; }
    // Audio-thread telemetry only, read immediately after sample(). No DSP mutation.
    double displayInput(unsigned channel) const noexcept {
        return raw_[channel < 2 ? channel : 0][(position_ + capacity - 1 - latency_) % capacity];
    }
    double displayDetector(unsigned channel) const noexcept {
        return detector_[channel < 2 ? channel : 0]
                        [(position_ + capacity - 1 - latency_) % capacity];
    }

  private:
    static constexpr unsigned capacity = 11522; // ceil(768 kHz * 15 ms) + interpolation
    std::array<std::array<double, capacity>, 2> raw_{}, audio_{}, detector_{}, requests_{};
    std::array<dsp::Ramp, parameterCount> ramps_{};
    std::array<dsp::Filter, 2> hp_{}, lp_{}, shelf_{};
    std::array<double, 2> power_{}, broadPower_{}, envelope_{};
    dsp::Coefficients hpCoefficients_{}, lpCoefficients_{};
    double lastLow_ = -1, lastHigh_ = -1;
    unsigned position_ = 0, latency_ = 720, smoothing_ = 480;
    double rate_ = 48000, powerDecay_ = 0, reduction_ = 0, detectorLevel_ = 0;
};
} // namespace openfilter::deesser
