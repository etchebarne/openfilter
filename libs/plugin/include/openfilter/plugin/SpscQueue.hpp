#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace openfilter::plugin {
// One producer, one consumer; no allocation, retries or blocking on either side.
template <class T, size_t Capacity> class SpscQueue {
    static_assert(Capacity > 1);
    static_assert(std::atomic<size_t>::is_always_lock_free);
    std::array<T, Capacity> slots_{};
    alignas(64) std::atomic<size_t> write_{0};
    alignas(64) std::atomic<size_t> read_{0};

  public:
    bool push(const T &value) noexcept {
        const auto w = write_.load(std::memory_order_relaxed);
        const auto next = (w + 1) % Capacity;
        if (next == read_.load(std::memory_order_acquire))
            return false;
        slots_[w] = value;
        write_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T &value) noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire))
            return false;
        value = slots_[r];
        read_.store((r + 1) % Capacity, std::memory_order_release);
        return true;
    }
    // Consumer can defer removal if a downstream host event queue is full.
    bool peek(T &value) const noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire))
            return false;
        value = slots_[r];
        return true;
    }
};
} // namespace openfilter::plugin
