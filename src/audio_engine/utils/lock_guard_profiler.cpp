/**
 * @file lock_guard_profiler.cpp
 * @brief Implementation of the LockGuardProfiler and LockWatchdog classes for deadlock detection.
 */

#include "lock_guard_profiler.h"
#include <sstream>
#include <iomanip>
#include <set>
#include <cstdlib>  // for std::abort()
#include <algorithm>

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
    : LockGuardProfiler(static_cast<void*>(&m), type, file, line, MutexFlavor::SHARED) {
    shared_mutex_ = &m;
    if (lock_type_ == LockType::WRITE) {
        shared_unique_lock_ = std::unique_lock<std::shared_mutex>(*shared_mutex_);
    } else {
        shared_shared_lock_ = std::shared_lock<std::shared_mutex>(*shared_mutex_);
    }
}

LockGuardProfiler::LockGuardProfiler(std::mutex& m, LockType type,
                                     const char* file, int line)
    : LockGuardProfiler(static_cast<void*>(&m), type, file, line, MutexFlavor::STD) {
    std_mutex_ = &m;
    std_unique_lock_ = std::unique_lock<std::mutex>(*std_mutex_);
}

LockGuardProfiler::LockGuardProfiler(void* mutex_ptr, LockType type, const char* file,
                                     int line, MutexFlavor flavor)
    : mutex_flavor_(flavor),
      mutex_ptr_(mutex_ptr),
      shared_mutex_(nullptr),
      std_mutex_(nullptr),
      lock_type_(type),
      file_(file),
      line_(line) {

    if (mutex_flavor_ == MutexFlavor::STD) {
        lock_type_ = LockType::WRITE;
    }

    check_self_deadlock();

    start_time_ = std::chrono::steady_clock::now();

    track_lock_acquisition();

    LockWatchdog::getInstance().registerLock(this, lock_type_, file_, line_, start_time_);
}

void LockGuardProfiler::check_self_deadlock() {
    if (tls_held_mutexes.find(mutex_ptr_) != tls_held_mutexes.end()) {
        std::stringstream ss;
        ss << "SELF-DEADLOCK DETECTED: Thread " << std::this_thread::get_id()
           << " attempting to lock mutex at " << mutex_ptr_
           << " which it already holds!"
           << "\n  Current lock attempt: " << file_ << ":" << line_
           << "\n  Previously locked at: " << tls_held_locks[mutex_ptr_].first
           << ":" << tls_held_locks[mutex_ptr_].second;
        
        // Dump all locks before fatal error
        LockWatchdog::getInstance().dumpAllHeldLocks();
        LOG_CPP_ERROR("%s", ss.str().c_str());
        std::abort(); // Fatal error - terminate the program
    }
}

void LockGuardProfiler::track_lock_acquisition() {
    tls_held_mutexes.insert(mutex_ptr_);
    tls_held_locks[mutex_ptr_] = {std::string(file_), line_};
    tls_lock_count++;
    
    {
        std::lock_guard<std::mutex> lock(g_lock_registry_mutex);
        std::stringstream ss;
        ss << (lock_type_ == LockType::WRITE ? "W" : "R") 
           << "@" << mutex_ptr_ << " (" << file_ << ":" << line_ << ")";
        g_thread_lock_registry[std::this_thread::get_id()].push_back(ss.str());
    }
    
    if (tls_lock_count > 1) {
        std::stringstream ss;
        ss << "WARNING: Thread " << std::this_thread::get_id() 
           << " now holds " << tls_lock_count << " locks simultaneously"
           << " (latest: " << file_ << ":" << line_ << ")";
        
        ss << "\n  Held locks:";
        for (const auto& [ptr, info] : tls_held_locks) {
            ss << "\n    - " << ptr << " at " << info.first << ":" << info.second;
        }
        
        LOG_CPP_WARNING("%s", ss.str().c_str());
    }
}

void LockGuardProfiler::release_lock_tracking() {
    tls_held_mutexes.erase(mutex_ptr_);
    tls_held_locks.erase(mutex_ptr_);
    tls_lock_count--;
    
    {
        std::lock_guard<std::mutex> lock(g_lock_registry_mutex);
        auto& locks = g_thread_lock_registry[std::this_thread::get_id()];
        if (!locks.empty()) {
            std::stringstream ss;
            ss << (lock_type_ == LockType::WRITE ? "W" : "R") 
               << "@" << mutex_ptr_ << " (" << file_ << ":" << line_ << ")";
            auto lock_str = ss.str();
            
            auto it = std::find(locks.rbegin(), locks.rend(), lock_str);
            if (it != locks.rend()) {
                locks.erase(std::next(it).base());
            }
        }
        
        if (locks.empty()) {
            g_thread_lock_registry.erase(std::this_thread::get_id());
        }
    }
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
    
    LockWatchdog::getInstance().unregisterLock(this);
    
    release_lock_tracking();
}

// Static method to dump all currently held locks (for debugging)
void dumpAllHeldLocks() {
    LockWatchdog::getInstance().dumpAllHeldLocks();
}

} // namespace utils
} // namespace audio
} // namespace screamrouter
