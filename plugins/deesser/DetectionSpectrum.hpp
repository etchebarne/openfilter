#pragma once
#include <openfilter/ui/Analysis.hpp>

namespace openfilter::deesser {
// Main-thread detector spectrum. Peak aggregation preserves narrow tones when
// a logarithmic display pixel covers several FFT bins at high frequencies.
class DetectionSpectrum {
    ui::Spectrum spectrum_;

  public:
    bool update(const ui::AudioFrame &frame) { return spectrum_.update(frame); }
    void decay(double seconds) { spectrum_.decay(seconds); }
    double level(double lowHz, double highHz) const {
        const double nyquist = spectrum_.rate * .5;
        if (lowHz >= nyquist || highHz <= 0 || highHz <= lowHz)
            return -120;
        const double scale = ui::fftSize / spectrum_.rate;
        const unsigned first = static_cast<unsigned>(
            std::clamp(std::floor(lowHz * scale), 0., double(ui::fftSize / 2)));
        const unsigned last = static_cast<unsigned>(
            std::clamp(std::ceil(highHz * scale), 0., double(ui::fftSize / 2)));
        double result = -120;
        for (unsigned bin = first; bin <= last; ++bin)
            result = std::max(result, spectrum_.db[0][bin]);
        return result;
    }
};
} // namespace openfilter::deesser
