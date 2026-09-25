#include "Engine.hpp"
namespace openfilter::saturator {
namespace {
// The shared TPT shelf/bell equations with a precomputed fixed-frequency
// tangent. Gain-dependent terms still update on every smoothed audio sample.
dsp::Coefficients toneCoefficients(unsigned tone, double g, double gainDb) noexcept {
    const double a = std::pow(10., gainDb / 40.);
    double k = 1. / std::sqrt(.5);
    dsp::Coefficients c;
    if (tone == 0) {
        g /= std::sqrt(a);
        c.m1 = k * (a - 1);
        c.m2 = a * a - 1;
    } else if (tone == 3) {
        g *= std::sqrt(a);
        c.m0 = a * a;
        c.m1 = k * (1 - a) * a;
        c.m2 = 1 - a * a;
    } else {
        k /= a;
        c.m1 = k * (a * a - 1);
    }
    c.a1 = 1. / (1 + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}
} // namespace

void Engine::prepare(double rate, const Values &v) noexcept {
    silent_ = true;
    silenceCheck_ = 0;
    rate_ = std::clamp(std::isfinite(rate) ? rate : 48000., 1000., 768000.);
    // C++ initializes this immutable table once, on the activation thread.
    static const Curves curves;
    curves_ = &curves;
    inputGain_ = outputGain_ = {};
    driveGain_ = {};
    bandGain_ = {};
    lastDrive_.fill(-1);
    lastCompensation_.fill(-1);
    factor_ = oversampling(rate_);
    padding_ = factor_ == 32 ? 0 : factor_ == 16 ? 1 : factor_ == 8 ? 2 : factor_ == 4 ? 4 : 12;
    dryKernel_ = MatchedDry::kernel(factor_);
    wetDelay_ = {};
    wetPosition_ = 0;
    smoothing_ = std::max(1u, unsigned(rate_ * .01));
    dcPole_ = std::exp(-2 * std::numbers::pi * 5 / rate_);
    constexpr double toneHz[]{160, 800, 3000, 8000};
    for (unsigned t = 0; t < 4; ++t)
        toneTangent_[t] = std::tan(std::numbers::pi * std::min(toneHz[t], rate_ * .4) / rate_);
    attack_ = std::exp(-1 / (rate_ * .01));
    release_ = std::exp(-1 / (rate_ * .1));
    split_ = {};
    dry_ = {};
    envelope_ = {};
    position_ = 0;
    lastLow_ = lastHigh_ = -1;
    for (unsigned i = 0; i < parameterCount; ++i)
        ramps_[i].reset(parameter(i).constrain(v[i]));
    for (unsigned b = 0; b < 3; ++b) {
        lastTone_[b].fill(1e9);
        for (unsigned s = 0; s < 4; ++s)
            styles_[b][s].reset(s == unsigned(ramps_[band(b, Style)].value) ? 1 : 0);
        for (auto &c : channels_[b]) {
            c.wet.prepare();
            c.dry.reset();
            c.tone = {};
            c.dcX = c.dcY = 0;
        }
    }
}
void Engine::set(unsigned i, double value) noexcept {
    if (i >= parameterCount)
        return;
    value = parameter(i).constrain(value);
    ramps_[i].set(value, smoothing_);
    if (i >= globals && (i - globals) % stride == Style)
        for (unsigned s = 0; s < 4; ++s)
            styles_[(i - globals) / stride][s].set(s == unsigned(value) ? 1 : 0, smoothing_);
}
bool Engine::historiesSilent() const noexcept {
    auto zero = [](const auto &a) {
        return std::all_of(a.begin(), a.end(), [](double x) { return x == 0; });
    };
    auto filterSilent = [](const auto &f) { return f.s1 == 0 && f.s2 == 0; };
    if (!zero(envelope_))
        return false;
    for (const auto &channel : split_)
        for (const auto &split : channel)
            for (unsigned n = 0; n < 2; ++n)
                if (!filterSilent(split.low[n]) || !filterSilent(split.high[n]))
                    return false;
    for (const auto &band : channels_)
        for (const auto &c : band) {
            if (c.dcX != 0 || c.dcY != 0)
                return false;
            for (const auto &f : c.tone)
                if (!filterSilent(f))
                    return false;
            if (!c.wet.silent() || !c.dry.silent())
                return false;
        }
    for (const auto &channel : dry_)
        if (!zero(channel))
            return false;
    for (const auto &channel : wetDelay_)
        if (!zero(channel))
            return false;
    return true;
}
void Engine::sample(double &l, double &r, bool mono) noexcept {
    Values p{};
    for (unsigned i = 0; i < parameterCount; ++i)
        p[i] = ramps_[i].next();
    auto clean = [](double x) { return std::isfinite(x) ? std::clamp(x, -1e6, 1e6) : 0.; };
    const double in[2]{clean(l), clean(mono ? l : r)};
    if (silent_ && in[0] == 0 && in[1] == 0) {
        // Histories are exactly zero, not merely below a noise gate. Keep the
        // same sample-based control timeline while avoiding silent convolution.
        for (auto &weights : styles_)
            for (auto &weight : weights)
                weight.next();
        l = r = 0;
        return;
    }
    silent_ = false;
    const double inputGain = inputGain_.get(p[Input]), outputGain = outputGain_.get(p[Output]);
    const double high = std::min(p[CrossoverHigh], rate_ * .45),
                 low = std::min(p[CrossoverLow], high * .8);
    if (low != lastLow_ || high != lastHigh_) {
        for (unsigned j = 0; j < 2; ++j) {
            lp_[j] = dsp::Coefficients::make(dsp::Shape::HighCut, j ? high : low, 0, std::sqrt(.5),
                                             rate_);
            hp_[j] = lp_[j];
            hp_[j].m0 = 1;
            hp_[j].m1 = -1. / std::sqrt(.5);
            hp_[j].m2 = -1;
        }
        lastLow_ = low;
        lastHigh_ = high;
    }
    // Bus configuration is fixed for an activation; prepare resets histories
    // before switching between mono and stereo. A mono bus needs one audio path.
    const unsigned channelCount = mono ? 1 : 2;
    double bands[3][2]{};
    for (unsigned c = 0; c < channelCount; ++c) {
        const auto first = split_[c][0].sample(in[c] * inputGain, lp_[0], hp_[0]);
        const auto second = split_[c][1].sample(first[1], lp_[1], hp_[1]);
        const auto align = split_[c][2].sample(first[0], lp_[1], hp_[1]);
        bands[0][c] = align[0] + align[1];
        bands[1][c] = second[0];
        bands[2][c] = second[1];
    }
    double result[2]{};
    const double solo = std::max({p[band(0, Solo)], p[band(1, Solo)], p[band(2, Solo)]});
    for (unsigned b = 0; b < 3; ++b) {
        const double level = std::max(std::abs(bands[b][0]), std::abs(bands[b][1]));
        const double coeff = level > envelope_[b] ? attack_ : release_;
        envelope_[b] = coeff * envelope_[b] + (1 - coeff) * level;
        if (envelope_[b] < 1e-30)
            envelope_[b] = 0;
        double dynamicGain = 1;
        const double dynamics = p[band(b, Dynamics)] * .01;
        if (dynamics != 0) {
            const double envDb = 20 * std::log10(std::max(1e-12, envelope_[b]));
            const double dynamicDb = dynamics >= 0 ? -dynamics * .75 * std::max(0., envDb + 18)
                                                   : (-dynamics) * .75 * std::min(0., envDb + 18);
            dynamicGain = std::pow(10., std::clamp(dynamicDb, -60., 0.) / 20);
        }
        const double drive = driveGain_[b].get(p[band(b, Drive)]);
        if (drive != lastDrive_[b] || p[Compensation] != lastCompensation_[b]) {
            lastDrive_[b] = drive;
            lastCompensation_[b] = p[Compensation];
            compensationGain_[b] = std::pow(drive, -p[Compensation] * .01);
        }
        const double gain = compensationGain_[b], bandGain = bandGain_[b].get(p[band(b, Level)]);
        const double wet = p[band(b, BandMix)] * .01, enable = p[band(b, Enabled)];
        const double audible = (1 - p[band(b, Mute)]) * std::min(1., 1 - solo + p[band(b, Solo)]);
        double weights[4]{};
        for (unsigned s = 0; s < 4; ++s)
            weights[s] = styles_[b][s].next();
        int singleStyle = -1;
        for (unsigned s = 0; s < 4; ++s)
            if (weights[s] == 1)
                singleStyle = int(s);
        for (unsigned t = 0; t < 4; ++t)
            if (lastTone_[b][t] != p[band(b, Bass + t)]) {
                lastTone_[b][t] = p[band(b, Bass + t)];
                tone_[b][t] = toneCoefficients(t, toneTangent_[t], lastTone_[b][t]);
            }
        for (unsigned c = 0; c < channelCount; ++c) {
            auto &state = channels_[b][c];
            auto process = [&](double x) {
                if (singleStyle >= 0)
                    return (*curves_)(unsigned(singleStyle), x * dynamicGain * drive) * gain;
                double y = 0;
                for (unsigned s = 0; s < 4; ++s)
                    if (weights[s] > 0)
                        y += weights[s] * (*curves_)(s, x * dynamicGain * drive) * gain;
                return y;
            };
            double y = state.wet.process(bands[b][c], factor_, process);
            const double x = state.dry.process(bands[b][c], dryKernel_, delay - padding_);
            // Tone is linear: host-rate processing avoids 32x redundant filter work.
            // Both audio paths have identical FIR filtering and group delay.
            for (unsigned t = 0; t < 4; ++t)
                y = state.tone[t].process(y, tone_[b][t]);
            const double residual = y - x;
            const double blocked = residual - state.dcX + dcPole_ * state.dcY;
            state.dcX = residual;
            state.dcY = std::abs(blocked) < 1e-30 ? 0 : blocked;
            const double processed = (x + wet * blocked) * bandGain;
            y = (x + enable * (processed - x)) * audible;
            result[c] += x + (y - x) * p[Mix] * .01;
        }
    }
    for (unsigned c = 0; c < channelCount; ++c) {
        dry_[c][position_] = in[c];
        const double dry = dry_[c][(position_ + 1) % (delay + 1)];
        wetDelay_[c][wetPosition_] = result[c];
        const double aligned = wetDelay_[c][(wetPosition_ + 13 - padding_) % 13];
        result[c] = aligned * outputGain * (1 - p[Bypass]) + dry * p[Bypass];
        if (std::abs(result[c]) < 1e-30)
            result[c] = 0;
    }
    position_ = (position_ + 1) % (delay + 1);
    wetPosition_ = (wetPosition_ + 1) % 13;
    if (in[0] == 0 && in[1] == 0) {
        if (++silenceCheck_ == 2048) {
            silenceCheck_ = 0;
            silent_ = historiesSilent();
        }
    } else
        silenceCheck_ = 0;
    l = result[0];
    r = mono ? result[0] : result[1];
}
} // namespace openfilter::saturator
