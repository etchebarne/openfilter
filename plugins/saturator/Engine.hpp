#pragma once
#include "Curves.hpp"
#include "Parameters.hpp"
#include "Resampling.hpp"
#include <openfilter/dsp/Filter.hpp>
#include <openfilter/dsp/HalfBand.hpp>
namespace openfilter::saturator {
// Original smooth static curves. Unit slope at the origin, zero output at zero.
inline double shape(unsigned style, double x) noexcept {
    switch (style) {
    case 1:
        return std::atan(x);
    case 2:
        return x / std::sqrt(1 + x * x);
    case 3: {
        constexpr double bias = .35;
        const double t = std::tanh(bias);
        return (std::tanh(x + bias) - t) / (1 - t * t);
    }
    default:
        return std::tanh(x);
    }
}
class Engine {
  public:
    static constexpr unsigned delay = 76;
    static unsigned oversampling(double rate) noexcept {
        return rate <= 48000    ? 32
               : rate <= 96000  ? 16
               : rate <= 192000 ? 8
               : rate <= 384000 ? 4
                                : 2;
    }
    void prepare(double rate, const Values &values) noexcept;
    void reset(const Values &v) noexcept { prepare(rate_, v); }
    void set(unsigned i, double value) noexcept;
    // Channel configuration remains fixed between prepare/reset calls.
    void sample(double &l, double &r, bool mono = false) noexcept;
    uint32_t latency() const noexcept { return delay; }
    bool silent() const noexcept { return silent_; }

  private:
    struct Split {
        std::array<dsp::Filter, 2> low{}, high{};
        std::array<double, 2> sample(double x, const dsp::Coefficients &lp,
                                     const dsp::Coefficients &hp) noexcept {
            return {low[1].process(low[0].process(x, lp), lp),
                    high[1].process(high[0].process(x, hp), hp)};
        }
    };
    struct Channel {
        Cascade wet;
        MatchedDry dry;
        std::array<dsp::Filter, 4> tone{};
        double dcX = 0, dcY = 0;
    };
    bool silent_ = true;
    unsigned silenceCheck_ = 0;
    bool historiesSilent() const noexcept;
    const Curves *curves_ = nullptr;
    // Cache only stationary controls. Smoothed values still update each sample.
    struct Gain {
        double previous = 1e99, value = 1;
        double get(double db) noexcept {
            if (db != previous) {
                previous = db;
                value = std::pow(10., db / 20);
            }
            return value;
        }
    };
    Gain inputGain_, outputGain_;
    std::array<Gain, 3> driveGain_{}, bandGain_{};
    std::array<double, 3> lastDrive_{}, lastCompensation_{}, compensationGain_{};
    MatchedDry::Kernel dryKernel_{};
    std::array<std::array<Split, 3>, 2> split_{};
    std::array<std::array<Channel, 2>, 3> channels_{};
    std::array<dsp::Ramp, parameterCount> ramps_{};
    std::array<std::array<dsp::Ramp, 4>, 3> styles_{};
    std::array<std::array<dsp::Coefficients, 4>, 3> tone_{};
    std::array<std::array<double, 4>, 3> lastTone_{};
    std::array<double, 4> toneTangent_{};
    std::array<dsp::Coefficients, 2> lp_{}, hp_{};
    double lastLow_ = -1, lastHigh_ = -1;
    std::array<double, 3> envelope_{};
    std::array<std::array<double, delay + 1>, 2> dry_{};
    std::array<std::array<double, 13>, 2> wetDelay_{};
    unsigned position_ = 0, smoothing_ = 480, factor_ = 32, padding_ = 0, wetPosition_ = 0;
    double rate_ = 48000, dcPole_ = 0, attack_ = 0, release_ = 0;
};
} // namespace openfilter::saturator
