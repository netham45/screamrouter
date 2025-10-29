/**
 * @file timeshift_manager.cpp
 * @brief Implements the TimeshiftManager class for handling global timeshifting.
 * @details This file contains the implementation of the TimeshiftManager, which manages
 *          a global buffer of audio packets to enable timeshifting and synchronized playback
 *          across multiple consumers.
 */
#include "timeshift_manager.h"
#include "stream_clock.h"
#include "../utils/cpp_logger.h"
#include "../audio_types.h"

#include <iostream>
#include <algorithm>
#include <thread>
#include <utility>
#include <vector>
#include <cmath>
#include <unordered_map>

namespace screamrouter {
namespace audio {

namespace {
constexpr double kProcessingBudgetAlpha = 0.2;
constexpr double kRtpContinuitySlackSeconds = 0.25; // allow ~250ms drift before declaring discontinuity

bool has_prefix(const std::string& value, const std::string& prefix) {
    if (prefix.empty()) {
        return true;
    }
    if (value.size() < prefix.size()) {
        return false;
    }
    return value.compare(0, prefix.size(), prefix) == 0;
}

bool match_and_bind_source(ProcessorTargetInfo& info, const std::string& actual_tag) {
    if (!info.is_wildcard) {
        return actual_tag == info.source_tag_filter;
    }
    if (!info.bound_source_tag.empty()) {
        return info.bound_source_tag == actual_tag;
    }
    if (has_prefix(actual_tag, info.wildcard_prefix)) {
        info.bound_source_tag = actual_tag;
        LOG_CPP_INFO("[TimeshiftManager] Bound wildcard '%s*' -> '%s'",
                     info.wildcard_prefix.c_str(), actual_tag.c_str());
        return true;
    }
    return false;
}

const std::string& active_tag(const ProcessorTargetInfo& info) {
    return info.is_wildcard ? info.bound_source_tag : info.source_tag_filter;
}
} // namespace

/**
 * @brief Constructs a TimeshiftManager.
 * @param max_buffer_duration The maximum duration of audio to hold in the global buffer.
 */
TimeshiftManager::TimeshiftManager(std::chrono::seconds max_buffer_duration, std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings)
    : max_buffer_duration_sec_(max_buffer_duration),
      m_settings(settings),
      last_cleanup_time_(std::chrono::steady_clock::now()),
      profiling_last_log_time_(std::chrono::steady_clock::now()) {
    LOG_CPP_INFO("[TimeshiftManager] Initializing with max buffer duration: %llds", (long long)max_buffer_duration_sec_.count());
}

/**
 * @brief Destructor for the TimeshiftManager.
 */
TimeshiftManager::~TimeshiftManager() {
    LOG_CPP_INFO("[TimeshiftManager] Destroying...");
    if (!stop_flag_) {
        stop();
    }
    LOG_CPP_INFO("[TimeshiftManager] Destruction complete.");
}

/**
 * @brief Starts the manager's processing thread.
 */
void TimeshiftManager::start() {
    if (is_running()) {
        LOG_CPP_WARNING("[TimeshiftManager] Already running.");
        return;
    }
    LOG_CPP_INFO("[TimeshiftManager] Starting...");
    stop_flag_ = false;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        reset_profiler_counters_unlocked(std::chrono::steady_clock::now());
    }
    try {
        component_thread_ = std::thread(&TimeshiftManager::run, this);
        LOG_CPP_INFO("[TimeshiftManager] Component thread launched.");
    } catch (const std::system_error& e) {
        LOG_CPP_ERROR("[TimeshiftManager] Failed to start component thread: %s", e.what());
        stop_flag_ = true;
        throw;
    }
}

/**
 * @brief Stops the manager's processing thread.
 */
void TimeshiftManager::stop() {
    if (stop_flag_) {
        LOG_CPP_WARNING("[TimeshiftManager] Already stopped or stopping.");
        return;
    }
    size_t buf_size = 0;
    size_t processor_count = 0;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        buf_size = global_timeshift_buffer_.size();
        for (auto const& kv : processor_targets_) {
            processor_count += kv.second.size();
        }
    }
    LOG_CPP_INFO("[TimeshiftManager] Stopping... buffer=%zu processors=%zu", buf_size, processor_count);
    stop_flag_ = true;
    m_state_version_++;
    run_loop_cv_.notify_all();

    if (component_thread_.joinable()) {
        try {
            component_thread_.join();
            LOG_CPP_INFO("[TimeshiftManager] Component thread joined.");
        } catch (const std::system_error& e) {
            LOG_CPP_ERROR("[TimeshiftManager] Error joining component thread: %s", e.what());
        }
    } else {
        LOG_CPP_WARNING("[TimeshiftManager] Component thread was not joinable in stop().");
    }
    LOG_CPP_INFO("[TimeshiftManager] Stopped.");
}

/**
 * @brief Adds a new audio packet to the global buffer and performs jitter calculation.
 * @param packet The packet to add, moved into the buffer.
 */
void TimeshiftManager::add_packet(TaggedAudioPacket&& packet) {
     if (stop_flag_ || !packet.rtp_timestamp.has_value() || packet.sample_rate <= 0) {
         return;
     }
 
     std::lock_guard<std::mutex> data_lock(data_mutex_);
     std::lock_guard<std::mutex> timing_lock(timing_mutex_);

     const uint32_t frames_per_second = static_cast<uint32_t>(packet.sample_rate);
     const uint32_t reset_threshold_frames = static_cast<uint32_t>(frames_per_second * 0.2f); // Treat >0.2s jumps as a new session by default

     auto state_it = stream_timing_states_.find(packet.source_tag);
     if (state_it != stream_timing_states_.end()) {
         auto& existing_state = state_it->second;
        if (!existing_state.is_first_packet && existing_state.clock && reset_threshold_frames > 0) {
            const uint32_t last_ts = existing_state.last_rtp_timestamp;
            const uint32_t current_ts = packet.rtp_timestamp.value();
            const uint32_t delta = std::abs((int)(current_ts) - (int)(last_ts)); // unsigned wrap-around friendly
            bool should_reset = delta > reset_threshold_frames;

            if (should_reset) {
                const auto last_wallclock = existing_state.last_wallclock;
                if (last_wallclock.time_since_epoch().count() != 0) {
                    const auto wallclock_gap = packet.received_time - last_wallclock;
                    const double wallclock_gap_sec = std::chrono::duration<double>(wallclock_gap).count();
                    if (wallclock_gap_sec > 0.0) {
                        const auto delta_frames = static_cast<uint64_t>(delta);
                        const auto expected_frames = static_cast<uint64_t>(std::llround(wallclock_gap_sec * static_cast<double>(frames_per_second)));
                        const auto continuity_slack_frames = static_cast<uint64_t>(std::llround(static_cast<double>(frames_per_second) * kRtpContinuitySlackSeconds));

                        const auto lower_bound = (expected_frames > continuity_slack_frames)
                                                     ? (expected_frames - continuity_slack_frames)
                                                     : 0ULL;
                        const auto upper_bound = expected_frames + continuity_slack_frames;

                        if (delta_frames >= lower_bound && delta_frames <= upper_bound) {
                            should_reset = false;
                            LOG_CPP_DEBUG("[TimeshiftManager] RTP jump matches wall-clock advance for '%s' (delta=%u frames, expected=%llu, slack=%llu). Keeping timing state.",
                                          packet.source_tag.c_str(), delta,
                                          static_cast<unsigned long long>(expected_frames),
                                          static_cast<unsigned long long>(continuity_slack_frames));
                        }
                    }
                }
            }

            if (should_reset) {
                LOG_CPP_INFO("[TimeshiftManager] Detected RTP jump for '%s' (delta=%u frames). Resetting timing state.",
                             packet.source_tag.c_str(), delta);

                size_t reset_position = global_timeshift_buffer_.size();
                auto targets_it = processor_targets_.find(packet.source_tag);
                 if (targets_it != processor_targets_.end()) {
                     for (auto& [instance_id, info] : targets_it->second) {
                         (void)instance_id;
                         info.next_packet_read_index = reset_position;
                         if (info.target_queue) {
                             TaggedAudioPacket discarded;
                             while (info.target_queue->try_pop(discarded)) {
                                 // Drain stale packets so consumers restart immediately.
                             }
                         }
                     }
                 }

                 stream_timing_states_.erase(state_it);
                 state_it = stream_timing_states_.try_emplace(packet.source_tag).first;
                 m_state_version_++;
                 run_loop_cv_.notify_one();
            }
        }
    } else {
         state_it = stream_timing_states_.try_emplace(packet.source_tag).first;
     }

    auto& state = state_it->second;
    if (state.is_first_packet) {
        state.target_buffer_level_ms = m_settings->timeshift_tuning.target_buffer_level_ms;
        state.last_target_update_time = packet.received_time;
    }
    state.total_packets++;
 
     // 1. Initialize StreamClock if it's the first packet for this source
     if (!state.clock) {
         state.clock = std::make_unique<StreamClock>(packet.sample_rate);
     }
 
     // 2. Update the stable clock model
    state.clock->update(packet.rtp_timestamp.value(), packet.received_time);

   state.is_first_packet = false;
   state.last_rtp_timestamp = packet.rtp_timestamp.value();
   state.last_wallclock = packet.received_time;
    state.sample_rate = packet.sample_rate;
    state.channels = packet.channels;
    state.bit_depth = packet.bit_depth;
    state.samples_per_chunk = 0;
    if (packet.sample_rate > 0 && packet.channels > 0 && packet.bit_depth > 0 && (packet.bit_depth % 8) == 0) {
        const std::size_t bytes_per_frame = static_cast<std::size_t>(packet.channels) * static_cast<std::size_t>(packet.bit_depth / 8);
        if (bytes_per_frame > 0) {
            state.samples_per_chunk = static_cast<uint32_t>(packet.audio_data.size() / bytes_per_frame);
        }
    }

    global_timeshift_buffer_.push_back(std::move(packet));
    m_total_packets_added++;
}

TimeshiftManagerStats TimeshiftManager::get_stats() {
    TimeshiftManagerStats stats;
    stats.total_packets_added = m_total_packets_added.load();

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        stats.global_buffer_size = global_timeshift_buffer_.size();
        for (const auto& [source_tag, source_map] : processor_targets_) {
            for (const auto& [instance_id, target_info] : source_map) {
                stats.processor_read_indices[instance_id] = target_info.next_packet_read_index;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(timing_mutex_);
        for (const auto& [source_tag, timing_state] : stream_timing_states_) {
            stats.jitter_estimates[source_tag] = timing_state.jitter_estimate;
            stats.stream_system_jitter_ms[source_tag] = timing_state.system_jitter_estimate_ms;
            stats.stream_total_packets[source_tag] = timing_state.total_packets.load();
            stats.stream_late_packets[source_tag] = timing_state.late_packets_count.load();
            stats.stream_lagging_events[source_tag] = timing_state.lagging_events_count.load();
            stats.stream_tm_buffer_underruns[source_tag] = timing_state.tm_buffer_underruns.load();
            stats.stream_tm_packets_discarded[source_tag] = timing_state.tm_packets_discarded.load();
            stats.stream_last_arrival_time_error_ms[source_tag] = timing_state.last_arrival_time_error_ms;
            stats.stream_target_buffer_level_ms[source_tag] = timing_state.target_buffer_level_ms;
            stats.stream_buffer_target_fill_percentage[source_tag] = timing_state.buffer_target_fill_percentage;

            if (timing_state.arrival_error_samples > 0) {
                stats.stream_avg_arrival_error_ms[source_tag] = timing_state.arrival_error_ms_sum / static_cast<double>(timing_state.arrival_error_samples);
                stats.stream_avg_abs_arrival_error_ms[source_tag] = timing_state.arrival_error_ms_abs_sum / static_cast<double>(timing_state.arrival_error_samples);
                stats.stream_max_arrival_error_ms[source_tag] = timing_state.arrival_error_ms_max;
                stats.stream_min_arrival_error_ms[source_tag] = timing_state.arrival_error_ms_min;
            } else {
                stats.stream_avg_arrival_error_ms[source_tag] = 0.0;
                stats.stream_avg_abs_arrival_error_ms[source_tag] = 0.0;
                stats.stream_max_arrival_error_ms[source_tag] = 0.0;
                stats.stream_min_arrival_error_ms[source_tag] = 0.0;
            }
            stats.stream_arrival_error_sample_count[source_tag] = timing_state.arrival_error_samples;

            if (timing_state.playout_deviation_samples > 0) {
                stats.stream_avg_playout_deviation_ms[source_tag] = timing_state.playout_deviation_ms_sum / static_cast<double>(timing_state.playout_deviation_samples);
                stats.stream_avg_abs_playout_deviation_ms[source_tag] = timing_state.playout_deviation_ms_abs_sum / static_cast<double>(timing_state.playout_deviation_samples);
                stats.stream_max_playout_deviation_ms[source_tag] = timing_state.playout_deviation_ms_max;
                stats.stream_min_playout_deviation_ms[source_tag] = timing_state.playout_deviation_ms_min;
            } else {
                stats.stream_avg_playout_deviation_ms[source_tag] = 0.0;
                stats.stream_avg_abs_playout_deviation_ms[source_tag] = 0.0;
                stats.stream_max_playout_deviation_ms[source_tag] = 0.0;
                stats.stream_min_playout_deviation_ms[source_tag] = 0.0;
            }
            stats.stream_playout_deviation_sample_count[source_tag] = timing_state.playout_deviation_samples;

            if (timing_state.head_playout_lag_samples > 0) {
                stats.stream_avg_head_playout_lag_ms[source_tag] = timing_state.head_playout_lag_ms_sum / static_cast<double>(timing_state.head_playout_lag_samples);
                stats.stream_max_head_playout_lag_ms[source_tag] = timing_state.head_playout_lag_ms_max;
            } else {
                stats.stream_avg_head_playout_lag_ms[source_tag] = 0.0;
                stats.stream_max_head_playout_lag_ms[source_tag] = 0.0;
            }
            stats.stream_head_playout_lag_sample_count[source_tag] = timing_state.head_playout_lag_samples;
            stats.stream_last_head_playout_lag_ms[source_tag] = timing_state.last_head_playout_lag_ms;

            stats.stream_clock_offset_ms[source_tag] = timing_state.last_clock_offset_ms;
            stats.stream_clock_drift_ppm[source_tag] = timing_state.last_clock_drift_ppm;
            stats.stream_clock_last_innovation_ms[source_tag] = timing_state.last_clock_innovation_ms;
            stats.stream_clock_last_measured_offset_ms[source_tag] = timing_state.last_clock_measured_offset_ms;
            if (timing_state.clock_innovation_samples > 0) {
                stats.stream_clock_avg_abs_innovation_ms[source_tag] = timing_state.clock_innovation_abs_sum_ms / static_cast<double>(timing_state.clock_innovation_samples);
            } else {
                stats.stream_clock_avg_abs_innovation_ms[source_tag] = 0.0;
            }
        }
    }

    return stats;
}

/**
 * @brief Registers a new processor as a consumer of the buffer.
 * @param instance_id A unique ID for the processor instance.
 * @param source_tag The source tag the processor is interested in.
 * @param target_queue The processor's input queue.
 * @param initial_delay_ms The initial static delay for the processor.
 * @param initial_timeshift_sec The initial timeshift delay for the processor.
 */
void TimeshiftManager::register_processor(
    const std::string& instance_id,
    const std::string& source_tag,
    std::shared_ptr<PacketQueue> target_queue,
    int initial_delay_ms,
    float initial_timeshift_sec) {
    LOG_CPP_INFO("[TimeshiftManager] Registering processor: instance_id=%s, source_tag=%s, delay=%dms, timeshift=%.2fs",
                 instance_id.c_str(), source_tag.c_str(), initial_delay_ms, initial_timeshift_sec);

    ProcessorTargetInfo info;
    info.target_queue = target_queue;
    info.current_delay_ms = initial_delay_ms;
    info.current_timeshift_backshift_sec = initial_timeshift_sec;
    info.source_tag_filter = source_tag;
    info.is_wildcard = !source_tag.empty() && source_tag.back() == '*';
    if (info.is_wildcard) {
        info.wildcard_prefix = source_tag.substr(0, source_tag.size() - 1);
        LOG_CPP_INFO("[TimeshiftManager] Processor %s registered with wildcard prefix '%s'",
                     instance_id.c_str(), info.wildcard_prefix.c_str());
    } else {
        info.bound_source_tag = source_tag;
    }

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (initial_timeshift_sec > 0.0f && !global_timeshift_buffer_.empty()) {
            auto now = std::chrono::steady_clock::now();
            auto target_past_time = now - std::chrono::milliseconds(initial_delay_ms) - std::chrono::duration<double>(initial_timeshift_sec);
            
            size_t found_idx = global_timeshift_buffer_.size();
            for (size_t i = 0; i < global_timeshift_buffer_.size(); ++i) {
                if (global_timeshift_buffer_[i].received_time >= target_past_time) {
                    found_idx = i;
                    break;
                }
            }
            info.next_packet_read_index = found_idx;
            LOG_CPP_INFO("[TimeshiftManager] Initial timeshift > 0. Set next_packet_read_index to %zu based on %.2fs backshift.",
                          found_idx, initial_timeshift_sec);
        } else {
            info.next_packet_read_index = global_timeshift_buffer_.size();
            LOG_CPP_INFO("[TimeshiftManager] Initial timeshift is 0 or buffer empty. Set next_packet_read_index to end of buffer: %zu", info.next_packet_read_index);
        }
        processor_targets_[source_tag][instance_id] = info;
        LOG_CPP_DEBUG("[TimeshiftManager] Processor %s stored under filter '%s' (wildcard=%d)",
                      instance_id.c_str(), source_tag.c_str(), info.is_wildcard ? 1 : 0);
    }
    LOG_CPP_INFO("[TimeshiftManager] Processor %s registered for source_tag %s with read_idx %zu",
                 instance_id.c_str(), source_tag.c_str(), info.next_packet_read_index);
    m_state_version_++;
    run_loop_cv_.notify_one();
}

/**
 * @brief Unregisters a processor.
 * @param instance_id The ID of the processor instance to unregister.
 * @param source_tag The source tag associated with the processor.
 */
void TimeshiftManager::unregister_processor(const std::string& instance_id, const std::string& source_tag) {
    LOG_CPP_INFO("[TimeshiftManager] Unregistering processor: instance_id=%s, source_tag=%s", instance_id.c_str(), source_tag.c_str());
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto& source_map = processor_targets_[source_tag];
    source_map.erase(instance_id);
    if (source_map.empty()) {
        processor_targets_.erase(source_tag);
        LOG_CPP_INFO("[TimeshiftManager] Source tag %s removed as no processors are listening to it.", source_tag.c_str());
    }
    LOG_CPP_INFO("[TimeshiftManager] Processor %s unregistered.", instance_id.c_str());
    m_state_version_++;
    run_loop_cv_.notify_one();
}

/**
 * @brief Updates the static delay for a registered processor.
 * @param instance_id The ID of the processor.
 * @param delay_ms The new delay in milliseconds.
 */
void TimeshiftManager::update_processor_delay(const std::string& instance_id, int delay_ms) {
    LOG_CPP_INFO("[TimeshiftManager] Updating delay for processor %s to %dms", instance_id.c_str(), delay_ms);
    std::lock_guard<std::mutex> lock(data_mutex_);
    bool found = false;
    for (auto& [tag, source_map] : processor_targets_) {
        auto it = source_map.find(instance_id);
        if (it != source_map.end()) {
            it->second.current_delay_ms = delay_ms;
            found = true;
            break;
        }
    }
    if (!found) {
        LOG_CPP_WARNING("[TimeshiftManager] Attempted to update delay for unknown processor instance_id: %s", instance_id.c_str());
    }
    m_state_version_++;
    run_loop_cv_.notify_one();
}

/**
 * @brief Updates the timeshift delay for a registered processor and recalculates its read position.
 * @param instance_id The ID of the processor.
 * @param timeshift_sec The new timeshift in seconds.
 */
void TimeshiftManager::update_processor_timeshift(const std::string& instance_id, float timeshift_sec) {
    LOG_CPP_INFO("[TimeshiftManager] Updating timeshift for processor %s to %.2fs", instance_id.c_str(), timeshift_sec);
    std::lock_guard<std::mutex> lock(data_mutex_);
    bool found_processor = false;
    for (auto& [tag, source_map] : processor_targets_) {
        auto proc_it = source_map.find(instance_id);
        if (proc_it != source_map.end()) {
            found_processor = true;
            proc_it->second.current_timeshift_backshift_sec = timeshift_sec;

            if (global_timeshift_buffer_.empty()) {
                proc_it->second.next_packet_read_index = 0;
                 LOG_CPP_INFO("[TimeshiftManager] Timeshift updated for %s, buffer empty. Read index set to 0.", instance_id.c_str());
            } else {
                auto now = std::chrono::steady_clock::now();
                auto target_past_time = now - std::chrono::milliseconds(proc_it->second.current_delay_ms) - std::chrono::duration<double>(timeshift_sec);
                
                size_t new_read_idx = global_timeshift_buffer_.size();
                for (size_t i = 0; i < global_timeshift_buffer_.size(); ++i) {
                    if (global_timeshift_buffer_[i].received_time >= target_past_time) {
                        new_read_idx = i;
                        break;
                    }
                }
                proc_it->second.next_packet_read_index = new_read_idx;
                LOG_CPP_INFO("[TimeshiftManager] Timeshift updated for %s. New read_idx: %zu based on %.2fs backshift.",
                             instance_id.c_str(), new_read_idx, timeshift_sec);
            }
            break;
        }
    }
    if (!found_processor) {
        LOG_CPP_WARNING("[TimeshiftManager] Attempted to update timeshift for unknown processor instance_id: %s", instance_id.c_str());
    }
    m_state_version_++;
    run_loop_cv_.notify_one();
}

void TimeshiftManager::reset_stream_state(const std::string& source_tag) {
    LOG_CPP_INFO("[TimeshiftManager] Resetting stream state for tag %s", source_tag.c_str());

    size_t reset_position = 0;
    {
        std::lock_guard<std::mutex> data_lock(data_mutex_);
        reset_position = global_timeshift_buffer_.size();

        for (auto& [filter_tag, source_map] : processor_targets_) {
            for (auto& [instance_id, info] : source_map) {
                (void)instance_id;
                const bool direct_match = (!info.is_wildcard && filter_tag == source_tag);
                const bool bound_match = info.is_wildcard && !info.bound_source_tag.empty() && info.bound_source_tag == source_tag;
                if (!direct_match && !bound_match) {
                    continue;
                }

                info.next_packet_read_index = reset_position;
                if (info.is_wildcard) {
                    info.bound_source_tag.clear();
                }

                if (info.target_queue) {
                    TaggedAudioPacket discarded;
                    while (info.target_queue->try_pop(discarded)) {
                        // Drain stale packets so consumers don't process the old stream.
                    }
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> timing_lock(timing_mutex_);
        stream_timing_states_.erase(source_tag);
    }

    m_state_version_++;
    run_loop_cv_.notify_one();
}

/**
 * @brief The main processing loop for the manager's thread.
 */
void TimeshiftManager::run() {
    LOG_CPP_INFO("[TimeshiftManager] Run loop started.");
    uint64_t last_processed_version = m_state_version_.load();

    while (!stop_flag_) {
        std::unique_lock<std::mutex> lock(data_mutex_);

        // Process any packets that are already due.
        processing_loop_iteration_unlocked();

        // Perform cleanup if needed.
        auto now = std::chrono::steady_clock::now();
        if (now - last_cleanup_time_ > std::chrono::milliseconds(m_settings->timeshift_tuning.cleanup_interval_ms)) {
            cleanup_global_buffer_unlocked();
            last_cleanup_time_ = now;
        }

        // Calculate the next time we need to wake up.
        auto next_wakeup_time = calculate_next_wakeup_time();
        
        // Wait until the next event or until notified.
        run_loop_cv_.wait_until(lock, next_wakeup_time, [this, &last_processed_version] {
            return stop_flag_.load() || (m_state_version_.load() != last_processed_version);
        });

        // Update the version so we don't spin on the same notification.
        last_processed_version = m_state_version_.load();
    }
    LOG_CPP_INFO("[TimeshiftManager] Run loop exiting.");
}

/**
 * @brief A single iteration of the processing loop to dispatch ready packets. Assumes data_mutex_ is held.
 */
void TimeshiftManager::processing_loop_iteration_unlocked() {
    if (global_timeshift_buffer_.empty()) {
        return;
    }

    const auto iteration_start = std::chrono::steady_clock::now();
    auto now = iteration_start;
    const double max_catchup_lag_ms = m_settings->timeshift_tuning.max_catchup_lag_ms;
    size_t packets_processed = 0;

    for (auto& [source_tag, source_map] : processor_targets_) {
        (void)source_tag;
        for (auto& [instance_id, target_info] : source_map) {
            while (target_info.next_packet_read_index < global_timeshift_buffer_.size()) {
                const auto& candidate_packet = global_timeshift_buffer_[target_info.next_packet_read_index];

                if (!match_and_bind_source(target_info, candidate_packet.source_tag)) {
                    if (target_info.is_wildcard && (target_info.next_packet_read_index % 256 == 0)) {
                        LOG_CPP_DEBUG("[TimeshiftManager] Instance %s skipping packet tag '%s' (filter '%s')",
                                      instance_id.c_str(), candidate_packet.source_tag.c_str(), target_info.source_tag_filter.c_str());
                    }
                    target_info.next_packet_read_index++;
                    continue;
                }

                StreamTimingState* timing_state = nullptr;
                {
                    std::lock_guard<std::mutex> lock(timing_mutex_);
                    auto timing_it = stream_timing_states_.find(candidate_packet.source_tag);
                    if (timing_it != stream_timing_states_.end() && timing_it->second.clock) {
                        timing_state = &timing_it->second;
                    }
                }

                if (!timing_state) {
                    target_info.next_packet_read_index++;
                    continue;
                }

                if (!candidate_packet.rtp_timestamp.has_value() || candidate_packet.sample_rate == 0) {
                    target_info.next_packet_read_index++;
                    continue;
                }

                now = std::chrono::steady_clock::now();

                // --- Playout Time Calculation ---
                // 1. Get expected arrival time from the stable clock
                auto expected_arrival_time = timing_state->clock->get_expected_arrival_time(candidate_packet.rtp_timestamp.value());

                // 2. Add the adaptive playout delay
                const double timeshift_backshift_ms = std::max(0.0f, target_info.current_timeshift_backshift_sec) * 1000.0;
                double base_latency_ms = std::max<double>(
                    target_info.current_delay_ms,
                    m_settings->timeshift_tuning.target_buffer_level_ms);
                const double max_adaptive_delay_ms = m_settings->timeshift_tuning.max_adaptive_delay_ms;
                if (max_adaptive_delay_ms > 0.0) {
                    base_latency_ms = std::min(base_latency_ms, max_adaptive_delay_ms);
                }
                const double desired_latency_ms = base_latency_ms + timeshift_backshift_ms;

                timing_state->target_buffer_level_ms = desired_latency_ms;
                timing_state->last_target_update_time = now;

                auto ideal_playout_time = expected_arrival_time + std::chrono::duration<double, std::milli>(desired_latency_ms);
                auto time_until_playout_ms = std::chrono::duration<double, std::milli>(ideal_playout_time - now).count();

                double head_lag_ms = std::max(-time_until_playout_ms, 0.0);
                timing_state->last_head_playout_lag_ms = head_lag_ms;
                timing_state->head_playout_lag_ms_sum += head_lag_ms;
                timing_state->head_playout_lag_ms_max = std::max(timing_state->head_playout_lag_ms_max, head_lag_ms);
                timing_state->head_playout_lag_samples++;

                // Check if the packet is ready to be played
                if (ideal_playout_time <= now) {
                    const double lateness_ms = -time_until_playout_ms;
                    if (lateness_ms > m_settings->timeshift_tuning.late_packet_threshold_ms) {
                        timing_state->late_packets_count++;
                    }
                    if (lateness_ms > 0.0) {
                        profiling_total_lateness_ms_ += lateness_ms;
                        profiling_packets_late_count_++;
                    }

                    if (timing_state) {
                        timing_state->playout_deviation_ms_sum += lateness_ms;
                        timing_state->playout_deviation_ms_abs_sum += std::abs(lateness_ms);
                        timing_state->playout_deviation_ms_max = std::max(timing_state->playout_deviation_ms_max, lateness_ms);
                        timing_state->playout_deviation_ms_min = std::min(timing_state->playout_deviation_ms_min, lateness_ms);
                        timing_state->playout_deviation_samples++;
                    }

                    auto compute_catchup_rate = [&](double backlog_ms) -> double {
                        if (backlog_ms <= 0.5 || !m_settings) {
                            return 1.0;
                        }
                        const auto& tuning = m_settings->timeshift_tuning;
                        double target_ms_per_sec = tuning.target_recovery_rate_ms_per_sec;
                        target_ms_per_sec += tuning.catchup_boost_gain * backlog_ms;
                        const double max_increment = tuning.absolute_max_playback_rate - 1.0;
                        if (max_increment <= 0.0) {
                            return 1.0;
                        }
                        double rate = 1.0 + std::min(target_ms_per_sec / 1000.0, max_increment);
                        rate = std::min(rate, tuning.max_playback_rate);
                        rate = std::max(rate, 1.0);
                        return rate;
                    };

                    double desired_rate = compute_catchup_rate(lateness_ms);
                    const double max_catchup_lag_ms = m_settings ? m_settings->timeshift_tuning.max_catchup_lag_ms : 0.0;
                    const double absolute_max = m_settings ? m_settings->timeshift_tuning.absolute_max_playback_rate : 1.0;
                    bool backlog_unmanageable = (max_catchup_lag_ms > 0.0 && lateness_ms > max_catchup_lag_ms && desired_rate >= absolute_max - 1e-3);

                    if (backlog_unmanageable) {
                        timing_state->tm_packets_discarded++;
                        profiling_packets_dropped_++;
                        const std::string& log_tag = target_info.source_tag_filter.empty() ? candidate_packet.source_tag : target_info.source_tag_filter;
                        LOG_CPP_WARNING(
                            "[TimeshiftManager] Dropping packet for %s; backlog %.2f ms exceeds limit with max rate %.2f",
                            log_tag.c_str(),
                            lateness_ms,
                            absolute_max);
                        target_info.next_packet_read_index++;
                        continue;
                    }

                    TaggedAudioPacket packet_to_send = candidate_packet;
                    timing_state->current_buffer_level_ms = 0.0;
                    timing_state->buffer_target_fill_percentage = 0.0;
                    timing_state->current_playback_rate = desired_rate;
                    packet_to_send.playback_rate = desired_rate;

                    const bool queue_missing = (target_info.target_queue == nullptr);
                    if (queue_missing) {
                        LOG_CPP_WARNING(
                            "[TimeshiftManager] Target queue missing for %s; dropping packet.",
                            instance_id.c_str());
                        timing_state->tm_packets_discarded++;
                        profiling_packets_dropped_++;
                        target_info.next_packet_read_index++;
                        continue;
                    }

                    const auto compute_chunk_duration_ms = [](const TaggedAudioPacket& pkt) -> double {
                        if (pkt.sample_rate <= 0 || pkt.channels <= 0 || pkt.bit_depth <= 0) {
                            return 0.0;
                        }
                        const std::size_t bytes_per_frame = static_cast<std::size_t>(pkt.channels) * static_cast<std::size_t>(pkt.bit_depth / 8);
                        if (bytes_per_frame == 0) {
                            return 0.0;
                        }
                        const std::size_t frames = bytes_per_frame > 0
                            ? pkt.audio_data.size() / bytes_per_frame
                            : 0;
                        if (frames == 0) {
                            return 0.0;
                        }
                        return (static_cast<double>(frames) * 1000.0) / static_cast<double>(pkt.sample_rate);
                    };

                    double chunk_duration_ms = compute_chunk_duration_ms(candidate_packet);
                    if (chunk_duration_ms <= 0.0) {
                        chunk_duration_ms = 1.0; // Fallback to avoid division by zero; will clamp to single chunk
                    }

                    std::size_t dynamic_cap = static_cast<std::size_t>(std::ceil(desired_latency_ms / chunk_duration_ms));
                    if (dynamic_cap == 0) {
                        dynamic_cap = 1;
                    }

                    const std::size_t configured_cap = (m_settings)
                        ? m_settings->timeshift_tuning.max_processor_queue_packets
                        : 0;
                    if (configured_cap > 0) {
                        dynamic_cap = std::min(dynamic_cap, configured_cap);
                    }

                    auto queue_ptr = target_info.target_queue;
                    if (dynamic_cap > 0) {
                        bool logged_trim = false;
                        while (queue_ptr->size() >= dynamic_cap) {
                            TaggedAudioPacket discarded_packet;
                            if (!queue_ptr->try_pop(discarded_packet)) {
                                break;
                            }
                            timing_state->tm_packets_discarded++;
                            profiling_packets_dropped_++;
                            if (!logged_trim) {
                                LOG_CPP_WARNING(
                                    "[TimeshiftManager] Trimmed queued packets for %s to catch up (cap=%zu chunks).",
                                    instance_id.c_str(),
                                    dynamic_cap);
                                logged_trim = true;
                            }
                        }
                    }

                    TaggedAudioPacket queued_packet = packet_to_send;
                    auto push_result = (dynamic_cap > 0)
                        ? queue_ptr->push_bounded(std::move(queued_packet), dynamic_cap, false)
                        : queue_ptr->push_bounded(std::move(queued_packet), 0, false);

                    if (push_result == PacketQueue::PushResult::QueueFull) {
                        TaggedAudioPacket dropped_oldest;
                        bool trimmed = queue_ptr->try_pop(dropped_oldest);
                        if (trimmed) {
                            timing_state->tm_packets_discarded++;
                            profiling_packets_dropped_++;
                            LOG_CPP_WARNING(
                                "[TimeshiftManager] Forced trim for %s while enqueueing (cap=%zu).",
                                instance_id.c_str(),
                                dynamic_cap);
                            queued_packet = packet_to_send;
                            push_result = (dynamic_cap > 0)
                                ? queue_ptr->push_bounded(std::move(queued_packet), dynamic_cap, false)
                                : queue_ptr->push_bounded(std::move(queued_packet), 0, false);
                        }
                    }

                    if (push_result == PacketQueue::PushResult::QueueStopped) {
                        LOG_CPP_WARNING(
                            "[TimeshiftManager] Target queue for %s stopped; discarding packet.",
                            instance_id.c_str());
                        timing_state->tm_packets_discarded++;
                        profiling_packets_dropped_++;
                        target_info.next_packet_read_index++;
                        continue;
                    }

                    if (push_result != PacketQueue::PushResult::Pushed) {
                        profiling_packets_dropped_++;
                        timing_state->tm_packets_discarded++;
                        target_info.next_packet_read_index++;
                        continue;
                    }

                    profiling_packets_dispatched_++;
                    packets_processed++;

                    timing_state->last_played_rtp_timestamp = candidate_packet.rtp_timestamp.value();

                    target_info.next_packet_read_index++;

                    now = std::chrono::steady_clock::now();
                    if (ideal_playout_time > now) {
                        continue;
                    }

                    // If we consumed the available slack, let the outer loop reschedule.
                    break;
                } else {
                    // The loop is breaking because the next packet's playout time is in the future.
                    timing_state->current_playback_rate = 1.0;
                    break;
                }
            }
        }
    }

    const auto iteration_end = std::chrono::steady_clock::now();
    last_iteration_finish_time_ = iteration_end;

    if (packets_processed > 0) {
        const double iteration_us = std::chrono::duration<double, std::micro>(iteration_end - iteration_start).count();
        const double per_packet_us = iteration_us / static_cast<double>(packets_processed);

        if (processing_budget_initialized_) {
            smoothed_processing_per_packet_us_ =
                smoothed_processing_per_packet_us_ * (1.0 - kProcessingBudgetAlpha) +
                per_packet_us * kProcessingBudgetAlpha;
        } else {
            smoothed_processing_per_packet_us_ = per_packet_us;
            processing_budget_initialized_ = true;
        }
    }

    if (m_settings->profiler.enabled) {
        maybe_log_profiler_unlocked(iteration_end);
    }
}

void TimeshiftManager::reset_profiler_counters_unlocked(std::chrono::steady_clock::time_point now) {
    profiling_last_log_time_ = now;
    profiling_packets_dispatched_ = 0;
    profiling_packets_dropped_ = 0;
    profiling_packets_late_count_ = 0;
    profiling_total_lateness_ms_ = 0.0;
}

void TimeshiftManager::maybe_log_profiler_unlocked(std::chrono::steady_clock::time_point now) {
    if (!m_settings || !m_settings->profiler.enabled) {
        return;
    }

    long interval_ms = m_settings->profiler.log_interval_ms;
    if (interval_ms <= 0) {
        interval_ms = 1000;
    }
    auto interval = std::chrono::milliseconds(interval_ms);
    if (now - profiling_last_log_time_ < interval) {
        return;
    }

    const size_t buffer_size = global_timeshift_buffer_.size();
    size_t total_targets = 0;
    size_t total_backlog = 0;
    size_t max_backlog = 0;

    for (const auto& [source_tag, source_map] : processor_targets_) {
        (void)source_tag;
        for (const auto& [instance_id, target_info] : source_map) {
            (void)instance_id;
            total_targets++;
            size_t backlog = 0;
            if (target_info.next_packet_read_index < global_timeshift_buffer_.size()) {
                backlog = global_timeshift_buffer_.size() - target_info.next_packet_read_index;
            }
            total_backlog += backlog;
            if (backlog > max_backlog) {
                max_backlog = backlog;
            }
        }
    }

    const double avg_backlog = total_targets > 0 ? static_cast<double>(total_backlog) / static_cast<double>(total_targets) : 0.0;
    const double avg_late_ms = profiling_packets_late_count_ > 0
                                    ? (profiling_total_lateness_ms_ / static_cast<double>(profiling_packets_late_count_))
                                    : 0.0;

    LOG_CPP_INFO(
        "[Profiler][Timeshift][Global] buffer=%zu targets=%zu avg_backlog=%.2f max_backlog=%zu dispatched=%llu dropped=%llu late_count=%llu avg_late_ms=%.2f late_ms_sum=%.2f proc_budget_us=%.2f",
        buffer_size,
        total_targets,
        avg_backlog,
        max_backlog,
        static_cast<unsigned long long>(profiling_packets_dispatched_),
        static_cast<unsigned long long>(profiling_packets_dropped_),
        static_cast<unsigned long long>(profiling_packets_late_count_),
        avg_late_ms,
        profiling_total_lateness_ms_,
        processing_budget_initialized_ ? smoothed_processing_per_packet_us_ : 0.0);

    {
        std::lock_guard<std::mutex> timing_lock(timing_mutex_);
        for (const auto& [source_tag, timing_state] : stream_timing_states_) {
            const double arrival_avg = timing_state.arrival_error_samples > 0
                                            ? (timing_state.arrival_error_ms_sum / static_cast<double>(timing_state.arrival_error_samples))
                                            : 0.0;
            const double arrival_abs_avg = timing_state.arrival_error_samples > 0
                                                ? (timing_state.arrival_error_ms_abs_sum / static_cast<double>(timing_state.arrival_error_samples))
                                                : 0.0;
            const double playout_avg = timing_state.playout_deviation_samples > 0
                                            ? (timing_state.playout_deviation_ms_sum / static_cast<double>(timing_state.playout_deviation_samples))
                                            : 0.0;
            const double playout_abs_avg = timing_state.playout_deviation_samples > 0
                                                 ? (timing_state.playout_deviation_ms_abs_sum / static_cast<double>(timing_state.playout_deviation_samples))
                                                 : 0.0;
            const double head_avg = timing_state.head_playout_lag_samples > 0
                                           ? (timing_state.head_playout_lag_ms_sum / static_cast<double>(timing_state.head_playout_lag_samples))
                                           : 0.0;
            double clock_update_age_ms = 0.0;
            if (timing_state.clock && timing_state.clock->is_initialized()) {
                auto last_update = timing_state.clock->get_last_update_time();
                if (last_update != std::chrono::steady_clock::time_point{}) {
                    clock_update_age_ms = std::chrono::duration<double, std::milli>(now - last_update).count();
                }
            }

        LOG_CPP_INFO(
            "[Profiler][Timeshift][Stream %s] jitter=%.2fms sys_jitter=%.2fms sys_delay=%.2fms clk_offset=%.3fms drift=%.3fppm clk_innov_last=%.3fms clk_innov_avg_abs=%.3fms clk_update_age=%.2fms clk_meas_offset=%.3fms arrival(avg=%.3fms abs_avg=%.3fms max=%.3fms min=%.3fms samples=%llu) playout_dev(avg=%.3fms abs_avg=%.3fms max=%.3fms min=%.3fms samples=%llu) head_lag(last=%.3fms avg=%.3fms max=%.3fms samples=%llu) buffer(cur=%.3fms target=%.3fms fill=%.1f%% playback_rate=%.6f)",
            source_tag.c_str(),
            timing_state.jitter_estimate,
            timing_state.system_jitter_estimate_ms,
            timing_state.last_system_delay_ms,
            timing_state.last_clock_offset_ms,
            timing_state.last_clock_drift_ppm,
            timing_state.last_clock_innovation_ms,
            timing_state.clock_innovation_samples > 0 ? (timing_state.clock_innovation_abs_sum_ms / static_cast<double>(timing_state.clock_innovation_samples)) : 0.0,
            clock_update_age_ms,
            timing_state.last_clock_measured_offset_ms,
            arrival_avg,
            arrival_abs_avg,
            timing_state.arrival_error_samples > 0 ? timing_state.arrival_error_ms_max : 0.0,
            timing_state.arrival_error_samples > 0 ? timing_state.arrival_error_ms_min : 0.0,
            static_cast<unsigned long long>(timing_state.arrival_error_samples),
            playout_avg,
            playout_abs_avg,
            timing_state.playout_deviation_samples > 0 ? timing_state.playout_deviation_ms_max : 0.0,
            timing_state.playout_deviation_samples > 0 ? timing_state.playout_deviation_ms_min : 0.0,
            static_cast<unsigned long long>(timing_state.playout_deviation_samples),
            timing_state.last_head_playout_lag_ms,
            head_avg,
            timing_state.head_playout_lag_samples > 0 ? timing_state.head_playout_lag_ms_max : 0.0,
            static_cast<unsigned long long>(timing_state.head_playout_lag_samples),
            timing_state.current_buffer_level_ms,
            timing_state.target_buffer_level_ms,
            timing_state.buffer_target_fill_percentage,
            timing_state.current_playback_rate);
        }
    }

    if (m_settings && m_settings->telemetry.enabled) {
        long telemetry_interval_ms = m_settings->telemetry.log_interval_ms;
        if (telemetry_interval_ms <= 0) {
            telemetry_interval_ms = 30000;
        }
        auto telemetry_interval = std::chrono::milliseconds(telemetry_interval_ms);
        if (telemetry_last_log_time_.time_since_epoch().count() == 0 || now - telemetry_last_log_time_ >= telemetry_interval) {
            telemetry_last_log_time_ = now;

            const size_t global_size = global_timeshift_buffer_.size();
            double global_span_ms = 0.0;
            if (global_size > 1) {
                const auto oldest = global_timeshift_buffer_.front().received_time;
                const auto newest = global_timeshift_buffer_.back().received_time;
                global_span_ms = std::chrono::duration<double, std::milli>(newest - oldest).count();
                if (global_span_ms < 0.0) {
                    global_span_ms = 0.0;
                }
            }
            size_t processor_count = 0;
            size_t total_queue = 0;
            size_t max_queue = 0;
            for (const auto& [source_tag, source_map] : processor_targets_) {
                (void)source_tag;
                for (const auto& [instance_id, info] : source_map) {
                    (void)instance_id;
                    processor_count++;
                    if (info.target_queue) {
                        const size_t queue_size = info.target_queue->size();
                        total_queue += queue_size;
                        if (queue_size > max_queue) {
                            max_queue = queue_size;
                        }
                    }
                }
            }
            const double avg_queue = processor_count > 0
                ? static_cast<double>(total_queue) / static_cast<double>(processor_count)
                : 0.0;

            struct StreamSnapshot {
                double current_buffer_ms = 0.0;
                double target_buffer_ms = 0.0;
                double playback_rate = 1.0;
                double chunk_ms = 0.0;
                double playhead_skew_ms = 0.0;
            };

            std::unordered_map<std::string, StreamSnapshot> stream_snapshots;
            size_t stream_count = 0;
            double avg_buffer_level_ms = 0.0;
            double max_buffer_level_ms = 0.0;
            {
                std::lock_guard<std::mutex> timing_lock(timing_mutex_);
                for (const auto& [tag, timing_state] : stream_timing_states_) {
                    StreamSnapshot snap;
                    snap.current_buffer_ms = timing_state.current_buffer_level_ms;
                    snap.target_buffer_ms = timing_state.target_buffer_level_ms;
                    snap.playback_rate = timing_state.current_playback_rate;
                    if (timing_state.sample_rate > 0 && timing_state.samples_per_chunk > 0) {
                        snap.chunk_ms = (static_cast<double>(timing_state.samples_per_chunk) * 1000.0) /
                                        static_cast<double>(timing_state.sample_rate);
                    }
                    if (timing_state.clock && timing_state.clock->is_initialized() && timing_state.last_played_rtp_timestamp != 0) {
                        auto expected = timing_state.clock->get_expected_arrival_time(timing_state.last_played_rtp_timestamp);
                        snap.playhead_skew_ms = std::chrono::duration<double, std::milli>(now - expected).count();
                    }
                    stream_snapshots.emplace(tag, snap);
                    stream_count++;
                    avg_buffer_level_ms += snap.current_buffer_ms;
                    if (snap.current_buffer_ms > max_buffer_level_ms) {
                        max_buffer_level_ms = snap.current_buffer_ms;
                    }
                }
                if (stream_count > 0) {
                    avg_buffer_level_ms /= static_cast<double>(stream_count);
                }
            }

            LOG_CPP_INFO(
                "[Telemetry][Timeshift] buffer=%zu span_ms=%.3f processors=%zu queue_total=%zu queue_max=%zu queue_avg=%.2f streams=%zu buffer_avg_ms=%.3f buffer_max_ms=%.3f",
                global_size,
                global_span_ms,
                processor_count,
                total_queue,
                max_queue,
                avg_queue,
                stream_count,
                avg_buffer_level_ms,
                max_buffer_level_ms);

            for (const auto& [source_tag, source_map] : processor_targets_) {
                for (const auto& [instance_id, info] : source_map) {
                    const std::string& active = active_tag(info);
                    const auto snap_it = stream_snapshots.find(active);
                    const double chunk_ms = (snap_it != stream_snapshots.end()) ? snap_it->second.chunk_ms : 0.0;
                    const size_t queue_size = info.target_queue ? info.target_queue->size() : 0;
                    const double queue_ms = (chunk_ms > 0.0)
                        ? static_cast<double>(queue_size) * chunk_ms
                        : 0.0;
                    const double current_buf_ms = (snap_it != stream_snapshots.end()) ? snap_it->second.current_buffer_ms : 0.0;
                    const double target_buf_ms = (snap_it != stream_snapshots.end()) ? snap_it->second.target_buffer_ms : 0.0;
                    const double playback_rate = (snap_it != stream_snapshots.end()) ? snap_it->second.playback_rate : 1.0;
                    double head_age_ms = 0.0;
                    if (info.next_packet_read_index < global_timeshift_buffer_.size()) {
                        const auto& head_packet = global_timeshift_buffer_[info.next_packet_read_index];
                        head_age_ms = std::chrono::duration<double, std::milli>(now - head_packet.received_time).count();
                        if (head_age_ms < 0.0) {
                            head_age_ms = 0.0;
                        }
                    }
                    LOG_CPP_INFO(
                        "[Telemetry][Timeshift][Queue %s] source=%s backlog_chunks=%zu backlog_ms=%.3f head_age_ms=%.3f playhead_skew_ms=%.3f current_buffer_ms=%.3f target_buffer_ms=%.3f playback_rate=%.3f",
                        instance_id.c_str(),
                        active.c_str(),
                        queue_size,
                        queue_ms,
                        head_age_ms,
                        (snap_it != stream_snapshots.end()) ? snap_it->second.playhead_skew_ms : 0.0,
                        current_buf_ms,
                        target_buf_ms,
                        playback_rate);
                }
            }
        }
    }

    reset_profiler_counters_unlocked(now);
}

/**
 * @brief Periodically cleans up old packets from the global buffer. Assumes data_mutex_ is held.
 */
void TimeshiftManager::cleanup_global_buffer_unlocked() {
    if (global_timeshift_buffer_.empty()) {
        return; // Nothing to do
    }

    auto oldest_allowed_time_by_duration = std::chrono::steady_clock::now() - max_buffer_duration_sec_;
    
    size_t remove_count = 0;
    for (const auto& packet : global_timeshift_buffer_) {
        if (packet.received_time < oldest_allowed_time_by_duration) {
            remove_count++;
        } else {
            // Packets are ordered by time, so we can stop here
            break;
        }
    }

    if (remove_count > 0) {
        LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: Removing %zu packets older than max duration.", remove_count);

        // Now, adjust all processor indices.
        for (auto& [tag, source_map] : processor_targets_) {
            for (auto& [id, proc_info] : source_map) {
                if (proc_info.next_packet_read_index < remove_count) {
                    // The processor's read index is within the block to be removed.
                    // We must determine if it's truly lagging or just idle.
                    bool is_truly_lagging = false;
                    const std::string& bound_tag = active_tag(proc_info);
                    for (size_t i = proc_info.next_packet_read_index; i < remove_count; ++i) {
                        if (!bound_tag.empty() && global_timeshift_buffer_[i].source_tag == bound_tag) {
                            // It missed a packet it was supposed to play. It's lagging.
                            is_truly_lagging = true;
                            break;
                        }
                    }

                    if (is_truly_lagging) {
                        LOG_CPP_WARNING("[TimeshiftManager] Cleanup: Processor %s was lagging. Its read index %zu was inside the removed block of size %zu. Forcing catch-up to index 0.",
                                        id.c_str(), proc_info.next_packet_read_index, remove_count);
                        
                        // Safely increment the lagging event counter for this stream
                        if (!bound_tag.empty()) {
                            std::lock_guard<std::mutex> lock(timing_mutex_);
                            if (stream_timing_states_.count(bound_tag)) {
                                stream_timing_states_.at(bound_tag).lagging_events_count++;
                            }
                        }

                        proc_info.next_packet_read_index = 0;
                    } else {
                        // The processor was not lagging, just idle. All packets in the removed
                        // block were for other streams. Silently catch it up.
                        LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: Idle processor %s caught up. Its read index %zu was shifted past the removed block of size %zu.",
                                      id.c_str(), proc_info.next_packet_read_index, remove_count);
                        proc_info.next_packet_read_index = 0; // The new start of the buffer
                    }
                } else {
                    // The processor was ahead of the removed section, just shift its index back.
                    proc_info.next_packet_read_index -= remove_count;
                }
            }
        }
        while (remove_count-- > 0 && !global_timeshift_buffer_.empty()) {
            global_timeshift_buffer_.pop_front();
        }
    } else {
        LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: No packets older than max duration to remove.");
    }
    LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: Global buffer size after cleanup: %zu", global_timeshift_buffer_.size());
}

/**
 * @brief Calculates the time point for the next event to occur.
 * @return The time point of the next scheduled event.
 * @note This function assumes the caller holds the data_mutex.
 */
std::chrono::steady_clock::time_point TimeshiftManager::calculate_next_wakeup_time() {
    const auto now = std::chrono::steady_clock::now();
    const auto reference_now = (last_iteration_finish_time_.time_since_epoch().count() != 0)
                                   ? std::max(now, last_iteration_finish_time_)
                                   : now;

    auto next_cleanup_time = last_cleanup_time_ + std::chrono::milliseconds(m_settings->timeshift_tuning.cleanup_interval_ms);
    auto earliest_time = std::chrono::steady_clock::time_point::max();

    const auto max_sleep_time = reference_now + std::chrono::milliseconds(m_settings->timeshift_tuning.loop_max_sleep_ms);

    for (const auto& [source_tag, source_map] : processor_targets_) {
        for (const auto& [instance_id, target_info] : source_map) {
            
            if (target_info.next_packet_read_index >= global_timeshift_buffer_.size()) {
                continue;
            }

            const auto& next_packet = global_timeshift_buffer_[target_info.next_packet_read_index];
            if (!next_packet.rtp_timestamp.has_value() || next_packet.sample_rate == 0) {
                continue;
            }
            
            const StreamTimingState* timing_state = nullptr;
            {
                std::lock_guard<std::mutex> lock(timing_mutex_);
                if (stream_timing_states_.count(source_tag) && stream_timing_states_.at(source_tag).clock) {
                    timing_state = &stream_timing_states_.at(source_tag);
                }
            }
            
            if (!timing_state) {
                continue;
            }

            auto expected_arrival_time = timing_state->clock->get_expected_arrival_time(next_packet.rtp_timestamp.value());
            const double timeshift_backshift_ms = std::max(0.0f, target_info.current_timeshift_backshift_sec) * 1000.0;
            double base_latency_ms = std::max<double>(
                target_info.current_delay_ms,
                m_settings->timeshift_tuning.target_buffer_level_ms);
            const double max_adaptive_delay_ms = m_settings->timeshift_tuning.max_adaptive_delay_ms;
            if (max_adaptive_delay_ms > 0.0) {
                base_latency_ms = std::min(base_latency_ms, max_adaptive_delay_ms);
            }
            const double desired_latency_ms = base_latency_ms + timeshift_backshift_ms;

            const double state_target_ms = (timing_state->target_buffer_level_ms > 0.0)
                                               ? timing_state->target_buffer_level_ms
                                               : desired_latency_ms;
            double effective_latency_ms = std::max(desired_latency_ms, state_target_ms);
            auto ideal_playout_time = expected_arrival_time + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                            std::chrono::duration<double, std::milli>(effective_latency_ms));

            auto candidate_time = ideal_playout_time;
            if (processing_budget_initialized_ && smoothed_processing_per_packet_us_ > 0.0) {
                const auto budget = std::chrono::microseconds(
                    static_cast<int64_t>(smoothed_processing_per_packet_us_));
                if (ideal_playout_time > reference_now) {
                    if (budget < (ideal_playout_time - reference_now)) {
                        candidate_time = ideal_playout_time - budget;
                    } else {
                        candidate_time = reference_now;
                    }
                }
            }

            if (candidate_time < earliest_time) {
                earliest_time = candidate_time;
            }
        }
    }

    if (earliest_time == std::chrono::steady_clock::time_point::max()) {
        earliest_time = reference_now;
    } else if (earliest_time < reference_now) {
        earliest_time = reference_now;
    }

    return std::min({earliest_time, next_cleanup_time, max_sleep_time});
}


} // namespace audio
} // namespace screamrouter
