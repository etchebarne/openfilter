#pragma once
#include "LegacyEngine.hpp"
#include "Minimum.hpp"
namespace openfilter::limiter {
class ModernEngine {
  public:
    void prepare(double rate, unsigned delay, const Values &values) noexcept;
    void set(unsigned i, double value) noexcept;
    void sample(double &l, double &r) noexcept;
    double gainReduction() const noexcept {
        return -20 * std::log10(std::max(1e-30, minimumGain_));
    }
    double minimumGain() const noexcept { return minimumGain_; }

  private:
    static constexpr unsigned capacity = 3842;
    static constexpr std::array<double, 6> horizons{0, .1, .5, 1, 2, 5};
    struct Path {
        std::array<Minimum, 2> minimum{};
        std::array<std::array<double, capacity>, 2> gains{};
        std::array<long double, 2> sum{};
        std::array<double, 2> envelope{1, 1};
        unsigned delay = 0, position = 0;
        long double reciprocal = 1;
    };
    std::array<Path, 6> paths_{};
    std::array<Minimum, 2> bodyMinimum_{};
    std::array<std::array<double, capacity>, 2> driven_{};
    std::array<dsp::Ramp, parameterCount> ramps_{};
    std::array<double, 2> bodyDb_{}, slow_{}, lastHeld_{}, heldDb_{};
    double rate_ = 48000, slowDecay_ = 0, minimumGain_ = 1;
    double lastStyle_ = -1, lastAttack_ = -1, fastDecay_ = 0, attackDecay_ = 0;
    unsigned mainDelay_ = 240, hold_ = 1248, smoothing_ = 480, position_ = 0;
    uint64_t samples_ = 0;
};
} // namespace openfilter::limiter
