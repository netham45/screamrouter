#include "timeshift_manager.h"
#include "../utils/cpp_logger.h" // For new C++ logger
#include "../audio_types.h"

#include <iostream> // For logging (cpp_logger fallback)
#include <algorithm> // For std::min, std::remove_if, std::lower_bound
#include <thread>    // For std::this_thread::sleep_for
#include <utility>   // For std::move

// Old logger macros are removed. New macros (LOG_CPP_INFO, etc.) are in cpp_logger.h
// The "[TimeshiftManager]" prefix will be manually added to the new log calls.


namespace screamrouter {
namespace audio {

// Constants
const std::chrono::milliseconds TIMESIFT_MANAGER_CLEANUP_INTERVAL(1000);
const std::chrono::milliseconds TIMESIFT_MANAGER_LOOP_WAIT_TIMEOUT(50);

TimeshiftManager::TimeshiftManager(std::chrono::seconds max_buffer_duration)
    : max_buffer_duration_sec_(max_buffer_duration),
      last_cleanup_time_(std::chrono::steady_clock::now()) {
    LOG_CPP_INFO("[TimeshiftManager] Initializing with max buffer duration: %llds", (long long)max_buffer_duration_sec_.count());
}

TimeshiftManager::~TimeshiftManager() {
    LOG_CPP_INFO("[TimeshiftManager] Destroying...");
    if (!stop_flag_) {
        stop();
    }
    // component_thread_ joining is handled by AudioComponent's destructor if not already joined by stop()
    LOG_CPP_INFO("[TimeshiftManager] Destruction complete.");
}

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
        stop_flag_ = true; // Ensure stopped state if launch fails
        // Rethrow or handle error appropriately
        throw;
    }
}

void TimeshiftManager::stop() {
    if (stop_flag_) {
        LOG_CPP_WARNING("[TimeshiftManager] Already stopped or stopping.");
        return;
    }
    LOG_CPP_INFO("[TimeshiftManager] Stopping...");
    stop_flag_ = true;
    run_loop_cv_.notify_all(); // Wake up the run loop if it's waiting

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

void TimeshiftManager::add_packet(TaggedAudioPacket&& packet) {
    if (stop_flag_) {
        LOG_CPP_WARNING("[TimeshiftManager] Attempted to add packet while stopped. Ignoring.");
        return;
    }

    // --- JITTER CALCULATION LOGIC ---
    if (packet.rtp_timestamp.has_value() && packet.sample_rate > 0) {
        std::lock_guard<std::mutex> lock(timing_mutex_);
        auto& state = stream_timing_states_[packet.source_tag]; // Creates if not exists
        
        if (state.is_first_packet) {
            state.is_first_packet = false;
        } else {
            // Time difference in wall-clock time (in milliseconds)
            auto arrival_diff = packet.received_time - state.last_wallclock;
            double arrival_diff_ms = std::chrono::duration<double, std::milli>(arrival_diff).count();

            // Time difference in RTP timestamps (in milliseconds)
            uint32_t rtp_diff_samples = packet.rtp_timestamp.value() - state.last_rtp_timestamp;
            double rtp_diff_ms = (static_cast<double>(rtp_diff_samples) * 1000.0) / static_cast<double>(packet.sample_rate);

            // Jitter calculation per RFC 3550
            double transit_time_diff = arrival_diff_ms - rtp_diff_ms;
            double jitter_diff = std::abs(transit_time_diff) - state.jitter_estimate;
            state.jitter_estimate += jitter_diff / 16.0;
        }

        state.last_rtp_timestamp = packet.rtp_timestamp.value();
        state.last_wallclock = packet.received_time;
    }
    // --- END JITTER CALCULATION ---

    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        global_timeshift_buffer_.push_back(std::move(packet));
    }
    // LOG_CPP_DEBUG("[TimeshiftManager] Packet added to global buffer. Size: %zu", global_timeshift_buffer_.size());
    run_loop_cv_.notify_one();
}

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
        std::lock_guard<std::mutex> buffer_lock(buffer_mutex_); // Lock buffer to read its size and potentially content
        // Simplified initial next_packet_read_index: Start from current end of buffer.
        // A more sophisticated approach would find the exact historical point based on initial_timeshift_sec.
        // For now, if timeshift > 0, it means we want to look back.
        // If buffer is empty or timeshift is 0, new packets are the target.
        if (initial_timeshift_sec > 0.0f && !global_timeshift_buffer_.empty()) {
            auto now = std::chrono::steady_clock::now();
            auto target_past_time = now - std::chrono::milliseconds(initial_delay_ms) - std::chrono::duration<double>(initial_timeshift_sec);
            
            // Iterate from the beginning to find the first packet >= target_past_time
            // This is a simple linear scan. Could be optimized if buffer is very large.
            size_t found_idx = global_timeshift_buffer_.size(); // Default to end if no suitable packet found
            for (size_t i = 0; i < global_timeshift_buffer_.size(); ++i) {
                if (global_timeshift_buffer_[i].received_time >= target_past_time) {
                    found_idx = i;
                    break;
                }
            }
            info.next_packet_read_index = found_idx;
            LOG_CPP_DEBUG("[TimeshiftManager] Initial timeshift > 0. Set next_packet_read_index to %zu based on %.2fs backshift.",
                          found_idx, initial_timeshift_sec);
        } else {
            info.next_packet_read_index = global_timeshift_buffer_.size();
            LOG_CPP_DEBUG("[TimeshiftManager] Initial timeshift is 0 or buffer empty. Set next_packet_read_index to end of buffer: %zu", info.next_packet_read_index);
        }
    } // buffer_mutex_ released

    {
        std::lock_guard<std::mutex> targets_lock(targets_mutex_);
        processor_targets_[source_tag][instance_id] = info;
    }
    LOG_CPP_INFO("[TimeshiftManager] Processor %s registered for source_tag %s with read_idx %zu",
                 instance_id.c_str(), source_tag.c_str(), info.next_packet_read_index);
    run_loop_cv_.notify_one(); // New processor might be able to process immediately
}

void TimeshiftManager::unregister_processor(const std::string& instance_id, const std::string& source_tag) {
    LOG_CPP_INFO("[TimeshiftManager] Unregistering processor: instance_id=%s, source_tag=%s", instance_id.c_str(), source_tag.c_str());
    std::lock_guard<std::mutex> lock(targets_mutex_);
    auto& source_map = processor_targets_[source_tag];
    source_map.erase(instance_id);
    if (source_map.empty()) {
        processor_targets_.erase(source_tag);
        LOG_CPP_INFO("[TimeshiftManager] Source tag %s removed as no processors are listening to it.", source_tag.c_str());
    }
    LOG_CPP_INFO("[TimeshiftManager] Processor %s unregistered.", instance_id.c_str());
}

void TimeshiftManager::update_processor_delay(const std::string& instance_id, int delay_ms) {
    LOG_CPP_INFO("[TimeshiftManager] Updating delay for processor %s to %dms", instance_id.c_str(), delay_ms);
    std::lock_guard<std::mutex> lock(targets_mutex_);
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
    run_loop_cv_.notify_one(); // Delay change affects readiness
}

void TimeshiftManager::update_processor_timeshift(const std::string& instance_id, float timeshift_sec) {
    LOG_CPP_INFO("[TimeshiftManager] Updating timeshift for processor %s to %.2fs", instance_id.c_str(), timeshift_sec);
    std::lock_guard<std::mutex> targets_lock(targets_mutex_);
    bool found_processor = false;
    for (auto& [tag, source_map] : processor_targets_) {
        auto proc_it = source_map.find(instance_id);
        if (proc_it != source_map.end()) {
            found_processor = true;
            proc_it->second.current_timeshift_backshift_sec = timeshift_sec;

            // Update next_packet_read_index
            {
                std::lock_guard<std::mutex> buffer_lock(buffer_mutex_);
                if (global_timeshift_buffer_.empty()) {
                    proc_it->second.next_packet_read_index = 0;
                     LOG_CPP_DEBUG("[TimeshiftManager] Timeshift updated for %s, buffer empty. Read index set to 0.", instance_id.c_str());
                } else {
                    auto now = std::chrono::steady_clock::now();
                    auto target_past_time = now - std::chrono::milliseconds(proc_it->second.current_delay_ms) - std::chrono::duration<double>(timeshift_sec);
                    
                    size_t new_read_idx = global_timeshift_buffer_.size(); // Default to end
                    // Find the first packet whose received_time is >= target_past_time
                    for (size_t i = 0; i < global_timeshift_buffer_.size(); ++i) {
                        if (global_timeshift_buffer_[i].received_time >= target_past_time) {
                            new_read_idx = i;
                            break;
                        }
                    }
                    proc_it->second.next_packet_read_index = new_read_idx;
                    LOG_CPP_DEBUG("[TimeshiftManager] Timeshift updated for %s. New read_idx: %zu based on %.2fs backshift.",
                                 instance_id.c_str(), new_read_idx, timeshift_sec);
                }
            } // buffer_mutex_ released
            break;
        }
    }
    if (!found_processor) {
        LOG_CPP_WARNING("[TimeshiftManager] Attempted to update timeshift for unknown processor instance_id: %s", instance_id.c_str());
    }
    run_loop_cv_.notify_one(); // Timeshift change affects readiness and next packet
}


void TimeshiftManager::run() {
    LOG_CPP_INFO("[TimeshiftManager] Run loop started.");
    while (!stop_flag_) {
        processing_loop_iteration();

        auto now = std::chrono::steady_clock::now();
        if (now - last_cleanup_time_ > TIMESIFT_MANAGER_CLEANUP_INTERVAL) {
            cleanup_global_buffer();
            last_cleanup_time_ = now;
        }

        std::unique_lock<std::mutex> lock(buffer_mutex_); // Mutex for CV wait, also protects buffer access for predicate
        // Wait if stop_flag_ is false AND (no packets in buffer OR all processors are caught up)
        // The predicate helps to avoid spurious wakeups if processing_loop_iteration didn't find ready packets.
        // A simpler predicate: wait if stop_flag_ is false. Timeout handles periodic checks.
        // Or, predicate: return stop_flag_.load() || !global_timeshift_buffer_.empty(); (wake if stopped or new packets)
        run_loop_cv_.wait_for(lock, TIMESIFT_MANAGER_LOOP_WAIT_TIMEOUT, [this] {
            if (stop_flag_.load()) return true; // Stop requested
            if (global_timeshift_buffer_.empty()) return false; // No packets, wait for more

            // Check if any processor has pending packets it *could* process (index < buffer size)
            // This is a simplified check; actual readiness depends on timing.
            // The main goal is to wake up if there's *any* potential work.
            // This lock on targets_mutex_ inside predicate is tricky.
            // A simpler approach is to rely on timeout and explicit notifications from add_packet/update_processor.
            // For now, let's use a simpler predicate: wake if stopped or if there are any packets at all.
            // The processing_loop_iteration will then determine if they are actually ready.
            return !global_timeshift_buffer_.empty();
        });
    }
    LOG_CPP_INFO("[TimeshiftManager] Run loop exiting.");
}

void TimeshiftManager::processing_loop_iteration() {
    // LOG_CPP_DEBUG("[TimeshiftManager] Processing loop iteration started.");
    std::lock_guard<std::mutex> targets_lock(targets_mutex_);
    std::lock_guard<std::mutex> buffer_lock(buffer_mutex_);

    if (global_timeshift_buffer_.empty()) {
        // LOG_CPP_DEBUG("[TimeshiftManager] Global buffer empty, nothing to process.");
        return;
    }

    for (auto& [source_tag, source_map] : processor_targets_) {
        for (auto& [instance_id, target_info] : source_map) {
            // Calculate durations once per processor per iteration
            auto delay_duration = std::chrono::milliseconds(target_info.current_delay_ms);
            auto backshift_duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                          std::chrono::duration<double>(target_info.current_timeshift_backshift_sec));
            auto current_steady_time = std::chrono::steady_clock::now();

            // If buffer doesn't go back far enough for the requested timeshift, play silence.
            // Check if the oldest packet we *could* play is still too new for the current timeshift settings.
            if (target_info.next_packet_read_index < global_timeshift_buffer_.size()) {
                const auto& first_potential_packet = global_timeshift_buffer_[target_info.next_packet_read_index];
                // This is the time this first_potential_packet is scheduled to play
                auto scheduled_time_for_first_potential = first_potential_packet.received_time + delay_duration + backshift_duration;
                
                if (scheduled_time_for_first_potential > current_steady_time) {
                    // The very first packet this processor is looking at is not yet due.
                    // So, nothing to do for this processor in this iteration.
                    // LOG_CPP_DEBUG("[TimeshiftManager] Processor %s: Oldest considered packet (idx %zu) not ready. Scheduled in %lldms. Skipping processor.",
                    // instance_id.c_str(), target_info.next_packet_read_index, (long long)std::chrono::duration_cast<std::chrono::milliseconds>(scheduled_time_for_first_potential - current_steady_time).count());
                    continue; // Move to the next processor
                }
            } else if (!global_timeshift_buffer_.empty() && target_info.next_packet_read_index >= global_timeshift_buffer_.size()) {
                // Read index is at or past the end of the buffer, but buffer is not empty.
                // This implies all prior packets were processed or skipped. Nothing new to process.
                // LOG_CPP_DEBUG("[TimeshiftManager] Processor %s: Read index %zu is at/past end of non-empty buffer. Nothing to process.", instance_id.c_str(), target_info.next_packet_read_index);
                continue;
            }


            while (target_info.next_packet_read_index < global_timeshift_buffer_.size()) {
                const auto& candidate_packet = global_timeshift_buffer_[target_info.next_packet_read_index];

                // Filter by source_tag (this check is somewhat redundant if processor_targets_ is correctly structured by source_tag,
                // but good for safety if a processor was mistakenly registered under the wrong top-level source_tag map)
                if (candidate_packet.source_tag != target_info.source_tag_filter) {
                    // This should ideally not happen if registration is correct.
                    // If it does, it means this processor is iterating through packets it's not interested in.
                    // For now, we just skip. A more optimized structure might avoid this iteration.
                    // LOG_CPP_DEBUG("[TimeshiftManager] Processor %s (filter: %s) skipping packet with tag %s at index %zu",
                    // instance_id.c_str(), target_info.source_tag_filter.c_str(), candidate_packet.source_tag.c_str(), target_info.next_packet_read_index);
                    target_info.next_packet_read_index++;
                    continue;
                }
                
                // Calculate scheduled play time, including adaptive jitter delay
                double jitter_ms = 0.0;
                {
                    std::lock_guard<std::mutex> lock(timing_mutex_);
                    if (stream_timing_states_.count(candidate_packet.source_tag)) {
                        // Use a factor of 4x the jitter estimate for the buffer
                        jitter_ms = stream_timing_states_.at(candidate_packet.source_tag).jitter_estimate * 4.0;
                    }
                }
                auto jitter_delay_duration = std::chrono::duration<double, std::milli>(jitter_ms);
                auto total_delay_duration = delay_duration + jitter_delay_duration;
                auto scheduled_play_time = candidate_packet.received_time + total_delay_duration + backshift_duration;

                if (scheduled_play_time <= current_steady_time) {
                    if (target_info.target_queue) {
                        // LOG_CPP_DEBUG("[TimeshiftManager] Processor %s pushing packet (idx %zu, tag %s, sched_time_ms_ago: %lld) to its queue.",
                        // instance_id.c_str(), target_info.next_packet_read_index, candidate_packet.source_tag.c_str(),
                        // (long long)std::chrono::duration_cast<std::chrono::milliseconds>(current_steady_time - scheduled_play_time).count());
                        target_info.target_queue->push(TaggedAudioPacket(candidate_packet)); // Push a copy
                    }
                    target_info.next_packet_read_index++;
                } else {
                    // Packet is not yet ready for this processor, break from inner while for this processor
                    // LOG_CPP_DEBUG("[TimeshiftManager] Processor %s: packet at index %zu not ready yet (scheduled in %lldms).",
                    // instance_id.c_str(), target_info.next_packet_read_index,
                    // (long long)std::chrono::duration_cast<std::chrono::milliseconds>(scheduled_play_time - current_steady_time).count());
                    break;
                }
            }
        }
    }
    // LOG_CPP_DEBUG("[TimeshiftManager] Processing loop iteration finished.");
}


void TimeshiftManager::cleanup_global_buffer() {
    std::lock_guard<std::mutex> buffer_lock(buffer_mutex_); // Protects global_timeshift_buffer_

    if (global_timeshift_buffer_.empty()) {
        LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: Global buffer is empty.");
        return;
    }

    auto oldest_allowed_time_by_duration = std::chrono::steady_clock::now() - max_buffer_duration_sec_;
    
    // Find the minimum next_packet_read_index across all processors
    // This is the earliest point in the buffer that *any* processor might still need.
    size_t min_read_index_across_all_procs = global_timeshift_buffer_.size(); // Default to end if no processors
    {
        std::lock_guard<std::mutex> targets_lock(targets_mutex_); // Protects processor_targets_
        if (!processor_targets_.empty()) {
            min_read_index_across_all_procs = global_timeshift_buffer_.size(); // Re-initialize if there are processors
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
             // No processors registered, can potentially clear buffer up to max_buffer_duration_sec_
             // min_read_index_across_all_procs remains global_timeshift_buffer_.size(), meaning all packets before it are candidates for removal by time.
             LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: No processors registered. Buffer can be cleaned based on time only.");
        }
    } // targets_mutex_ released

    LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: oldest_allowed_time_by_duration calculated. Min_read_index_across_all_procs: %zu", min_read_index_across_all_procs);

    size_t remove_count = 0;
    // Iterate from the front of the buffer
    // Remove packets if:
    // 1. They are older than max_buffer_duration_sec_ AND
    // 2. Their index is less than min_read_index_across_all_procs (i.e., no processor is still looking at or before this packet)
    while (!global_timeshift_buffer_.empty() &&
           global_timeshift_buffer_.front().received_time < oldest_allowed_time_by_duration &&
           remove_count < min_read_index_across_all_procs) { // The 'remove_count < min_read_index' ensures we don't pop elements that are at or after the min_read_index
        global_timeshift_buffer_.pop_front();
        remove_count++;
    }

    if (remove_count > 0) {
        LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: Removed %zu old packets from global buffer.", remove_count);
        // Adjust next_packet_read_index for all processors
        std::lock_guard<std::mutex> targets_lock(targets_mutex_);
        for (auto& [tag, source_map] : processor_targets_) {
            for (auto& [id, proc_info] : source_map) {
                if (proc_info.next_packet_read_index >= remove_count) {
                    proc_info.next_packet_read_index -= remove_count;
                } else {
                    // This case implies the processor's read index was pointing to packets that were just removed.
                    // It should be set to 0, as it now needs to start from the new beginning of the (relevant part of) the buffer.
                    LOG_CPP_WARNING("[TimeshiftManager] Cleanup: Processor %s read index (%zu) was less than remove_count (%zu). Resetting to 0.",
                                    id.c_str(), proc_info.next_packet_read_index, remove_count);
                    proc_info.next_packet_read_index = 0;
                }
            }
        }
        LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: Adjusted next_packet_read_index for all processors by %zu.", remove_count);
    } else {
        LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: No packets removed.");
    }
    LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: Global buffer size after cleanup: %zu", global_timeshift_buffer_.size());
}


} // namespace audio
} // namespace screamrouter
