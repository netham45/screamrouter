/**
 * @file thread_priority.cpp
 * @brief Implements helpers for elevating audio threads to real-time priority.
 */

#include "thread_priority.h"
#include "cpp_logger.h"

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <fstream>
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
std::optional<int> detect_thread_cpu(pthread_t handle, const char* thread_name) {
    const pid_t tid = pthread_gettid_np(handle);
    if (tid <= 0) {
        LOG_CPP_WARNING("[ThreadPriority] %s: Failed to resolve TID for CPU detection (errno=%d, %s).",
                        safe_name(thread_name), errno, strerror(errno));
        return std::nullopt;
    }

    const std::string stat_path = "/proc/self/task/" + std::to_string(tid) + "/stat";
    std::ifstream stat_file(stat_path);
    if (!stat_file.is_open()) {
        LOG_CPP_WARNING("[ThreadPriority] %s: Unable to open %s for CPU detection (errno=%d, %s).",
                        safe_name(thread_name), stat_path.c_str(), errno, strerror(errno));
        return std::nullopt;
    }

    std::string stat_line;
    std::getline(stat_file, stat_line);
    const auto comm_end = stat_line.rfind(')');
    if (comm_end == std::string::npos || comm_end + 2 >= stat_line.size()) {
        LOG_CPP_WARNING("[ThreadPriority] %s: Malformed stat entry while detecting CPU.", safe_name(thread_name));
        return std::nullopt;
    }

    // Fields after the comm begin with state (field #3). CPU id is overall field #39.
    constexpr int kProcessorFieldIndex = 39 - 3; // Zero-based offset into tokens after comm
    std::istringstream fields(stat_line.substr(comm_end + 2));
    std::string token;
    int index = 0;
    while (fields >> token) {
        if (index == kProcessorFieldIndex) {
            try {
                return std::stoi(token);
            } catch (...) {
                LOG_CPP_WARNING("[ThreadPriority] %s: Failed to parse CPU field '%s'.",
                                safe_name(thread_name), token.c_str());
                return std::nullopt;
            }
        }
        ++index;
    }

    LOG_CPP_WARNING("[ThreadPriority] %s: CPU field missing while detecting processor affinity.", safe_name(thread_name));
    return std::nullopt;
}

void apply_affinity_to_cpu(pthread_t handle, int cpu, const char* thread_name) {
    if (cpu < 0 || cpu >= CPU_SETSIZE) {
        LOG_CPP_WARNING("[ThreadPriority] %s: CPU %d is out of affinity set range (CPU_SETSIZE=%d).",
                        safe_name(thread_name), cpu, CPU_SETSIZE);
        return;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    const int ret = pthread_setaffinity_np(handle, sizeof(cpuset), &cpuset);
    if (ret != 0) {
        LOG_CPP_WARNING("[ThreadPriority] %s: Failed to pin to CPU %d (err=%d, %s).",
                        safe_name(thread_name), cpu, ret, strerror(ret));
        return;
    }

    LOG_CPP_INFO("[ThreadPriority] %s pinned to CPU %d.", safe_name(thread_name), cpu);
}

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

    if (const auto cpu = detect_thread_cpu(handle, thread_name)) {
        apply_affinity_to_cpu(handle, *cpu, thread_name);
    }

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
