#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace openfilter::limiter {
// Original linear-phase half-band FIR. N = 4m+1 gives an exact integer
// round-trip delay. Zero taps and symmetric pairs are omitted from convolution.
// Coefficients are constructed only at prepare; histories are fixed storage.
template <unsigned N> class HalfBand {
    static_assert(N % 4 == 1);
    static constexpr unsigned middle = (N - 1) / 2, count = middle;
    std::array<double, count> coefficients_{};
    std::array<double, 2 * (count + 1)> upHistory_{};
    std::array<double, 2 * N> downHistory_{};
    unsigned upPosition_ = 0, downPosition_ = 0;
    static double bessel(double x) noexcept {
        double sum = 1, term = 1;
        for (unsigned k = 1; k <= 40; ++k) {
            term *= x * x / (4 * k * k);
            sum += term;
        }
        return sum;
    }
    void push(double x) noexcept {
        downPosition_ = (downPosition_ + N - 1) % N;
        downHistory_[downPosition_] = downHistory_[downPosition_ + N] = x;
    }

  public:
    void prepare() noexcept {
        const double normal = bessel(10);
        double sum = 0;
        for (unsigned k = 0; k < count; ++k) {
            const double t = double(2 * k + 1) - middle;
            const double window =
                bessel(10 * std::sqrt(std::max(0., 1 - t * t / (middle * middle)))) / normal;
            coefficients_[k] = std::sin(std::numbers::pi * t / 2) / (std::numbers::pi * t) * window;
            sum += coefficients_[k];
        }
        // Each phase has exact unit DC gain; the even phase is a delayed wire.
        for (auto &c : coefficients_)
            c *= .5 / sum;
        upHistory_.fill(0);
        downHistory_.fill(0);
        upPosition_ = downPosition_ = 0;
    }
    std::array<double, 2> up(double x) noexcept {
        constexpr unsigned length = count + 1;
        upPosition_ = (upPosition_ + length - 1) % length;
        upHistory_[upPosition_] = upHistory_[upPosition_ + length] = x;
        const auto *h = upHistory_.data() + upPosition_;
        std::array<double, 4> sums{};
        for (unsigned k = 0; k < count / 2; k += 4)
            for (unsigned j = 0; j < 4; ++j)
                sums[j] += coefficients_[k + j] * (h[k + j] + h[count - 1 - k - j]);
        return {h[middle / 2], 2 * ((sums[0] + sums[1]) + (sums[2] + sums[3]))};
    }
    double down(double even, double odd) noexcept {
        push(even);
        const auto *h = downHistory_.data() + downPosition_;
        std::array<double, 4> sums{};
        for (unsigned k = 0; k < count / 2; k += 4)
            for (unsigned j = 0; j < 4; ++j)
                sums[j] += coefficients_[k + j] * (h[2 * (k + j) + 1] + h[N - 2 - 2 * (k + j)]);
        const double out = .5 * h[middle] + ((sums[0] + sums[1]) + (sums[2] + sums[3]));
        push(odd);
        return out;
    }
};

// Anti-alias filters at exactly Nyquist leave a half-amplitude boundary.
// This final band limit moves that transition below Nyquist, preventing the
// unbounded reconstruction ringing of abruptly gated alternating samples.
class BandLimit {
    static constexpr unsigned taps = 257;
    std::array<double, taps> coefficients_{};
    std::array<double, taps * 2> history_{};
    unsigned position_ = 0;

  public:
    static constexpr unsigned delay = 128;
    void prepare() noexcept {
        auto bessel = [](double x) {
            double sum = 1, term = 1;
            for (unsigned k = 1; k <= 40; ++k) {
                term *= x * x / (4 * k * k);
                sum += term;
            }
            return sum;
        };
        double sum = 0;
        for (unsigned k = 0; k < taps; ++k) {
            const double t = double(k) - delay;
            const double sinc =
                t == 0 ? .95 : std::sin(std::numbers::pi * .95 * t) / (std::numbers::pi * t);
            coefficients_[k] = sinc *
                               bessel(10 * std::sqrt(std::max(0., 1 - t * t / (delay * delay)))) /
                               bessel(10);
            sum += coefficients_[k];
        }
        for (auto &c : coefficients_)
            c /= sum;
        history_.fill(0);
        position_ = 0;
    }
    double sample(double x) noexcept {
        position_ = (position_ + taps - 1) % taps;
        history_[position_] = history_[position_ + taps] = x;
        const auto *h = history_.data() + position_;
        std::array<double, 4> sums{};
        for (unsigned k = 0; k < delay; k += 4)
            for (unsigned j = 0; j < 4; ++j)
                sums[j] += coefficients_[k + j] * (h[k + j] + h[taps - 1 - k - j]);
        return coefficients_[delay] * h[delay] + ((sums[0] + sums[1]) + (sums[2] + sums[3]));
    }
};
class PeakDetector {
    HalfBand<513> first_;
    HalfBand<33> second_;
    HalfBand<17> third_;

  public:
    // Full support of all three stages, rounded up at the input sample rate.
    static constexpr unsigned support = 266;
    void prepare() noexcept {
        first_.prepare();
        second_.prepare();
        third_.prepare();
    }
    double sample(double x) noexcept {
        double peak = std::abs(x);
        for (double a : first_.up(x))
            for (double b : second_.up(a))
                for (double c : third_.up(b))
                    peak = std::max(peak, std::abs(c));
        return peak;
    }
};
} // namespace openfilter::limiter
