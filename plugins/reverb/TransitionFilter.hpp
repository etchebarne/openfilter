#pragma once
#include <openfilter/dsp/Filter.hpp>

namespace openfilter::reverb {
// Two bounded branches avoid reinterpreting an energized SVF state as a new
// filter shape. Rapid shape requests coalesce until the current fade finishes.
struct TransitionFilter {
    dsp::Filter current{}, previous{};
    dsp::Coefficients coefficients{}, previousCoefficients{}, requested{};
    int shape = 0, requestedShape = 0;
    unsigned position = 0, length = 1;
    void reset(const dsp::Coefficients &c, int type, unsigned samples) noexcept {
        current.reset();
        previous.reset();
        coefficients = previousCoefficients = requested = c;
        shape = requestedShape = type;
        position = length = std::max(1u, samples);
    }
    void update(const dsp::Coefficients &c, int type) noexcept {
        requested = c;
        requestedShape = type;
        if (shape == type)
            coefficients = c;
    }
    double process(double x) noexcept {
        if (position == length && requestedShape != shape) {
            previous = current;
            previousCoefficients = coefficients;
            current.reset();
            shape = requestedShape;
            coefficients = requested;
            position = 0;
        }
        const double y = current.process(x, coefficients);
        if (position == length)
            return y;
        const double old = previous.process(x, previousCoefficients);
        const double t = double(++position) / length;
        const double mix = t * t * (3 - 2 * t);
        return old + (y - old) * mix;
    }
};
} // namespace openfilter::reverb
