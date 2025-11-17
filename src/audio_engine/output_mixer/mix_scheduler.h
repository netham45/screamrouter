#ifndef MIX_SCHEDULER_H
#define MIX_SCHEDULER_H

#include "../audio_types.h"
#include "../configuration/audio_engine_settings.h"
#include "../utils/thread_safe_queue.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace screamrouter {
namespace audio {

/**
 * @brief Coordinates delivery of processed audio chunks from source processors to a sink mixer.
 */
class MixScheduler {
public:
    using InputChunkQueue = utils::ThreadSafeQueue<ProcessedAudioChunk>;

    struct ReadyChunk {
        ProcessedAudioChunk chunk;
        std::chrono::steady_clock::time_point arrival_time{};
    };

    struct HarvestResult {
        std::map<std::string, ReadyChunk> ready_chunks;
        std::vector<std::string> drained_sources;
    };

    MixScheduler(std::string mixer_id,
                 std::shared_ptr<AudioEngineSettings> settings);
    ~MixScheduler();

    MixScheduler(const MixScheduler&) = delete;
    MixScheduler& operator=(const MixScheduler&) = delete;

    void attach_source(const std::string& instance_id,
                       std::shared_ptr<InputChunkQueue> queue);
    void detach_source(const std::string& instance_id);
    void set_timing_parameters(std::size_t frames_per_chunk, int sample_rate);

    HarvestResult collect_ready_chunks();
    std::map<std::string, std::size_t> get_ready_depths() const;
    std::size_t drop_ready_chunks(const std::string& instance_id, std::size_t count);
    std::size_t drop_all_ready_chunks();

    void shutdown();

private:
    struct SourceState {
        std::string instance_id;
        std::shared_ptr<InputChunkQueue> queue;
        std::thread worker_thread;
        std::atomic<bool> stopping{false};
    };

    void worker_loop(SourceState* state);
    void append_ready_chunk(const std::string& instance_id,
                            ProcessedAudioChunk&& chunk,
                            std::chrono::steady_clock::time_point arrival_time);
    void maybe_log_telemetry();
    std::size_t compute_ready_capacity() const;

    const std::string mixer_id_;
    std::shared_ptr<AudioEngineSettings> settings_;
    std::size_t frames_per_chunk_ = 0;
    int timer_sample_rate_ = 0;

    std::mutex sources_mutex_;
    std::unordered_map<std::string, std::unique_ptr<SourceState>> sources_;

    mutable std::mutex ready_mutex_;
    std::unordered_map<std::string, std::deque<ReadyChunk>> ready_chunks_;

    std::mutex drained_mutex_;
    std::vector<std::string> drained_sources_;

    std::atomic<bool> shutting_down_{false};
    std::chrono::steady_clock::time_point telemetry_last_log_time_{};
};

} // namespace audio
} // namespace screamrouter

#endif // MIX_SCHEDULER_H
