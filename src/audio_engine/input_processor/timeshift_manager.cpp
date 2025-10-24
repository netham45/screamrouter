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

namespace screamrouter {
namespace audio {

namespace {
constexpr double kProcessingBudgetAlpha = 0.2;
}

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
     const uint32_t reset_threshold_frames = frames_per_second * .2; // Treat >.2s jumps as a new session

     auto state_it = stream_timing_states_.find(packet.source_tag);
     if (state_it != stream_timing_states_.end()) {
         auto& existing_state = state_it->second;
         if (!existing_state.is_first_packet && existing_state.clock && reset_threshold_frames > 0) {
             const uint32_t last_ts = existing_state.last_rtp_timestamp;
             const uint32_t current_ts = packet.rtp_timestamp.value();
             const uint32_t delta = std::abs((int)(current_ts) - (int)(last_ts)); // unsigned wrap-around friendly
             if (delta > reset_threshold_frames) {
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
 
     // 3. Calculate jitter based on the stable clock
     if (!state.is_first_packet) {
        auto expected_arrival_time = state.clock->get_expected_arrival_time(packet.rtp_timestamp.value());
        auto arrival_error = packet.received_time - expected_arrival_time;
        double arrival_error_ms = std::chrono::duration<double, std::milli>(arrival_error).count();

          // Inter-arrival jitter calculation (RFC 3550)
          // D(i,j) = (Rj - Ri) - (Sj - Si) = (Rj - Sj) - (Ri - Si)
         // Our 'arrival_error' is equivalent to (Ri - Si) if we consider Si to be the expected arrival time.
         // So, transit_time_diff = arrival_error(i) - arrival_error(i-1)
         // For simplicity and robustness, we use a slightly different but effective approach.
         // We calculate the deviation from the expected arrival time.
         // Correct inter-arrival jitter calculation (based on RFC 3550)
         // J(i) = J(i-1) + (|D(i-1, i)| - J(i-1))/16
         // D is the difference in transit time for two packets.
         // Our arrival_error_ms is the transit time relative to the stable clock.
         double transit_time_variation = std::abs(arrival_error_ms - state.last_arrival_time_error_ms);
         double jitter_diff = transit_time_variation - state.jitter_estimate;
         state.jitter_estimate += jitter_diff / m_settings->timeshift_tuning.jitter_smoothing_factor;
         state.jitter_estimate = std::max(1.0, state.jitter_estimate); // Enforce a minimum jitter of 1.0ms
         state.last_arrival_time_error_ms = arrival_error_ms;

         state.arrival_error_ms_sum += arrival_error_ms;
         state.arrival_error_ms_abs_sum += std::abs(arrival_error_ms);
         state.arrival_error_ms_max = std::max(state.arrival_error_ms_max, arrival_error_ms);
         state.arrival_error_ms_min = std::min(state.arrival_error_ms_min, arrival_error_ms);
         state.arrival_error_samples++;

         if (state.clock->is_initialized()) {
             state.last_clock_offset_ms = state.clock->get_offset_seconds() * 1000.0;
             state.last_clock_drift_ppm = state.clock->get_drift_ppm();
             state.last_clock_innovation_ms = state.clock->get_last_innovation_seconds() * 1000.0;
             state.last_clock_measured_offset_ms = state.clock->get_last_measured_offset_seconds() * 1000.0;
             state.clock_innovation_abs_sum_ms += std::abs(state.last_clock_innovation_ms);
             state.clock_innovation_samples++;
         }
     }

     state.is_first_packet = false;
     state.last_rtp_timestamp = packet.rtp_timestamp.value();
     state.last_wallclock = packet.received_time;
 
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

        auto targets_it = processor_targets_.find(source_tag);
        if (targets_it != processor_targets_.end()) {
            for (auto& [instance_id, info] : targets_it->second) {
                (void)instance_id;
                info.next_packet_read_index = reset_position;
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
        for (auto& [instance_id, target_info] : source_map) {
            
            StreamTimingState* timing_state = nullptr;
            {
                std::lock_guard<std::mutex> lock(timing_mutex_);
                if (stream_timing_states_.count(source_tag) && stream_timing_states_.at(source_tag).clock) {
                    timing_state = &stream_timing_states_.at(source_tag);
                }
            }

            while (target_info.next_packet_read_index < global_timeshift_buffer_.size()) {
                const auto& candidate_packet = global_timeshift_buffer_[target_info.next_packet_read_index];

                if (candidate_packet.source_tag != target_info.source_tag_filter) {
                    target_info.next_packet_read_index++;
                    continue;
                }

                if (!timing_state) {
                    break;
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
                const double base_latency_target_ms = std::max<double>(
                    target_info.current_delay_ms,
                    m_settings->timeshift_tuning.target_buffer_level_ms);
                const double jitter_padding_ms = m_settings->timeshift_tuning.jitter_safety_margin_multiplier * timing_state->jitter_estimate;
                double desired_latency_ms = base_latency_target_ms + timeshift_backshift_ms + jitter_padding_ms;

                double effective_latency_ms = timing_state->target_buffer_level_ms;
                if (effective_latency_ms <= 0.0) {
                    effective_latency_ms = desired_latency_ms;
                }

                if (desired_latency_ms > effective_latency_ms) {
                    effective_latency_ms = desired_latency_ms;
                } else {
                    double delta_seconds = 0.0;
                    if (timing_state->last_target_update_time.time_since_epoch().count() != 0) {
                        delta_seconds = std::chrono::duration<double>(now - timing_state->last_target_update_time).count();
                        delta_seconds = std::max(0.0, delta_seconds);
                    }
                    effective_latency_ms = std::max(
                        desired_latency_ms,
                        effective_latency_ms - (m_settings->timeshift_tuning.target_recovery_rate_ms_per_sec * delta_seconds));
                }

                timing_state->target_buffer_level_ms = effective_latency_ms;
                timing_state->last_target_update_time = now;

                auto ideal_playout_time = expected_arrival_time + std::chrono::duration<double, std::milli>(effective_latency_ms);

                auto time_until_playout_ms = std::chrono::duration<double, std::milli>(ideal_playout_time - now).count();

                if (timing_state) {
                    double head_lag_ms = std::max(-time_until_playout_ms, 0.0);
                    timing_state->last_head_playout_lag_ms = head_lag_ms;
                    timing_state->head_playout_lag_ms_sum += head_lag_ms;
                    timing_state->head_playout_lag_ms_max = std::max(timing_state->head_playout_lag_ms_max, head_lag_ms);
                    timing_state->head_playout_lag_samples++;
                }

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

                    if (max_catchup_lag_ms > 0.0 && lateness_ms > max_catchup_lag_ms) {
                        timing_state->tm_packets_discarded++;
                        profiling_packets_dropped_++;
                        LOG_CPP_DEBUG(
                            "[TimeshiftManager] Dropping late packet for source '%s'. Lateness=%.2f ms exceeds catchup limit=%.2f ms.",
                            target_info.source_tag_filter.c_str(),
                            lateness_ms,
                            max_catchup_lag_ms);

                        target_info.next_packet_read_index++;
                        continue;
                    }

                    TaggedAudioPacket packet_to_send = candidate_packet;
                    
                    // --- Adaptive Jitter Buffer Logic ---
                    // 1. Calculate current buffer level in milliseconds.
                    // This is based on the number of packets available for this consumer in the timeshift buffer.
                    if (candidate_packet.sample_rate > 0 && candidate_packet.channels > 0 && candidate_packet.bit_depth > 0) {
                        size_t available_packets = 0;
                        for (size_t i = target_info.next_packet_read_index; i < global_timeshift_buffer_.size(); ++i) {
                            if (global_timeshift_buffer_[i].source_tag == target_info.source_tag_filter) {
                                available_packets++;
                            }
                        }

                        // Estimate packet duration from the candidate packet's format.
                        // This assumes all packets in the stream have a similar duration.
                        int bytes_per_sample = candidate_packet.bit_depth / 8;
                        size_t num_samples_per_channel = candidate_packet.audio_data.size() / (candidate_packet.channels * bytes_per_sample);
                        double packet_duration_ms = (static_cast<double>(num_samples_per_channel) * 1000.0) / static_cast<double>(candidate_packet.sample_rate);
                        
                        timing_state->current_buffer_level_ms = available_packets * packet_duration_ms;
                    } else {
                        timing_state->current_buffer_level_ms = 0;
                    }

                    // 2. Implement P-controller to adjust playback rate based on the effective latency target.
                    const double controller_target_ms = effective_latency_ms;
                    timing_state->target_buffer_level_ms = controller_target_ms;
                    double error_ms = controller_target_ms - timing_state->current_buffer_level_ms;

                    if (controller_target_ms > 0) {
                        timing_state->buffer_target_fill_percentage = (timing_state->current_buffer_level_ms / controller_target_ms) * 100.0;
                    } else {
                        timing_state->buffer_target_fill_percentage = 0.0;
                    }

                    double rate_adjustment = error_ms * m_settings->timeshift_tuning.proportional_gain_kp;

                    // Adjust rate and clamp to a safe range (e.g., +/- 2%) to prevent audible artifacts.
                    double new_rate = 1.0 - rate_adjustment;
                    timing_state->current_playback_rate = std::max(m_settings->timeshift_tuning.min_playback_rate, std::min(m_settings->timeshift_tuning.max_playback_rate, new_rate));
                    packet_to_send.playback_rate = timing_state->current_playback_rate;

                    target_info.target_queue->push(std::move(packet_to_send));
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
                "[Profiler][Timeshift][Stream %s] jitter=%.2fms clk_offset=%.3fms drift=%.3fppm clk_innov_last=%.3fms clk_innov_avg_abs=%.3fms clk_update_age=%.2fms clk_meas_offset=%.3fms arrival(avg=%.3fms abs_avg=%.3fms max=%.3fms min=%.3fms samples=%llu) playout_dev(avg=%.3fms abs_avg=%.3fms max=%.3fms min=%.3fms samples=%llu) head_lag(last=%.3fms avg=%.3fms max=%.3fms samples=%llu) buffer(cur=%.3fms target=%.3fms fill=%.1f%% playback_rate=%.6f)",
                source_tag.c_str(),
                timing_state.jitter_estimate,
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
                    for (size_t i = proc_info.next_packet_read_index; i < remove_count; ++i) {
                        if (global_timeshift_buffer_[i].source_tag == proc_info.source_tag_filter) {
                            // It missed a packet it was supposed to play. It's lagging.
                            is_truly_lagging = true;
                            break;
                        }
                    }

                    if (is_truly_lagging) {
                        LOG_CPP_WARNING("[TimeshiftManager] Cleanup: Processor %s was lagging. Its read index %zu was inside the removed block of size %zu. Forcing catch-up to index 0.",
                                        id.c_str(), proc_info.next_packet_read_index, remove_count);
                        
                        // Safely increment the lagging event counter for this stream
                        {
                            std::lock_guard<std::mutex> lock(timing_mutex_);
                            if (stream_timing_states_.count(proc_info.source_tag_filter)) {
                                stream_timing_states_.at(proc_info.source_tag_filter).lagging_events_count++;
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
        global_timeshift_buffer_.erase(global_timeshift_buffer_.begin(), global_timeshift_buffer_.begin() + remove_count);
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
            const double base_latency_target_ms = std::max<double>(
                target_info.current_delay_ms,
                m_settings->timeshift_tuning.target_buffer_level_ms);
            const double jitter_padding_ms = m_settings->timeshift_tuning.jitter_safety_margin_multiplier * timing_state->jitter_estimate;
            double desired_latency_ms = base_latency_target_ms + timeshift_backshift_ms + jitter_padding_ms;
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
