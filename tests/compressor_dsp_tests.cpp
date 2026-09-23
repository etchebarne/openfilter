#include "Engine.hpp"
#include "Test.hpp"
#include <iostream>
#include <limits>
#include <random>
extern thread_local bool realtime;
using namespace openfilter::compressor;
void run(Engine &e, double &l, double &r, double sc = 0) {
    realtime = true;
    e.sample(l, r, false, sc, sc);
    realtime = false;
}
int main() {
    try {
        for (double rate : {44100., 48000., 88200., 96000., 176400., 192000., 768000.}) {
            Engine e;
            auto v = defaults();
            v[Bypass] = 1;
            e.prepare(rate, v);
            const unsigned delay = static_cast<unsigned>(std::ceil(rate * .01));
            CHECK(e.latency() == delay);
            for (unsigned n = 0; n < delay + 10; ++n) {
                double l = n == 0 ? 1 : 0, r = l;
                run(e, l, r);
                near(l, n == delay ? 1 : 0, 0);
                near(l, r, 0);
            }
            v[Bypass] = 0;
            v[Ratio] = 1;
            v[Mix] = 37;
            e.prepare(rate, v);
            for (unsigned n = 0; n < delay + 10; ++n) {
                double l = n == 0 ? 1 : 0, r = l;
                run(e, l, r);
                near(l, n == delay ? 1 : 0, 1e-15);
            }
        }
        Engine e;
        auto v = defaults();
        v[Knee] = 0;
        v[Attack] = .1;
        v[Release] = 10;
        e.prepare(48000, v);
        double l = 0, r = 0;
        for (unsigned n = 0; n < 48000; ++n) {
            l = .5;
            r = .125;
            run(e, l, r);
        }
        const double expected = std::pow(10., (-18 + (20 * std::log10(.5) + 18) / 4) / 20.);
        near(l, expected, 1e-12);
        near(r / l, .25, 1e-12);
        v[StereoLink] = 0;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 48000; ++n) {
            l = .5;
            r = .125;
            run(e, l, r);
        }
        near(l, expected, 1e-12);
        near(r, .125, 1e-12);
        v[Range] = 3;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 48000; ++n) {
            l = .5;
            r = .125;
            run(e, l, r);
        }
        near(l, .5 * std::pow(10., -3. / 20), 1e-12);
        // External silence prevents compression; hot sidechain compresses quiet audio.
        v = defaults();
        v[Sidechain] = 1;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 48000; ++n) {
            l = .5;
            r = .5;
            run(e, l, r);
        }
        near(l, .5, 1e-12);
        for (unsigned n = 0; n < 48000; ++n) {
            l = .01;
            r = .01;
            run(e, l, r, 1);
        }
        CHECK(l < .003);
        // High-pass is detector-only: rejects DC without filtering program audio.
        v[SidechainHP] = 200;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 96000; ++n) {
            l = .1;
            r = .1;
            run(e, l, r, 1);
        }
        near(l, .1, 1e-5);
        // Lookahead attenuates the first delayed transient; zero lookahead preserves it.
        auto first = [&](double lookahead) {
            auto p = defaults();
            p[Attack] = 1;
            p[Lookahead] = lookahead;
            e.prepare(48000, p);
            double out = 0;
            for (unsigned n = 0; n <= 480; ++n) {
                l = 1;
                r = 1;
                run(e, l, r);
                out = l;
            }
            return out;
        };
        CHECK(first(10) < first(0) * .4);
        // Attack dB time constant: peak detector sees constant external DC immediately.
        v = defaults();
        v[Sidechain] = 1;
        v[Lookahead] = 10;
        v[Knee] = 0;
        v[Attack] = 10;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 480; ++n) {
            l = 1;
            r = 1;
            run(e, l, r, 1);
        }
        near(e.gainReduction(), 13.5 * (1 - std::exp(-1.)), 1e-10);
        // Hold postpones release even after detector silence.
        v[Hold] = 100;
        v[Attack] = .1;
        v[Release] = 10;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 48000; ++n) {
            l = 1;
            r = 1;
            run(e, l, r, 1);
        }
        const auto held = e.gainReduction();
        for (unsigned n = 0; n < 4000; ++n) {
            l = 1;
            r = 1;
            run(e, l, r, 0);
        }
        near(e.gainReduction(), held, 1e-10);
        for (unsigned n = 0; n < 10000; ++n) {
            l = 1;
            r = 1;
            run(e, l, r, 0);
        }
        CHECK(e.gainReduction() < .01);
        // Random extreme automation, invalid input, silence and reset recovery.
        std::mt19937 gen(421);
        v = defaults();
        e.prepare(192000, v);
        for (unsigned n = 0; n < 100000; ++n) {
            if (n % 113 == 0) {
                unsigned i = gen() % parameterCount;
                const auto p = parameter(i);
                e.set(i, n % 2 ? p.min : p.max);
            }
            l = n % 509 == 0 ? std::numeric_limits<double>::infinity() : std::sin(n * .1) * 3;
            r = n % 727 == 0 ? std::numeric_limits<double>::quiet_NaN() : -l;
            run(e, l, r);
            CHECK(std::isfinite(l) && std::isfinite(r));
            CHECK(std::abs(l) < 1e6);
        }
        e.reset(defaults());
        for (unsigned n = 0; n < 2000; ++n) {
            l = r = 0;
            run(e, l, r);
            near(l, 0, 0);
        }
        std::cout << "Compressor audio: latency/rates, transfer, link, range, sidechain, "
                     "lookahead, timing, hold, finite stress and allocation guards passed\n";
    } catch (const std::exception &e) {
        realtime = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
