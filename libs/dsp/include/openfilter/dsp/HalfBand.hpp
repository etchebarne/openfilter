#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numbers>
namespace openfilter::dsp {
// Original linear-phase half-band FIR. N = 4m+1 gives an exact integer
// round-trip delay. Zero taps and symmetric pairs are omitted from convolution.
// Coefficients are constructed only at prepare; histories are fixed storage.
template <unsigned N> class HalfBand {
    static_assert(N % 16 == 1, "Four-way symmetric convolution needs N = 16m + 1");
    static constexpr unsigned middle = (N - 1) / 2, count = middle;
    std::array<double, count> coefficients_{};
    static constexpr unsigned length = std::bit_ceil(count), mask = length - 1;
    std::array<double, 2 * length> upHistory_{}, oddHistory_{};
    std::array<double, length> evenHistory_{};
    unsigned upPosition_ = 0, downPosition_ = 0;
    static double bessel(double x) noexcept {
        double sum = 1, term = 1;
        for (unsigned k = 1; k <= 40; ++k) {
            term *= x * x / (4 * k * k);
            sum += term;
        }
        return sum;
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
        oddHistory_.fill(0);
        evenHistory_.fill(0);
        upPosition_ = downPosition_ = 0;
    }
    bool silent() const noexcept {
        const auto zero = [](const auto &a) {
            return std::all_of(a.begin(), a.end(), [](double x) { return x == 0; });
        };
        return zero(upHistory_) && zero(oddHistory_) && zero(evenHistory_);
    }
    std::array<double, 2> up(double x) noexcept {
        upPosition_ = (upPosition_ - 1) & mask;
        upHistory_[upPosition_] = upHistory_[upPosition_ + length] = x;
        const auto *h = upHistory_.data() + upPosition_;
        std::array<double, 4> sums{};
        for (unsigned k = 0; k < count / 2; k += 4)
            for (unsigned j = 0; j < 4; ++j)
                sums[j] += coefficients_[k + j] * (h[k + j] + h[count - 1 - k - j]);
        return {h[middle / 2], 2 * ((sums[0] + sums[1]) + (sums[2] + sums[3]))};
    }
    double down(double even, double odd) noexcept {
        downPosition_ = (downPosition_ - 1) & mask;
        evenHistory_[downPosition_] = even;
        // Odd polyphase taps see the previous odd sample. Push this call's odd
        // input only after convolution, preserving the original decimation phase.
        const auto *h = oddHistory_.data() + downPosition_ + 1;
        std::array<double, 4> sums{};
        for (unsigned k = 0; k < count / 2; k += 4)
            for (unsigned j = 0; j < 4; ++j)
                sums[j] += coefficients_[k + j] * (h[k + j] + h[count - 1 - k - j]);
        const double out = .5 * evenHistory_[(downPosition_ + middle / 2) & mask] +
                           ((sums[0] + sums[1]) + (sums[2] + sums[3]));
        oddHistory_[downPosition_] = oddHistory_[downPosition_ + length] = odd;
        return out;
    }
};

} // namespace openfilter::dsp
