#include "Engine.hpp"
#include <numbers>

namespace openfilter::eq {
void Engine::prepare(double rate, const Values &values) noexcept {
    rate_ = rate;
    smoothSamples_ = static_cast<unsigned>(rate * 0.010);
    fadeSamples_ = static_cast<unsigned>(rate * 0.005);
    output_.reset(values[1]);
    outputGain_ = std::pow(10.0, values[1] / 20);
    wet_.reset(1 - values[0]);
    for (unsigned i = 0; i < bands; ++i) {
        auto &b = bands_[i];
        b.frequency.reset(values[index(i, Frequency)]);
        b.gain.reset(values[index(i, Gain)]);
        b.q.reset(values[index(i, Q)]);
        b.type = b.nextType = static_cast<int>(values[index(i, Type)]);
        b.slope = b.nextSlope = static_cast<int>(values[index(i, Slope)]);
        b.route = b.nextRoute = static_cast<int>(values[index(i, Routing)]);
        b.enabled = values[index(i, Enabled)] != 0;
        b.wet.reset(b.enabled ? 1 : 0);
        b.clear();
        configure(b);
    }
}
void Engine::set(unsigned i, double value) noexcept {
    if (i >= parameterCount)
        return;
    value = parameter(i).constrain(value);
    if (i == 0) {
        wet_.set(1 - value, fadeSamples_);
        return;
    }
    if (i == 1) {
        output_.set(value, smoothSamples_);
        return;
    }
    auto &b = bands_[(i - 2) / fields];
    switch (static_cast<Field>((i - 2) % fields)) {
    case Enabled:
        b.enabled = value != 0;
        break;
    case Type:
        b.nextType = static_cast<int>(value);
        break;
    case Frequency:
        b.frequency.set(value, smoothSamples_);
        break;
    case Gain:
        b.gain.set(value, smoothSamples_);
        break;
    case Q:
        b.q.set(value, smoothSamples_);
        break;
    case Slope:
        b.nextSlope = static_cast<int>(value);
        break;
    case Routing:
        b.nextRoute = static_cast<int>(value);
        break;
    }
    b.wet.set(b.enabled && !b.changing() ? 1 : 0, fadeSamples_);
}
void Engine::configure(Band &b) noexcept {
    const bool cut = b.type == 3 || b.type == 4;
    const unsigned stages = stageCount(b.type, b.slope);
    if (isBrickwall(b.type, b.slope)) {
        const double warped = std::tan(
            std::numbers::pi * std::clamp(std::exp2(b.frequency.value), 1., rate_ * .475) / rate_);
        for (unsigned s = 0; s < stages; ++s)
            b.coefficients[s] = dsp::brickwallSection(b.type == 3, s, warped);
        b.dirty = false;
        return;
    }
    for (unsigned s = 0; s < stages; ++s) {
        double q = std::exp2(b.q.value);
        if (cut && stages > 1) {
            // Butterworth alignment at Q=sqrt(1/2). Q scales every section.
            q *= std::sqrt(2.0) / (2 * std::cos(std::numbers::pi * (2 * s + 1) / (4 * stages)));
        }
        b.coefficients[s] = dsp::Coefficients::make(
            static_cast<dsp::Shape>(b.type), std::exp2(b.frequency.value), b.gain.value, q, rate_);
    }
    b.dirty = false;
}
void Engine::bandSample(Band &b, double &left, double &right, bool mono) noexcept {
    if (b.changing() && b.wet.value == 0) {
        b.type = b.nextType;
        b.slope = b.nextSlope;
        b.route = b.nextRoute;
        b.clear();
        b.dirty = true;
        b.wet.set(b.enabled ? 1 : 0, fadeSamples_);
    }
    const bool moving = b.frequency.remaining || b.gain.remaining || b.q.remaining;
    b.frequency.next();
    b.gain.next();
    b.q.next();
    const double wet = b.wet.next();
    if (moving)
        b.dirty = true;
    if (wet == 0) {
        if (!b.silent)
            b.clear();
        return;
    }
    b.silent = false;
    if (b.dirty)
        configure(b);
    const unsigned stages = stageCount(b.type, b.slope);
    auto filter = [&](double x, unsigned channel) {
        for (unsigned s = 0; s < stages; ++s)
            x = b.filters[channel][s].process(x, b.coefficients[s]);
        return x;
    };
    auto blend = [wet](double dry, double filtered) {
        return wet == 1 ? filtered : dry + wet * (filtered - dry);
    };
    const auto route = static_cast<Route>(b.route);
    if (mono) {
        // A mono signal has no right or side component. Mid acts on the mono bus.
        if (route != Route::Right && route != Route::Side)
            left = blend(left, filter(left, 0));
        return;
    }
    if (route == Route::Mid || route == Route::Side) {
        const double component = (left + (route == Route::Mid ? right : -right)) * 0.5;
        const double delta = wet * (filter(component, 0) - component);
        left += delta;
        right += route == Route::Mid ? delta : -delta;
    } else {
        if (route != Route::Right)
            left = blend(left, filter(left, 0));
        if (route != Route::Left)
            right = blend(right, filter(right, 1));
    }
}
void Engine::sample(double &left, double &right, bool mono) noexcept {
    const double dryL = left, dryR = right;
    for (auto &b : bands_)
        bandSample(b, left, right, mono);
    const bool moving = output_.remaining != 0;
    output_.next();
    if (moving)
        outputGain_ = std::pow(10.0, output_.value / 20);
    const double wet = wet_.next();
    if (wet == 0) {
        left = dryL;
        right = dryR;
        return;
    }
    left *= outputGain_;
    right *= outputGain_;
    if (wet != 1) {
        left = dryL + wet * (left - dryL);
        right = dryR + wet * (right - dryR);
    }
}
} // namespace openfilter::eq
