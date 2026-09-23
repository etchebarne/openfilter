#include "Engine.hpp"
#include "Test.hpp"
#include <iostream>
#include <random>
using namespace openfilter;

void identities() {
    for (double rate : {44100., 48000., 88200., 96000., 176400., 192000.}) {
        eq::Engine e;
        auto v = eq::defaults();
        e.prepare(rate, v);
        for (int i = 0; i < 4096; ++i) {
            const double a = std::sin(i * 0.1), b = std::cos(i * 0.071);
            double l = a, r = b;
            e.sample(l, r);
            CHECK(l == a && r == b);
        }
        v[eq::index(0, eq::Enabled)] = 1;
        for (unsigned type = 0; type < 3; ++type) {
            v[eq::index(0, eq::Type)] = type;
            e.prepare(rate, v);
            for (int i = 0; i < 1024; ++i) {
                double l = std::sin(i * .1), r = l, expected = l;
                e.sample(l, r);
                near(l, expected);
                near(r, expected);
            }
        }
        v[0] = 1;
        v[eq::index(0, eq::Gain)] = 24;
        v[1] = 24;
        e.prepare(rate, v);
        for (int i = 0; i < 1024; ++i) {
            double l = .25, r = -.125;
            e.sample(l, r);
            CHECK(l == .25 && r == -.125);
        }
    }
}
void routing() {
    auto v = eq::defaults();
    v[eq::index(0, eq::Enabled)] = 1;
    v[eq::index(0, eq::Gain)] = 12;
    eq::Engine e;
    v[eq::index(0, eq::Routing)] = static_cast<int>(eq::Route::Left);
    e.prepare(48000, v);
    for (int i = 0; i < 4096; ++i) {
        double l = std::sin(i * .1), r = .25;
        e.sample(l, r);
        CHECK(r == .25);
    }
    v[eq::index(0, eq::Routing)] = static_cast<int>(eq::Route::Side);
    e.prepare(48000, v);
    for (int i = 0; i < 4096; ++i) {
        double l = std::sin(i * .1), r = l, expected = l;
        e.sample(l, r);
        CHECK(l == expected && r == expected);
    }
    v[eq::index(0, eq::Routing)] = static_cast<int>(eq::Route::Mid);
    e.prepare(48000, v);
    for (int i = 0; i < 4096; ++i) {
        double l = std::sin(i * .1), r = -l, expected = l;
        e.sample(l, r);
        CHECK(l == expected && r == -expected);
    }
    v[eq::index(0, eq::Routing)] = static_cast<int>(eq::Route::Side);
    e.prepare(48000, v);
    double l = .5, r = 0;
    e.sample(l, r, true);
    CHECK(l == .5);
}
void smoothAndReset() {
    auto v = eq::defaults();
    eq::Engine e;
    e.prepare(48000, v);
    e.set(1, 12);
    double previous = 1;
    for (int i = 0; i < 480; ++i) {
        double l = 1, r = 1;
        e.sample(l, r);
        CHECK(l >= previous);
        CHECK(l - previous < .02);
        previous = l;
    }
    near(previous, std::pow(10., 12. / 20));
    e.reset(v);
    double l = .5, r = .2;
    e.sample(l, r);
    CHECK(l == .5 && r == .2);
    v[eq::index(0, eq::Enabled)] = 1;
    v[eq::index(0, eq::Gain)] = 24;
    e.prepare(48000, v);
    for (int i = 0; i < 1000; ++i) {
        l = std::sin(i * .1);
        r = l;
        e.sample(l, r);
    }
    e.set(eq::index(0, eq::Type), 4);
    for (int i = 0; i < 1000; ++i) {
        l = std::sin(i * .1);
        r = l;
        e.sample(l, r);
        CHECK(std::isfinite(l));
    }
    e.set(0, 1);
    for (int i = 0; i < 241; ++i) {
        l = .5;
        r = .5;
        e.sample(l, r);
    }
    CHECK(l == .5 && r == .5);
}
void stress() {
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> unit(0, 1);
    for (double rate : {8000., 44100., 48000., 192000., 384000.}) {
        auto v = eq::defaults();
        eq::Engine e;
        e.prepare(rate, v);
        for (unsigned n = 0; n < 24000; ++n) {
            if (n % 31 == 0) {
                unsigned i = 2 + rng() % (eq::parameterCount - 2);
                auto p = eq::parameter(i);
                double value = p.constrain(p.min + unit(rng) * (p.max - p.min));
                e.set(i, value);
            }
            double l = unit(rng) * .2 - .1, r = unit(rng) * .2 - .1;
            e.sample(l, r);
            CHECK(std::isfinite(l) && std::isfinite(r));
        }
    }
    // Static extremes and silence tails, including both sides of the Nyquist clamp.
    for (unsigned shape = 0; shape < 6; ++shape)
        for (double hz : {10., 20000., 30000.})
            for (double q : {.1, 40.}) {
                auto v = eq::defaults();
                v[eq::index(0, eq::Enabled)] = 1;
                v[eq::index(0, eq::Type)] = shape;
                v[eq::index(0, eq::Frequency)] = std::log2(hz);
                v[eq::index(0, eq::Q)] = std::log2(q);
                v[eq::index(0, eq::Gain)] = 24;
                v[eq::index(0, eq::Slope)] = 2;
                eq::Engine e;
                e.prepare(44100, v);
                for (unsigned n = 0; n < 44100; ++n) {
                    double l = n == 0 ? 1 : 0, r = l;
                    e.sample(l, r);
                    CHECK(std::isfinite(l));
                    CHECK(std::abs(l) < 1e12);
                }
            }
}
void brickwall() {
    for (double rate : {1000., 8000., 44100., 48000., 192000., 768000.}) {
        for (int type : {3, 4}) {
            auto v = eq::defaults();
            v[eq::index(0, eq::Enabled)] = 1;
            v[eq::index(0, eq::Type)] = type;
            v[eq::index(0, eq::Slope)] = 3;
            v[eq::index(0, eq::Frequency)] = std::log2(146.9);
            v[eq::index(0, eq::Routing)] = 1;
            eq::Engine a, b;
            a.prepare(rate, v);
            v[eq::index(0, eq::Q)] = std::log2(40.);
            b.prepare(rate, v);
            for (unsigned n = 0; n < 8192; ++n) {
                double l = n == 0 ? 1 : 0, r = .25, l2 = l, r2 = r;
                a.sample(l, r);
                b.sample(l2, r2);
                CHECK(l == l2 && r == .25 && r2 == .25); // Q is inactive; routing is preserved.
                CHECK(std::isfinite(l) && std::abs(l) < 2);
            }
            v[eq::index(0, eq::Q)] = eq::defaults()[eq::index(0, eq::Q)];
            a.reset(v);
            b.reset(v);
            for (unsigned n = 0; n < 60000; ++n) {
                if (n % 2000 == 0) {
                    const double hz = n % 4000 ? 10 : 30000;
                    a.set(eq::index(0, eq::Frequency), std::log2(hz));
                    b.set(eq::index(0, eq::Frequency), std::log2(hz));
                }
                if (n % 9000 == 0) {
                    const double slope = (n / 9000) % 4;
                    a.set(eq::index(0, eq::Slope), slope);
                    b.set(eq::index(0, eq::Slope), slope);
                }
                double l = .1 * std::sin(n * .12), r = .1, l2 = l, r2 = r;
                a.sample(l, r);
                b.sample(l2, r2);
                CHECK(l == l2 && r == r2); // Sample timeline/reset remain deterministic.
                if (!(std::isfinite(l) && std::abs(l) < 8))
                    std::cerr << "Brickwall sweep: rate=" << rate << " type=" << type
                              << " sample=" << n << " value=" << l << '\n';
                CHECK(std::isfinite(l) && std::abs(l) < 8);
            }
        }
    }
}
int main() {
    try {
        identities();
        routing();
        smoothAndReset();
        stress();
        brickwall();
        std::cout
            << "DSP: identity, routing, ramps, reset, randomized automation and extremes passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
