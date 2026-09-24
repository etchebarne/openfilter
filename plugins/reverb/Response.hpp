#pragma once
#include "Engine.hpp"
#include <complex>
namespace openfilter::reverb {
// Frequency response of the shared trapezoidal SVF via its state-space equations.
inline std::complex<double> transfer(const dsp::Coefficients &c, double hz, double rate) {
    using Complex = std::complex<double>;
    const Complex z = std::polar(1., 2 * std::numbers::pi * hz / rate);
    const double a = 2 * c.a1 - 1, b = -2 * c.a2, d = 1 - 2 * c.a3, e = 2 * c.a2;
    const Complex det = (z - a) * (z - d) - b * e;
    const Complex s1 = ((z - d) * 2. * c.a2 + b * 2 * c.a3) / det;
    const Complex s2 = (e * 2 * c.a2 + (z - a) * 2. * c.a3) / det;
    return c.m0 + c.m1 * c.a2 + c.m2 * c.a3 + (c.m1 * c.a1 + c.m2 * c.a2) * s1 +
           (-c.m1 * c.a2 + c.m2 * (1 - c.a3)) * s2;
}
inline double postResponse(const Values &v, double hz, double rate) {
    std::complex<double> h = 1;
    for (unsigned b = 0; b < bands; ++b) {
        const auto i = bandIndex(true, b, 0);
        if (v[i + Enabled])
            h *= transfer(dsp::Coefficients::make(static_cast<dsp::Shape>(int(v[i + Type])),
                                                  v[i + Frequency], v[i + Amount], v[i + Q], rate),
                          hz, rate);
    }
    return 20 * std::log10(std::max(1e-12, std::abs(h)));
}
inline double decayResponse(const Values &v, double hz, double rate) {
    // Representative loop duration; this is a loss estimate, not a measured T60.
    const double delay = .05 * (.55 + .65 * std::sqrt(v[Space]));
    const double loss = 60 * delay / (v[Space] * v[DecayRate] * .01);
    double db = -loss;
    for (unsigned b = 0; b < bands; ++b) {
        const auto i = bandIndex(false, b, 0);
        if (!v[i + Enabled])
            continue;
        const auto shape = static_cast<dsp::Shape>(int(v[i + Type]));
        db +=
            20 * std::log10(std::max(
                     1e-12, std::abs(transfer(
                                dsp::Coefficients::make(
                                    shape, v[i + Frequency], loss * decayContribution(v, b),
                                    shape == dsp::Shape::Bell ? v[i + Q] : .7071067811865476, rate),
                                hz, rate))));
    }
    db += 20 * std::log10(std::max(
                   1e-12,
                   std::abs(transfer(dsp::Coefficients::make(dsp::Shape::HighShelf, 4000,
                                                             -loss * (1 - v[Brightness] * .01) * 3,
                                                             .7071067811865476, rate),
                                     hz, rate))));
    return std::clamp(-loss / db, .0625, 16.);
}
} // namespace openfilter::reverb
