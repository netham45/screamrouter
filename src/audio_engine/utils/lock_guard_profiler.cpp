/**
 * @file lock_guard_profiler.cpp
 * @brief Implementation of the LockGuardProfiler and LockWatchdog classes for deadlock detection.
 */

#include "lock_guard_profiler.h"
#include <sstream>
#include <iomanip>
#include <set>
#include <cstdlib>  // for std::abort()

namespace screamrouter {
namespace audio {
namespace utils {

// Thread-local storage for tracking locks held by each thread
thread_local std::unordered_map<void*, std::pair<std::string, int>> tls_held_locks;
thread_local std::set<void*> tls_held_mutexes;
thread_local int tls_lock_count = 0;

// Global registry of all locks for debugging
std::mutex g_lock_registry_mutex;
std::unordered_map<std::thread::id, std::vector<std::string>> g_thread_lock_registry;

//=============================================================================
// LockWatchdog Implementation
//=============================================================================

LockWatchdog& LockWatchdog::getInstance() {
    static LockWatchdog instance;
    return instance;
}

LockWatchdog::LockWatchdog() : running_(true) {
    watchdog_thread_ = std::thread(&LockWatchdog::watchdogLoop, this);
}

LockWatchdog::~LockWatchdog() {
    running_ = false;
    if (watchdog_thread_.joinable()) {
        watchdog_thread_.join();
    }
}

void LockWatchdog::registerLock(LockGuardProfiler* profiler, LockType type, 
                                const char* file, int line, 
                                std::chrono::steady_clock::time_point start_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_locks_[profiler] = {type, file, line, start_time};
}

void LockWatchdog::unregisterLock(LockGuardProfiler* profiler) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_locks_.erase(profiler);
}

void LockWatchdog::watchdogLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        
        for (const auto& [profiler, info] : active_locks_) {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - info.start_time);
            
            auto threshold = (info.type == LockType::WRITE) ? 
                WRITE_LOCK_WATCHDOG_THRESHOLD : READ_LOCK_WATCHDOG_THRESHOLD;
            
            if (duration > threshold) {
                std::stringstream ss;
                ss << "DEADLOCK DETECTED: " 
                   << (info.type == LockType::WRITE ? "Write" : "Read")
                   << " lock held for " << duration.count() << "ms"
                   << " at " << info.file << ":" << info.line
                   << " (threshold: " << threshold.count() << "ms)";
                
                // Dump all currently held locks for debugging
                dumpAllHeldLocks();
                
                LOG_CPP_ERROR("%s", ss.str().c_str());
                std::abort(); // Fatal error - terminate the program
            }
        }
    }
}

void LockWatchdog::dumpAllHeldLocks() {
    std::lock_guard<std::mutex> lock(g_lock_registry_mutex);
    
    LOG_CPP_ERROR("=== DUMPING ALL HELD LOCKS ===");
    for (const auto& [thread_id, locks] : g_thread_lock_registry) {
        if (!locks.empty()) {
            std::stringstream ss;
            ss << "Thread " << thread_id << " holds " << locks.size() << " lock(s):";
            for (const auto& lock_info : locks) {
                ss << "\n  - " << lock_info;
            }
            LOG_CPP_ERROR("%s", ss.str().c_str());
        }
    }
    LOG_CPP_ERROR("=== END LOCK DUMP ===");
}

//=============================================================================
// LockGuardProfiler Implementation
//=============================================================================

LockGuardProfiler::LockGuardProfiler(std::shared_mutex& m, LockType type, 
                                     const char* file, int line)
    : mutex_(m), lock_type_(type), file_(file), line_(line) {
    
    // Check for self-deadlock (same thread trying to lock non-recursive mutex twice)
    void* mutex_ptr = static_cast<void*>(&mutex_);
    
    if (tls_held_mutexes.find(mutex_ptr) != tls_held_mutexes.end()) {
        std::stringstream ss;
        ss << "SELF-DEADLOCK DETECTED: Thread " << std::this_thread::get_id()
           << " attempting to lock mutex at " << mutex_ptr 
           << " which it already holds!"
           << "\n  Current lock attempt: " << file << ":" << line
           << "\n  Previously locked at: " << tls_held_locks[mutex_ptr].first 
           << ":" << tls_held_locks[mutex_ptr].second;
        
        // Dump all locks before fatal error
        LockWatchdog::getInstance().dumpAllHeldLocks();
        LOG_CPP_ERROR("%s", ss.str().c_str());
        std::abort(); // Fatal error - terminate the program
    }
    
    // Record the start time for profiling
    start_time_ = std::chrono::steady_clock::now();
    
    // Acquire the lock based on type
    if (lock_type_ == LockType::WRITE) {
        unique_lock_ = std::unique_lock<std::shared_mutex>(mutex_);
    } else {
        shared_lock_ = std::shared_lock<std::shared_mutex>(mutex_);
    }
    
    // Track this lock in thread-local storage
    tls_held_mutexes.insert(mutex_ptr);
    tls_held_locks[mutex_ptr] = {std::string(file), line};
    tls_lock_count++;
    
    // Update global registry for debugging
    {
        std::lock_guard<std::mutex> lock(g_lock_registry_mutex);
        std::stringstream ss;
        ss << (lock_type_ == LockType::WRITE ? "W" : "R") 
           << "@" << mutex_ptr << " (" << file << ":" << line << ")";
        g_thread_lock_registry[std::this_thread::get_id()].push_back(ss.str());
    }
    
    // Warn if holding multiple locks (potential deadlock risk)
    if (tls_lock_count > 1) {
        std::stringstream ss;
        ss << "WARNING: Thread " << std::this_thread::get_id() 
           << " now holds " << tls_lock_count << " locks simultaneously"
           << " (latest: " << file << ":" << line << ")";
        
        // List all held locks
        ss << "\n  Held locks:";
        for (const auto& [ptr, info] : tls_held_locks) {
            ss << "\n    - " << ptr << " at " << info.first << ":" << info.second;
        }
        
        LOG_CPP_WARNING("%s", ss.str().c_str());
    }
    
    // Register with watchdog
    LockWatchdog::getInstance().registerLock(this, lock_type_, file_, line_, start_time_);
}

LockGuardProfiler::~LockGuardProfiler() {
    // Calculate lock duration
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time_);
    
    // Check against threshold and log if exceeded
    auto threshold = (lock_type_ == LockType::WRITE) ? 
        WRITE_LOCK_THRESHOLD : READ_LOCK_THRESHOLD;
    
    if (duration > threshold) {
        std::stringstream ss;
        ss << "Long " << (lock_type_ == LockType::WRITE ? "write" : "read")
           << " lock held for " << duration.count() << "ms"
           << " at " << file_ << ":" << line_
           << " (threshold: " << threshold.count() << "ms)";
        LOG_CPP_WARNING("%s", ss.str().c_str());
    }
    
    // Unregister from watchdog
    LockWatchdog::getInstance().unregisterLock(this);
    
    // Remove from thread-local tracking
    void* mutex_ptr = static_cast<void*>(&mutex_);
    tls_held_mutexes.erase(mutex_ptr);
    tls_held_locks.erase(mutex_ptr);
    tls_lock_count--;
    
    // Update global registry
    {
        std::lock_guard<std::mutex> lock(g_lock_registry_mutex);
        auto& locks = g_thread_lock_registry[std::this_thread::get_id()];
        if (!locks.empty()) {
            // Remove the last matching lock entry
            std::stringstream ss;
            ss << (lock_type_ == LockType::WRITE ? "W" : "R") 
               << "@" << mutex_ptr << " (" << file_ << ":" << line_ << ")";
            auto lock_str = ss.str();
            
            auto it = std::find(locks.rbegin(), locks.rend(), lock_str);
            if (it != locks.rend()) {
                locks.erase(std::next(it).base());
            }
        }
        
        // Clean up empty entries
        if (locks.empty()) {
            g_thread_lock_registry.erase(std::this_thread::get_id());
        }
    }
    
    // Release the lock (happens automatically when unique_lock/shared_lock go out of scope)
}

// Static method to dump all currently held locks (for debugging)
void dumpAllHeldLocks() {
    LockWatchdog::getInstance().dumpAllHeldLocks();
}

} // namespace utils
} // namespace audio
} // namespace screamrouter