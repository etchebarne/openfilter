#include "ModernEngine.hpp"
namespace openfilter::limiter {
namespace {
double gain(double db) noexcept {
    return std::exp(db * (std::log(10.) / 20));
}
} // namespace
void ModernEngine::prepare(double rate, unsigned delay, const Values &v) noexcept {
    rate_ = rate;
    mainDelay_ = delay;
    hold_ = static_cast<unsigned>(std::ceil(rate_ * .026));
    smoothing_ = std::max(1u, static_cast<unsigned>(rate_ * .01));
    slowDecay_ = std::exp(-1 / (rate_ * .25));
    for (unsigned i = 0; i < parameterCount; ++i)
        ramps_[i].reset(parameter(i).constrain(v[i]));
    for (unsigned j = 0; j < paths_.size(); ++j) {
        auto &p = paths_[j];
        p.delay = static_cast<unsigned>(std::ceil(rate_ * horizons[j] / 1000));
        p.position = 0;
        p.reciprocal = 1.L / (p.delay + 1);
        p.envelope.fill(1);
        p.sum.fill(p.delay + 1);
        for (unsigned c = 0; c < 2; ++c) {
            p.minimum[c].reset();
            p.gains[c].fill(1);
        }
    }

    for (auto &c : driven_)
        c.fill(0);
    for (auto &m : bodyMinimum_)
        m.reset();
    bodyDb_.fill(0);
    slow_.fill(0);
    lastHeld_.fill(1);
    heldDb_.fill(0);
    position_ = 0;
    samples_ = 0;
    minimumGain_ = 1;
    lastStyle_ = lastAttack_ = -1;
}
void ModernEngine::set(unsigned i, double v) noexcept {
    if (i < parameterCount)
        ramps_[i].set(parameter(i).constrain(v), smoothing_);
}
void ModernEngine::sample(double &l, double &r) noexcept {
    Values p{};
    for (unsigned i : {Release, StereoLink, AutoRelease, Style, Lookahead, Attack, ReleaseLink})
        p[i] = ramps_[i].next();
    const unsigned mainAt = (position_ + capacity - mainDelay_) % capacity;
    driven_[0][position_] = l;
    driven_[1][position_] = r;
    // Original voicings: fast release and the sustained envelope's depth vary.
    // The continuously slewed policy avoids resetting histories on style changes.
    const double punch = std::max(0., 1 - std::abs(p[Style] - double(Punch)));
    const double dense = std::clamp(p[Style] - double(Punch), 0., 1.);
    const double fastMs = 40 - 32 * punch + 40 * dense;
    if (p[Style] != lastStyle_) {
        lastStyle_ = p[Style];
        fastDecay_ = std::exp(-1000 / (rate_ * fastMs));
    }
    if (p[Attack] != lastAttack_) {
        lastAttack_ = p[Attack];
        attackDecay_ = std::exp(-1000 / (rate_ * p[Attack]));
    }
    unsigned upper = 1;
    while (upper + 1 < horizons.size() && p[Lookahead] > horizons[upper])
        ++upper;
    const double mix =
        (p[Lookahead] - horizons[upper - 1]) / (horizons[upper] - horizons[upper - 1]);
    const std::array<double, 2> bound{1 / std::max(1., std::abs(driven_[0][mainAt])),
                                      1 / std::max(1., std::abs(driven_[1][mainAt]))};
    std::array<std::array<double, 2>, 6> fast{};
    for (unsigned j = 0; j < paths_.size(); ++j) {
        auto &path = paths_[j];
        const unsigned ahead = (position_ + capacity - mainDelay_ + path.delay) % capacity;
        for (unsigned c = 0; c < 2; ++c) {
            const double required = 1 / std::max(1., std::abs(driven_[c][ahead]));
            const double target = path.minimum[c].push(required, samples_, path.delay + hold_);
            path.envelope[c] = std::min(target, 1 - fastDecay_ * (1 - path.envelope[c]));
            path.sum[c] +=
                static_cast<long double>(path.envelope[c]) - path.gains[c][path.position];
            path.gains[c][path.position] = path.envelope[c];
            // Histories stay warm in every path, but only the selected pair
            // needs gain conversion. Avoid repeated long-double division.
            if (j == upper || j + 1 == upper)
                fast[j][c] =
                    std::clamp(static_cast<double>(path.sum[c] * path.reciprocal), 0., bound[c]);
        }
        path.position = (path.position + 1) % (path.delay + 1);
    }
    std::array<double, 2> transient{}, body{};
    for (unsigned c = 0; c < 2; ++c) {
        transient[c] = fast[upper - 1][c] + mix * (fast[upper][c] - fast[upper - 1][c]);
        const double held = bodyMinimum_[c].push(bound[c], samples_, hold_);
        if (held != lastHeld_[c]) {
            lastHeld_[c] = held;
            heldDb_[c] = -20 * std::log10(std::max(1e-30, held));
        }
        const double demand = heldDb_[c] * (1 - .25 * punch + .2 * dense);
        slow_[c] = slowDecay_ * slow_[c] + (1 - slowDecay_) * bodyDb_[c];
        const double releaseMs =
            p[Release] * (1 + p[AutoRelease] * 3 * std::min(1., slow_[c] / 12));
        const double decay =
            demand > bodyDb_[c] ? attackDecay_ : std::exp(-1000 / (rate_ * releaseMs));
        bodyDb_[c] = demand + decay * (bodyDb_[c] - demand);
        if (bodyDb_[c] < 1e-30)
            bodyDb_[c] = 0;
        if (slow_[c] < 1e-30)
            slow_[c] = 0;
        body[c] = gain(-bodyDb_[c]);
    }
    const double linkedTransient = std::min(transient[0], transient[1]);
    const double linkedBody = std::min(body[0], body[1]);

    minimumGain_ = 1;
    for (unsigned c = 0; c < 2; ++c) {
        const double tg = transient[c] + p[StereoLink] / 100 * (linkedTransient - transient[c]);
        const double bg = body[c] + p[ReleaseLink] / 100 * (linkedBody - body[c]);
        const double g = std::min(tg, bg);
        const double y = driven_[c][mainAt] * g;
        if (c == 0)
            l = y;
        else
            r = y;
        minimumGain_ = std::min(minimumGain_, g);
    }
    position_ = (position_ + 1) % capacity;
    ++samples_;
}
} // namespace openfilter::limiter
