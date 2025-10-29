#include "mix_scheduler.h"
#include "../utils/cpp_logger.h"

#include <algorithm>

namespace screamrouter {
namespace audio {

namespace {
constexpr std::size_t kMaxReadyChunksPerSource = 5; // cap to ~12ms of backlog at 48kHz
}

MixScheduler::MixScheduler(std::string mixer_id,
                           std::shared_ptr<AudioEngineSettings> settings)
    : mixer_id_(std::move(mixer_id)),
      settings_(std::move(settings)) {
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

std::size_t MixScheduler::drop_ready_chunks(const std::string& instance_id, std::size_t count) {
    if (count == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(ready_mutex_);
    auto it = ready_chunks_.find(instance_id);
    if (it == ready_chunks_.end()) {
        return 0;
    }

    auto& deque_ref = it->second;
    std::size_t dropped = 0;

    if (count >= deque_ref.size()) {
        dropped = deque_ref.size();
        deque_ref.clear();
        ready_chunks_.erase(it);
        return dropped;
    }

    while (dropped < count && !deque_ref.empty()) {
        // Drop newest ready chunks first so the next-to-dispatch item stays intact.
        deque_ref.pop_back();
        ++dropped;
    }

    if (deque_ref.empty()) {
        ready_chunks_.erase(it);
    }

    return dropped;
}

std::size_t MixScheduler::drop_all_ready_chunks() {
    std::lock_guard<std::mutex> lock(ready_mutex_);
    std::size_t dropped = 0;
    for (auto& entry : ready_chunks_) {
        dropped += entry.second.size();
    }
    ready_chunks_.clear();
    return dropped;
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
    ReadyChunk ready;
    ready.chunk = std::move(chunk);
    ready.arrival_time = arrival_time;

    {
        std::lock_guard<std::mutex> lock(ready_mutex_);
        auto& queue = ready_chunks_[instance_id];
        if (queue.size() >= kMaxReadyChunksPerSource) {
            queue.pop_front();
            LOG_CPP_DEBUG("[MixScheduler:%s] Dropping oldest ready chunk for %s to enforce cap=%zu.",
                          mixer_id_.c_str(), instance_id.c_str(), kMaxReadyChunksPerSource);
        }
        queue.push_back(std::move(ready));
    }
}

} // namespace audio
} // namespace screamrouter
