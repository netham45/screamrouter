#ifndef SCREAMROUTER_AUDIO_CLOCK_MANAGER_H
#define SCREAMROUTER_AUDIO_CLOCK_MANAGER_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

namespace screamrouter {
namespace audio {

class ClockManager {
public:
    using ClockKey = std::tuple<int, int, int>; // sample_rate, channels, bit_depth
    using CallbackId = std::uint64_t;
    using Callback = std::function<void()>;

    ClockManager();
    ~ClockManager();

    ClockManager(const ClockManager&) = delete;
    ClockManager& operator=(const ClockManager&) = delete;

    CallbackId register_clock(int sample_rate, int channels, int bit_depth, Callback callback);
    void unregister_clock(int sample_rate, int channels, int bit_depth, CallbackId callback_id);

private:
    struct CallbackEntry {
        CallbackId id = 0;
        Callback callback;
        std::atomic<bool> active{false};
    };

    struct ClockEntry {
        std::chrono::nanoseconds period{0};
        std::chrono::steady_clock::time_point next_fire{};
        std::vector<std::shared_ptr<CallbackEntry>> callbacks;
    };

    static constexpr std::size_t kChunkSizeBytes = 1152;

    static std::chrono::nanoseconds calculate_period(int sample_rate, int channels, int bit_depth);
    bool has_active_callbacks(const ClockEntry& entry) const;
    void cleanup_inactive_callbacks(ClockEntry& entry);
    void run();

    std::map<ClockKey, ClockEntry> clock_entries_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_thread_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<CallbackId> next_callback_id_{1};
};

} // namespace audio
} // namespace screamrouter

#endif // SCREAMROUTER_AUDIO_CLOCK_MANAGER_H
