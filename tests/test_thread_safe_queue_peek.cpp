/**
 * @file test_thread_safe_queue_peek.cpp
 * @brief Unit tests for ThreadSafeQueue peek() method
 */

#include "../src/audio_engine/utils/thread_safe_queue.h"
#include <cassert>
#include <iostream>
#include <thread>

using namespace screamrouter::audio::utils;

/**
 * @brief Test basic peek functionality
 */
void test_peek_basic() {
    ThreadSafeQueue<int> queue;
    
    // Test peek on empty queue
    int value = 0;
    assert(!queue.peek(value) && "Peek on empty queue should return false");
    
    // Add items
    queue.push(10);
    queue.push(20);
    queue.push(30);
    
    // Peek should return first item without removing it
    assert(queue.peek(value) && "Peek should succeed");
    assert(value == 10 && "Peek should return first item");
    assert(queue.size() == 3 && "Peek should not remove items");
    
    // Peek again - should still be same value
    assert(queue.peek(value) && "Peek should succeed again");
    assert(value == 10 && "Peek should return same item");
    assert(queue.size() == 3 && "Queue size should remain unchanged");
    
    // Pop and verify
    int popped;
    assert(queue.pop(popped) && "Pop should succeed");
    assert(popped == 10 && "Pop should return first item");
    assert(queue.size() == 2 && "Size should decrease after pop");
    
    // Peek after pop
    assert(queue.peek(value) && "Peek should succeed after pop");
    assert(value == 20 && "Peek should return new first item");
    
    std::cout << "✓ test_peek_basic passed" << std::endl;
}

/**
 * @brief Test peek with try_pop
 */
void test_peek_with_try_pop() {
    ThreadSafeQueue<int> queue;
    
    queue.push(100);
    queue.push(200);
    
    int peek_value, pop_value;
    
    // Peek first
    assert(queue.peek(peek_value) && "Peek should succeed");
    assert(peek_value == 100 && "Peek should return 100");
    
    // try_pop should get the same value
    assert(queue.try_pop(pop_value) && "try_pop should succeed");
    assert(pop_value == 100 && "try_pop should return same value as peek");
    
    // Next peek should return next item
    assert(queue.peek(peek_value) && "Peek should succeed");
    assert(peek_value == 200 && "Peek should return 200");
    
    std::cout << "✓ test_peek_with_try_pop passed" << std::endl;
}

/**
 * @brief Test peek thread safety
 */
void test_peek_thread_safety() {
    ThreadSafeQueue<int> queue;
    
    // Push some items
    for (int i = 0; i < 10; i++) {
        queue.push(i);
    }
    
    // Thread 1: Repeatedly peek
    std::thread peeker([&queue]() {
        for (int i = 0; i < 100; i++) {
            int value;
            if (queue.peek(value)) {
                // Just peek, don't do anything
            }
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });
    
    // Thread 2: Pop items
    std::thread popper([&queue]() {
        for (int i = 0; i < 5; i++) {
            int value;
            queue.try_pop(value);
            std::this_thread::sleep_for(std::chrono::microseconds(20));
        }
    });
    
    peeker.join();
    popper.join();
    
    // Verify queue is in valid state
    assert(queue.size() == 5 && "Queue should have 5 items remaining");
    
    std::cout << "✓ test_peek_thread_safety passed" << std::endl;
}

/**
 * @brief Test peek with complex types
 */
struct TestData {
    int id;
    std::string name;
    
    TestData() : id(0), name("") {}
    TestData(int i, std::string n) : id(i), name(n) {}
};

void test_peek_complex_type() {
    ThreadSafeQueue<TestData> queue;
    
    // Test empty queue
    TestData data;
    assert(!queue.peek(data) && "Peek on empty queue should return false");
    
    // Push complex objects
    queue.push(TestData(1, "first"));
    queue.push(TestData(2, "second"));
    
    // Peek should copy the object
    TestData peeked;
    assert(queue.peek(peeked) && "Peek should succeed");
    assert(peeked.id == 1 && "Peeked ID should be 1");
    assert(peeked.name == "first" && "Peeked name should be 'first'");
    assert(queue.size() == 2 && "Queue size should remain 2");
    
    std::cout << "✓ test_peek_complex_type passed" << std::endl;
}

int main() {
    std::cout << "Running ThreadSafeQueue peek() tests..." << std::endl;
    
    try {
        test_peek_basic();
        test_peek_with_try_pop();
        test_peek_thread_safety();
        test_peek_complex_type();
        
        std::cout << "\n✓ All ThreadSafeQueue peek() tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}