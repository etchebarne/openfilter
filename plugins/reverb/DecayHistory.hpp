#pragma once
#include <openfilter/ui/Analysis.hpp>

namespace openfilter::reverb {
// Main-thread visualization only. Each ribbon is a captured wet spectrum, not
// an extrapolated decay envelope. Sample serials retain timing across UI stalls.
class DecayHistory {
  public:
    static constexpr unsigned points = 256, capacity = 56;
    static constexpr double duration = 2.2, interval = .04;
    using Curve = std::array<double, points>;
    struct Slice {
        Curve wet{};
        double time = -duration;
    };
    ui::Spectrum spectrum;
    Curve wet{}, output{};
    DecayHistory() { clear(); }
    void clear() {
        spectrum = ui::Spectrum{};
        wet.fill(-120);
        output.fill(-120);
        slices_ = {};
        count_ = position_ = 0;
        serial_ = 0;
        time_ = idleSinceFrame_ = 0;
        lastCapture_ = -interval;
        rate_ = 0;
    }
    static double frequency(unsigned i) { return 20 * std::pow(1000., double(i) / (points - 1)); }
    bool update(const ui::AudioFrame &frame) {
        if (frame.serial == 0 || frame.serial == serial_)
            return false;
        if (frame.rate != rate_ || frame.serial < serial_)
            clear();
        const double elapsed = serial_ ? (frame.serial - serial_) * 1024. / frame.rate : 0;
        // Idle fading may already have aged this interval during a stalled host.
        time_ += std::max(0., elapsed - idleSinceFrame_);
        idleSinceFrame_ = 0;
        serial_ = frame.serial;
        rate_ = frame.rate;
        spectrum.update(frame);
        for (unsigned i = 0; i < points; ++i) {
            const double f = frequency(i);
            // Light log-frequency averaging removes distracting single-bin teeth.
            for (unsigned channel = 0; channel < 2; ++channel) {
                double level = -120;
                if (f < frame.rate * .475) {
                    const double lo =
                        std::pow(10., spectrum.at(channel, f / std::exp2(1. / 48)) / 10);
                    const double mid = std::pow(10., spectrum.at(channel, f) / 10);
                    const double hi =
                        std::pow(10., spectrum.at(channel, f * std::exp2(1. / 48)) / 10);
                    level = 10 * std::log10(std::max(1e-12, (lo + 2 * mid + hi) * .25));
                }
                (channel ? output : wet)[i] = level;
            }
        }
        if (time_ - lastCapture_ >= interval - 1e-9) {
            slices_[position_] = {wet, time_};
            position_ = (position_ + 1) % capacity;
            count_ = std::min(count_ + 1, capacity);
            lastCapture_ = time_;
        }
        return true;
    }
    void idle(double seconds) {
        time_ += seconds;
        idleSinceFrame_ += seconds;
        spectrum.decay(seconds);
        for (auto *curve : {&wet, &output})
            for (auto &db : *curve)
                db = std::max(-120., db - seconds * 48);
    }
    unsigned count() const { return count_; }
    const Slice &slice(unsigned oldestFirst) const {
        return slices_[(position_ + capacity - count_ + oldestFirst) % capacity];
    }
    double age(const Slice &slice) const { return std::max(0., time_ - slice.time); }

  private:
    std::array<Slice, capacity> slices_{};
    unsigned position_ = 0, count_ = 0;
    uint64_t serial_ = 0;
    double time_ = 0, lastCapture_ = -interval, rate_ = 0, idleSinceFrame_ = 0;
};
} // namespace openfilter::reverb
