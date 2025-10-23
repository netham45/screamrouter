#include "profiler.h"

#ifdef ENABLE_AUDIO_PROFILING

#include "cpp_logger.h"
#include <algorithm>
#include <vector>

namespace screamrouter {
namespace audio {
namespace utils {

FunctionProfiler& FunctionProfiler::instance() {
    static FunctionProfiler instance;
    return instance;
}

void FunctionProfiler::record(const char* name, uint64_t duration_ns) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entry = stats_[name];
    entry.total_ns += duration_ns;
    entry.count += 1;
    entry.max_ns = std::max(entry.max_ns, duration_ns);
}

void FunctionProfiler::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.clear();
}

void FunctionProfiler::log_stats() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stats_.empty()) {
        LOG_CPP_INFO("[Profiler] No profiling data collected yet.");
        return;
    }

    std::vector<std::pair<std::string, Stats>> snapshot;
    snapshot.reserve(stats_.size());
    for (const auto& kv : stats_) {
        snapshot.emplace_back(kv.first, kv.second);
    }

    std::sort(snapshot.begin(), snapshot.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.second.total_ns > rhs.second.total_ns;
    });

    LOG_CPP_INFO("[Profiler] Function timing (total_ms | avg_us | max_us | calls)");
    for (const auto& [name, stat] : snapshot) {
        const double total_ms = static_cast<double>(stat.total_ns) / 1'000'000.0;
        const double avg_us = stat.count > 0 ? (static_cast<double>(stat.total_ns) / static_cast<double>(stat.count)) / 1'000.0 : 0.0;
        const double max_us = static_cast<double>(stat.max_ns) / 1'000.0;
        LOG_CPP_INFO("[Profiler] %s => %.3f ms | %.3f us | %.3f us | %llu", name.c_str(), total_ms, avg_us, max_us, static_cast<unsigned long long>(stat.count));
    }
}

ScopedProfileTimer::ScopedProfileTimer(const char* name)
    : name_(name), start_(std::chrono::steady_clock::now()) {}

ScopedProfileTimer::~ScopedProfileTimer() {
    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
    FunctionProfiler::instance().record(name_, static_cast<uint64_t>(duration));
}

} // namespace utils
} // namespace audio
} // namespace screamrouter

#endif // ENABLE_AUDIO_PROFILING
