#pragma once
#include "ModernEngine.hpp"
#include "Resampling.hpp"

namespace openfilter::limiter {
class Engine {
  public:
    void prepare(double rate, const Values &values) noexcept;
    void reset(const Values &values) noexcept { prepare(rate_, values); }
    void set(unsigned i, double value) noexcept;
    void sample(double &l, double &r, bool mono = false) noexcept;
    uint32_t latency() const noexcept {
        return mainDelay_ + resamplingDelay_ + BandLimit::delay + guardDelay_;
    }
    uint32_t tail() const noexcept { return latency() + resamplingDelay_ + BandLimit::delay; }
    unsigned oversampling() const noexcept { return factor_; }
    double gainReduction() const noexcept { return reduction_; }
    double inputPeak() const noexcept { return inputPeak_; }
    double outputPeak(unsigned channel) const noexcept {
        return outputPeak_[channel < 2 ? channel : 0];
    }

  private:
    static constexpr unsigned capacity = 8192;
    LegacyEngine legacy_;
    ModernEngine modern_;
    std::array<HalfBand<257>, 2> first_{};
    std::array<HalfBand<33>, 2> second_{};
    std::array<PeakDetector, 2> protection_{}, meters_{};
    std::array<BandLimit, 2> bandLimit_{};
    std::array<std::array<double, capacity>, 2> preBand_{};
    std::array<Minimum, 2> guardMinimum_{};
    std::array<std::array<double, capacity>, 2> dry_{}, wet_{}, legacyDelay_{}, guardGain_{};
    std::array<double, capacity> drive_{}, inputHistory_{}, reductionHistory_{},
        legacyReductionHistory_{};
    std::array<dsp::Ramp, parameterCount> ramps_{};
    std::array<long double, 2> guardSum_{};
    std::array<double, 2> guardEnvelope_{1, 1}, outputPeak_{};
    double guardDecay_ = 0, rate_ = 48000, reduction_ = 0, inputPeak_ = 0;
    unsigned mainDelay_ = 240, resamplingDelay_ = 136, guardDelay_ = 666, guardSmooth_ = 144,
             guardHold_ = 480, factor_ = 4, smoothing_ = 480, position_ = 0, guardPosition_ = 0;
    uint64_t samples_ = 0;
};
} // namespace openfilter::limiter
