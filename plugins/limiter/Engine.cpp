#include "Engine.hpp"
namespace openfilter::limiter {
namespace {
double clean(double x) noexcept {
    return std::isfinite(x) && std::abs(x) >= 1e-30 ? std::clamp(x, -1e12, 1e12) : 0.;
}
double gain(double db) noexcept {
    return std::exp(db * (std::log(10.) / 20));
}
} // namespace
void Engine::prepare(double rate, const Values &v) noexcept {
    rate_ = std::isfinite(rate) ? std::clamp(rate, 1000., 768000.) : 48000.;
    factor_ = rate_ <= 192000 ? 4 : rate_ <= 384000 ? 2 : 1;
    resamplingDelay_ = factor_ == 4 ? 136 : factor_ == 2 ? 128 : 0;
    mainDelay_ = static_cast<unsigned>(std::ceil(rate_ * .005));
    guardSmooth_ = static_cast<unsigned>(std::ceil(rate_ * .003));
    guardDelay_ = guardSmooth_ + PeakDetector::support;
    guardHold_ = static_cast<unsigned>(std::ceil(rate_ * .01));
    smoothing_ = std::max(1u, static_cast<unsigned>(rate_ * .01));
    guardDecay_ = std::exp(-1 / (rate_ * .1));
    for (unsigned i = 0; i < parameterCount; ++i)
        ramps_[i].reset(parameter(i).constrain(v[i]));
    auto old = v;
    old[Ceiling] = old[UnityGain] = old[Bypass] = 0;
    legacy_.prepare(rate_, old);
    modern_.prepare(rate_ * factor_, mainDelay_ * factor_, v);
    for (auto &c : first_)
        c.prepare();
    for (auto &c : second_)
        c.prepare();
    for (auto &c : protection_)
        c.prepare();
    for (auto &c : bandLimit_)
        c.prepare();
    for (auto &c : meters_)
        c.prepare();
    for (auto *buffer : {&dry_, &wet_, &legacyDelay_, &preBand_})
        for (auto &c : *buffer)
            c.fill(0);
    for (auto &c : guardGain_)
        c.fill(1);
    for (auto &m : guardMinimum_)
        m.reset();
    drive_.fill(gain(ramps_[Gain].value));
    inputHistory_.fill(0);
    reductionHistory_.fill(0);
    legacyReductionHistory_.fill(0);
    guardSum_.fill(guardSmooth_ + 1);
    guardEnvelope_.fill(1);
    outputPeak_.fill(0);
    samples_ = position_ = guardPosition_ = 0;
    reduction_ = inputPeak_ = 0;
}
void Engine::set(unsigned i, double v) noexcept {
    if (i >= parameterCount)
        return;
    ramps_[i].set(parameter(i).constrain(v), smoothing_);
    modern_.set(i, v);
    if (i == Gain || i == Release || i == StereoLink || i == AutoRelease)
        legacy_.set(i, v);
}
void Engine::sample(double &l, double &r, bool mono) noexcept {
    Values p{};
    for (unsigned i = 0; i < parameterCount; ++i)
        p[i] = ramps_[i].next();
    const double raw[]{clean(l), clean(mono ? l : r)};
    const unsigned outputAt = (position_ + capacity - latency()) % capacity;
    const unsigned wetAt = (position_ + capacity - guardDelay_) % capacity;
    const unsigned legacyAt = (position_ + capacity - resamplingDelay_) % capacity;
    drive_[position_] = gain(p[Gain]);
    std::array<std::array<double, 4>, 2> high{};
    for (unsigned c = 0; c < 2; ++c) {
        dry_[c][position_] = raw[c];
        const double driven = raw[c] * drive_[position_];
        if (factor_ == 1)
            high[c][0] = driven;
        else {
            const auto pair = first_[c].up(driven);
            if (factor_ == 2) {
                high[c][0] = pair[0];
                high[c][1] = pair[1];
            } else
                for (unsigned j = 0; j < 2; ++j) {
                    const auto q = second_[c].up(pair[j]);
                    high[c][2 * j] = q[0];
                    high[c][2 * j + 1] = q[1];
                }
        }
    }
    double mainGain = 1;
    for (unsigned j = 0; j < factor_; ++j) {
        modern_.sample(high[0][j], high[1][j]);
        mainGain = std::min(mainGain, modern_.minimumGain());
    }
    const double mainReduction = -20 * std::log10(std::max(1e-30, mainGain));
    double legacyL = raw[0], legacyR = raw[1];
    legacy_.sample(legacyL, legacyR, mono);
    legacyDelay_[0][position_] = legacyL;
    legacyDelay_[1][position_] = legacyR;
    const double legacyMix = std::clamp(1 - p[Style], 0., 1.);
    double samplePeak = 0, reconstructedPeak = 0;
    for (unsigned c = 0; c < 2; ++c) {
        double modern = high[c][0];
        if (factor_ == 2)
            modern = first_[c].down(high[c][0], high[c][1]);
        else if (factor_ == 4) {
            const double a = second_[c].down(high[c][0], high[c][1]);
            const double b = second_[c].down(high[c][2], high[c][3]);
            modern = first_[c].down(a, b);
        }
        preBand_[c][position_] = modern + legacyMix * (legacyDelay_[c][legacyAt] - modern);
        const double filtered = bandLimit_[c].sample(preBand_[c][position_]);
        const double unfiltered = preBand_[c][(position_ + capacity - BandLimit::delay) % capacity];
        const double filterMix = 1 - legacyMix * (1 - p[TruePeak]);
        wet_[c][position_] = unfiltered + filterMix * (filtered - unfiltered);
        samplePeak = std::max(samplePeak, std::abs(wet_[c][position_]));
        reconstructedPeak = std::max(reconstructedPeak, protection_[c].sample(wet_[c][position_]));
    }
    reductionHistory_[position_] = mainReduction;
    legacyReductionHistory_[position_] = legacy_.gainReduction();
    inputHistory_[position_] = std::max(std::abs(raw[0]), std::abs(raw[1])) * drive_[position_];
    // Two always-warm guards: decimation can create sample overshoots even
    // with True Peak disabled. No clipping is used to conceal those overshoots.
    std::array<double, 2> guards{};
    for (unsigned k = 0; k < 2; ++k) {
        const double peak = k ? reconstructedPeak * 1.035142166679344 : samplePeak;
        const double target =
            guardMinimum_[k].push(1 / std::max(1., peak), samples_, guardDelay_ + guardHold_);
        guardEnvelope_[k] = std::min(target, 1 - guardDecay_ * (1 - guardEnvelope_[k]));
        guardSum_[k] += static_cast<long double>(guardEnvelope_[k]) - guardGain_[k][guardPosition_];
        guardGain_[k][guardPosition_] = guardEnvelope_[k];
        guards[k] = std::clamp(static_cast<double>(guardSum_[k] / (guardSmooth_ + 1)), 0., 1.);
    }
    guardPosition_ = (guardPosition_ + 1) % (guardSmooth_ + 1);
    const double guard = std::min(guards[0], 1 + p[TruePeak] * (guards[1] - 1));
    const double ceiling = gain(p[Ceiling]);
    const double comparison = 1 + p[UnityGain] * (1 / drive_[outputAt] - 1);
    for (unsigned c = 0; c < 2; ++c) {
        const double wet = wet_[c][wetAt] * guard * ceiling * comparison;
        const double out = wet * (1 - p[Bypass]) + dry_[c][outputAt] * p[Bypass];
        outputPeak_[c] = meters_[c].sample(out);
        if (c == 0)
            l = out;
        else
            r = out;
    }
    inputPeak_ = inputHistory_[outputAt];
    const double modernReduction =
        reductionHistory_[(wetAt + capacity - resamplingDelay_ / 2 - BandLimit::delay) % capacity];
    const double legacyReduction =
        legacyReductionHistory_[(wetAt + capacity - resamplingDelay_ - BandLimit::delay) %
                                capacity];
    reduction_ = (modernReduction + legacyMix * (legacyReduction - modernReduction) -
                  20 * std::log10(std::max(1e-30, guard))) *
                 (1 - p[Bypass]);
    position_ = (position_ + 1) % capacity;
    ++samples_;
    if (mono)
        r = l;
}
} // namespace openfilter::limiter
