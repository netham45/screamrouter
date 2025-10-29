/**
 * @file thread_safe_queue.h
 * @brief Defines a generic, thread-safe queue for inter-thread communication.
 * @details This file contains the `ThreadSafeQueue` class template, which provides a
 *          blocking, thread-safe queue implementation using a `std::deque`, `std::mutex`,
 *          and `std::condition_variable`. It's a fundamental utility for passing data
 *          between different audio components running in separate threads.
 */
#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <cstddef>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

namespace screamrouter {
namespace audio {
namespace utils {

/**
 * @class ThreadSafeQueue
 * @brief A template class for a thread-safe queue.
 * @tparam T The type of elements to be stored in the queue.
 */
template <typename T>
class ThreadSafeQueue {
public:
    enum class PushResult {
        Pushed,
        DroppedOldest,
        QueueStopped,
        QueueFull
    };

    /**
     * @brief Default constructor.
     */
    ThreadSafeQueue() : stop_requested_(false) {}

    // The queue is non-copyable and non-movable to enforce management via pointers.
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue(ThreadSafeQueue&&) = delete;
    ThreadSafeQueue& operator=(ThreadSafeQueue&&) = delete;

    /**
     * @brief Pushes an item onto the queue in a thread-safe manner.
     * @param item The item to push (will be moved into the queue).
     */
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_) {
                return;
            }
            queue_.push_back(std::move(item));
        }
        cond_.notify_one();
    }

    /**
     * @brief Attempts to push an item while enforcing a maximum size.
     * @param item Item to enqueue (moved on success).
     * @param max_size Maximum number of items allowed (0 disables the bound).
     * @param drop_oldest When true, the oldest queued item is discarded to make room.
     * @return Outcome describing whether the item was queued or dropped.
     */
    PushResult push_bounded(T item, std::size_t max_size, bool drop_oldest) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stop_requested_) {
            return PushResult::QueueStopped;
        }

        if (max_size > 0 && queue_.size() >= max_size) {
            if (!drop_oldest) {
                return PushResult::QueueFull;
            }
            queue_.pop_front();
            queue_.push_back(std::move(item));
            lock.unlock();
            cond_.notify_one();
            return PushResult::DroppedOldest;
        }

        queue_.push_back(std::move(item));
        lock.unlock();
        cond_.notify_one();
        return PushResult::Pushed;
    }

    /**
     * @brief Pops an item from the queue, blocking if the queue is empty.
     * @details This method will wait until an item is available or until `stop()` is called.
     * @param item A reference to store the popped item.
     * @return `true` if an item was successfully popped, `false` if the queue was stopped and is empty.
     */
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty() || stop_requested_; });

        if (stop_requested_ && queue_.empty()) {
            return false;
        }

        if (!queue_.empty()) {
            item = std::move(queue_.front());
            queue_.pop_front();
            return true;
        }
        return false;
    }

    /**
     * @brief Attempts to pop an item from the queue without blocking.
     * @param item A reference to store the popped item if successful.
     * @return `true` if an item was popped, `false` if the queue was empty.
     */
    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    /**
     * @brief Signals the queue to stop blocking operations.
     * @details This notifies all waiting threads to wake up. After `stop()` is called,
     *          `pop()` will return `false` once the queue becomes empty.
     */
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_requested_ = true;
        }
        cond_.notify_all();
    }

    /**
     * @brief Checks if the queue is currently empty.
     * @return `true` if the queue is empty, `false` otherwise.
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    /**
     * @brief Gets the current number of items in the queue.
     * @return The number of items in the queue.
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /**
     * @brief Checks if the queue has been stopped.
     * @return `true` if `stop()` has been called, `false` otherwise.
     */
    bool is_stopped() const {
        return stop_requested_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    std::deque<T> queue_;
    std::atomic<bool> stop_requested_;
};

} // namespace utils
} // namespace audio
} // namespace screamrouter
#endif // THREAD_SAFE_QUEUE_H
