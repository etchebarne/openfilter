#pragma once
#include <array>
#include <atomic>

namespace openfilter::plugin {
// Latest coherent snapshot, one producer and one consumer. Each owns a slot;
// the middle slot exchanges ownership atomically. Neither side retries or waits.
template <class T> class TripleBuffer {
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    std::array<T, 3> slots_{};
    unsigned back_ = 2;               // producer only
    unsigned front_ = 0;              // consumer only
    std::atomic<unsigned> middle_{1}; // bit 2: a new snapshot is available
  public:
    void publish(const T &value) noexcept {
        slots_[back_] = value;
        back_ = middle_.exchange(back_ | 4u, std::memory_order_acq_rel) & 3u;
    }
    const T &read() noexcept {
        if (middle_.load(std::memory_order_acquire) & 4u)
            front_ = middle_.exchange(front_, std::memory_order_acq_rel) & 3u;
        return slots_[front_];
    }
};
} // namespace openfilter::plugin
