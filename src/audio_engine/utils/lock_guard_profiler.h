#ifndef LOCK_GUARD_PROFILER_H
#define LOCK_GUARD_PROFILER_H

#include <chrono>
#include <shared_mutex>
#include <string>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include "cpp_logger.h"

namespace screamrouter {
namespace audio {
namespace utils {

enum class LockType {
    READ,
    WRITE
};

// Define lock duration thresholds
const std::chrono::milliseconds WRITE_LOCK_THRESHOLD(20);
const std::chrono::milliseconds READ_LOCK_THRESHOLD(1000);

// Define lock duration watchdog thresholds
const std::chrono::milliseconds WRITE_LOCK_WATCHDOG_THRESHOLD(200);
const std::chrono::milliseconds READ_LOCK_WATCHDOG_THRESHOLD(5000);

class LockGuardProfiler;

class LockWatchdog {
public:
    static LockWatchdog& getInstance() {
        static LockWatchdog instance;
        return instance;
    }

    void registerLock(LockGuardProfiler* profiler, LockType type, const char* file, int line, std::chrono::steady_clock::time_point start_time) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_locks_[profiler] = {type, file, line, start_time};
    }

    void unregisterLock(LockGuardProfiler* profiler) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_locks_.erase(profiler);
    }

    LockWatchdog(const LockWatchdog&) = delete;
    LockWatchdog& operator=(const LockWatchdog&) = delete;

private:
    LockWatchdog() : running_(true) {
        watchdog_thread_ = std::thread(&LockWatchdog::watchdogLoop, this);
    }

    ~LockWatchdog() {
        running_ = false;
        if (watchdog_thread_.joinable()) {
            watchdog_thread_.join();
        }
    }

    void watchdogLoop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            std::lock_guard<std::mutex> lock(mutex_);
            auto now = std::chrono::steady_clock::now();

            for (const auto& pair : active_locks_) {
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - pair.second.start_time);
                if (pair.second.type == LockType::WRITE) {
                    if (duration > WRITE_LOCK_WATCHDOG_THRESHOLD) {
                        LOG_CPP_ERROR("[LockWatchdog] Write lock held for %lldms at %s:%d (Threshold: %lldms). Terminating.",
                                      (long long)duration.count(), pair.second.file, pair.second.line, (long long)WRITE_LOCK_WATCHDOG_THRESHOLD.count());
                        std::terminate();
                    }
                } else { // READ
                    if (duration > READ_LOCK_WATCHDOG_THRESHOLD) {
                         LOG_CPP_ERROR("[LockWatchdog] Read lock held for %lldms at %s:%d (Threshold: %lldms). Terminating.",
                                      (long long)duration.count(), pair.second.file, pair.second.line, (long long)READ_LOCK_WATCHDOG_THRESHOLD.count());
                        std::terminate();
                    }
                }
            }
        }
    }

    struct LockInfo {
        LockType type;
        const char* file;
        int line;
        std::chrono::steady_clock::time_point start_time;
    };

    std::mutex mutex_;
    std::unordered_map<LockGuardProfiler*, LockInfo> active_locks_;
    std::thread watchdog_thread_;
    std::atomic<bool> running_;
};


class LockGuardProfiler {
public:
    LockGuardProfiler(std::shared_mutex& m, LockType type, const char* file, int line)
        : mutex_(m), lock_type_(type), file_(file), line_(line), start_time_(std::chrono::steady_clock::now()) {
        LockWatchdog::getInstance().registerLock(this, lock_type_, file_, line_, start_time_);
        LOG_CPP_DEBUG("[LockProfiler] Attempting %s lock at %s:%d", lock_type_ == LockType::WRITE ? "WRITE" : "READ", file_, line_);
        if (lock_type_ == LockType::WRITE) {
            unique_lock_ = std::unique_lock<std::shared_mutex>(mutex_);
        } else {
            shared_lock_ = std::shared_lock<std::shared_mutex>(mutex_);
        }
        LOG_CPP_DEBUG("[LockProfiler] Acquired %s lock at %s:%d", lock_type_ == LockType::WRITE ? "WRITE" : "READ", file_, line_);
    }

    ~LockGuardProfiler() {
        LOG_CPP_DEBUG("[LockProfiler] Releasing %s lock from %s:%d", lock_type_ == LockType::WRITE ? "WRITE" : "READ", file_, line_);
        LockWatchdog::getInstance().unregisterLock(this);
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_);

        if (lock_type_ == LockType::WRITE) {
            if (duration > WRITE_LOCK_THRESHOLD) {
                LOG_CPP_ERROR("[LockProfiler] Write lock held for %lldms at %s:%d (Threshold: %lldms)",
                              (long long)duration.count(), file_, line_, (long long)WRITE_LOCK_THRESHOLD.count());
            }
            if (unique_lock_.owns_lock()) {
                unique_lock_.unlock();
            }
        } else { // READ
            if (duration > READ_LOCK_THRESHOLD) {
                LOG_CPP_ERROR("[LockProfiler] Read lock held for %lldms at %s:%d (Threshold: %lldms)",
                              (long long)duration.count(), file_, line_, (long long)READ_LOCK_THRESHOLD.count());
            }
            if (shared_lock_.owns_lock()) {
                shared_lock_.unlock();
            }
        }
    }

    // Prevent copying/moving
    LockGuardProfiler(const LockGuardProfiler&) = delete;
    LockGuardProfiler& operator=(const LockGuardProfiler&) = delete;
    LockGuardProfiler(LockGuardProfiler&&) = delete;
    LockGuardProfiler& operator=(LockGuardProfiler&&) = delete;

private:
    std::shared_mutex& mutex_;
    LockType lock_type_;
    const char* file_;
    int line_;
    std::chrono::steady_clock::time_point start_time_;

    // A lock guard can hold one of two types of locks
    std::unique_lock<std::shared_mutex> unique_lock_;
    std::shared_lock<std::shared_mutex> shared_lock_;
};

} // namespace utils
} // namespace audio
} // namespace screamrouter

#endif // LOCK_GUARD_PROFILER_H