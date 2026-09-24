#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <openfilter/plugin/SpscQueue.hpp>
#include <openfilter/ui/Analysis.hpp>
namespace openfilter::deesser {
// Signed extrema preserve waveform shape and never cancel anti-phase stereo.
struct MeterFrame {
    double positive = 0, negative = 0, detectedPositive = 0, detectedNegative = 0;
};
struct MeterTap {
    std::atomic<bool> enabled{false};
    openfilter::plugin::SpscQueue<MeterFrame, 1024> frames;
    unsigned interval = 240, count = 0;
    MeterFrame pending{};
    ui::AnalysisTap spectrum;
    void reset(double rate) noexcept {
        spectrum.reset(rate);
        interval = std::max(1u, static_cast<unsigned>(rate * .005));
        count = 0;
        pending = {};
    }
    void sample(double left, double right, double gr, double detectorLeft,
                double detectorRight) noexcept {
        spectrum.sample(detectorLeft, detectorRight, 0, 0);
        auto clean = [](double v) { return std::isfinite(v) ? v : 0.; };
        pending.positive = std::max({pending.positive, clean(left), clean(right)});
        pending.negative = std::min({pending.negative, clean(left), clean(right)});
        // Release alone must not paint an entire following vowel bright green:
        // the overlay's height comes from the delayed, filtered detector audio.
        if (gr > .1) {
            pending.detectedPositive =
                std::max({pending.detectedPositive, clean(detectorLeft), clean(detectorRight)});
            pending.detectedNegative =
                std::min({pending.detectedNegative, clean(detectorLeft), clean(detectorRight)});
        }
        if (++count >= interval) {
            frames.push(pending);
            pending = {};
            count = 0;
        }
    }
};
} // namespace openfilter::deesser
