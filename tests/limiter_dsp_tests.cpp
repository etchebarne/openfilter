#include "Engine.hpp"
#include "Test.hpp"
#include <deque>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <vector>
extern thread_local bool realtime;
using namespace openfilter::limiter;
void run(Engine &e, double &l, double &r, bool mono = false) {
    realtime = true;
    e.sample(l, r, mono);
    realtime = false;
}
Values legacyDefaults() {
    auto v = defaults();
    v[Style] = Legacy;
    v[TruePeak] = 0;
    return v;
}
int main() {
    try {
        std::mt19937 gen(421);
        auto e = std::make_unique<Engine>();
        // Increasing queues followed by full/partial replacement exercise the
        // bounded tail search, ring wrap, expiry and uint32 timestamp wrap.
        auto minimum = std::make_unique<Minimum>();
        std::deque<double> referenceWindow;
        for (unsigned n = 0; n < 60000; ++n) {
            double x = double(n % 19000) / 19000;
            if (n % 19000 == 18998)
                x = .2;
            referenceWindow.push_back(x);
            if (referenceWindow.size() > 10001)
                referenceWindow.pop_front();
            const double result = minimum->push(x, uint64_t(UINT32_MAX) - 20000 + n, 10000);
            near(result, *std::min_element(referenceWindow.begin(), referenceWindow.end()), 0);
        }
        for (double rate : {1000., 44100., 48000., 88200., 96000., 176400., 192000., 768000.}) {
            auto v = legacyDefaults();
            v[Ceiling] = 0;
            const auto delay =
                static_cast<unsigned>(std::ceil(rate * .005) + std::ceil(rate * .003) + 266 + 128 +
                                      (rate <= 192000   ? 136
                                       : rate <= 384000 ? 128
                                                        : 0));
            e->prepare(rate, v);
            CHECK(e->latency() == delay);
            for (unsigned n = 0; n < delay + 8; ++n) {
                double l = n == 0 ? .25 : 0, r = -.5 * l;
                run(*e, l, r);
                near(l, n == delay ? .25 : 0, 1e-15);
                near(r, -.5 * l, 1e-15);
            }
            // Full-range random samples and isolated extreme impulses meet the ceiling
            // from the first delayed sample; linking preserves stereo ratio.
            for (double boost : {0., 12., 36.}) {
                v[Gain] = boost;
                v[Ceiling] = -1;
                e->prepare(rate, v);
                for (unsigned n = 0; n < 20000; ++n) {
                    double l = (double(gen()) / gen.max() - .5) * 4;
                    if (n % 1301 == 0)
                        l = n % 2 ? 1e12 : -1e12;
                    double r = -.25 * l;
                    run(*e, l, r);
                    CHECK(std::abs(l) <= std::pow(10., -.05) + 1e-14);
                    CHECK(std::abs(r) <= std::pow(10., -.05) + 1e-14);
                    near(r, -.25 * l, 1e-8);
                }
            }
            v[Bypass] = 1;
            e->prepare(rate, v);
            for (unsigned n = 0; n < delay + 8; ++n) {
                double l = n == 0 ? 2 : 0, r = l;
                run(*e, l, r);
                near(l, n == delay ? 2 : 0, 0);
            }
        }
        // Subthreshold program nulls against an independently delayed/gained signal.
        auto v = legacyDefaults();
        v[Gain] = 3;
        v[Ceiling] = -2;
        e->prepare(48000, v);
        for (unsigned n = 0; n < 48000; ++n) {
            double l = .1 * std::sin(n * .13), r = l;
            run(*e, l, r);
            near(l, n < 914 ? 0 : .1 * std::sin((n - 914) * .13) * std::pow(10., 1. / 20), 1e-14);
        }
        // Steady gain, channel linking, and unity comparison in actual audio.
        for (double link : {0., 35., 100.}) {
            v = legacyDefaults();
            v[Gain] = 12;
            v[StereoLink] = link;
            e->prepare(48000, v);
            double l = 0, r = 0;
            for (unsigned n = 0; n < 12000; ++n) {
                l = .8;
                r = .1;
                run(*e, l, r);
            }
            const double drive = std::pow(10., .6), ceiling = std::pow(10., -.05);
            near(l, ceiling, 1e-12);
            near(r, .1 * drive * (1 + link / 100 * (1 / (.8 * drive) - 1)) * ceiling, 1e-12);
            v[UnityGain] = 1;
            e->prepare(48000, v);
            for (unsigned n = 0; n < 12000; ++n) {
                l = .8;
                r = .1;
                run(*e, l, r);
            }
            near(l, ceiling / drive, 1e-12);
        }
        // Release and program dependence are observable after overload ends.
        auto recovery = [&](bool automatic, double release) {
            auto p = legacyDefaults();
            p[AutoRelease] = automatic;
            p[Release] = release;
            e->prepare(48000, p);
            double l, r;
            for (unsigned n = 0; n < 48000; ++n) {
                l = r = 4;
                run(*e, l, r);
            }
            for (unsigned n = 0; n < 8000; ++n) {
                l = r = .1;
                run(*e, l, r);
            }
            return l;
        };
        CHECK(recovery(false, 50) > recovery(false, 500));
        CHECK(recovery(true, 100) < recovery(false, 100));
        // Extreme automation, mono, non-finite values and reset never poison audio.
        e->prepare(192000, defaults());
        for (unsigned n = 0; n < 100000; ++n) {
            if (n % 113 == 0) {
                const auto i = 1 + gen() % (parameterCount - 1);
                const auto p = parameter(i);
                realtime = true;
                e->set(i, n % 2 ? p.min : p.max);
                realtime = false;
            }
            double l =
                n % 509 == 0 ? std::numeric_limits<double>::infinity() : std::sin(n * .1) * 3;
            double r = std::numeric_limits<double>::quiet_NaN();
            run(*e, l, r, true);
            CHECK(std::isfinite(l) && l == r && std::abs(l) <= 1);
        }
        e->reset(defaults());
        for (unsigned n = 0; n < 2000; ++n) {
            double l = 0, r = 0;
            run(*e, l, r);
            near(l, 0, 0);
        }
        // Every new style and lookahead endpoint is bounded at supported rate extremes.
        for (double rate : {1000., 44100., 96000., 768000.})
            for (double style : {1., 2., 3.})
                for (double ahead : {0., .37, 5.}) {
                    auto p = defaults();
                    p[Style] = style;
                    p[Lookahead] = ahead;
                    p[Gain] = 36;
                    e->prepare(rate, p);
                    const auto delay = e->latency();
                    for (unsigned n = 0; n < delay + 4000; ++n) {
                        double l = n % 997 == 0 ? 1e12 : (double(gen()) / gen.max() - .5) * 4;
                        double r = -.25 * l;
                        run(*e, l, r);
                        CHECK(std::abs(l) <= std::pow(10., -.05) + 1e-12);
                        near(r, -.25 * l, 1e-12);
                    }
                }
        // Lookahead changes pre-attenuation, not output timing. Attack acts on
        // sustained recovery; the two links independently affect the quiet side.
        auto probe = [&](double ahead, double attack, double transientLink, double releaseLink,
                         unsigned sample, bool impulse) {
            auto p = defaults();
            p[TruePeak] = 0;
            p[Ceiling] = 0;
            p[Lookahead] = ahead;
            p[Attack] = attack;
            p[StereoLink] = transientLink;
            p[ReleaseLink] = releaseLink;
            e->prepare(48000, p);
            double l = 0, r = 0;
            for (unsigned n = 0; n <= sample; ++n) {
                l = (impulse ? n == 4000 : n >= 4000 && n < 8000) ? 4 : .1;
                r = .1;
                run(*e, l, r);
            }
            return std::array{l, r};
        };
        CHECK(probe(5, 100, 100, 100, 4804, true)[0] < probe(0, 100, 100, 100, 4804, true)[0]);
        CHECK(probe(5, 1, 100, 100, 10274, false)[0] < probe(5, 1000, 100, 100, 10274, false)[0]);
        CHECK(probe(5, 1000, 100, 0, 4914, true)[1] < probe(5, 1000, 0, 0, 4914, true)[1]);
        CHECK(probe(5, 1, 0, 100, 10274, false)[1] < probe(5, 1, 0, 0, 10274, false)[1]);
        // Oversampled modern path: exact DC gain and a symmetric impulse
        // centered at the reported latency. Quiet audio remains linear.
        auto impulse = [&](double level) {
            auto p = defaults();
            p[Ceiling] = 0;
            e->prepare(48000, p);
            std::vector<double> y(3000);
            for (unsigned n = 0; n < y.size(); ++n) {
                double l = n == 0 ? level : 0, r = l;
                run(*e, l, r);
                y[n] = l;
            }
            return y;
        };
        const auto small = impulse(.001), large = impulse(.1);
        double sum = 0;
        CHECK(std::max_element(large.begin(), large.end()) - large.begin() == 914);
        for (unsigned n = 0; n < large.size(); ++n) {
            near(large[n], 100 * small[n], 1e-14);
            sum += large[n];
            if (n <= 1828)
                near(large[n], large[1828 - n], 1e-14);
        }
        near(sum, .1, 1e-14);
        // Unity compensation must also cover the FIR pre-ringing before the
        // nominal group-delay sample, including a nonzero initial drive.
        auto unity = defaults();
        unity[Gain] = 12;
        unity[UnityGain] = 1;
        unity[Ceiling] = 0;
        e->prepare(48000, unity);
        CHECK(e->tail() == 1178);
        for (unsigned n = 0; n < small.size(); ++n) {
            double l = n == 0 ? .001 : 0, r = l;
            run(*e, l, r);
            near(l, small[n], 1e-14);
            if (n > e->tail())
                near(l, 0, 0);
        }
        // Independent analytic true peak of a quarter-rate sinusoid. Its sample
        // peaks are lower by 3.01 dB, but the output meter must recover amplitude.
        v = defaults();
        v[Ceiling] = 0;
        e->prepare(48000, v);
        double measured = 0;
        for (unsigned n = 0; n < 5000; ++n) {
            double l = .7 * std::sin(std::acos(-1.) * (.5 * n + .25)), r = l;
            run(*e, l, r);
            if (n > 3000)
                measured = std::max(measured, e->outputPeak(0));
        }
        near(measured, .7, 2e-5);
        std::cout << "Limiter audio: ceiling, latency/rates, null, stereo/link, release, unity, "
                     "finite stress and allocation guards passed\n";
    } catch (const std::exception &e) {
        realtime = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
