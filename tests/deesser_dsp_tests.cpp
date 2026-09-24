#include "Engine.hpp"
#include "Test.hpp"
#include <iostream>
#include <limits>
#include <memory>
#include <random>
extern thread_local bool realtime;
using namespace openfilter::deesser;
void run(Engine &e, double &l, double &r, double sc = 0) {
    realtime = true;
    e.sample(l, r, false, sc, sc);
    realtime = false;
}
int main() {
    try {
        auto owner = std::make_unique<Engine>();
        auto &e = *owner;
        for (double rate :
             {1000., 22050., 44100., 48000., 88200., 96000., 176400., 192000., 768000.}) {
            for (unsigned mode : {0u, 1u}) {
                auto v = defaults();
                v[Range] = 0;
                v[Processing] = mode;
                e.prepare(rate, v);
                const auto delay = static_cast<unsigned>(std::ceil(rate * .015));
                CHECK(e.latency() == delay);
                for (unsigned n = 0; n < delay + 32; ++n) {
                    double l = n == 0 ? 1 : 0, r = -l;
                    run(e, l, r);
                    near(l, n == delay ? 1 : 0, 1e-15);
                    near(e.displayInput(0), n == delay ? 1 : 0, 0);
                    near(e.displayInput(1), n == delay ? -1 : 0, 0);
                    near(r, -l, 1e-15);
                }
                v[Bypass] = 1;
                v[Input] = 12;
                v[Output] = -12;
                v[Audition] = 1;
                e.prepare(rate, v);
                for (unsigned n = 0; n < delay + 32; ++n) {
                    double l = n == 0 ? .5 : 0, r = l;
                    run(e, l, r);
                    near(l, n == delay ? .5 : 0, 0);
                }
            }
        }
        // Strong out-of-band program must not trigger default vocal detection.
        auto v = defaults();
        e.prepare(48000, v);
        for (unsigned n = 0; n < 48000; ++n) {
            double l = .8 * std::sin(2 * std::acos(-1.) * 220 * n / 48000), r = l;
            run(e, l, r);
            CHECK(e.gainReduction() < .001);
        }
        // A sustained sibilant tone reaches range; linking preserves stereo gain ratio.
        v[Processing] = 0;
        v[Threshold] = -50;
        v[Range] = 6;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 48000; ++n) {
            double l = .7 * std::sin(n * 2 * std::acos(-1.) * 8000 / 48000), r = l * .1;
            run(e, l, r);
            if (n > 24000) {
                near(r, l * .1, 1e-13);
                near(e.gainReduction(), 6, 1e-10);
            }
        }
        // Silent external detector leaves the program unchanged, including split band.
        v = defaults();
        v[Sidechain] = 1;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 10000; ++n) {
            double l = .5, r = -.5;
            run(e, l, r);
            if (n >= e.latency())
                near(l, .5, 0);
        }
        // Lookahead acts before the delayed onset. Comparison measures actual output.
        auto onset = [&](double look) {
            v = defaults();
            v[Processing] = 0;
            v[Detection] = 1;
            v[Threshold] = -50;
            v[Range] = 12;
            v[Lookahead] = look;
            e.prepare(48000, v);
            double out = 0;
            for (unsigned n = 0; n <= e.latency() + 2; ++n) {
                double l = .8 * std::cos(n * 2 * std::acos(-1.) * 8000 / 48000), r = l;
                run(e, l, r);
                out = std::abs(l);
            }
            return out;
        };
        CHECK(onset(15) < onset(0) * .5);
        // Extreme automation and malformed audio cannot poison histories or allocate.
        std::mt19937 random(42);
        e.prepare(192000, defaults());
        for (unsigned n = 0; n < 100000; ++n) {
            realtime = true;
            if (n % 113 == 0) {
                const unsigned i = random() % parameterCount;
                e.set(i, n % 2 ? parameter(i).min : parameter(i).max);
            }
            realtime = false;
            double l =
                n % 509 == 0 ? std::numeric_limits<double>::infinity() : std::sin(n * .9) * 2;
            double r = n % 727 == 0 ? std::numeric_limits<double>::quiet_NaN() : -l;
            run(e, l, r, .8 * std::cos(n * .7));
            CHECK(std::isfinite(l) && std::isfinite(r) && std::abs(l) < 1e6);
        }
        e.reset(defaults());
        for (unsigned n = 0; n < 5000; ++n) {
            double l = 0, r = 0;
            run(e, l, r);
            near(l, 0, 0);
        }
        std::cout << "De-esser audio: unity/bypass/rates, selectivity, range/link, sidechain, "
                     "lookahead, finite automation and allocation guards passed\n";
    } catch (const std::exception &e) {
        realtime = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
