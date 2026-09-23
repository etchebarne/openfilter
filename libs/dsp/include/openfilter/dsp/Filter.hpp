#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>

namespace openfilter::dsp {
enum class Shape { Bell, LowShelf, HighShelf, LowCut, HighCut, Notch };

// Trapezoidal SVF, using the output mixing equations published by Andrew Simper.
// Baseline bilinear-transform response: deliberately not advertised as decramped.
struct Coefficients {
    double a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
    static Coefficients make(Shape shape, double hz, double gainDb, double q,
                             double rate) noexcept {
        const double a = std::pow(10.0, gainDb / 40.0);
        double g = std::tan(std::numbers::pi * std::clamp(hz, 1.0, rate * 0.475) / rate);
        double k = 1.0 / std::clamp(q, 0.1, 40.0);
        Coefficients c;
        switch (shape) {
        case Shape::Bell:
            k /= a;
            c.m1 = k * (a * a - 1);
            break;
        case Shape::LowShelf:
            g /= std::sqrt(a);
            c.m1 = k * (a - 1);
            c.m2 = a * a - 1;
            break;
        case Shape::HighShelf:
            g *= std::sqrt(a);
            c.m0 = a * a;
            c.m1 = k * (1 - a) * a;
            c.m2 = 1 - a * a;
            break;
        case Shape::LowCut:
            c.m1 = -k;
            c.m2 = -1;
            break;
        case Shape::HighCut:
            c.m0 = 0;
            c.m2 = 1;
            break;
        case Shape::Notch:
            c.m1 = -k;
            break;
        }
        c.a1 = 1.0 / (1 + g * (g + k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }
};

struct Filter {
    double s1 = 0, s2 = 0;
    void reset() noexcept { s1 = s2 = 0; }
    double process(double x, const Coefficients &c) noexcept {
        const double v3 = x - s2;
        const double v1 = c.a1 * s1 + c.a2 * v3;
        const double v2 = s2 + c.a2 * s1 + c.a3 * v3;
        s1 = 2 * v1 - s1;
        s2 = 2 * v2 - s2;
        // Portable denormal protection without changing the host's FP environment.
        if (std::abs(s1) < 1e-30)
            s1 = 0;
        if (std::abs(s2) < 1e-30)
            s2 = 0;
        return c.m0 * x + c.m1 * v1 + c.m2 * v2;
    }
};

struct Ramp {
    double value = 0, target = 0, step = 0;
    unsigned remaining = 0;
    void reset(double v) noexcept {
        value = target = v;
        remaining = 0;
        step = 0;
    }
    void set(double v, unsigned samples) noexcept {
        if (v == target)
            return;
        target = v;
        remaining = std::max(1u, samples);
        step = (v - value) / remaining;
    }
    double next() noexcept {
        if (remaining && --remaining == 0)
            value = target;
        else if (remaining)
            value += step;
        return value;
    }
};
} // namespace openfilter::dsp
