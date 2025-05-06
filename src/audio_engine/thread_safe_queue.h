#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory> // For std::move

namespace screamrouter {
namespace utils {

template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() : stop_requested_(false) {}

    // Non-copyable and non-movable for simplicity, manage via pointers (e.g., shared_ptr)
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue(ThreadSafeQueue&&) = delete;
    ThreadSafeQueue& operator=(ThreadSafeQueue&&) = delete;

    /**
     * @brief Pushes an item onto the queue. Thread-safe.
     * @param item The item to push (will be moved).
     */
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_) {
                // Optional: Throw an exception or handle if pushing after stop is invalid
                return;
            }
            queue_.push_back(std::move(item));
        } // Mutex released here
        cond_.notify_one();
    }

    /**
     * @brief Pops an item from the queue. Blocks if the queue is empty until an item
     *        is available or stop() is called. Thread-safe.
     * @param item Reference to store the popped item.
     * @return true if an item was successfully popped, false if the queue was stopped.
     */
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty() || stop_requested_; });

        if (stop_requested_ && queue_.empty()) {
            return false; // Stopped and no items left
        }

        if (!queue_.empty()) {
            item = std::move(queue_.front());
            queue_.pop_front();
            return true;
        }

        // Should not be reached if logic is correct, but handle defensively
        return false;
    }

    /**
     * @brief Attempts to pop an item from the queue without blocking. Thread-safe.
     * @param item Reference to store the popped item if successful.
     * @return true if an item was popped, false if the queue was empty.
     */
    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty() || stop_requested_) { // Don't pop if stopped, even if items remain? Or allow draining? Let's allow draining.
             if (queue_.empty()) {
                return false;
             }
        }
        item = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    /**
     * @brief Signals the queue to stop blocking operations and notifies waiting threads.
     *        After calling stop(), subsequent push operations might be ignored or throw,
     *        and pop operations will return false once the queue is empty. Thread-safe.
     */
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_requested_ = true;
        } // Mutex released here
        cond_.notify_all(); // Wake up all waiting threads
    }

    /**
     * @brief Checks if the queue is currently empty. Thread-safe.
     * @return true if the queue is empty, false otherwise.
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    /**
     * @brief Gets the current number of items in the queue. Thread-safe.
     * @return The number of items in the queue.
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /**
     * @brief Checks if the queue has been stopped. Thread-safe.
     * @return true if stop() has been called, false otherwise.
     */
    bool is_stopped() const {
        // No lock needed for atomic read
        return stop_requested_;
    }

private:
    mutable std::mutex mutex_; // Mutable to allow locking in const methods like empty() and size()
    std::condition_variable cond_;
    std::deque<T> queue_;
    std::atomic<bool> stop_requested_;
};

} // namespace utils
} // namespace screamrouter

#endif // THREAD_SAFE_QUEUE_H
