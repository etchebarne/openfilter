#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <numbers>
#include <openfilter/plugin/TripleBuffer.hpp>

namespace openfilter::ui {
inline constexpr unsigned fftSize = 4096;
struct AudioFrame {
    std::array<std::array<float, fftSize>, 4> samples{};
    double rate = 48000;
    uint64_t serial = 0;
};
// Audio writes a circular history. A bounded copy publishes every 1024 samples.
// FFTs and drawing are exclusively main-thread work; no audio-thread allocation.
class AnalysisTap {
    AudioFrame frame_{};
    unsigned position_ = 0, sincePublish_ = 0;

  public:
    std::atomic<bool> enabled{false};
    plugin::TripleBuffer<AudioFrame> frames;
    void reset(double rate) noexcept {
        frame_.samples = {};
        frame_.rate = rate;
        position_ = sincePublish_ = 0;
    }
    void sample(double il, double ir, double ol, double oright) noexcept {
        const double values[]{il, ir, ol, oright};
        for (unsigned c = 0; c < 4; ++c)
            frame_.samples[c][position_] = static_cast<float>(
                std::isfinite(values[c]) ? std::clamp(values[c], -1e12, 1e12) : 0);
        position_ = (position_ + 1) % fftSize;
        if (++sincePublish_ < 1024)
            return;
        sincePublish_ = 0;
        AudioFrame ordered;
        ordered.rate = frame_.rate;
        ordered.serial = ++frame_.serial;
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned n = 0; n < fftSize; ++n)
                ordered.samples[c][n] = frame_.samples[c][(position_ + n) % fftSize];
        frames.publish(ordered);
    }
};

class Spectrum {
    std::array<std::complex<double>, fftSize> work_{};
    std::array<std::array<double, fftSize / 2 + 1>, 4> power_{};
    uint64_t serial_ = 0;
    void transform(const std::array<float, fftSize> &input) {
        for (unsigned i = 0; i < fftSize; ++i)
            work_[i] = input[i] * (0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / fftSize));
        for (unsigned i = 1, j = 0; i < fftSize; ++i) {
            unsigned bit = fftSize >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(work_[i], work_[j]);
        }
        for (unsigned length = 2; length <= fftSize; length <<= 1) {
            const auto step = std::polar(1.0, -2 * std::numbers::pi / length);
            for (unsigned offset = 0; offset < fftSize; offset += length) {
                std::complex<double> w = 1;
                for (unsigned j = 0; j < length / 2; ++j) {
                    const auto a = work_[offset + j], b = work_[offset + j + length / 2] * w;
                    work_[offset + j] = a + b;
                    work_[offset + j + length / 2] = a - b;
                    w *= step;
                }
            }
        }
    }

  public:
    std::array<std::array<double, fftSize / 2 + 1>, 2> db{};
    double rate = 48000;
    Spectrum() {
        for (auto &channel : db)
            channel.fill(-120);
    }
    bool update(const AudioFrame &frame) {
        if (frame.serial == serial_)
            return false;
        serial_ = frame.serial;
        rate = frame.rate;
        for (unsigned c = 0; c < 4; ++c) {
            transform(frame.samples[c]);
            for (unsigned k = 0; k <= fftSize / 2; ++k)
                power_[c][k] = std::norm(work_[k]) * 16 / (double(fftSize) * fftSize);
        }
        for (unsigned c = 0; c < 2; ++c)
            for (unsigned k = 0; k <= fftSize / 2; ++k) {
                const double target =
                    10 *
                    std::log10(std::max(1e-12, (power_[2 * c][k] + power_[2 * c + 1][k]) * .5));
                db[c][k] += (target > db[c][k] ? .75 : .18) * (target - db[c][k]);
            }
        return true;
    }
    void decay(double seconds) {
        for (auto &channel : db)
            for (auto &level : channel)
                level = std::max(-120., level - seconds * 48);
    }
    double at(unsigned channel, double hz) const {
        const double bin = std::clamp(hz * fftSize / rate, 1.0, double(fftSize / 2 - 1));
        const unsigned i = static_cast<unsigned>(bin);
        const double t = bin - i;
        return db[channel][i] * (1 - t) + db[channel][i + 1] * t;
    }
};
} // namespace openfilter::ui
