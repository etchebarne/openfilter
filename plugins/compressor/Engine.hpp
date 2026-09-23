#pragma once
#include "Parameters.hpp"
#include <openfilter/dsp/Filter.hpp>

namespace openfilter::compressor {
// Positive attenuation in dB. The editor uses exactly this static characteristic.
inline double reduction(double level, double threshold, double ratio, double knee,
                        double range) noexcept {
    const double x = level - threshold, slope = 1 - 1 / ratio;
    const double g = knee > 0 && std::abs(x) < knee * .5
                         ? slope * (x + knee * .5) * (x + knee * .5) / (2 * knee)
                         : slope * std::max(0., x);
    return std::min(range, g);
}
class Engine {
  public:
    void prepare(double rate, const Values &values) noexcept;
    void reset(const Values &values) noexcept { prepare(rate_, values); }
    void set(unsigned i, double value) noexcept;
    void sample(double &l, double &r, bool mono = false, double scL = 0, double scR = 0) noexcept;
    uint32_t latency() const noexcept { return latency_; }
    double gainReduction() const noexcept { return reduction_; }

  private:
    // Maximum supported rate: 768 kHz. Fixed storage is never resized in process.
    static constexpr unsigned capacity = 7682;
    std::array<std::array<double, capacity>, 2> audio_{}, detector_{};
    std::array<dsp::Ramp, parameterCount> ramps_{};
    std::array<double, 2> hpX_{}, hpY_{}, peak_{}, power_{}, envelope_{}, slow_{};
    std::array<unsigned, 2> hold_{};
    unsigned position_ = 0, latency_ = 480, smoothing_ = 480;
    double rate_ = 48000, detectorDecay_ = 0, slowDecay_ = 0, reduction_ = 0;
};
} // namespace openfilter::compressor
