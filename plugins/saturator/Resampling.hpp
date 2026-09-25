#pragma once
#include <openfilter/dsp/HalfBand.hpp>
namespace openfilter::saturator {
// The same five FIR stages and decimation phase as the initial engine.
class Cascade {
    dsp::HalfBand<129> first_;
    dsp::HalfBand<33> second_, fifth_;
    dsp::HalfBand<17> third_, fourth_;
    template <unsigned S> auto &stage() noexcept {
        if constexpr (S == 0)
            return first_;
        else if constexpr (S == 1)
            return second_;
        else if constexpr (S == 2)
            return third_;
        else if constexpr (S == 3)
            return fourth_;
        else
            return fifth_;
    }
    using Block = std::array<double, 32>;
    template <unsigned S, unsigned Count> void expand(const Block &in, Block &out) noexcept {
        auto &filter = stage<S>();
        for (unsigned n = 0; n < Count; ++n) {
            const auto pair = filter.up(in[n]);
            out[n * 2] = pair[0];
            out[n * 2 + 1] = pair[1];
        }
    }
    template <unsigned S, unsigned Count> void reduce(Block &samples) noexcept {
        auto &filter = stage<S>();
        for (unsigned n = 0; n < Count; ++n)
            samples[n] = filter.down(samples[2 * n], samples[2 * n + 1]);
    }

  public:
    void prepare() noexcept {
        first_.prepare();
        second_.prepare();
        third_.prepare();
        fourth_.prepare();
        fifth_.prepare();
    }
    bool silent() const noexcept {
        return first_.silent() && second_.silent() && third_.silent() && fourth_.silent() &&
               fifth_.silent();
    }
    template <class F> double process(double x, unsigned factor, const F &fn) noexcept {
        // Batch each stage in time order. Nonlinear evaluation is one contiguous
        // loop rather than a callback deep inside five nested resampling loops.
        Block a{}, b{};
        const auto first = first_.up(x);
        a[0] = first[0];
        a[1] = first[1];
        Block *samples = &a;
        if (factor >= 4) {
            expand<1, 2>(a, b);
            samples = &b;
        }
        if (factor >= 8) {
            expand<2, 4>(b, a);
            samples = &a;
        }
        if (factor >= 16) {
            expand<3, 8>(a, b);
            samples = &b;
        }
        if (factor >= 32) {
            expand<4, 16>(b, a);
            samples = &a;
        }
        for (unsigned n = 0; n < factor; ++n)
            (*samples)[n] = fn((*samples)[n]);
        if (factor >= 32)
            reduce<4, 16>(*samples);
        if (factor >= 16)
            reduce<3, 8>(*samples);
        if (factor >= 8)
            reduce<2, 4>(*samples);
        if (factor >= 4)
            reduce<1, 2>(*samples);
        reduce<0, 1>(*samples);
        return (*samples)[0];
    }
};
// A linear interpolation/decimation round trip is an FIR at the host rate.
// Its impulse is derived at activation from the original cascade, including
// decimation phase. This avoids running a second high-rate decimator for dry.
class MatchedDry {
  public:
    static constexpr unsigned taps = 153;
    using Kernel = std::array<double, taps>;
    static Kernel kernel(unsigned factor) noexcept {
        Cascade probe;
        probe.prepare();
        Kernel k{};
        for (unsigned n = 0; n < taps; ++n)
            k[n] = probe.process(n == 0 ? 1. : 0., factor, [](double v) { return v; });
        return k;
    }
    void reset() noexcept {
        history_.fill(0);
        position_ = 0;
    }
    bool silent() const noexcept {
        return std::all_of(history_.begin(), history_.end(), [](double x) { return x == 0; });
    }
    double process(double x, const Kernel &kernel, unsigned delay) noexcept {
        position_ = position_ ? position_ - 1 : taps - 1;
        history_[position_] = history_[position_ + taps] = x;
        const auto *h = history_.data() + position_;
        std::array<double, 4> sums{};
        // Linear-phase symmetry halves the host-rate convolution. The largest
        // observed rounding difference versus the original cascade is < 1e-14.
        unsigned n = 0;
        for (; n + 4 <= delay; n += 4)
            for (unsigned j = 0; j < 4; ++j)
                sums[j] += kernel[n + j] * (h[n + j] + h[2 * delay - n - j]);
        double out = ((sums[0] + sums[1]) + (sums[2] + sums[3])) + kernel[delay] * h[delay];
        for (; n < delay; ++n)
            out += kernel[n] * (h[n] + h[2 * delay - n]);
        return out;
    }

  private:
    std::array<double, 2 * taps> history_{};
    unsigned position_ = 0;
};
} // namespace openfilter::saturator
