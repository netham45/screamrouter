/**
 * @file timeshift_manager.cpp
 * @brief Implements the TimeshiftManager class for handling global timeshifting.
 * @details This file contains the implementation of the TimeshiftManager, which manages
 *          a global buffer of audio packets to enable timeshifting and synchronized playback
 *          across multiple consumers.
 */
#include "timeshift_manager.h"
#include "../utils/cpp_logger.h"
#include "../audio_types.h"

#include <iostream>
#include <algorithm>
#include <thread>
#include <utility>
#include <vector>

namespace screamrouter {
namespace audio {

// Constants
const std::chrono::milliseconds TIMESIFT_MANAGER_CLEANUP_INTERVAL(1000);

/**
 * @brief Constructs a TimeshiftManager.
 * @param max_buffer_duration The maximum duration of audio to hold in the global buffer.
 */
TimeshiftManager::TimeshiftManager(std::chrono::seconds max_buffer_duration)
    : max_buffer_duration_sec_(max_buffer_duration),
      last_cleanup_time_(std::chrono::steady_clock::now()) {
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
    LOG_CPP_INFO("[TimeshiftManager] Stopping...");
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
    if (stop_flag_) {
        LOG_CPP_WARNING("[TimeshiftManager] Attempted to add packet while stopped. Ignoring.");
        return;
    }

    // Acquire data_mutex_ first to ensure consistent lock order and prevent deadlocks.
    std::lock_guard<std::mutex> data_lock(data_mutex_);

    // Perform jitter calculation and update timing state under timing_mutex_
    // This is now nested within data_mutex_ to maintain the lock order.
    if (packet.rtp_timestamp.has_value() && packet.sample_rate > 0) {
        std::lock_guard<std::mutex> timing_lock(timing_mutex_);
        auto& state = stream_timing_states_[packet.source_tag];
        state.total_packets++;
        
        if (state.is_first_packet) {
            state.is_first_packet = false;
            // Initialize the playout clock anchor with the very first packet
            if (!state.playout_clock_initialized) {
                state.playout_clock_initialized = true;
                state.anchor_rtp_timestamp = packet.rtp_timestamp.value();
                state.anchor_wallclock_time = packet.received_time;
                state.last_played_rtp_timestamp = packet.rtp_timestamp.value();
            }
        } else {
            auto arrival_diff = packet.received_time - state.last_wallclock;
            double arrival_diff_ms = std::chrono::duration<double, std::milli>(arrival_diff).count();

            uint32_t rtp_diff_samples = packet.rtp_timestamp.value() - state.last_rtp_timestamp;
            double rtp_diff_ms = (static_cast<double>(rtp_diff_samples) * 1000.0) / static_cast<double>(packet.sample_rate);

            double transit_time_diff = arrival_diff_ms - rtp_diff_ms;
            double jitter_diff = std::abs(transit_time_diff) - state.jitter_estimate;
            state.jitter_estimate += jitter_diff / 16.0;
        }

        state.last_rtp_timestamp = packet.rtp_timestamp.value();
        state.last_wallclock = packet.received_time;
    }

    global_timeshift_buffer_.push_back(std::move(packet));
    m_total_packets_added++;
    
    // NOTE: Waking the thread on every single packet is inefficient and causes scheduling instability.
    // The run loop is designed to wake up automatically at the correct time for the next
    // scheduled packet via wait_until. Only configuration changes should wake the thread.
    // m_state_version_++;
    // run_loop_cv_.notify_one();
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
        if (now - last_cleanup_time_ > TIMESIFT_MANAGER_CLEANUP_INTERVAL) {
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

    auto current_steady_time = std::chrono::steady_clock::now();

    for (auto& [source_tag, source_map] : processor_targets_) {
        for (auto& [instance_id, target_info] : source_map) {
            
            StreamTimingState* timing_state = nullptr;
            {
                std::lock_guard<std::mutex> lock(timing_mutex_);
                if (stream_timing_states_.count(source_tag)) {
                    timing_state = &stream_timing_states_.at(source_tag);
                }
            }

            if (!timing_state || !timing_state->playout_clock_initialized) {
                continue; // Cannot process if timing is not initialized
            }

            // This loop is simplified. The wakeup time calculation now does the heavy lifting.
            // We just need to check if the next packet is ready based on our predictive clock.
            while (target_info.next_packet_read_index < global_timeshift_buffer_.size()) {
                const auto& candidate_packet = global_timeshift_buffer_[target_info.next_packet_read_index];

                if (candidate_packet.source_tag != target_info.source_tag_filter) {
                    target_info.next_packet_read_index++;
                    continue;
                }

                if (!candidate_packet.rtp_timestamp.has_value() || candidate_packet.sample_rate == 0) {
                    // This packet is unusable for timing, skip it.
                    target_info.next_packet_read_index++;
                    continue;
                }

                int64_t rtp_delta = static_cast<int64_t>(candidate_packet.rtp_timestamp.value()) - static_cast<int64_t>(timing_state->anchor_rtp_timestamp);
                double rtp_delta_ms = (static_cast<double>(rtp_delta) * 1000.0) / static_cast<double>(candidate_packet.sample_rate);
                auto rtp_delta_duration = std::chrono::duration<double, std::milli>(rtp_delta_ms);

                auto ideal_playout_time = timing_state->anchor_wallclock_time +
                                          std::chrono::duration_cast<std::chrono::steady_clock::duration>(rtp_delta_duration) +
                                          std::chrono::milliseconds(target_info.current_delay_ms);
                
                auto time_until_playout_ms = std::chrono::duration<double, std::milli>(ideal_playout_time - current_steady_time).count();

                if (ideal_playout_time <= current_steady_time) {
                    LOG_CPP_DEBUG("[TimeshiftManager] PLAYING packet for %s. rtp_ts=%u, ideal_playout_time was %.2fms ago.",
                                  instance_id.c_str(), candidate_packet.rtp_timestamp.value_or(0), -time_until_playout_ms);
                    if (target_info.target_queue) {
                        target_info.target_queue->push(TaggedAudioPacket(candidate_packet));
                        timing_state->last_played_rtp_timestamp = candidate_packet.rtp_timestamp.value();
                    }
                    target_info.next_packet_read_index++;
                } else {
                    LOG_CPP_DEBUG("[TimeshiftManager] WAITING for packet for %s. rtp_ts=%u, ideal_playout_time is in %.2fms.",
                                  instance_id.c_str(), candidate_packet.rtp_timestamp.value_or(0), time_until_playout_ms);
                    // This packet is for the future, so we can stop checking for this consumer
                    break;
                }
            }
        }
    }
}

/**
 * @brief Periodically cleans up old packets from the global buffer. Assumes data_mutex_ is held.
 */
void TimeshiftManager::cleanup_global_buffer_unlocked() {
    if (global_timeshift_buffer_.empty()) {
        LOG_CPP_INFO("[TimeshiftManager] Cleanup: Global buffer is empty.");
        return;
    }

    auto oldest_allowed_time_by_duration = std::chrono::steady_clock::now() - max_buffer_duration_sec_;
    
    size_t min_read_index_across_all_procs = global_timeshift_buffer_.size();
    if (!processor_targets_.empty()) {
        bool first = true;
        for (const auto& [tag, source_map] : processor_targets_) {
            for (const auto& [id, proc_info] : source_map) {
                if (first) {
                    min_read_index_across_all_procs = proc_info.next_packet_read_index;
                    first = false;
                } else {
                    min_read_index_across_all_procs = std::min(min_read_index_across_all_procs, proc_info.next_packet_read_index);
                }
            }
        }
    } else {
         LOG_CPP_INFO("[TimeshiftManager] Cleanup: No processors registered. Buffer can be cleaned based on time only.");
    }

    LOG_CPP_INFO("[TimeshiftManager] Cleanup: oldest_allowed_time_by_duration calculated. Min_read_index_across_all_procs: %zu", min_read_index_across_all_procs);

    size_t remove_count = 0;
    while (!global_timeshift_buffer_.empty() &&
           global_timeshift_buffer_.front().received_time < oldest_allowed_time_by_duration &&
           remove_count < min_read_index_across_all_procs) {
        global_timeshift_buffer_.pop_front();
        remove_count++;
    }

    if (remove_count > 0) {
        LOG_CPP_INFO("[TimeshiftManager] Cleanup: Removed %zu old packets from global buffer.", remove_count);
        for (auto& [tag, source_map] : processor_targets_) {
            for (auto& [id, proc_info] : source_map) {
                if (proc_info.next_packet_read_index >= remove_count) {
                    proc_info.next_packet_read_index -= remove_count;
                } else {
                    LOG_CPP_WARNING("[TimeshiftManager] Cleanup: Processor %s read index (%zu) was less than remove_count (%zu). Resetting to 0.",
                                    id.c_str(), proc_info.next_packet_read_index, remove_count);
                    proc_info.next_packet_read_index = 0;
                }
            }
        }
        LOG_CPP_INFO("[TimeshiftManager] Cleanup: Adjusted next_packet_read_index for all processors by %zu.", remove_count);
    } else {
        LOG_CPP_INFO("[TimeshiftManager] Cleanup: No packets removed.");
    }
    LOG_CPP_INFO("[TimeshiftManager] Cleanup: Global buffer size after cleanup: %zu", global_timeshift_buffer_.size());
}

/**
 * @brief Calculates the time point for the next event to occur.
 * @return The time point of the next scheduled event.
 * @note This function assumes the caller holds the data_mutex.
 */
std::chrono::steady_clock::time_point TimeshiftManager::calculate_next_wakeup_time() {
    auto next_cleanup_time = last_cleanup_time_ + TIMESIFT_MANAGER_CLEANUP_INTERVAL;
    auto earliest_time = std::chrono::steady_clock::time_point::max();

    // Ensure a maximum sleep time to stay responsive
    const auto max_sleep_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);

    for (const auto& [source_tag, source_map] : processor_targets_) {
        for (const auto& [instance_id, target_info] : source_map) {
            
            // Only consider processors that actually have packets to read from the buffer
            if (target_info.next_packet_read_index >= global_timeshift_buffer_.size()) {
                continue;
            }

            const auto& next_packet = global_timeshift_buffer_[target_info.next_packet_read_index];
            if (!next_packet.rtp_timestamp.has_value() || next_packet.sample_rate == 0) {
                continue; // Cannot schedule without timing info
            }
            
            const StreamTimingState* timing_state = nullptr;
            {
                // This function is called with data_mutex_ held.
                // Locking timing_mutex_ here maintains the correct lock order.
                std::lock_guard<std::mutex> lock(timing_mutex_);
                if (stream_timing_states_.count(source_tag)) {
                    timing_state = &stream_timing_states_.at(source_tag);
                }
            }
            
            if (!timing_state || !timing_state->playout_clock_initialized) {
                continue;
            }

            uint32_t next_rtp_ts = next_packet.rtp_timestamp.value();
            uint32_t sample_rate = next_packet.sample_rate;

            // Calculate the ideal playout time based on the RTP clock, not arrival time
            int64_t rtp_delta = static_cast<int64_t>(next_rtp_ts) - static_cast<int64_t>(timing_state->anchor_rtp_timestamp);
            double rtp_delta_ms = (static_cast<double>(rtp_delta) * 1000.0) / static_cast<double>(sample_rate);
            auto rtp_delta_duration = std::chrono::duration<double, std::milli>(rtp_delta_ms);

            auto ideal_playout_time = timing_state->anchor_wallclock_time +
                                      std::chrono::duration_cast<std::chrono::steady_clock::duration>(rtp_delta_duration) +
                                      std::chrono::milliseconds(target_info.current_delay_ms);

            if (ideal_playout_time < earliest_time) {
                earliest_time = ideal_playout_time;
            }
        }
    }

    return std::min({earliest_time, next_cleanup_time, max_sleep_time});
}


} // namespace audio
} // namespace screamrouter
