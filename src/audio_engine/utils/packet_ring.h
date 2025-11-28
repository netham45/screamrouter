#ifndef PACKET_RING_H
#define PACKET_RING_H

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

namespace screamrouter {
namespace audio {
namespace utils {

/**
 * @brief Simple single-producer/single-consumer ring buffer with drop-oldest semantics on overflow.
 */
template <typename T>
class PacketRing {
public:
    explicit PacketRing(std::size_t capacity)
        : capacity_(capacity),
          buffer_(capacity),
          head_(0),
          tail_(0),
          drop_count_(0) {}

    bool push(const T& value) {
        return emplace(value);
    }

    bool push(T&& value) {
        return emplace(std::move(value));
    }

    bool pop(T& out) {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head == tail) {
            return false; // empty
        }
        out = std::move(buffer_[head]);
        const std::size_t next = (head + 1) % capacity_;
        head_.store(next, std::memory_order_release);
        return true;
    }

    std::size_t size() const {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (tail >= head) {
            return tail - head;
        }
        return capacity_ - head + tail;
    }

    std::size_t capacity() const { return capacity_; }

    std::size_t drop_count() const { return drop_count_.load(std::memory_order_relaxed); }

private:
    template <typename U>
    bool emplace(U&& value) {
        std::size_t head = head_.load(std::memory_order_acquire);
        std::size_t tail = tail_.load(std::memory_order_acquire);
        std::size_t next = (tail + 1) % capacity_;

        if (next == head) {
            // overflow: drop oldest
            head = (head + 1) % capacity_;
            head_.store(head, std::memory_order_release);
            drop_count_.fetch_add(1, std::memory_order_relaxed);
        }

        buffer_[tail] = std::forward<U>(value);
        tail_.store(next, std::memory_order_release);
        return true;
    }

    const std::size_t capacity_;
    std::vector<T> buffer_;
    std::atomic<std::size_t> head_;
    std::atomic<std::size_t> tail_;
    std::atomic<std::size_t> drop_count_;
};

} // namespace utils
} // namespace audio
} // namespace screamrouter

#endif // PACKET_RING_H
