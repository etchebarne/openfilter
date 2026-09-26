#pragma once
#include "Engine.hpp"
namespace openfilter::saturator {
// Fixed automation sequence for the 0.1.0 audio compatibility fixture. Includes
// control ramps during silence, overlapping style fades, crossed crossovers,
// tone/dynamics changes and independently driven stereo channels.
template <class Sink> void regressionAudio(Engine &engine, double rate, Sink sink) {
    auto values = defaults();
    values[AutoLevel] = 0;
    values[Compensation] = 100;
    for (unsigned b = 0; b < 3; ++b)
        values[band(b, Style)] = 0;
    engine.prepare(rate, values);
    for (unsigned n = 0; n < 8192; ++n) {
        if (n % 117 == 25) {
            const double t = std::sin(n * .01);
            engine.set(Input, t * 6);
            engine.set(Output, t * -3);
            engine.set(Compensation, 50 + 50 * t);
            engine.set(Mix, 50 + 40 * t);
            engine.set(CrossoverLow, 250 + 200 * t);
            engine.set(CrossoverHigh, 4000 + 2000 * t);
            for (unsigned b = 0; b < 3; ++b) {
                engine.set(band(b, Style), (n / 117 + b) % 4);
                engine.set(band(b, Drive), 18 + 17 * t);
                engine.set(band(b, Dynamics), 80 * t);
                engine.set(band(b, Level), -3 * t);
                for (unsigned f = Bass; f <= Presence; ++f)
                    engine.set(band(b, f), 9 * t);
            }
        }
        if (n == 1250) {
            engine.set(CrossoverLow, 4000);
            engine.set(CrossoverHigh, 200);
        }
        double l = n < 128 ? 0 : .21 * std::sin(n * .317) + .07 * std::cos(n * .071);
        double r = n < 128 ? 0 : .3 * std::cos(n * .219);
        engine.sample(l, r);
        sink(n, l, r);
    }
}
} // namespace openfilter::saturator
