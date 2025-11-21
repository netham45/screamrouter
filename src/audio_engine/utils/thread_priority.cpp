/**
 * @file thread_priority.cpp
 * @brief Implements helpers for elevating audio threads to real-time priority.
 */

#include "thread_priority.h"
#include "cpp_logger.h"

#include <algorithm>
#include <string>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <cerrno>
#include <cstring>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace screamrouter {
namespace audio {
namespace utils {
namespace {

#if defined(__linux__)
constexpr int kPriorityBackoff = 1; // Avoid absolute max to reduce risk of starving the system
#endif

const char* safe_name(const char* name) {
    return (name && *name) ? name : "audio_thread";
}

#if defined(__linux__)
bool set_posix_realtime_priority(pthread_t handle, const char* thread_name) {
    sched_param params{};
    const int policy = SCHED_FIFO;
    const int max_prio = sched_get_priority_max(policy);
    const int min_prio = sched_get_priority_min(policy);

    if (max_prio == -1 || min_prio == -1) {
        LOG_CPP_WARNING("[ThreadPriority] %s: Failed to query FIFO priority range (err=%d).",
                        safe_name(thread_name), errno);
        return false;
    }

    int desired = max_prio - kPriorityBackoff;
    if (desired < min_prio) {
        desired = max_prio;
    }
    params.sched_priority = desired;

    const int ret = pthread_setschedparam(handle, policy, &params);
    if (ret != 0) {
        LOG_CPP_WARNING("[ThreadPriority] %s: Failed to set real-time priority (err=%d, %s).",
                        safe_name(thread_name), ret, strerror(ret));
        return false;
    }

    LOG_CPP_INFO("[ThreadPriority] %s promoted to real-time (policy=SCHED_FIFO priority=%d).",
                 safe_name(thread_name), params.sched_priority);
    return true;
}
#elif defined(_WIN32)
bool set_win32_realtime_priority(HANDLE handle, const char* thread_name) {
    if (SetThreadPriority(handle, THREAD_PRIORITY_TIME_CRITICAL) != 0) {
        LOG_CPP_INFO("[ThreadPriority] %s promoted to real-time (THREAD_PRIORITY_TIME_CRITICAL).",
                     safe_name(thread_name));
        return true;
    }

    LOG_CPP_WARNING("[ThreadPriority] %s: Failed to set real-time priority (GetLastError=%lu).",
                    safe_name(thread_name), GetLastError());
    return false;
}
#endif

} // namespace

bool set_current_thread_realtime_priority(const char* thread_name) {
#if defined(__linux__)
    return set_posix_realtime_priority(pthread_self(), thread_name);
#elif defined(_WIN32)
    return set_win32_realtime_priority(GetCurrentThread(), thread_name);
#else
    (void)thread_name;
    LOG_CPP_WARNING("[ThreadPriority] Real-time priority not supported on this platform.");
    return false;
#endif
}

bool set_thread_realtime_priority(std::thread& thread, const char* thread_name) {
#if defined(__linux__)
    return set_posix_realtime_priority(thread.native_handle(), thread_name);
#elif defined(_WIN32)
    return set_win32_realtime_priority(static_cast<HANDLE>(thread.native_handle()), thread_name);
#else
    (void)thread;
    (void)thread_name;
    LOG_CPP_WARNING("[ThreadPriority] Real-time priority not supported on this platform.");
    return false;
#endif
}

} // namespace utils
} // namespace audio
} // namespace screamrouter
