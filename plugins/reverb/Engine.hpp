#pragma once
#include "Parameters.hpp"
#include "TransitionFilter.hpp"
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
    void prepare(double rate, const Values &values, unsigned revision = 2) noexcept;
    unsigned revision() const noexcept { return revision_; }
    void setRevision(unsigned revision, const Values &values) noexcept {
        if (revision_ != revision) {
            const double tempo = bpm_;
            revision_ = revision;
            reset(values);
            bpm_ = tempo;
        }
    }
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
    struct MovingTap {
        double from = 0, to = 0, pending = 0;
        unsigned position = 0, length = 1;
        void reset(double delay, unsigned samples) {
            from = to = pending = delay;
            position = length = samples;
        }
        void request(double delay) { pending = delay; }
        double weight() const {
            const double t = double(position) / length;
            return t * t * (3 - 2 * t);
        }
        void step() {
            if (position < length)
                ++position;
            if (position == length) {
                from = to;
                if (pending != to) {
                    to = pending;
                    position = 0;
                }
            }
        }
    };
    unsigned revision_ = 2;
    std::array<MovingTap, 8> roomTaps_{};
    MovingTap preTap_{};
    static constexpr unsigned scatterCapacity = 65536;
    std::array<std::array<double, scatterCapacity>, 2> reflections_{};
    static constexpr unsigned inputCapacity = 8192;
    std::array<std::array<std::array<double, inputCapacity>, 8>, 3> inputDiffusion_{};
    std::array<std::array<unsigned, 8>, 3> inputDelays_{};
    std::array<std::array<unsigned, 12>, 2> reflectionDelays_{};
    std::array<double, 4> oscSin_{}, oscCos_{}, oscStepSin_{}, oscStepCos_{};
    std::array<bool, bands + 1> dampingActive_{};
    std::array<bool, bands> postActive_{};
    double cachedInput_ = 1, cachedOutput_ = 1, cachedHold_ = 0, gateRelease_ = 0;
    double lastInput_ = 1e100, lastOutput_ = 1e100, lastHold_ = -1;
    unsigned scatterPosition_ = 0;
    double movingRead(const double *buffer, unsigned size, unsigned position,
                      const MovingTap &tap) const noexcept;
    static constexpr unsigned capacity = 196608, preCapacity = 393218, diffuserCapacity = 16384;
    std::array<std::array<double, capacity>, 8> tank_{};
    std::array<std::array<double, preCapacity>, 2> pre_{};
    std::array<std::array<std::array<double, diffuserCapacity>, 4>, 2> diffuser_{};
    std::array<std::array<dsp::Filter, bands + 1>, 8> damping_{};
    std::array<std::array<TransitionFilter, bands>, 8> refinedDamping_{};
    std::array<std::array<dsp::Coefficients, bands + 1>, 8> dampingCoefficients_{};
    std::array<std::array<dsp::Filter, bands>, 2> post_{};
    std::array<dsp::Coefficients, bands> postCoefficients_{};
    std::array<std::array<TransitionFilter, bands>, 2> refinedPost_{};
    std::array<double, 8> delays_{}, feedback_{}, phase_{};
    std::array<unsigned, 4> diffuserLengths_{};
    std::array<dsp::Ramp, parameterCount> ramps_{};
    Values values_ = defaults(), coefficientValues_ = defaults();
    std::array<double, 2> wet_{};
    unsigned position_ = 0, prePosition_ = 0, diffuserPosition_ = 0, clock_ = 0, smoothing_ = 960,
             hold_ = 0;
    double rate_ = 48000, bpm_ = 120, env_ = 0, gate_ = 0, reduction_ = 0, attack_ = 0,
           release_ = 0;
    void coefficients(bool force = false) noexcept;
    static double read(const double *buffer, unsigned size, unsigned position,
                       double delay) noexcept;
};
} // namespace openfilter::reverb
