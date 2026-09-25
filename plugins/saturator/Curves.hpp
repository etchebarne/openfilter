#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
namespace openfilter::saturator {
// Cubic Hermite evaluation of the original analytic functions. Tables are
// constructed during activation, never on the audio thread. No fast-math,
// reduced oversampling or replacement transfer function is involved.
class Curves {
    struct Segment {
        double a, b, c, d;
        double at(double t) const noexcept { return ((a * t + b) * t + c) * t + d; }
    };
    template <unsigned N> using Table = std::array<Segment, N>;
    Table<4096> tanh_{};
    Table<1024> atan_{};
    double bias_ = 0;
    template <unsigned N, class F, class D>
    static void fill(Table<N> &table, double limit, F fn, D derivative) noexcept {
        const double h = limit / N;
        for (unsigned n = 0; n < N; ++n) {
            const double x = n * h, y0 = fn(x), y1 = fn(x + h);
            const double d0 = h * derivative(x), d1 = h * derivative(x + h);
            table[n] = {2 * (y0 - y1) + d0 + d1, 3 * (y1 - y0) - 2 * d0 - d1, d0, y0};
        }
    }
    double tanh(double x) const noexcept {
        const double v = std::abs(x);
        if (v >= 12)
            return std::tanh(x);
        const double position = v * (4096. / 12.);
        const auto n = static_cast<unsigned>(position);
        return std::copysign(tanh_[n].at(position - n), x);
    }
    double atan(double x) const noexcept {
        const double v = std::abs(x), t = v > 1 ? 1 / v : v;
        const double position = t * 1024;
        const unsigned n = std::min(1023u, static_cast<unsigned>(position));
        const double value = atan_[n].at(position - n);
        return std::copysign(v > 1 ? std::numbers::pi / 2 - value : value, x);
    }

  public:
    Curves() noexcept {
        fill<4096>(
            tanh_, 12, [](double x) { return std::tanh(x); },
            [](double x) {
                const double t = std::tanh(x);
                return 1 - t * t;
            });
        fill<1024>(
            atan_, 1, [](double x) { return std::atan(x); },
            [](double x) { return 1 / (1 + x * x); });
        bias_ = tanh(.35);
    }
    double operator()(unsigned style, double x) const noexcept {
        switch (style) {
        case 1:
            return atan(x);
        case 2:
            return x / std::sqrt(1 + x * x);
        case 3: {
            constexpr double bias = .35;
            const double t = std::tanh(bias);
            return (tanh(x + bias) - bias_) / (1 - t * t);
        }
        default:
            return tanh(x);
        }
    }
};
} // namespace openfilter::saturator
