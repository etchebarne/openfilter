#include "LegacyEngine.hpp"

namespace openfilter::limiter {
namespace {
double clean(double x) noexcept {
    return std::isfinite(x) && std::abs(x) >= 1e-30 ? std::clamp(x, -1e12, 1e12) : 0.;
}
double gain(double db) noexcept {
    return std::pow(10., db / 20.);
}
} // namespace
double LegacyEngine::Minimum::push(double v, uint64_t n, unsigned window) noexcept {
    while (size && n - time[head] > window) {
        head = (head + 1) % peakCapacity;
        --size;
    }
    // A new minimum invalidates the whole tail in constant time. For a
    // partial tail replacement, cap linear work and binary-search the sorted
    // remainder. A single peak cannot cause thousands of audio-thread pops.
    if (size && v <= value[head])
        size = 0;
    for (unsigned popped = 0; popped < 4 && size && value[(head + size - 1) % peakCapacity] >= v;
         ++popped)
        --size;
    if (size && value[(head + size - 1) % peakCapacity] >= v) {
        unsigned lo = 0, hi = size;
        while (lo < hi) {
            const unsigned mid = lo + (hi - lo) / 2;
            if (value[(head + mid) % peakCapacity] < v)
                lo = mid + 1;
            else
                hi = mid;
        }
        size = lo;
    }
    const auto tail = (head + size++) % peakCapacity;
    value[tail] = v;
    time[tail] = n;
    return value[head];
}
void LegacyEngine::prepare(double rate, const Values &v) noexcept {
    rate_ = std::isfinite(rate) ? std::clamp(rate, 1000., 768000.) : 48000.;
    latency_ = static_cast<unsigned>(std::ceil(rate_ * .005));
    hold_ = static_cast<unsigned>(std::ceil(rate_ * .01));
    smoothing_ = std::max(1u, static_cast<unsigned>(rate_ * .01));
    slowDecay_ = std::exp(-1 / (rate_ * .25));
    for (unsigned i = 0; i < parameterCount; ++i)
        ramps_[i].reset(parameter(i).constrain(v[i]));
    for (auto &a : dry_)
        a.fill(0);
    for (auto &a : driven_)
        a.fill(0);
    for (auto &g : gains_)
        g.fill(1);
    for (auto &m : minimum_)
        m.head = m.size = 0;
    drive_.fill(1);
    envelope_.fill(1);
    slow_.fill(0);
    sum_.fill(latency_ + 1);
    position_ = 0;
    samples_ = 0;
    reduction_ = inputPeak_ = 0;
}
void LegacyEngine::set(unsigned i, double value) noexcept {
    if (i < parameterCount)
        ramps_[i].set(parameter(i).constrain(value), smoothing_);
}
void LegacyEngine::sample(double &l, double &r, bool mono) noexcept {
    Values p{};
    for (unsigned i = 0; i < parameterCount; ++i)
        p[i] = ramps_[i].next();
    const double raw[]{clean(l), clean(mono ? l : r)}, inputGain = gain(p[Gain]);
    const unsigned length = latency_ + 1, delayed = (position_ + length - latency_) % length;
    std::array<double, 2> smooth{};
    drive_[position_] = inputGain;
    for (unsigned c = 0; c < 2; ++c) {
        dry_[c][position_] = raw[c];
        driven_[c][position_] = raw[c] * inputGain;
        const double required = 1 / std::max(1., std::abs(driven_[c][position_]));
        const double target = minimum_[c].push(required, samples_, latency_ + hold_);
        slow_[c] = slowDecay_ * slow_[c] + (1 - slowDecay_) * (1 - envelope_[c]);
        const double release = p[Release] * (1 + p[AutoRelease] * 3 * slow_[c]);
        const double coefficient = std::exp(-1000 / (rate_ * release));
        envelope_[c] = std::min(target, 1 - coefficient * (1 - envelope_[c]));
        // Every member of this average includes the delayed sample in its minimum
        // window, so its average cannot exceed that sample's safe gain.
        sum_[c] += static_cast<long double>(envelope_[c]) - gains_[c][position_];
        gains_[c][position_] = envelope_[c];
        // Apply the rounding bound before linking so even extreme magnitudes
        // retain an identical stereo gain when fully linked.
        smooth[c] = std::clamp(static_cast<double>(sum_[c] / length), 0.,
                               1 / std::max(1., std::abs(driven_[c][delayed])));
    }
    const double linked = std::min(smooth[0], smooth[1]), link = p[StereoLink] / 100.;
    const double ceiling = gain(p[Ceiling]);
    const double comparison = 1 + p[UnityGain] * (1 / drive_[delayed] - 1);
    inputPeak_ = std::max(std::abs(driven_[0][delayed]), std::abs(driven_[1][delayed]));
    reduction_ = 0;
    for (unsigned c = 0; c < 2; ++c) {
        const double g = smooth[c] + link * (linked - smooth[c]);
        reduction_ = std::max(reduction_, -20 * std::log10(std::max(1e-30, g)) * (1 - p[Bypass]));
        // Rounding guard only; the predictive envelope provides the limiting.
        const double wet = std::clamp(driven_[c][delayed] * g, -1., 1.) * ceiling * comparison;
        const double out = wet * (1 - p[Bypass]) + dry_[c][delayed] * p[Bypass];
        if (c == 0)
            l = out;
        else
            r = out;
    }
    position_ = (position_ + 1) % length;
    ++samples_;
    if (mono)
        r = l;
}
} // namespace openfilter::limiter
