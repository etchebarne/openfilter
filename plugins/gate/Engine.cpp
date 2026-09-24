#include "Engine.hpp"
#include <numbers>

namespace openfilter::gate {
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
    detectorDecay_ = std::exp(-1 / (rate_ * .005));
    for (unsigned i = 0; i < parameterCount; ++i)
        ramps_[i].reset(parameter(i).constrain(v[i]));
    for (auto *ring : {&audio_, &trimmed_, &detector_})
        for (auto &channel : *ring)
            channel.fill(0);
    hpX_ = hpY_ = lp_ = peak_ = power_ = {};
    hold_ = {};
    armed_ = {};
    envelope_.fill(reduction(
        -300, parameter(Threshold).constrain(v[Threshold]), parameter(Ratio).constrain(v[Ratio]),
        parameter(Knee).constrain(v[Knee]), parameter(Range).constrain(v[Range])));
    position_ = 0;
    inputPeak_ = 0;
    reduction_ = envelope_[0] * (1 - parameter(Bypass).constrain(v[Bypass]));
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
    const double hp =
        std::exp(-2 * std::numbers::pi * std::min(p[SidechainHP], rate_ * .45) / rate_);
    const double lp =
        std::exp(-2 * std::numbers::pi * std::min(p[SidechainLP], rate_ * .45) / rate_);
    const double delay = std::max(0., latency_ - p[Lookahead] * rate_ / 1000.);
    const unsigned offset = static_cast<unsigned>(delay);
    const double fraction = delay - offset;
    const unsigned a = (position_ + capacity - offset) % capacity,
                   b = (a + capacity - 1) % capacity,
                   audioRead = (position_ + capacity - latency_) % capacity;
    double level[2]{}, dry[2]{}, program[2]{}, listen[2]{};
    for (unsigned c = 0; c < 2; ++c) {
        audio_[c][position_] = raw[c];
        trimmed_[c][position_] = raw[c] * inputGain;
        dry[c] = audio_[c][audioRead];
        program[c] = trimmed_[c][audioRead];
        const double x = raw[c] * inputGain * (1 - p[Sidechain]) + external[c] * p[Sidechain];
        hpY_[c] = clean(hp * (hpY_[c] + x - hpX_[c]));
        hpX_[c] = x;
        const double high = p[SidechainHP] < .01 ? x : hpY_[c];
        lp_[c] = clean(lp * lp_[c] + (1 - lp) * high);
        detector_[c][position_] = p[SidechainLP] >= 19999.99 ? high : lp_[c];
        listen[c] = detector_[c][audioRead];
        const double d = detector_[c][a] * (1 - fraction) + detector_[c][b] * fraction;
        peak_[c] = std::max(std::abs(d), clean(peak_[c] * detectorDecay_));
        power_[c] = clean(detectorDecay_ * power_[c] + (1 - detectorDecay_) * d * d);
        level[c] = (1 - p[Detector]) * peak_[c] + p[Detector] * std::sqrt(power_[c]);
    }
    inputPeak_ = std::max(std::abs(dry[0]), std::abs(dry[1]));
    // A louder channel opens both gates when linked; never link toward more attenuation.
    const double linked = std::max(level[0], level[1]), link = p[StereoLink] / 100.;
    const double attack = p[Attack] <= 0 ? 0 : std::exp(-1000 / (rate_ * p[Attack]));
    const double release = std::exp(-1000 / (rate_ * p[Release]));
    reduction_ = 0;
    for (unsigned c = 0; c < 2; ++c) {
        const double db = 20 * std::log10(std::max(1e-15, level[c] * (1 - link) + linked * link));
        const double boundary = p[Threshold] + p[Knee] * .5;
        if (db >= boundary || (armed_[c] && db >= boundary - p[Hysteresis])) {
            armed_[c] = true;
            hold_[c] = static_cast<unsigned>(p[Hold] * rate_ / 1000.);
        } else if (hold_[c] > 0) {
            --hold_[c];
        } else {
            armed_[c] = false;
        }
        const double target =
            armed_[c] ? 0 : reduction(db, p[Threshold], p[Ratio], p[Knee], p[Range]);
        const double coefficient = target < envelope_[c] ? attack : release;
        envelope_[c] = clean(coefficient * envelope_[c] + (1 - coefficient) * target);
        reduction_ = std::max(reduction_, envelope_[c] * (1 - p[Bypass]) * (1 - p[Audition]));
        const double wetBalance = mono ? 1 : 1 - std::max(0., (c == 0 ? 1 : -1) * p[WetPan] / 100.);
        const double dryBalance = mono ? 1 : 1 - std::max(0., (c == 0 ? 1 : -1) * p[DryPan] / 100.);
        const double wet = program[c] * gain(p[WetGain] - envelope_[c]) * wetBalance;
        const double mixed =
            program[c] * gain(p[DryGain]) * dryBalance * (1 - p[Mix] / 100.) + wet * p[Mix] / 100.;
        const double selected =
            (mixed * (1 - p[Audition]) + listen[c] * p[Audition]) * gain(p[Output]);
        const double out = clean(selected * (1 - p[Bypass]) + dry[c] * p[Bypass]);
        if (c == 0)
            l = out;
        else
            r = out;
    }
    position_ = (position_ + 1) % capacity;
    if (mono)
        r = l;
}
} // namespace openfilter::gate
