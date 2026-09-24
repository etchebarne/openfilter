#include "Engine.hpp"
#include "Test.hpp"
#include <iostream>
#include <limits>
#include <memory>
extern thread_local bool realtime;
using namespace openfilter::reverb;
int main() {
    try {
        auto e = std::make_unique<Engine>();
        // The analyzer tap is the actual wet output, independent of the dry mix.
        auto wetReference = std::make_unique<Engine>();
        for (bool mono : {false, true}) {
            auto wetValues = defaults();
            wetValues[Mix] = 100;
            wetReference->prepare(48000, wetValues);
            wetValues[Mix] = 0;
            e->prepare(48000, wetValues);
            double wetEnergy = 0;
            realtime = true;
            for (unsigned n = 0; n < 48000; ++n) {
                double l = n == 0 ? 1 : 0, r = l * .5, wl = l, wr = r;
                e->sample(l, r, mono);
                wetReference->sample(wl, wr, mono);
                near(e->wet(0), wl, 1e-15);
                near(e->wet(1), wr, 1e-15);
                wetEnergy += wl * wl + wr * wr;
            }
            realtime = false;
            CHECK(wetEnergy > .001);
            e->reset(wetValues);
            near(e->wet(0), 0);
            near(e->wet(1), 0);
        }
        for (double rate : {1000., 8000., 44100., 48000., 96000., 192000., 768000.}) {
            auto v = defaults();
            v[Mix] = 0;
            e->prepare(rate, v);
            realtime = true;
            for (unsigned n = 0; n < 1000; ++n) {
                double l = .2 * std::sin(n * .1), r = -l, x = l, y = r;
                e->sample(l, r);
                near(l, x, 1e-15);
                near(r, y, 1e-15);
            }
            e->reset(v);
            realtime = false;
            v[Bypass] = 1;
            v[Mix] = 100;
            v[Output] = 24;
            e->prepare(rate, v);
            for (unsigned n = 0; n < 1000; ++n) {
                double l = .2, r = -.1;
                e->sample(l, r);
                near(l, .2);
                near(r, -.1);
            }
            v = defaults();
            v[Mix] = 100;
            v[Predelay] = 50;
            v[Character] = 0;
            e->prepare(rate, v);
            double energy = 0, stereo = 0;
            realtime = true;
            for (unsigned n = 0; n < unsigned(rate); ++n) {
                double l = n == 0 ? 1 : 0, r = 0;
                e->sample(l, r);
                CHECK(std::isfinite(l) && std::isfinite(r));
                if (n < unsigned(rate * .05)) {
                    near(l, 0, 1e-15);
                    near(r, 0, 1e-15);
                }
                energy += l * l + r * r;
                stereo += (l - r) * (l - r);
            }
            realtime = false;
            CHECK(energy > .005 && energy < 10 && stereo > .001);
            e->reset(v);
            double l = std::numeric_limits<double>::quiet_NaN(),
                   r = std::numeric_limits<double>::infinity();
            e->sample(l, r);
            CHECK(std::isfinite(l) && std::isfinite(r));
        }
        // Sync timing has an independent sample-count oracle at two host tempos.
        for (double bpm : {120., 240.}) {
            auto v = defaults();
            v[Mix] = 100;
            v[PredelaySync] = 3;
            v[Character] = 0;
            e->prepare(48000, v);
            e->tempo(bpm);
            const unsigned arrival = unsigned(48000 * 60 / bpm / 4);
            for (unsigned n = 0; n <= arrival; ++n) {
                double l = n == 0 ? 1 : 0, r = l;
                e->sample(l, r);
                if (n < arrival)
                    near(l, 0, 1e-15);
                else
                    CHECK(l > .01);
            }
        }
        // Restoring a different engine revision retains the host's current tempo.
        {
            auto v = defaults();
            v[Mix] = 100;
            v[PredelaySync] = 3;
            e->prepare(48000, v, 1);
            e->tempo(240);
            e->setRevision(2, v);
            for (unsigned n = 0; n <= 3000; ++n) {
                double l = n == 0 ? 1 : 0, r = l;
                e->sample(l, r);
                if (n < 3000)
                    near(l, 0, 1e-15);
                else
                    CHECK(l > .01);
            }
        }
        // Freeze keeps an excited tank alive and rejects newly arriving input.
        auto frozen = std::make_unique<Engine>();
        auto f = defaults();
        f[Mix] = 100;
        f[Brightness] = 100;
        f[Character] = 0;
        f[Space] = 1;
        e->prepare(48000, f);
        frozen->prepare(48000, f);
        double sustained = 0;
        for (unsigned n = 0; n < 144000; ++n) {
            if (n == 12000) {
                e->set(Freeze, 1);
                frozen->set(Freeze, 1);
            }
            double l = n == 0 ? 1 : 0, r = l, fl = l, fr = r;
            if (n >= 24000)
                fl = fr = .3 * std::sin(n * .13);
            e->sample(l, r);
            frozen->sample(fl, fr);
            near(l, fl, 1e-14);
            near(r, fr, 1e-14);
            if (n >= 96000)
                sustained += l * l + r * r;
        }
        CHECK(sustained > 1e-6);
        // Extreme overlapping decay boosts, narrow filters, freeze transitions and automation.
        auto v = defaults();
        v[Mix] = 100;
        v[Space] = .2;
        v[DecayRate] = 25;
        for (unsigned b = 0; b < bands; ++b) {
            auto i = bandIndex(false, b, 0);
            v[i + Enabled] = 1;
            v[i + Amount] = 400;
            v[i + Frequency] = 1000;
            v[i + Q] = 10;
        }
        e->prepare(48000, v);
        realtime = true;
        double energy = 0;
        for (unsigned n = 0; n < 480000; ++n) {
            if (n == 48000)
                e->set(Freeze, 1);
            if (n == 96000)
                e->set(Freeze, 0);
            if (n == 200000) {
                e->set(Space, 10);
                e->set(Character, 100);
            }
            double l = n == 0 ? 1 : 0, r = l;
            e->sample(l, r);
            CHECK(std::isfinite(l) && std::abs(l) < 10 && std::abs(r) < 10);
            energy += l * l + r * r;
        }
        realtime = false;
        CHECK(energy > 0 && energy < 100);
        // Exercise overlapping shape/geometry requests through actual audio.
        // Updates arrive faster than the transition window and must stay bounded.
        v = defaults();
        v[Mix] = 100;
        v[bandIndex(true, 0, Enabled)] = 1;
        v[bandIndex(true, 0, Amount)] = 12;
        v[bandIndex(false, 0, Enabled)] = 1;
        v[bandIndex(false, 0, Amount)] = 200;
        e->prepare(48000, v);
        realtime = true;
        for (unsigned n = 0; n < 144000; ++n) {
            if (n % 64 == 0) {
                e->set(bandIndex(true, 0, Type), (n / 64) % 6);
                e->set(bandIndex(false, 0, Type), (n / 64) % 3);
                e->set(bandIndex(true, 0, Frequency), n % 128 ? 20 : 20000);
                e->set(bandIndex(true, 0, Q), n % 128 ? .2 : 10);
            }
            if (n % 4096 == 0) {
                e->set(Space, n % 8192 ? .2 : 10);
                e->set(Predelay, n % 8192 ? 0 : 500);
                e->set(Freeze, n % 8192 ? 0 : 1);
            }
            double l = .1 * std::sin(n * .13), r = .1 * std::cos(n * .037);
            e->sample(l, r);
            CHECK(std::isfinite(l) && std::isfinite(r));
            CHECK(std::abs(l) < 20 && std::abs(r) < 20);
        }
        realtime = false;
        std::cout << "Reverb DSP: dry/bypass, rate matrix, predelay, stereo tail, reset, extremes "
                     "and realtime allocation guards passed\n";
    } catch (const std::exception &e) {
        realtime = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
