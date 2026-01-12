#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include "utils/thread_safe_queue.h"

using screamrouter::audio::utils::ThreadSafeQueue;

class ThreadSafeQueueTest : public ::testing::Test {
protected:
    ThreadSafeQueue<int> queue;
};

TEST_F(ThreadSafeQueueTest, InitiallyEmpty) {
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0u);
}

TEST_F(ThreadSafeQueueTest, PushAndPop) {
    queue.push(42);
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1u);
    
    int value = 0;
    bool success = queue.try_pop(value);
    EXPECT_TRUE(success);
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(queue.empty());
}

TEST_F(ThreadSafeQueueTest, TryPopEmpty) {
    int value = -1;
    bool success = queue.try_pop(value);
    EXPECT_FALSE(success);
    EXPECT_EQ(value, -1);  // Unchanged
}

TEST_F(ThreadSafeQueueTest, FIFOOrder) {
    queue.push(1);
    queue.push(2);
    queue.push(3);
    
    int value;
    queue.try_pop(value);
    EXPECT_EQ(value, 1);
    queue.try_pop(value);
    EXPECT_EQ(value, 2);
    queue.try_pop(value);
    EXPECT_EQ(value, 3);
}

TEST_F(ThreadSafeQueueTest, StopUnblocksBlockingPop) {
    std::atomic<bool> popped{false};
    std::atomic<bool> returned{false};
    
    std::thread consumer([this, &popped, &returned]() {
        int value;
        bool success = queue.pop(value);  // Blocking
        popped = success;
        returned = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(returned);  // Should still be blocked
    
    queue.stop();
    consumer.join();
    
    EXPECT_TRUE(returned);
    EXPECT_FALSE(popped);  // Queue was empty when stopped
    EXPECT_TRUE(queue.is_stopped());
}

TEST_F(ThreadSafeQueueTest, PushBoundedDropOldest) {
    using PushResult = ThreadSafeQueue<int>::PushResult;
    
    queue.push(1);
    queue.push(2);
    queue.push(3);
    
    // Push with max_size=3, should drop oldest
    auto result = queue.push_bounded(4, 3, true);
    EXPECT_EQ(result, PushResult::DroppedOldest);
    EXPECT_EQ(queue.size(), 3u);
    
    // Verify oldest (1) was dropped
    int value;
    queue.try_pop(value);
    EXPECT_EQ(value, 2);  // Not 1
}

TEST_F(ThreadSafeQueueTest, PushBoundedQueueFull) {
    using PushResult = ThreadSafeQueue<int>::PushResult;
    
    queue.push(1);
    queue.push(2);
    queue.push(3);
    
    // Push with max_size=3, drop_oldest=false
    auto result = queue.push_bounded(4, 3, false);
    EXPECT_EQ(result, PushResult::QueueFull);
    EXPECT_EQ(queue.size(), 3u);  // Unchanged
}

TEST_F(ThreadSafeQueueTest, PushBoundedNormal) {
    using PushResult = ThreadSafeQueue<int>::PushResult;
    
    queue.push(1);
    
    auto result = queue.push_bounded(2, 3, false);
    EXPECT_EQ(result, PushResult::Pushed);
    EXPECT_EQ(queue.size(), 2u);
}

TEST_F(ThreadSafeQueueTest, PushAfterStop) {
    queue.stop();
    queue.push(42);  // Should not crash, just ignored
    EXPECT_TRUE(queue.empty());  // Not added
}

TEST_F(ThreadSafeQueueTest, ConcurrentProducerConsumer) {
    const int num_items = 1000;
    std::atomic<int> consumed_sum{0};
    
    std::thread producer([this, num_items]() {
        for (int i = 0; i < num_items; ++i) {
            queue.push(i);
        }
    });
    
    std::thread consumer([this, num_items, &consumed_sum]() {
        int count = 0;
        while (count < num_items) {
            int value;
            if (queue.try_pop(value)) {
                consumed_sum += value;
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    // Sum of 0..999 = 999*1000/2 = 499500
    EXPECT_EQ(consumed_sum, 499500);
    EXPECT_TRUE(queue.empty());
}
