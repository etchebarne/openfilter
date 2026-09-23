#pragma once
#include "Parameters.hpp"
#include <complex>
#include <numbers>
#include <openfilter/dsp/Brickwall.hpp>

namespace openfilter::eq {
using Complex = std::complex<double>;
inline Complex transfer(const dsp::Coefficients &c, double hz, double rate) {
    const Complex z = std::polar(1.0, 2 * std::numbers::pi * hz / rate);
    const double a = 2 * c.a1 - 1, b = -2 * c.a2, d = 1 - 2 * c.a3, e = 2 * c.a2;
    const double b1 = 2 * c.a2, b2 = 2 * c.a3;
    const Complex determinant = (z - a) * (z - d) - b * e;
    const Complex s1 = ((z - d) * b1 + b * b2) / determinant;
    const Complex s2 = (e * b1 + (z - a) * b2) / determinant;
    return c.m0 + c.m1 * c.a2 + c.m2 * c.a3 + (c.m1 * c.a1 + c.m2 * c.a2) * s1 +
           (-c.m1 * c.a2 + c.m2 * (1 - c.a3)) * s2;
}
inline Complex bandTransfer(const Values &v, unsigned band, double hz, double rate) {
    const int type = static_cast<int>(v[index(band, Type)]);
    const int slope = static_cast<int>(v[index(band, Slope)]);
    const unsigned stages = stageCount(type, slope);
    if (isBrickwall(type, slope)) {
        const double warped =
            std::tan(std::numbers::pi *
                     std::clamp(std::exp2(v[index(band, Frequency)]), 1., rate * .475) / rate);
        Complex response = 1;
        for (unsigned s = 0; s < stages; ++s)
            response *= transfer(dsp::brickwallSection(type == 3, s, warped), hz, rate);
        return response;
    }
    Complex response = 1;
    for (unsigned s = 0; s < stages; ++s) {
        double q = std::exp2(v[index(band, Q)]);
        if (stages > 1)
            q *= std::sqrt(2.) / (2 * std::cos(std::numbers::pi * (2 * s + 1) / (4 * stages)));
        response *= transfer(dsp::Coefficients::make(static_cast<dsp::Shape>(type),
                                                     std::exp2(v[index(band, Frequency)]),
                                                     v[index(band, Gain)], q, rate),
                             hz, rate);
    }
    return response;
}
// Two input columns, two output rows. Mixed L/R/M/S ordering matches Engine.
struct StereoResponse {
    Complex ll = 1, lr = 0, rl = 0, rr = 1;
};
inline StereoResponse totalTransfer(const Values &values, double hz, double rate,
                                    bool mono = false) {
    StereoResponse total;
    if (values[0] != 0)
        return total;
    for (unsigned i = 0; i < bands; ++i) {
        if (values[index(i, Enabled)] == 0)
            continue;
        const auto h = bandTransfer(values, i, hz, rate);
        const auto route = static_cast<Route>(static_cast<int>(values[index(i, Routing)]));
        StereoResponse b;
        if (mono) {
            if (route != Route::Side && route != Route::Right)
                b.ll = h;
        } else if (route == Route::Stereo)
            b.ll = b.rr = h;
        else if (route == Route::Left)
            b.ll = h;
        else if (route == Route::Right)
            b.rr = h;
        else {
            b.ll = b.rr = (h + 1.) * .5;
            b.lr = b.rl = (h - 1.) * (route == Route::Mid ? .5 : -.5);
        }
        total = {b.ll * total.ll + b.lr * total.rl, b.ll * total.lr + b.lr * total.rr,
                 b.rl * total.ll + b.rr * total.rl, b.rl * total.lr + b.rr * total.rr};
    }
    const double gain = std::pow(10., values[1] / 20);
    total.ll *= gain;
    total.lr *= gain;
    total.rl *= gain;
    total.rr *= gain;
    return total;
}
} // namespace openfilter::eq
