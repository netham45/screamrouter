#include "mix_scheduler.h"
#include "../utils/cpp_logger.h"
#include "../utils/thread_priority.h"

#include <algorithm>

namespace screamrouter {
namespace audio {

namespace {
constexpr std::size_t kMaxReadyChunksPerSource = 4;
}

MixScheduler::MixScheduler(std::string mixer_id,
                           std::shared_ptr<AudioEngineSettings> settings)
    : mixer_id_(std::move(mixer_id)),
      settings_(std::move(settings)) {
    frames_per_chunk_ = resolve_base_frames_per_chunk(settings_);
    timer_sample_rate_ = 48000;
    LOG_CPP_INFO("[MixScheduler:%s] Created.", mixer_id_.c_str());
}

MixScheduler::~MixScheduler() {
    shutdown();
    LOG_CPP_INFO("[MixScheduler:%s] Destroyed.", mixer_id_.c_str());
}

void MixScheduler::attach_source(const std::string& instance_id,
                                 std::shared_ptr<InputChunkQueue> queue) {
    if (!queue) {
        LOG_CPP_WARNING("[MixScheduler:%s] attach_source called with null queue for %s.",
                        mixer_id_.c_str(), instance_id.c_str());
        return;
    }
    if (shutting_down_.load()) {
        LOG_CPP_WARNING("[MixScheduler:%s] attach_source called during shutdown.", mixer_id_.c_str());
        return;
    }

    auto state = std::make_unique<SourceState>();
    state->instance_id = instance_id;
    state->queue = std::move(queue);
    SourceState* state_ptr = state.get();

    {
        std::lock_guard<std::mutex> lock(sources_mutex_);
        if (sources_.count(instance_id)) {
            LOG_CPP_WARNING("[MixScheduler:%s] Source %s already attached.", mixer_id_.c_str(), instance_id.c_str());
            return;
        }
        sources_.emplace(instance_id, std::move(state));
    }

    try {
        state_ptr->worker_thread = std::thread(&MixScheduler::worker_loop, this, state_ptr);
        LOG_CPP_INFO("[MixScheduler:%s] Worker started for source %s.", mixer_id_.c_str(), instance_id.c_str());
    } catch (const std::exception& ex) {
        LOG_CPP_ERROR("[MixScheduler:%s] Failed to launch worker for %s: %s", mixer_id_.c_str(), instance_id.c_str(), ex.what());
        std::lock_guard<std::mutex> lock(sources_mutex_);
        sources_.erase(instance_id);
        throw;
    }
}

void MixScheduler::detach_source(const std::string& instance_id) {
    SourceState* state_ptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(sources_mutex_);
        auto it = sources_.find(instance_id);
        if (it == sources_.end()) {
            return;
        }
        state_ptr = it->second.get();
        state_ptr->stopping.store(true);
    }

    if (state_ptr && state_ptr->queue) {
        // Push a sentinel chunk to unblock the worker if it is waiting.
        ProcessedAudioChunk sentinel;
        state_ptr->queue->push(std::move(sentinel));
    }

    if (state_ptr && state_ptr->worker_thread.joinable()) {
        try {
            state_ptr->worker_thread.join();
        } catch (const std::exception& ex) {
            LOG_CPP_ERROR("[MixScheduler:%s] Error joining worker for %s: %s", mixer_id_.c_str(), instance_id.c_str(), ex.what());
        }
    }

    {
        std::lock_guard<std::mutex> lock(sources_mutex_);
        sources_.erase(instance_id);
    }

    {
        std::lock_guard<std::mutex> lock(ready_mutex_);
        ready_chunks_.erase(instance_id);
        per_source_received_.erase(instance_id);
        per_source_dropped_.erase(instance_id);
        per_source_popped_.erase(instance_id);
        per_source_high_water_.erase(instance_id);
    }
    LOG_CPP_INFO("[MixScheduler:%s] Source %s detached.", mixer_id_.c_str(), instance_id.c_str());
}

MixScheduler::HarvestResult MixScheduler::collect_ready_chunks() {
    HarvestResult result;

    {
        std::lock_guard<std::mutex> lock(ready_mutex_);
        std::vector<std::string> to_erase;
        to_erase.reserve(ready_chunks_.size());

        for (auto& entry : ready_chunks_) {
            auto& deque_ref = entry.second;
            if (!deque_ref.empty()) {
                result.ready_chunks.emplace(entry.first, std::move(deque_ref.front()));
                per_source_popped_[entry.first]++;
                deque_ref.pop_front();
            }
            if (deque_ref.empty()) {
                to_erase.push_back(entry.first);
            }
        }

        for (const auto& key : to_erase) {
            ready_chunks_.erase(key);
        }
    }

    {
        std::lock_guard<std::mutex> lock(drained_mutex_);
        if (!drained_sources_.empty()) {
            result.drained_sources = std::move(drained_sources_);
            drained_sources_.clear();
        }
    }

    return result;
}

std::map<std::string, std::size_t> MixScheduler::get_ready_depths() const {
    std::lock_guard<std::mutex> lock(ready_mutex_);
    std::map<std::string, std::size_t> depths;
    for (const auto& entry : ready_chunks_) {
        depths[entry.first] = entry.second.size();
    }
    return depths;
}

std::map<std::string, MixScheduler::ReadyQueueStats> MixScheduler::get_ready_stats() const {
    std::lock_guard<std::mutex> lock(ready_mutex_);
    std::map<std::string, ReadyQueueStats> stats;
    const auto now = std::chrono::steady_clock::now();

    auto populate = [&](const std::string& id, const std::deque<ReadyChunk>* queue_ptr) {
        ReadyQueueStats q;
        if (queue_ptr) {
            q.depth = queue_ptr->size();
            if (!queue_ptr->empty()) {
                q.head_age_ms = std::chrono::duration<double, std::milli>(now - queue_ptr->front().arrival_time).count();
                q.tail_age_ms = std::chrono::duration<double, std::milli>(now - queue_ptr->back().arrival_time).count();
                if (q.head_age_ms < 0.0) q.head_age_ms = 0.0;
                if (q.tail_age_ms < 0.0) q.tail_age_ms = 0.0;
            }
        }
        auto hw_it = per_source_high_water_.find(id);
        if (hw_it != per_source_high_water_.end()) {
            q.high_water = hw_it->second;
        }
        auto recv_it = per_source_received_.find(id);
        if (recv_it != per_source_received_.end()) {
            q.total_received = recv_it->second;
        }
        auto pop_it = per_source_popped_.find(id);
        if (pop_it != per_source_popped_.end()) {
            q.total_popped = pop_it->second;
        }
        auto drop_it = per_source_dropped_.find(id);
        if (drop_it != per_source_dropped_.end()) {
            q.total_dropped = drop_it->second;
        }
        stats[id] = q;
    };

    for (const auto& entry : ready_chunks_) {
        populate(entry.first, &entry.second);
    }
    for (const auto& kv : per_source_received_) {
        if (!stats.count(kv.first)) {
            populate(kv.first, nullptr);
        }
    }
    return stats;
}

void MixScheduler::shutdown() {
    if (shutting_down_.exchange(true)) {
        return;
    }

    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(sources_mutex_);
        ids.reserve(sources_.size());
        for (const auto& kv : sources_) {
            ids.push_back(kv.first);
        }
    }

    for (const auto& id : ids) {
        detach_source(id);
    }
}

void MixScheduler::worker_loop(SourceState* state) {
    if (!state || !state->queue) {
        return;
    }

    const auto log_prefix = mixer_id_ + ":" + state->instance_id;
    const std::string thread_name = "[MixScheduler:" + log_prefix + "]";
    utils::set_current_thread_realtime_priority(thread_name.c_str());
    LOG_CPP_DEBUG("[MixScheduler:%s] Worker entering loop.", log_prefix.c_str());

    while (!state->stopping.load()) {
        ProcessedAudioChunk chunk;
        bool popped = state->queue->pop(chunk);
        if (!popped) {
            break;
        }

        if (chunk.audio_data.empty()) {
            if (state->stopping.load()) {
                break;
            }
            continue;
        }

        auto arrival_time = std::chrono::steady_clock::now();
        append_ready_chunk(state->instance_id, std::move(chunk), arrival_time);
    }

    {
        std::lock_guard<std::mutex> lock(drained_mutex_);
        drained_sources_.push_back(state->instance_id);
    }

    LOG_CPP_DEBUG("[MixScheduler:%s] Worker exiting.", log_prefix.c_str());
}

void MixScheduler::append_ready_chunk(const std::string& instance_id,
                                      ProcessedAudioChunk&& chunk,
                                      std::chrono::steady_clock::time_point arrival_time) {
    if (chunk.audio_data.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(ready_mutex_);
        auto& queue = ready_chunks_[instance_id];
        per_source_received_[instance_id]++;
        const std::size_t cap = compute_ready_capacity();
        if (cap > 0 && queue.size() >= cap) {
            queue.pop_front();
            per_source_dropped_[instance_id]++;
            LOG_CPP_DEBUG("[MixScheduler:%s] Dropping oldest ready chunk for %s to enforce cap=%zu.",
                          mixer_id_.c_str(), instance_id.c_str(), cap);
        }
        queue.push_back(ReadyChunk{std::move(chunk), arrival_time});
        auto depth = queue.size();
        auto& hw = per_source_high_water_[instance_id];
        if (depth > hw) {
            hw = depth;
        }
    }

    maybe_log_telemetry();
}

void MixScheduler::set_timing_parameters(std::size_t frames_per_chunk, int sample_rate) {
    frames_per_chunk_ = frames_per_chunk > 0 ? frames_per_chunk : frames_per_chunk_;
    timer_sample_rate_ = sample_rate > 0 ? sample_rate : timer_sample_rate_;
}

void MixScheduler::maybe_log_telemetry() {
    if (!settings_ || !settings_->telemetry.enabled) {
        return;
    }

    long interval_ms = settings_->telemetry.log_interval_ms;
    if (interval_ms <= 0) {
        interval_ms = 30000;
    }

    const auto now = std::chrono::steady_clock::now();
    if (telemetry_last_log_time_.time_since_epoch().count() != 0 &&
        now - telemetry_last_log_time_ < std::chrono::milliseconds(interval_ms)) {
        return;
    }

    telemetry_last_log_time_ = now;

    std::unordered_map<std::string, std::deque<ReadyChunk>> snapshot;
    {
        std::lock_guard<std::mutex> lock(ready_mutex_);
        snapshot = ready_chunks_;
    }

    size_t total_chunks = 0;
    double total_head_age_ms = 0.0;
    double max_head_age_ms = 0.0;

    for (const auto& [instance_id, ready_queue] : snapshot) {
        size_t queue_size = ready_queue.size();
        total_chunks += queue_size;

        double head_age_ms = 0.0;
        double tail_age_ms = 0.0;
        if (!ready_queue.empty()) {
            head_age_ms = std::chrono::duration<double, std::milli>(now - ready_queue.front().arrival_time).count();
            if (head_age_ms < 0.0) {
                head_age_ms = 0.0;
            }
            tail_age_ms = std::chrono::duration<double, std::milli>(now - ready_queue.back().arrival_time).count();
            if (tail_age_ms < 0.0) {
                tail_age_ms = 0.0;
            }
            total_head_age_ms += head_age_ms;
            if (head_age_ms > max_head_age_ms) {
                max_head_age_ms = head_age_ms;
            }
        }

        LOG_CPP_INFO(
            "[Telemetry][MixScheduler:%s][Source %s] ready_chunks=%zu head_age_ms=%.3f tail_age_ms=%.3f",
            mixer_id_.c_str(),
            instance_id.c_str(),
            queue_size,
            head_age_ms,
            tail_age_ms);
    }

    double avg_head_age_ms = 0.0;
    if (!snapshot.empty()) {
        avg_head_age_ms = total_head_age_ms / static_cast<double>(snapshot.size());
    }

    LOG_CPP_INFO(
        "[Telemetry][MixScheduler:%s] total_ready_chunks=%zu avg_head_age_ms=%.3f max_head_age_ms=%.3f sources=%zu",
        mixer_id_.c_str(),
        total_chunks,
        avg_head_age_ms,
        max_head_age_ms,
        snapshot.size());
}

std::size_t MixScheduler::compute_ready_capacity() const {
    if (!settings_) {
        return kMaxReadyChunksPerSource;
    }

    const double duration_ms = settings_->mixer_tuning.max_ready_queue_duration_ms;
    if (duration_ms > 0.0 && frames_per_chunk_ > 0 && timer_sample_rate_ > 0) {
        const double chunk_duration_ms =
            (static_cast<double>(frames_per_chunk_) * 1000.0) / static_cast<double>(timer_sample_rate_);
        if (chunk_duration_ms > 0.0) {
            return std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(duration_ms / chunk_duration_ms)));
        }
    }

    const std::size_t fallback = settings_->mixer_tuning.max_ready_chunks_per_source;
    if (fallback > 0) {
        return fallback;
    }
    return kMaxReadyChunksPerSource;
}

} // namespace audio
} // namespace screamrouter
