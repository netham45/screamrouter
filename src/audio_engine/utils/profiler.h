#ifndef SCREAMROUTER_AUDIO_UTILS_PROFILER_H
#define SCREAMROUTER_AUDIO_UTILS_PROFILER_H

#include <chrono>
#include <cstdint>
#include <string>

#define ENABLE_AUDIO_PROFILING 1

#ifdef ENABLE_AUDIO_PROFILING
#include <mutex>
#include <unordered_map>
#endif

namespace screamrouter {
namespace audio {
namespace utils {

#ifdef ENABLE_AUDIO_PROFILING

class FunctionProfiler {
public:
    struct Stats {
        uint64_t total_ns = 0;
        uint64_t count = 0;
        uint64_t max_ns = 0;
    };

    static FunctionProfiler& instance();

    void record(const char* name, uint64_t duration_ns);
    void reset();
    void log_stats();

private:
    FunctionProfiler() = default;
    FunctionProfiler(const FunctionProfiler&) = delete;
    FunctionProfiler& operator=(const FunctionProfiler&) = delete;

    std::mutex mutex_;
    std::unordered_map<std::string, Stats> stats_;
};

class ScopedProfileTimer {
public:
    explicit ScopedProfileTimer(const char* name);
    ~ScopedProfileTimer();

private:
    const char* name_;
    std::chrono::steady_clock::time_point start_;
};

#define PROFILE_FUNCTION() ::screamrouter::audio::utils::ScopedProfileTimer profile_timer_##__LINE__(__FUNCTION__)
#define PROFILE_SCOPE(name) ::screamrouter::audio::utils::ScopedProfileTimer profile_timer_##__LINE__(name)

#else  // ENABLE_AUDIO_PROFILING

class ScopedProfileTimer {
public:
    explicit ScopedProfileTimer(const char*) {}
};

inline void profile_log_stats() {}
inline void profile_reset() {}

#define PROFILE_FUNCTION() ((void)0)
#define PROFILE_SCOPE(name) ((void)0)

#endif // ENABLE_AUDIO_PROFILING

} // namespace utils
} // namespace audio
} // namespace screamrouter

#endif // SCREAMROUTER_AUDIO_UTILS_PROFILER_H
