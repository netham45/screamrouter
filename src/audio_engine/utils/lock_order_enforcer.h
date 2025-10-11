#pragma once

#include <mutex>
#include <vector>
#include <thread>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <algorithm>

namespace audio_engine {
namespace utils {

/**
 * Lock Level Hierarchy
 * ====================
 * 
 * This enum defines the global lock ordering policy for the audio engine.
 * Locks must be acquired in ascending order of their level values to prevent deadlocks.
 * 
 * RULES:
 * 1. Always acquire manager-level locks before component locks
 * 2. Always acquire component locks before queue/buffer locks  
 * 3. Never acquire a lower-level lock while holding a higher-level lock
 * 
 * HIERARCHY (lower numbers = acquired first):
 * - Manager locks (100-199): High-level coordination locks
 * - Component locks (300-399): Processing component locks
 * - Queue/Buffer locks (400-499): Low-level data structure locks
 */
enum class LockLevel : int {
    // Manager-level locks (acquired first)
    AUDIO_MANAGER = 100,        // Main audio manager lock
    TIMESHIFT_MANAGER = 200,     // Timeshift buffer manager lock
    
    // Component-level locks
    SOURCE_PROCESSOR = 300,      // Audio source processor lock
    SINK_MIXER = 300,           // Audio sink mixer lock (same level as source)
    
    // Queue and buffer locks (acquired last)
    QUEUE_BUFFER = 400          // Generic queue/buffer lock
};

/**
 * LockOrderEnforcer
 * =================
 * 
 * Static class that enforces lock ordering at runtime to prevent deadlocks.
 * Uses thread-local storage to track the lock acquisition order for each thread.
 * 
 * In debug builds, this will detect and abort on lock order violations.
 * In release builds, the overhead is minimal (just thread-local vector operations).
 */
class LockOrderEnforcer {
public:
    /**
     * Record that a lock at the given level is being acquired.
     * Checks if this violates the lock ordering policy.
     * 
     * @param level The level of the lock being acquired
     * @param lock_name Optional name for better error messages
     */
    static void acquire(LockLevel level, const char* lock_name = nullptr) {
        auto& stack = getLockStack();
        
        // Check for lock order violation
        if (!stack.empty()) {
            LockLevel current_highest = stack.back().level;
            if (static_cast<int>(level) < static_cast<int>(current_highest)) {
                // Lock order violation detected!
                reportViolation(level, current_highest, lock_name, stack);
            }
        }
        
        // Add to the stack
        stack.push_back({level, lock_name ? lock_name : "unnamed"});
    }
    
    /**
     * Record that a lock at the given level is being released.
     * 
     * @param level The level of the lock being released
     */
    static void release(LockLevel level) {
        auto& stack = getLockStack();
        
        // Find and remove the lock from the stack
        // We search from the end since locks should be released in LIFO order
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            if (it->level == level) {
                stack.erase(std::next(it).base());
                return;
            }
        }
        
        // If we get here, we're releasing a lock we didn't acquire - this is also bad
        std::cerr << "ERROR: Attempting to release lock at level " 
                  << static_cast<int>(level) 
                  << " that was not acquired by this thread!" << std::endl;
    }
    
    /**
     * Clear all locks for the current thread.
     * Useful for cleanup in error paths.
     */
    static void clearThread() {
        getLockStack().clear();
    }
    
private:
    struct LockInfo {
        LockLevel level;
        std::string name;
    };
    
    // Thread-local storage for lock stack
    static std::vector<LockInfo>& getLockStack() {
        thread_local std::vector<LockInfo> stack;
        return stack;
    }
    
    // Report a lock order violation and abort
    [[noreturn]] static void reportViolation(
        LockLevel attempted_level,
        LockLevel current_highest,
        const char* attempted_name,
        const std::vector<LockInfo>& stack) {
        
        std::stringstream ss;
        ss << "\n========================================\n";
        ss << "FATAL: Lock Order Violation Detected!\n";
        ss << "========================================\n";
        ss << "Thread ID: " << std::this_thread::get_id() << "\n\n";
        
        ss << "Attempted to acquire lock:\n";
        ss << "  Level: " << static_cast<int>(attempted_level);
        if (attempted_name) {
            ss << " (" << attempted_name << ")";
        }
        ss << "\n\n";
        
        ss << "While holding higher-level lock:\n";
        ss << "  Level: " << static_cast<int>(current_highest) << "\n\n";
        
        ss << "Current lock stack (oldest to newest):\n";
        for (size_t i = 0; i < stack.size(); ++i) {
            ss << "  [" << i << "] Level " << static_cast<int>(stack[i].level)
               << " - " << stack[i].name << "\n";
        }
        
        ss << "\nLock Ordering Rules:\n";
        ss << "  - Acquire locks in ascending order of level values\n";
        ss << "  - Manager locks (100-199) before component locks (300-399)\n";
        ss << "  - Component locks before queue/buffer locks (400-499)\n";
        ss << "========================================\n";
        
        std::cerr << ss.str() << std::endl;
        std::abort();
    }
};

/**
 * OrderedLock
 * ===========
 * 
 * RAII wrapper that enforces lock ordering.
 * Use this as a drop-in replacement for std::scoped_lock when lock ordering matters.
 * 
 * Example:
 *   std::mutex my_mutex;
 *   OrderedLock lock(my_mutex, LockLevel::AUDIO_MANAGER);
 *   // ... critical section ...
 *   // Lock automatically released and order tracking updated on scope exit
 */
class OrderedLock {
public:
    /**
     * Construct an ordered lock.
     * 
     * @param mutex The mutex to lock
     * @param level The level of this lock in the hierarchy
     * @param name Optional name for debugging
     */
    OrderedLock(std::mutex& mutex, LockLevel level, const char* name = nullptr)
        : mutex_(mutex), level_(level), owns_lock_(false) {
        
        // Check ordering before acquiring
        LockOrderEnforcer::acquire(level_, name);
        
        // Now actually acquire the lock
        mutex_.lock();
        owns_lock_ = true;
    }
    
    // Disable copy
    OrderedLock(const OrderedLock&) = delete;
    OrderedLock& operator=(const OrderedLock&) = delete;
    
    // Enable move
    OrderedLock(OrderedLock&& other) noexcept
        : mutex_(other.mutex_), level_(other.level_), owns_lock_(other.owns_lock_) {
        other.owns_lock_ = false;
    }
    
    OrderedLock& operator=(OrderedLock&& other) noexcept {
        if (this != &other) {
            if (owns_lock_) {
                unlock();
            }
            mutex_ = std::ref(other.mutex_);
            level_ = other.level_;
            owns_lock_ = other.owns_lock_;
            other.owns_lock_ = false;
        }
        return *this;
    }
    
    ~OrderedLock() {
        if (owns_lock_) {
            unlock();
        }
    }
    
    /**
     * Manually unlock the mutex.
     * The lock order tracking is updated.
     */
    void unlock() {
        if (owns_lock_) {
            mutex_.get().unlock();
            LockOrderEnforcer::release(level_);
            owns_lock_ = false;
        }
    }
    
    /**
     * Check if this lock owns the mutex.
     */
    bool owns_lock() const {
        return owns_lock_;
    }
    
private:
    std::reference_wrapper<std::mutex> mutex_;
    LockLevel level_;
    bool owns_lock_;
};

/**
 * Convenience macro for creating ordered locks with automatic naming.
 * The name will be the stringified mutex variable name.
 * 
 * Usage:
 *   std::mutex audio_mutex;
 *   ORDERED_LOCK(audio_mutex, LockLevel::AUDIO_MANAGER);
 */
#define ORDERED_LOCK(mutex, level) \
    ::audio_engine::utils::OrderedLock _ordered_lock_##__LINE__(mutex, level, #mutex)

/**
 * Additional helper for creating multiple ordered locks.
 * Ensures they are acquired in the correct order.
 * 
 * Usage:
 *   ORDERED_LOCK_MULTI(lock1, mutex1, LockLevel::AUDIO_MANAGER,
 *                      lock2, mutex2, LockLevel::SOURCE_PROCESSOR);
 */
#define ORDERED_LOCK_MULTI(lock1_name, mutex1, level1, lock2_name, mutex2, level2) \
    static_assert(static_cast<int>(level1) <= static_cast<int>(level2), \
                  "Locks must be acquired in ascending order of levels"); \
    ::audio_engine::utils::OrderedLock lock1_name(mutex1, level1, #mutex1); \
    ::audio_engine::utils::OrderedLock lock2_name(mutex2, level2, #mutex2)

} // namespace utils
} // namespace audio_engine