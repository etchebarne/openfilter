#include "Engine.hpp"
#include <numbers>

namespace openfilter::compressor {
namespace {
double clean(double x) noexcept {
    return std::isfinite(x) && std::abs(x) >= 1e-30 ? std::clamp(x, -1e12, 1e12) : 0.;
}
double gain(double db) noexcept {
    return std::pow(10., db / 20.);
}
} // namespace
void Engine::prepare(double rate, const Values &v) noexcept {
    rate_ = std::clamp(rate, 1000., 768000.);
    latency_ = static_cast<unsigned>(std::ceil(rate_ * .01));
    smoothing_ = std::max(1u, static_cast<unsigned>(rate_ * .01));
    detectorDecay_ = std::exp(-1 / (rate_ * .01));
    slowDecay_ = std::exp(-1 / (rate_ * .25));
    for (unsigned i = 0; i < parameterCount; ++i)
        ramps_[i].reset(parameter(i).constrain(v[i]));
    for (auto &a : audio_)
        a.fill(0);
    for (auto &d : detector_)
        d.fill(0);
    hpX_ = {};
    hpY_ = {};
    peak_ = {};
    power_ = {};
    envelope_ = {};
    slow_ = {};
    hold_ = {};
    position_ = 0;
    reduction_ = 0;
}
void Engine::set(unsigned i, double value) noexcept {
    if (i < parameterCount)
        ramps_[i].set(parameter(i).constrain(value), smoothing_);
}
void Engine::sample(double &l, double &r, bool mono, double scL, double scR) noexcept {
    Values p{};
    for (unsigned i = 0; i < parameterCount; ++i)
        p[i] = ramps_[i].next();
    const double raw[]{clean(l), clean(mono ? l : r)},
        external[]{clean(scL), clean(mono ? scL : scR)};
    const double inputGain = gain(p[Input]);
    const double hp = std::exp(-2 * std::numbers::pi * p[SidechainHP] / rate_);
    const double delay = latency_ - p[Lookahead] * rate_ / 1000.;
    const unsigned offset = static_cast<unsigned>(std::max(0., delay));
    const double fraction = delay - offset;
    const unsigned a = (position_ + capacity - offset) % capacity,
                   b = (a + capacity - 1) % capacity;
    double requested[2]{}, dry[2]{};
    for (unsigned c = 0; c < 2; ++c) {
        audio_[c][position_] = raw[c];
        dry[c] = audio_[c][(position_ + capacity - latency_) % capacity];
        const double x = raw[c] * inputGain * (1 - p[Sidechain]) + external[c] * p[Sidechain];
        const double filtered = hp * (hpY_[c] + x - hpX_[c]);
        hpX_[c] = x;
        hpY_[c] = clean(filtered);
        detector_[c][position_] = p[SidechainHP] < .01 ? x : filtered;
        const double d = detector_[c][a] * (1 - fraction) + detector_[c][b] * fraction;
        peak_[c] = std::max(std::abs(d), clean(peak_[c] * detectorDecay_));
        power_[c] = clean(detectorDecay_ * power_[c] + (1 - detectorDecay_) * d * d);
        const double level = (1 - p[Detector]) * peak_[c] + p[Detector] * std::sqrt(power_[c]);
        requested[c] = reduction(20 * std::log10(std::max(1e-15, level)), p[Threshold], p[Ratio],
                                 p[Knee], p[Range]);
    }
    const double linked = std::max(requested[0], requested[1]), link = p[StereoLink] / 100.;
    const double attack = std::exp(-1000 / (rate_ * p[Attack]));
    reduction_ = 0;
    for (unsigned c = 0; c < 2; ++c) {
        const double target = requested[c] * (1 - link) + linked * link;
        slow_[c] = slowDecay_ * slow_[c] + (1 - slowDecay_) * envelope_[c];
        if (target >= envelope_[c]) {
            envelope_[c] = attack * envelope_[c] + (1 - attack) * target;
            hold_[c] = static_cast<unsigned>(p[Hold] * rate_ / 1000.);
        } else if (hold_[c] > 0)
            --hold_[c];
        else {
            const double release =
                p[Release] * (1 + p[AutoRelease] * 3 * std::clamp(slow_[c] / 12., 0., 1.));
            const double coefficient = std::exp(-1000 / (rate_ * release));
            envelope_[c] = coefficient * envelope_[c] + (1 - coefficient) * target;
        }
        envelope_[c] = clean(envelope_[c]);
        reduction_ = std::max(reduction_, envelope_[c] * (1 - p[Bypass]));
        const double wet = dry[c] * inputGain * gain(p[Makeup] - envelope_[c]);
        const double mixed =
            (dry[c] * inputGain * (1 - p[Mix] / 100.) + wet * p[Mix] / 100.) * gain(p[Output]);
        const double out = clean(mixed * (1 - p[Bypass]) + dry[c] * p[Bypass]);
        if (c == 0)
            l = out;
        else
            r = out;
    }
    position_ = (position_ + 1) % capacity;
    if (mono)
        r = l;
}
} // namespace openfilter::compressor
