/**
 * @file lock_guard_profiler.h
 * @brief Defines a debugging utility for profiling and monitoring mutex lock durations.
 * @details This file contains the `LockGuardProfiler` and `LockWatchdog` classes,
 *          which are used to detect long-held locks and potential deadlocks in
 *          a multithreaded environment. This is a debugging tool and may not be
 *          intended for production builds.
 */
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

#if defined(__linux__)
#include <pthread.h>
#include <signal.h>
#endif

namespace screamrouter {
namespace audio {
namespace utils {

/**
 * @enum LockType
 * @brief Specifies the type of lock being acquired (read or write).
 */
enum class LockType {
    READ,  ///< A shared (read) lock.
    WRITE  ///< An exclusive (write) lock.
};

// Define lock duration thresholds for logging (tightened for faster detection)
const std::chrono::milliseconds WRITE_LOCK_THRESHOLD(10);
const std::chrono::milliseconds READ_LOCK_THRESHOLD(100);

// Define lock duration thresholds for the watchdog to terminate the program (tightened)
const std::chrono::milliseconds WRITE_LOCK_WATCHDOG_THRESHOLD(100);
const std::chrono::milliseconds READ_LOCK_WATCHDOG_THRESHOLD(1000);

class LockGuardProfiler;

/**
 * @class LockWatchdog
 * @brief A singleton that monitors all active `LockGuardProfiler` instances.
 * @details This class runs a background thread that periodically checks all registered
 *          locks. If a lock is held for longer than a predefined threshold, it logs
 *          a fatal error and terminates the program to prevent deadlocks.
 */
class LockWatchdog {
public:
    /** @brief Gets the singleton instance of the watchdog. */
    static LockWatchdog& getInstance();

    /** @brief Registers a lock with the watchdog. */
    void registerLock(LockGuardProfiler* profiler, LockType type, const char* file, int line, std::chrono::steady_clock::time_point start_time);

    /** @brief Unregisters a lock from the watchdog when it's released. */
    void unregisterLock(LockGuardProfiler* profiler);

    LockWatchdog(const LockWatchdog&) = delete;
    LockWatchdog& operator=(const LockWatchdog&) = delete;

    /** @brief Dumps all currently held locks across all threads for debugging. */
    void dumpAllHeldLocks();

private:
    LockWatchdog();
    ~LockWatchdog();

    void watchdogLoop();

    struct LockInfo {
        LockType type;
        const char* file;
        int line;
        std::chrono::steady_clock::time_point start_time;
#if defined(__linux__)
        pthread_t holder_thread;
#endif
    };

    std::mutex mutex_;
    std::unordered_map<LockGuardProfiler*, LockInfo> active_locks_;
    std::thread watchdog_thread_;
    std::atomic<bool> running_;
};

/**
 * @class LockGuardProfiler
 * @brief A RAII-style lock guard that profiles lock duration.
 * @details This class wraps `std::unique_lock` and `std::shared_lock` to provide
 *          automatic profiling of lock acquisition and hold times. When a lock is
 *          held for too long, it logs a warning. It also registers itself with the
 *          `LockWatchdog` for deadlock detection.
 */
class LockGuardProfiler {
public:
    /**
     * @brief Constructs a LockGuardProfiler and acquires a lock.
     * @param m The `std::shared_mutex` to lock.
     * @param type The type of lock to acquire (READ or WRITE).
     * @param file The source file where the lock is acquired.
     * @param line The line number where the lock is acquired.
     */
    LockGuardProfiler(std::shared_mutex& m, LockType type, const char* file, int line);

    /**
     * @brief Constructs a LockGuardProfiler for standard mutex types.
     * @param m The `std::mutex` to lock (exclusive only).
     * @param type The lock type (READ treated as WRITE for std::mutex).
     * @param file Source file where the lock is acquired.
     * @param line Line where the lock is acquired.
     */
    LockGuardProfiler(std::mutex& m, LockType type, const char* file, int line);

    /**
     * @brief Destructor. Releases the lock and logs the duration if it exceeds a threshold.
     */
    ~LockGuardProfiler();

    // Prevent copying/moving
    LockGuardProfiler(const LockGuardProfiler&) = delete;
    LockGuardProfiler& operator=(const LockGuardProfiler&) = delete;
    LockGuardProfiler(LockGuardProfiler&&) = delete;
    LockGuardProfiler& operator=(LockGuardProfiler&&) = delete;

private:
    enum class MutexFlavor {
        SHARED,
        STD
    };

    LockGuardProfiler(void* mutex_ptr, LockType type, const char* file, int line, MutexFlavor flavor);

    void check_self_deadlock();
    void track_lock_acquisition();
    void release_lock_tracking();

    MutexFlavor mutex_flavor_;
    void* mutex_ptr_;
    std::shared_mutex* shared_mutex_;
    std::mutex* std_mutex_;
    LockType lock_type_;
    const char* file_;
    int line_;
    std::chrono::steady_clock::time_point start_time_;

    std::unique_lock<std::shared_mutex> shared_unique_lock_;
    std::shared_lock<std::shared_mutex> shared_shared_lock_;
    std::unique_lock<std::mutex> std_unique_lock_;
};

/**
 * @brief Global function to dump all currently held locks for debugging.
 * @details This function can be called from anywhere to get a snapshot of all
 *          locks currently held by all threads in the application.
 */
void dumpAllHeldLocks();

} // namespace utils
} // namespace audio
} // namespace screamrouter

#endif // LOCK_GUARD_PROFILER_H
