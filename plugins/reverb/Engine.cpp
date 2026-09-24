#include "Engine.hpp"
#include <numbers>
namespace openfilter::reverb {
namespace {
constexpr double times[]{.0297, .0371, .0411, .0437, .0531, .0617, .0719, .0793};
constexpr double signL[]{1, 1, -1, 1, -1, -1, 1, -1};
constexpr double signR[]{1, -1, 1, 1, 1, -1, -1, -1};
double clean(double x) noexcept {
    return std::isfinite(x) ? std::clamp(x, -1e6, 1e6) : 0.;
}
} // namespace
void Engine::prepare(double rate, const Values &v, unsigned revision) noexcept {
    revision_ = revision;
    rate_ = std::clamp(rate, 1000., 768000.);
    smoothing_ = unsigned(rate_ * .02);
    attack_ = std::exp(-1 / (rate_ * .005));
    release_ = std::exp(-1 / (rate_ * .25));
    constexpr double ds[]{.0047, .0079, .0113, .0179};
    for (unsigned i = 0; i < 4; ++i)
        diffuserLengths_[i] = unsigned(ds[i] * rate_);
    gateRelease_ = std::exp(-1 / (rate_ * .08));
    constexpr double frequencies[]{.071, .113, .173, .227};
    for (unsigned j = 0; j < 4; ++j) {
        oscStepSin_[j] = std::sin(2 * std::numbers::pi * frequencies[j] / rate_);
        oscStepCos_[j] = std::cos(2 * std::numbers::pi * frequencies[j] / rate_);
    }
    constexpr double delays[3][8]{{0, .0011, .0023, .0037, .0049, .0057, .0067, .0079},
                                  {.0071, .0043, .0017, .0061, 0, .0029, .0053, .0007},
                                  {.0031, .0003, .0059, .0013, .0073, .0047, .0021, 0}};
    for (unsigned stage = 0; stage < 3; ++stage)
        for (unsigned j = 0; j < 8; ++j)
            inputDelays_[stage][j] = unsigned(delays[stage][j] * rate_);
    constexpr double earlyTimes[]{0,     .0033, .0071, .0117, .0173, .0239,
                                  .0311, .0379, .0437, .0511, .0583, .0671};
    for (unsigned ch = 0; ch < 2; ++ch)
        for (unsigned k = 0; k < 12; ++k)
            reflectionDelays_[ch][k] = unsigned((earlyTimes[k] + (k ? ch * .00073 : 0)) * rate_);
    reset(v);
}
void Engine::reset(const Values &v) noexcept {
    for (auto &stage : inputDiffusion_)
        for (auto &a : stage)
            a.fill(0);
    for (auto &a : reflections_)
        a.fill(0);
    scatterPosition_ = 0;
    for (unsigned j = 0; j < 4; ++j) {
        oscSin_[j] = std::sin(j * 1.731);
        oscCos_[j] = std::cos(j * 1.731);
    }
    lastInput_ = lastOutput_ = 1e100;
    lastHold_ = -1;
    for (auto &a : tank_)
        a.fill(0);
    for (auto &a : pre_)
        a.fill(0);
    for (auto &ch : diffuser_)
        for (auto &a : ch)
            a.fill(0);
    for (auto &a : damping_)
        for (auto &f : a)
            f.reset();
    for (auto &a : post_)
        for (auto &f : a)
            f.reset();
    for (unsigned i = 0; i < parameterCount; ++i)
        ramps_[i].reset(parameter(i).constrain(v[i]));
    values_ = v;
    phase_.fill(0);
    position_ = prePosition_ = diffuserPosition_ = clock_ = hold_ = 0;
    wet_.fill(0);
    bpm_ = 120;
    env_ = gate_ = reduction_ = 0;
    coefficients(true);
    for (unsigned j = 0; j < 8; ++j)
        for (unsigned b = 0; b < bands; ++b)
            refinedDamping_[j][b].reset(dampingCoefficients_[j][b],
                                        int(v[bandIndex(false, b, Type)]), smoothing_);
    for (auto &ch : refinedPost_)
        for (unsigned b = 0; b < bands; ++b)
            ch[b].reset(postCoefficients_[b], int(v[bandIndex(true, b, Type)]), smoothing_);
    for (unsigned j = 0; j < 8; ++j)
        roomTaps_[j].reset(delays_[j], unsigned(rate_ * .05));
    preTap_.reset(v[Predelay] * .001 * rate_, unsigned(rate_ * .03));
}
void Engine::set(unsigned i, double v) noexcept {
    if (i < parameterCount) {
        const bool shape = i >= globals && (i - globals) % fields == Type;
        ramps_[i].set(parameter(i).constrain(v), revision_ >= 2 && shape ? 1
                                                 : i == Space            ? unsigned(rate_ * .1)
                                                                         : smoothing_);
    }
}
double Engine::read(const double *a, unsigned size, unsigned pos, double delay) noexcept {
    delay = std::clamp(delay, 0., double(size - 2));
    const auto d = unsigned(delay);
    const auto index = (pos + size - d) % size;
    const double f = delay - d;
    return a[index] * (1 - f) + a[(index + size - 1) % size] * f;
}
double Engine::movingRead(const double *a, unsigned size, unsigned pos,
                          const MovingTap &tap) const noexcept {
    const double first = read(a, size, pos, tap.from);
    if (tap.from == tap.to)
        return first;
    return first + (read(a, size, pos, tap.to) - first) * tap.weight();
}
void Engine::coefficients(bool force) noexcept {
    const auto previousValues = coefficientValues_;
    coefficientValues_ = values_;
    const auto &v = values_;
    const double t = v[Space] * v[DecayRate] * .01;
    const double geometry = .55 + .65 * std::sqrt(v[Space]);
    const double frozen = v[Freeze];
    const bool dampingChanged =
        force || revision_ < 2 || v[Space] != previousValues[Space] ||
        v[DecayRate] != previousValues[DecayRate] || v[Freeze] != previousValues[Freeze] ||
        v[Brightness] != previousValues[Brightness] ||
        !std::equal(v.begin() + globals, v.begin() + globals + bands * fields,
                    previousValues.begin() + globals);
    if (dampingChanged)
        for (unsigned j = 0; j < 8; ++j) {
            delays_[j] = std::min(double(capacity - 512), times[j] * geometry * rate_);
            if (revision_ >= 2) {
                delays_[j] = std::round(delays_[j]);
                roomTaps_[j].request(delays_[j]);
            }
            const double loss = 60 * delays_[j] / rate_ / t * (1 - frozen);
            feedback_[j] = std::pow(10., -loss / 20);
            for (unsigned b = 0; b < bands; ++b) {
                const auto i = bandIndex(false, b, 0);
                const auto shape = static_cast<dsp::Shape>(int(v[i + Type]));
                dampingActive_[b] = v[i + Enabled] > 0 && v[i + Amount] != 100 && frozen < 1;
                dampingCoefficients_[j][b] = dsp::Coefficients::make(
                    shape, v[i + Frequency], loss * decayContribution(v, b),
                    shape == dsp::Shape::Bell ? v[i + Q] : .7071067811865476, rate_);
                if (!dampingActive_[b])
                    refinedDamping_[j][b].reset(dampingCoefficients_[j][b], int(v[i + Type]),
                                                smoothing_);
                else
                    refinedDamping_[j][b].update(dampingCoefficients_[j][b], int(v[i + Type]));
            }
            dampingActive_[bands] = v[Brightness] < 100 && frozen < 1;
            dampingCoefficients_[j][bands] = dsp::Coefficients::make(
                dsp::Shape::HighShelf, 4000, -loss * (1 - v[Brightness] * .01) * 3,
                .7071067811865476, rate_);
        }
    for (unsigned b = 0; b < bands; ++b) {
        const auto i = bandIndex(true, b, 0);
        if (!force && revision_ >= 2 &&
            std::equal(v.begin() + i, v.begin() + i + fields, previousValues.begin() + i))
            continue;
        const bool wasActive = postActive_[b];
        postActive_[b] = v[i + Enabled] > 0;
        postCoefficients_[b] =
            dsp::Coefficients::make(static_cast<dsp::Shape>(int(v[i + Type])), v[i + Frequency],
                                    v[i + Amount], v[i + Q], rate_);
        for (auto &ch : refinedPost_) {
            if (wasActive && !postActive_[b])
                ch[b].reset(postCoefficients_[b], int(v[i + Type]), smoothing_);
            else
                ch[b].update(postCoefficients_[b], int(v[i + Type]));
        }
    }
}
void Engine::sample(double &l, double &r, bool mono) noexcept {
    l = clean(l);
    r = mono ? l : clean(r);
    const double dryL = l, dryR = r;
    for (unsigned i = 0; i < parameterCount; ++i)
        values_[i] = ramps_[i].next();
    if ((clock_++ & 31u) == 0 && values_ != coefficientValues_)
        coefficients();
    const auto &v = values_;
    if (v[Input] != lastInput_) {
        lastInput_ = v[Input];
        cachedInput_ = std::pow(10., v[Input] / 20);
    }
    if (v[Output] != lastOutput_) {
        lastOutput_ = v[Output];
        cachedOutput_ = std::pow(10., v[Output] / 20);
    }
    if (v[GateHold] != lastHold_) {
        lastHold_ = v[GateHold];
        cachedHold_ = unsigned(v[GateHold] * .001 * rate_);
    }
    const double inputGain = cachedInput_;
    const double in[2]{l * inputGain, r * inputGain};
    const double detector = std::max(std::abs(in[0]), std::abs(in[1]));
    const double ec = detector > env_ ? attack_ : release_;
    env_ = ec * env_ + (1 - ec) * detector;
    if (env_ < 1e-30)
        env_ = 0;
    reduction_ = 24 * v[Ducking] * .01 * std::clamp(env_ * 4, 0., 1.);
    if (detector > std::pow(10., -45. / 20))
        hold_ = unsigned(cachedHold_);
    else if (hold_)
        --hold_;
    const double gateTarget = hold_ ? 1. : 0.;
    const double gc = gateTarget > gate_ ? attack_ : gateRelease_;
    gate_ = gc * gate_ + (1 - gc) * gateTarget;
    if (gate_ < 1e-30)
        gate_ = 0;
    double preMs = v[Predelay];
    if (v[PredelaySync] >= .5)
        preMs = std::min(500., 60000 / bpm_ / std::pow(2., std::round(v[PredelaySync]) - 1) *
                                   v[PredelayOffset] * .01);
    if (revision_ >= 2) {
        if (clock_ == 1)
            preTap_.reset(preMs * .001 * rate_, unsigned(rate_ * .03));
        preTap_.request(preMs * .001 * rate_);
        preTap_.step();
    }
    double feed[2]{}, early[2]{};
    for (unsigned ch = 0; ch < 2; ++ch) {
        pre_[ch][prePosition_] = in[ch] * (1 - v[Freeze]);
        early[ch] = revision_ >= 2
                        ? movingRead(pre_[ch].data(), preCapacity, prePosition_, preTap_)
                        : read(pre_[ch].data(), preCapacity, prePosition_, preMs * .001 * rate_);
        reflections_[ch][scatterPosition_] = early[ch];
        double x = early[ch];
        const double ap = .15 + .50 * v[Thickness] * .01 + .04 * v[Style];
        for (unsigned d = 0; d < 4; ++d) {
            const auto delay = diffuserLengths_[d] + ch * 17;
            auto &a = diffuser_[ch][d];
            const double z = a[(diffuserPosition_ + diffuserCapacity - delay) % diffuserCapacity];
            const double y = z - ap * x;
            const double next = x + ap * y;
            a[diffuserPosition_] = std::abs(next) < 1e-30 ? 0 : next;
            x = y;
        }
        feed[ch] = x * (1 - v[Freeze]);
    }
    std::array<double, 8> injection{};
    if (revision_ >= 2) {
        for (unsigned j = 0; j < 8; ++j)
            injection[j] = (feed[0] * signL[j] + feed[1] * signR[j]) * .18;
        // Each stage is a unitary delay bank followed by normalized Hadamard
        // scattering: more paths without imposing an arbitrary input FIR EQ.
        for (unsigned stage = 0; stage < 3; ++stage) {
            for (unsigned j = 0; j < 8; ++j) {
                auto &buffer = inputDiffusion_[stage][j];
                buffer[scatterPosition_ % inputCapacity] = injection[j];
                injection[j] = buffer[(scatterPosition_ + inputCapacity - inputDelays_[stage][j]) %
                                      inputCapacity];
            }
            for (unsigned stride = 1; stride < 8; stride *= 2)
                for (unsigned base = 0; base < 8; base += stride * 2)
                    for (unsigned j = 0; j < stride; ++j) {
                        const double a = injection[base + j], b = injection[base + j + stride];
                        injection[base + j] = a + b;
                        injection[base + j + stride] = a - b;
                    }
            for (auto &x : injection)
                x *= .3535533905932738;
        }
    }
    std::array<double, 8> delayed{}, mixed{};
    for (unsigned j = 0; j < 8; ++j) {
        if (revision_ < 2) {
            phase_[j] += 2 * std::numbers::pi * (.13 + .037 * j) / rate_;
            if (phase_[j] > 2 * std::numbers::pi)
                phase_[j] -= 2 * std::numbers::pi;
            const double depth =
                (.00058 * v[Character] * .01) * (1 + .6 * v[Style]) * (1 - v[Freeze]);
            delayed[j] = read(tank_[j].data(), capacity, position_,
                              delays_[j] + std::sin(phase_[j]) * depth * rate_);
        } else {
            roomTaps_[j].step();
            delayed[j] = movingRead(tank_[j].data(), capacity, position_, roomTaps_[j]);
        }
        double x = delayed[j];
        for (unsigned b = 0; b <= bands; ++b) {
            if (revision_ < 2 || b == bands) {
                x = damping_[j][b].process(x, dampingCoefficients_[j][b]);
            } else if (dampingActive_[b]) {
                x = refinedDamping_[j][b].process(x);
            }
        }
        mixed[j] = x * feedback_[j];
    }
    // Orthogonal Hadamard scattering, normalized to preserve vector energy.
    for (unsigned stride = 1; stride < 8; stride *= 2)
        for (unsigned base = 0; base < 8; base += stride * 2)
            for (unsigned j = 0; j < stride; ++j) {
                const double a = mixed[base + j], b = mixed[base + j + stride];
                mixed[base + j] = a + b;
                mixed[base + j + stride] = a - b;
            }
    if (revision_ >= 2) {
        // Givens rotations preserve vector energy at every sample; modulation
        // changes scattering instead of resampling the feedback signal.
        const double depth = .32 * v[Character] * .01 * (1 + .35 * v[Style]) * (1 - v[Freeze]);
        for (unsigned j = 0; j < 4; ++j) {
            const double sn = oscSin_[j] * oscStepCos_[j] + oscCos_[j] * oscStepSin_[j];
            oscCos_[j] = oscCos_[j] * oscStepCos_[j] - oscSin_[j] * oscStepSin_[j];
            oscSin_[j] = sn;
            const double b = depth * sn, a = std::sqrt(std::max(0., 1 - b * b));
            const double x = mixed[j], y = mixed[j + 4];
            mixed[j] = a * x - b * y;
            mixed[j + 4] = b * x + a * y;
        }
    }
    double wet[2]{};
    for (unsigned j = 0; j < 8; ++j) {
        double x = mixed[j] * .3535533905932738 + (feed[0] * signL[j] + feed[1] * signR[j]) * .18;
        if (revision_ >= 2)
            x = mixed[j] * .3535533905932738 + injection[j];
        if (std::abs(x) < 1e-30)
            x = 0;
        tank_[j][position_] = clean(x);
        wet[0] += delayed[j] * signR[j] * .25;
        wet[1] += delayed[j] * signL[j] * .25;
    }
    for (unsigned ch = 0; ch < 2; ++ch) {
        if (revision_ >= 2) {
            constexpr double gains[]{.5, .36, -.3, .27, .24, -.21, .18, .16, -.14, .12, .1, -.08};
            early[ch] = 0;
            for (unsigned k = 0; k < 12; ++k) {
                const unsigned source = k % 3 == 2 ? 1 - ch : ch;
                early[ch] += gains[k] * reflections_[source][(scatterPosition_ + scatterCapacity -
                                                              reflectionDelays_[ch][k]) %
                                                             scatterCapacity];
            }
            early[ch] *= 1 - v[Freeze];
        }
        const double distance = v[Distance] * .01;
        wet[ch] = wet[ch] * (.65 + .35 * distance) +
                  early[ch] * (1 - distance) * (.08 + .15 * v[Character] * .01);
        for (unsigned b = 0; b < bands; ++b) {
            const auto i = bandIndex(true, b, 0);
            if (revision_ >= 2 && !postActive_[b])
                continue;
            const double filtered = revision_ >= 2
                                        ? refinedPost_[ch][b].process(wet[ch])
                                        : post_[ch][b].process(wet[ch], postCoefficients_[b]);
            wet[ch] += v[i + Enabled] * (filtered - wet[ch]);
        }
    }
    const double mid = (wet[0] + wet[1]) * .5, side = (wet[0] - wet[1]) * .5 * v[Width] * .01;
    const double wetGain =
        std::pow(10., -reduction_ / 20) * (1 - v[AutoGate] + v[AutoGate] * gate_);
    wet[0] = (mid + side) * wetGain;
    wet[1] = (mid - side) * wetGain;
    if (mono)
        wet[0] = wet[1] = mid * wetGain;
    wet_ = {wet[0], wet[1]};
    const double mix = v[Mix] * .01, out = cachedOutput_, bypass = v[Bypass];
    l = dryL * bypass + (dryL * (1 - mix) + wet[0] * mix) * out * (1 - bypass);
    r = dryR * bypass + (dryR * (1 - mix) + wet[1] * mix) * out * (1 - bypass);
    scatterPosition_ = (scatterPosition_ + 1) % scatterCapacity;
    position_ = (position_ + 1) % capacity;
    prePosition_ = (prePosition_ + 1) % preCapacity;
    diffuserPosition_ = (diffuserPosition_ + 1) % diffuserCapacity;
}
} // namespace openfilter::reverb
