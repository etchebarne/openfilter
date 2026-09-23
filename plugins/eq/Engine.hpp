#pragma once
#include "Parameters.hpp"
#include <openfilter/dsp/Brickwall.hpp>

namespace openfilter::eq {
class Engine {
  public:
    void prepare(double rate, const Values &values) noexcept;
    void set(unsigned index, double value) noexcept;
    void reset(const Values &values) noexcept { prepare(rate_, values); }
    void sample(double &left, double &right, bool mono = false) noexcept;

  private:
    struct Band {
        dsp::Ramp frequency, gain, q, wet;
        std::array<std::array<dsp::Filter, dsp::brickwallStages>, 2> filters{};
        std::array<dsp::Coefficients, dsp::brickwallStages> coefficients{};
        int type = 0, slope = 0, route = 0;
        int nextType = 0, nextSlope = 0, nextRoute = 0;
        bool enabled = false, dirty = true, silent = true;
        bool changing() const {
            return type != nextType || slope != nextSlope || route != nextRoute;
        }
        void clear() noexcept {
            silent = true;
            for (auto &channel : filters)
                for (auto &f : channel)
                    f.reset();
        }
    };
    double rate_ = 48000;
    unsigned smoothSamples_ = 480, fadeSamples_ = 240;
    std::array<Band, bands> bands_{};
    dsp::Ramp output_, wet_;
    double outputGain_ = 1;
    void configure(Band &band) noexcept;
    void bandSample(Band &band, double &left, double &right, bool mono) noexcept;
};
} // namespace openfilter::eq
