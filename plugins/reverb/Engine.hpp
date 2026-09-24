#pragma once
#include "Parameters.hpp"
#include <openfilter/dsp/Filter.hpp>

namespace openfilter::reverb {
// Positive feedback boosts are bounded jointly; stationary loop gain < 1.
inline double boostScale(const Values &v) noexcept {
    double sum = 0;
    for (unsigned b = 0; b < bands; ++b)
        if (v[bandIndex(false, b, Enabled)] > 0)
            sum += v[bandIndex(false, b, Enabled)] *
                   std::max(0., 1 - 100 / v[bandIndex(false, b, Amount)]);
    return sum > .75 ? .75 / sum : 1.;
}
inline double decayContribution(const Values &v, unsigned b) noexcept {
    const auto i = bandIndex(false, b, 0);
    if (v[i + Enabled] <= 0)
        return 0;
    const double a = v[i + Enabled] * (1 - 100 / v[i + Amount]);
    return a > 0 ? a * boostScale(v) : a;
}
class Engine {
  public:
    void prepare(double rate, const Values &values) noexcept;
    void reset(const Values &values) noexcept;
    void set(unsigned i, double value) noexcept;
    void tempo(double bpm) noexcept {
        if (std::isfinite(bpm) && bpm > 0)
            bpm_ = bpm;
    }
    void sample(double &l, double &r, bool mono = false) noexcept;
    uint32_t latency() const noexcept { return 0; }
    double gainReduction() const noexcept { return reduction_; }
    double wet(unsigned channel) const noexcept { return wet_[channel & 1u]; }

  private:
    static constexpr unsigned capacity = 196608, preCapacity = 393218, diffuserCapacity = 16384;
    std::array<std::array<double, capacity>, 8> tank_{};
    std::array<std::array<double, preCapacity>, 2> pre_{};
    std::array<std::array<std::array<double, diffuserCapacity>, 4>, 2> diffuser_{};
    std::array<std::array<dsp::Filter, bands + 1>, 8> damping_{};
    std::array<std::array<dsp::Coefficients, bands + 1>, 8> dampingCoefficients_{};
    std::array<std::array<dsp::Filter, bands>, 2> post_{};
    std::array<dsp::Coefficients, bands> postCoefficients_{};
    std::array<double, 8> delays_{}, feedback_{}, phase_{};
    std::array<unsigned, 4> diffuserLengths_{};
    std::array<dsp::Ramp, parameterCount> ramps_{};
    Values values_ = defaults(), coefficientValues_ = defaults();
    std::array<double, 2> wet_{};
    unsigned position_ = 0, prePosition_ = 0, diffuserPosition_ = 0, clock_ = 0, smoothing_ = 960,
             hold_ = 0;
    double rate_ = 48000, bpm_ = 120, env_ = 0, gate_ = 0, reduction_ = 0, attack_ = 0,
           release_ = 0;
    void coefficients() noexcept;
    static double read(const double *buffer, unsigned size, unsigned position,
                       double delay) noexcept;
};
} // namespace openfilter::reverb
