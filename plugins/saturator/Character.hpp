#pragma once
#include <cmath>
namespace openfilter::saturator {
// Original drive calibration, not a model of a particular analog circuit.
// g is the displayed pre-gain. Transform it into a bounded excitation:
// 0 dB is exactly linear; 6/12 dB excite the curve at 5.32/9.58 instead
// of 2/4. The ceiling of 16 avoids ever-sharper transitions at high Drive.
// The outer gain retains g as the small-signal pre-gain for Compensation.
struct Character {
    double depth = 0, scale = 1;
    explicit Character(double drive) noexcept {
        depth = 16 * (drive - 1) / (drive + 1);
        scale = depth == 0 ? 1 : drive / depth;
    }
    template <class Tanh> double process(unsigned style, double x, Tanh tanh) const noexcept {
        if (depth == 0)
            return x;
        const double t = tanh(x * depth);
        // Shifted tanh with the DC offset removed and unity origin slope.
        // This identity avoids cancellation around zero. Color adds even
        // harmonics; Punch is odd symmetric. Both are smooth and monotonic.
        return scale * (style == 5 ? t / (1 + .65 * t) : t);
    }
};
} // namespace openfilter::saturator
