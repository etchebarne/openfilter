#include "Engine.hpp"
#include <numbers>

namespace openfilter::deesser {
namespace {
double clean(double x) noexcept {
    return std::isfinite(x) && std::abs(x) >= 1e-30 ? std::clamp(x, -1e12, 1e12) : 0.;
}
double gain(double db) noexcept {
    return std::pow(10., db / 20.);
}
} // namespace
void Engine::prepare(double rate, const Values &v) noexcept {
    rate_ = std::isfinite(rate) ? std::clamp(rate, 1000., 768000.) : 48000.;
    latency_ = static_cast<unsigned>(std::ceil(rate_ * .015));
    smoothing_ = std::max(1u, static_cast<unsigned>(rate_ * .01));
    powerDecay_ = std::exp(-1 / (rate_ * .001));
    for (unsigned i = 0; i < parameterCount; ++i)
        ramps_[i].reset(parameter(i).constrain(v[i]));
    for (auto *bank : {&raw_, &audio_, &detector_, &requests_})
        for (auto &c : *bank)
            c.fill(0);
    for (auto *filters : {&hp_, &lp_, &shelf_})
        for (auto &f : *filters)
            f.reset();
    power_ = {};
    broadPower_ = {};
    envelope_ = {};
    position_ = 0;
    reduction_ = detectorLevel_ = 0;
    lastLow_ = lastHigh_ = -1;
}
void Engine::set(unsigned i, double v) noexcept {
    if (i < parameterCount)
        ramps_[i].set(parameter(i).constrain(v), smoothing_);
}
void Engine::sample(double &l, double &r, bool mono, double scL, double scR) noexcept {
    Values p{};
    for (unsigned i = 0; i < parameterCount; ++i)
        p[i] = ramps_[i].next();
    const double low = std::clamp(std::min(p[Low], p[High]), 1., rate_ * .475 - 10),
                 high = std::clamp(std::max(p[Low], p[High]), low + 10, rate_ * .475);
    if (low != lastLow_ || high != lastHigh_) {
        hpCoefficients_ =
            dsp::Coefficients::make(dsp::Shape::LowCut, low, 0, std::numbers::sqrt2 / 2, rate_);
        lpCoefficients_ =
            dsp::Coefficients::make(dsp::Shape::HighCut, high, 0, std::numbers::sqrt2 / 2, rate_);
        lastLow_ = low;
        lastHigh_ = high;
    }
    const double source[]{clean(l), clean(mono ? l : r)},
        external[]{clean(scL), clean(mono ? scL : scR)};
    const double inputGain = gain(p[Input]);
    const unsigned delayed = (position_ + capacity - latency_) % capacity;
    const double offset = std::max(0., latency_ - p[Lookahead] * rate_ / 1000.);
    const auto whole = static_cast<unsigned>(offset);
    const double fraction = offset - whole;
    const unsigned a = (position_ + capacity - whole) % capacity, b = (a + capacity - 1) % capacity;
    double wanted[2]{};
    detectorLevel_ = 0;
    for (unsigned c = 0; c < 2; ++c) {
        raw_[c][position_] = source[c];
        audio_[c][position_] = source[c] * inputGain;
        const double x = audio_[c][position_] * (1 - p[Sidechain]) + external[c] * p[Sidechain];
        const double band = lp_[c].process(hp_[c].process(x, hpCoefficients_), lpCoefficients_);
        detector_[c][position_] = clean(band);
        power_[c] = clean(powerDecay_ * power_[c] + (1 - powerDecay_) * band * band);
        broadPower_[c] = clean(powerDecay_ * broadPower_[c] + (1 - powerDecay_) * x * x);
        detectorLevel_ = std::max(detectorLevel_, std::sqrt(power_[c]));
        const double excess = 10 * std::log10(std::max(1e-30, power_[c])) - p[Threshold];
        const double knee = excess <= -3 ? 0
                            : excess < 3 ? (excess + 3) * (excess + 3) / 12
                                         : excess;
        const double balance =
            10 * std::log10(std::max(1e-30, power_[c]) / std::max(1e-30, broadPower_[c]));
        const double t = std::clamp((balance + 18) / 12, 0., 1.);
        const double vocal = t * t * (3 - 2 * t);
        requests_[c][position_] =
            std::min(p[Range], .875 * knee) * (vocal * (1 - p[Detection]) + p[Detection]);
        wanted[c] = requests_[c][a] * (1 - fraction) + requests_[c][b] * fraction;
    }
    const double linked = std::max(wanted[0], wanted[1]), link = p[StereoLink] / 100.;
    const double attack = std::exp(-1000 / (rate_ * p[Attack])),
                 release = std::exp(-1000 / (rate_ * p[Release]));
    reduction_ = 0;
    for (unsigned c = 0; c < 2; ++c) {
        const double target = wanted[c] * (1 - link) + linked * link;
        const double coefficient = target > envelope_[c] ? attack : release;
        envelope_[c] = clean(coefficient * envelope_[c] + (1 - coefficient) * target);
        // Automation may lower Range below a previous envelope; respect the live bound.
        const double attenuation = std::min(p[Range], envelope_[c]);
        reduction_ = std::max(reduction_, attenuation * (1 - p[Bypass]) * (1 - p[Audition]));
        const double dry = raw_[c][delayed], program = audio_[c][delayed];
        const auto coefficients = dsp::Coefficients::make(dsp::Shape::HighShelf, low, -attenuation,
                                                          std::numbers::sqrt2 / 2, rate_);
        const double split = shelf_[c].process(program, coefficients);
        const double wide = program * gain(-attenuation);
        const double processed = wide * (1 - p[Processing]) + split * p[Processing];
        const double monitored =
            processed * (1 - p[Audition]) + detector_[c][delayed] * p[Audition];
        const double out = clean(monitored * gain(p[Output]) * (1 - p[Bypass]) + dry * p[Bypass]);
        if (c == 0)
            l = out;
        else
            r = out;
    }
    position_ = (position_ + 1) % capacity;
    if (mono)
        r = l;
}
} // namespace openfilter::deesser
