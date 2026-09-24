#include "Engine.hpp"
#include "Test.hpp"
#include <iostream>
#include <limits>
#include <random>
extern thread_local bool realtime;
using namespace openfilter::gate;
void run(Engine &e, double &l, double &r, double sc = 0) {
    realtime = true;
    e.sample(l, r, false, sc, sc);
    realtime = false;
}
int main() {
    try {
        // Rounded host text stays canonical at both filter Off boundaries.
        // The GUI supplies Off labels separately; CLAP text remains numeric.
        for (auto i : {SidechainHP, SidechainLP}) {
            for (double value : {0., .1, .499, 19999.49, 19999.907467602738, 20000.}) {
                const double original = parameter(i).constrain(value);
                char text[80], roundtrip[80];
                format(i, original, text, sizeof(text));
                double parsed = 0;
                CHECK(parse(i, text, parsed));
                format(i, parsed, roundtrip, sizeof(roundtrip));
                CHECK(std::string(text) == roundtrip);
            }
        }
        Engine e;
        for (double rate : {1000., 44100., 48000., 88200., 96000., 176400., 192000., 768000.}) {
            for (unsigned mode = 0; mode < 3; ++mode) {
                auto v = defaults();
                if (mode == 0)
                    v[Bypass] = 1;
                if (mode == 1)
                    v[Ratio] = 1;
                if (mode == 2)
                    v[Mix] = 0;
                e.prepare(rate, v);
                const unsigned delay = static_cast<unsigned>(std::ceil(rate * .01));
                CHECK(e.latency() == delay);
                for (unsigned n = 0; n < delay + 10; ++n) {
                    double l = n == 0 ? 1 : 0, r = l;
                    run(e, l, r);
                    near(l, n == delay ? 1 : 0, 1e-15);
                    near(l, r, 0);
                    near(e.delayedInputPeak(), n == delay ? 1 : 0, 0);
                }
            }
        }
        auto v = defaults();
        v[Threshold] = -20;
        v[Knee] = 0;
        v[Attack] = 0;
        v[Release] = 1;
        v[Hold] = 0;
        v[Hysteresis] = 0;
        double l = 0, r = 0;
        // Analytic expansion at -30 dB: ratio 4 produces -60 dB output.
        v[StereoLink] = 0;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 48000; ++n) {
            l = std::pow(10., -30. / 20);
            r = .5;
            run(e, l, r);
        }
        near(l, .001, 1e-12);
        near(r, .5, 1e-12);
        // The loud right channel opens both channels at full link.
        v[StereoLink] = 100;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 48000; ++n) {
            l = std::pow(10., -30. / 20);
            r = .5;
            run(e, l, r);
        }
        near(l, std::pow(10., -30. / 20), 1e-12);
        v[Sidechain] = 1;
        v[Lookahead] = 10;
        v[Range] = 12;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 1000; ++n) {
            l = r = .5;
            run(e, l, r);
        }
        near(l, .5 * std::pow(10., -12. / 20), 1e-12);
        for (unsigned n = 0; n < 1000; ++n) {
            l = r = .5;
            run(e, l, r, 1);
        }
        near(l, .5, 1e-12);
        // Opening attack: exactly one dB-domain time constant from closed.
        v[Range] = 60;
        v[Attack] = 10;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 480; ++n) {
            l = r = 1;
            run(e, l, r, 1);
        }
        near(e.gainReduction(), 60 * std::exp(-1.), 1e-10);
        // Hysteresis retains the open state at -22 dB until below -26 dB.
        v[Attack] = 0;
        v[Hysteresis] = 6;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 1000; ++n) {
            l = r = 1;
            run(e, l, r, 1);
        }
        for (unsigned n = 0; n < 48000; ++n) {
            l = r = 1;
            run(e, l, r, std::pow(10., -22. / 20));
        }
        near(l, 1, 1e-12);
        for (unsigned n = 0; n < 48000; ++n) {
            l = r = 1;
            run(e, l, r, std::pow(10., -30. / 20));
        }
        near(l, std::pow(10., -30. / 20), 1e-12);
        // Hold counts samples after the peak crosses the closing boundary.
        v[Hysteresis] = 0;
        v[Hold] = 100;
        v[Release] = 10;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 1000; ++n) {
            l = r = 1;
            run(e, l, r, 1);
        }
        // A 5 ms peak decay reaches -20 dB on sample ceil(240*ln(10)) = 553.
        for (unsigned n = 1; n <= 5352; ++n) {
            l = r = 1;
            run(e, l, r);
            near(e.gainReduction(), 0, 1e-12);
        }
        for (unsigned n = 0; n < 480; ++n) {
            l = r = 1;
            run(e, l, r);
        }
        near(e.gainReduction(), 60 * (1 - std::exp(-1.)), 1e-9);
        // Detector HP rejects DC, and audition is delayed filtered sidechain.
        v = defaults();
        v[Sidechain] = 1;
        v[SidechainHP] = 200;
        v[Attack] = 0;
        v[Release] = 1;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 48000; ++n) {
            l = r = .5;
            run(e, l, r, 1);
        }
        near(l, .0005, 1e-10);
        v[Audition] = 1;
        v[SidechainHP] = 0;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 500; ++n) {
            l = r = .5;
            run(e, l, r, n == 0 ? .25 : 0);
            near(l, n == 480 ? .25 : 0, 1e-15);
        }
        // Lookahead opens before the first delayed transient.
        auto first = [&](double lookahead) {
            auto p = defaults();
            p[Attack] = 2;
            p[Lookahead] = lookahead;
            e.prepare(48000, p);
            for (unsigned n = 0; n <= 480; ++n) {
                l = r = 1;
                run(e, l, r);
            }
            return l;
        };
        CHECK(first(10) > first(0) * 100);
        // Wet/dry center unity and opposite extreme stereo balance.
        v = defaults();
        v[Ratio] = 1;
        v[Mix] = 50;
        v[WetPan] = -100;
        v[DryPan] = 100;
        e.prepare(48000, v);
        for (unsigned n = 0; n < 500; ++n) {
            l = r = 1;
            run(e, l, r);
        }
        near(l, .5, 1e-15);
        near(r, .5, 1e-15);
        e.reset(v);
        for (unsigned n = 0; n < 500; ++n) {
            l = r = 1;
            e.sample(l, r, true);
        }
        near(l, 1, 1e-15);
        near(r, 1, 1e-15);
        // Extreme automation must stay finite and reset must clear every ring.
        std::mt19937 gen(421);
        e.prepare(192000, defaults());
        for (unsigned n = 0; n < 100000; ++n) {
            if (n % 113 == 0) {
                unsigned i = gen() % parameterCount;
                const auto p = parameter(i);
                e.set(i, n % 2 ? p.min : p.max);
            }
            l = n % 509 == 0 ? std::numeric_limits<double>::infinity() : std::sin(n * .1) * 3;
            r = n % 727 == 0 ? std::numeric_limits<double>::quiet_NaN() : -l;
            run(e, l, r);
            CHECK(std::isfinite(l) && std::isfinite(r) && std::abs(l) < 1e6);
        }
        e.reset(defaults());
        for (unsigned n = 0; n < 4000; ++n) {
            l = r = 0;
            run(e, l, r);
            near(l, 0, 0);
        }
        std::cout << "Gate audio: transfer, latency, link, timing, hysteresis, hold, sidechain, "
                     "audition, lookahead, balance, reset and allocation guards passed\n";
    } catch (const std::exception &error) {
        realtime = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
