#pragma once
#include <atomic>
#include <cmath>
#include <openfilter/plugin/SpscQueue.hpp>
namespace openfilter::compressor {
struct MeterFrame {
    double input = 0, output = 0, reduction = 0;
};
struct MeterTap {
    std::atomic<bool> enabled{false};
    openfilter::plugin::SpscQueue<MeterFrame, 1024> frames;
    unsigned interval = 480, count = 0;
    MeterFrame pending{};
    void reset(double rate) noexcept {
        interval = std::max(1u, static_cast<unsigned>(rate * .01));
        count = 0;
        pending = {};
    }
    void sample(double in, double out, double gr) noexcept {
        pending.input = std::max(pending.input, std::abs(in));
        pending.output = std::max(pending.output, std::abs(out));
        pending.reduction = std::max(pending.reduction, gr);
        if (++count >= interval) {
            frames.push(pending);
            pending = {};
            count = 0;
        }
    }
};
} // namespace openfilter::compressor
