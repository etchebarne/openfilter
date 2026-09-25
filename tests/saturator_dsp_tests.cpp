#include "Engine.hpp"
#include "SaturatorRegression.hpp"
#include "Test.hpp"
#include "fixtures/saturator-v1.hpp"
#include <iostream>
#include <limits>
#include <vector>
extern thread_local bool realtime;
using namespace openfilter::saturator;
// Direct-form, explicitly zero-stuffed FIR reference for the shared polyphase
// optimization. Includes N=49 so power-of-two history padding is exercised.
template <unsigned N> void resamplingReference() {
    openfilter::dsp::HalfBand<N> filter;
    filter.prepare();
    std::array<double, N> h{};
    double sum = 0;
    for (unsigned k = 1; k < N; k += 2) {
        const double t = double(k) - (N - 1) / 2.;
        h[k] = std::sin(std::numbers::pi * t / 2) / (std::numbers::pi * t) *
               std::cyl_bessel_i(0, 10 * std::sqrt(1 - t * t / ((N - 1) * (N - 1) / 4.))) /
               std::cyl_bessel_i(0, 10);
        sum += h[k];
    }
    for (auto &v : h)
        v *= .5 / sum;
    h[(N - 1) / 2] = .5;
    std::array<double, 2048> input{}, up{}, shaped{};
    for (unsigned n = 0; n < input.size() / 2; ++n) {
        input[2 * n] = .4 * std::sin(n * .731) + .2 * std::cos(n * .173);
        for (unsigned phase = 0; phase < 2; ++phase) {
            const unsigned at = 2 * n + phase;
            for (unsigned k = 0; k < N && k <= at; ++k)
                up[at] += 2 * h[k] * input[at - k];
            shaped[at] = std::tanh(up[at]);
        }
        const auto pair = filter.up(input[2 * n]);
        near(pair[0], up[2 * n], 2e-14);
        near(pair[1], up[2 * n + 1], 2e-14);
        double ref = 0;
        for (unsigned k = 0; k < N && k <= 2 * n; ++k)
            ref += h[k] * shaped[2 * n - k];
        near(filter.down(std::tanh(pair[0]), std::tanh(pair[1])), ref, 2e-14);
    }
}
int main() {
    try {
        resamplingReference<17>();
        resamplingReference<33>();
        resamplingReference<49>();
        resamplingReference<129>();
        for (double rate : {44100., 48000., 96000., 192000., 768000.}) {
            Engine e;
            regressionAudio(e, rate, [&](unsigned frame, double left, double right) {
                for (const auto &point : saturator_golden::points)
                    if (point.rate == rate && point.frame == frame) {
                        near(left, point.left, 2e-10);
                        near(right, point.right, 2e-10);
                    }
            });
        }
        Curves curves;
        double maxCurveError = 0;
        for (unsigned style = 0; style < 4; ++style) {
            near(curves(style, 0), 0, 0);
            for (unsigned n = 0; n <= 200000; ++n) {
                const double x = (int(n) - 100000) * .0002;
                maxCurveError =
                    std::max(maxCurveError, std::abs(curves(style, x) - shape(style, x)));
            }
            for (double x : {-1e9, -12., -1., -.35, 1e-15, .35, 1., 12., 1e9})
                near(curves(style, x), shape(style, x), 5e-12);
        }
        CHECK(maxCurveError < 5e-12);
        std::cout << "Max analytic curve residual: " << maxCurveError << '\n';
        // Silent fast path must advance control smoothing identically to an
        // awake engine. A -580 dBFS probe keeps the comparison engine awake.
        Engine quiet, awake;
        quiet.prepare(48000, defaults());
        awake.prepare(48000, defaults());
        for (unsigned n = 0; n < 4096; ++n) {
            if (n == 123)
                for (Engine *e : {&quiet, &awake}) {
                    e->set(Input, 3);
                    e->set(CrossoverLow, 800);
                    e->set(band(1, Drive), 17);
                    e->set(band(1, Style), 3);
                    e->set(band(1, Presence), -4);
                }
            double a = n < 900 ? 0 : .2 * std::sin(n * .3), b = a;
            double c = n < 900 ? 1e-29 : a, d = c;
            quiet.sample(a, b);
            awake.sample(c, d);
            near(a, c, 3e-12);
            near(b, d, 3e-12);
            if (n < 900)
                CHECK(quiet.silent());
        }
        // No threshold gate: process the entire tail until all histories are zero.
        for (unsigned n = 0; n < 48000 * 9; ++n) {
            double a = 0, b = 0;
            quiet.sample(a, b);
        }
        CHECK(quiet.silent());
        for (double rate : {1000., 44100., 48000., 96000., 192000., 768000.}) {
            Engine e;
            auto v = defaults();
            v[Bypass] = 1;
            e.prepare(rate, v);
            realtime = true;
            for (unsigned n = 0; n < 1024; ++n) {
                double l = n == 0 ? .7 : 0, r = -l;
                e.sample(l, r);
                near(l, n == Engine::delay ? .7 : 0, 1e-15);
                near(r, -l, 1e-15);
            }
            realtime = false;
            v = defaults();
            e.prepare(rate, v);
            for (unsigned n = 0; n < 4096; ++n) {
                double l = .3 * std::sin(n * .13), r = 0;
                e.sample(l, r);
                CHECK(std::isfinite(l));
                near(r, 0, 1e-15);
            }
            // Wet zero and disabled bands must share the exact same linear path.
            Engine dry, off;
            v[Mix] = 0;
            dry.prepare(rate, v);
            v[Mix] = 100;
            for (unsigned b = 0; b < 3; ++b)
                v[band(b, Enabled)] = 0;
            off.prepare(rate, v);
            for (unsigned n = 0; n < 2048; ++n) {
                double a = .7 * std::sin(n * .7), b = -a, c = a, d = b;
                dry.sample(a, b);
                off.sample(c, d);
                near(a, c, 1e-13);
                near(b, d, 1e-13);
            }
            // Every descriptor extreme, automation and nonfinite input stays bounded.
            e.prepare(rate, defaults());
            realtime = true;
            for (unsigned n = 0; n < 8192; ++n) {
                if (n % 31 == 0)
                    for (unsigned i = 0; i < parameterCount; ++i)
                        e.set(i, (n / 31) % 2 ? parameter(i).min : parameter(i).max);
                double l = n % 53 == 0 ? std::numeric_limits<double>::infinity()
                                       : 2 * std::sin(n * .3),
                       r = -l;
                e.sample(l, r);
                CHECK(std::isfinite(l) && std::isfinite(r));
                CHECK(std::abs(l) < 1e9);
            }
            realtime = false;
        }
        // Mono bus processing equals dual-mono stereo, including linked dynamics.
        for (unsigned style = 0; style < 4; ++style) {
            Engine mono, stereo;
            auto v = defaults();
            for (unsigned b = 0; b < 3; ++b) {
                v[band(b, Style)] = style;
                v[band(b, Dynamics)] = 65;
            }
            mono.prepare(48000, v);
            stereo.prepare(48000, v);
            for (unsigned n = 0; n < 4096; ++n) {
                double a = .2 * std::sin(n * .7), b = a, c = a, d = a;
                mono.sample(a, b, true);
                stereo.sample(c, d);
                near(a, c, 0);
                near(b, d, 0);
            }
        }
        // Reset clears crossover, FIR, DC, dynamics and interpolation histories.
        Engine e;
        e.prepare(48000, defaults());
        std::vector<double> first;
        for (unsigned pass = 0; pass < 2; ++pass) {
            e.reset(defaults());
            for (unsigned n = 0; n < 4096; ++n) {
                double l = .5 * std::sin(n * .13), r = l;
                e.sample(l, r);
                if (!pass)
                    first.push_back(l);
                else
                    near(l, first[n], 0);
            }
        }
        for (unsigned style = 0; style < 4; ++style) {
            auto v = defaults();
            for (unsigned b = 0; b < 3; ++b) {
                v[band(b, Style)] = style;
                v[band(b, Drive)] = 24;
            }
            e.prepare(48000, v);
            for (unsigned n = 0; n < 10000; ++n) {
                double l = 0, r = 0;
                e.sample(l, r);
                near(l, 0, 1e-15);
                near(r, 0, 1e-15);
            }
        }
        std::cout << "Saturator DSP: delay, channel isolation, aligned dry, extreme automation, "
                     "silence and deterministic reset passed\n";
    } catch (const std::exception &e) {
        realtime = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
