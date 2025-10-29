#ifndef SCREAMROUTER_AUDIO_CLOCK_MANAGER_H
#define SCREAMROUTER_AUDIO_CLOCK_MANAGER_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

#include "../configuration/audio_engine_settings.h"

namespace screamrouter {
namespace audio {

class ClockManager {
public:
    using ClockKey = std::tuple<int, int, int>; // sample_rate, channels, bit_depth

    struct ClockCondition {
        std::mutex mutex;
        std::condition_variable cv;
        uint64_t sequence = 0;
    };

    struct ConditionHandle {
        ClockKey key{};
        std::uint64_t id = 0;
        std::shared_ptr<ClockCondition> condition;

        bool valid() const {
            return condition != nullptr && id != 0;
        }
    };

    explicit ClockManager(std::size_t chunk_size_bytes = kDefaultChunkSizeBytes);
    ~ClockManager();

    ClockManager(const ClockManager&) = delete;
    ClockManager& operator=(const ClockManager&) = delete;

    ConditionHandle register_clock_condition(int sample_rate, int channels, int bit_depth);
    void unregister_clock_condition(const ConditionHandle& handle);

private:
    struct ConditionEntry {
        std::uint64_t id = 0;
        std::weak_ptr<ClockCondition> condition;
        std::atomic<bool> active{false};
    };

    struct ClockEntry {
        std::chrono::nanoseconds period{0};
        std::chrono::steady_clock::time_point next_fire{};
        std::vector<std::shared_ptr<ConditionEntry>> conditions;
    };

    std::chrono::nanoseconds calculate_period(int sample_rate, int channels, int bit_depth) const;
    bool has_active_conditions(const ClockEntry& entry) const;
    void cleanup_inactive_conditions(ClockEntry& entry);
    void run();

    std::map<ClockKey, ClockEntry> clock_entries_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_thread_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<std::uint64_t> next_condition_id_{1};
    const std::size_t chunk_size_bytes_;
};

} // namespace audio
} // namespace screamrouter

#endif // SCREAMROUTER_AUDIO_CLOCK_MANAGER_H
