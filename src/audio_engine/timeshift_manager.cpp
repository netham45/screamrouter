#include "timeshift_manager.h"
#include "audio_types.h" // For LOG_TM macro, if it's defined there or in a common logging header

#include <iostream> // For logging (can be replaced with a proper logger)
#include <algorithm> // For std::min, std::remove_if, std::lower_bound
#include <thread>    // For std::this_thread::sleep_for
#include <utility>   // For std::move

#if 1
#define LOG_TM(level, msg)
#define LOG_TM_DEBUG(msg)
#define LOG_TM_INFO(msg)
#define LOG_TM_WARN(msg)
#define LOG_TM_ERROR(msg)
#endif
#ifndef LOG_TM
#define LOG_TM(level, msg) std::cout << "[TimeshiftManager:" << level << "] " << msg << std::endl
#endif
#ifndef LOG_TM_DEBUG
#define LOG_TM_DEBUG(msg) std::cout << "[TimeshiftManager:DEBUG] " << msg << std::endl
#endif
#ifndef LOG_TM_INFO
#define LOG_TM_INFO(msg) std::cout << "[TimeshiftManager:INFO] " << msg << std::endl
#endif
#ifndef LOG_TM_WARN
#define LOG_TM_WARN(msg) std::cout << "[TimeshiftManager:WARN] " << msg << std::endl
#endif
#ifndef LOG_TM_ERROR
#define LOG_TM_ERROR(msg) std::cerr << "[TimeshiftManager:ERROR] " << msg << std::endl
#endif


namespace screamrouter {
namespace audio {

// Constants
const std::chrono::milliseconds TIMESIFT_MANAGER_CLEANUP_INTERVAL(1000);
const std::chrono::milliseconds TIMESIFT_MANAGER_LOOP_WAIT_TIMEOUT(50);

TimeshiftManager::TimeshiftManager(std::chrono::seconds max_buffer_duration)
    : max_buffer_duration_sec_(max_buffer_duration),
      last_cleanup_time_(std::chrono::steady_clock::now()) {
    LOG_TM_INFO("Initializing with max buffer duration: " + std::to_string(max_buffer_duration_sec_.count()) + "s");
}

TimeshiftManager::~TimeshiftManager() {
    LOG_TM_INFO("Destroying...");
    if (!stop_flag_) {
        stop();
    }
    // component_thread_ joining is handled by AudioComponent's destructor if not already joined by stop()
    LOG_TM_INFO("Destruction complete.");
}

void TimeshiftManager::start() {
    if (is_running()) {
        LOG_TM_WARN("Already running.");
        return;
    }
    LOG_TM_INFO("Starting...");
    stop_flag_ = false;
    try {
        component_thread_ = std::thread(&TimeshiftManager::run, this);
        LOG_TM_INFO("Component thread launched.");
    } catch (const std::system_error& e) {
        LOG_TM_ERROR("Failed to start component thread: " + std::string(e.what()));
        stop_flag_ = true; // Ensure stopped state if launch fails
        // Rethrow or handle error appropriately
        throw;
    }
}

void TimeshiftManager::stop() {
    if (stop_flag_) {
        LOG_TM_WARN("Already stopped or stopping.");
        return;
    }
    LOG_TM_INFO("Stopping...");
    stop_flag_ = true;
    run_loop_cv_.notify_all(); // Wake up the run loop if it's waiting

    if (component_thread_.joinable()) {
        try {
            component_thread_.join();
            LOG_TM_INFO("Component thread joined.");
        } catch (const std::system_error& e) {
            LOG_TM_ERROR("Error joining component thread: " + std::string(e.what()));
        }
    } else {
        LOG_TM_WARN("Component thread was not joinable in stop().");
    }
    LOG_TM_INFO("Stopped.");
}

void TimeshiftManager::add_packet(TaggedAudioPacket&& packet) {
    if (stop_flag_) {
        LOG_TM_WARN("Attempted to add packet while stopped. Ignoring.");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        global_timeshift_buffer_.push_back(std::move(packet));
    }
    // LOG_TM_DEBUG("Packet added to global buffer. Size: " + std::to_string(global_timeshift_buffer_.size()));
    run_loop_cv_.notify_one();
}

void TimeshiftManager::register_processor(
    const std::string& instance_id,
    const std::string& source_tag,
    std::shared_ptr<PacketQueue> target_queue,
    int initial_delay_ms,
    float initial_timeshift_sec) {
    LOG_TM_INFO("Registering processor: instance_id=" + instance_id + ", source_tag=" + source_tag +
                ", delay=" + std::to_string(initial_delay_ms) + "ms, timeshift=" + std::to_string(initial_timeshift_sec) + "s");

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
            LOG_TM_DEBUG("Initial timeshift > 0. Set next_packet_read_index to " + std::to_string(found_idx) + 
                         " based on " + std::to_string(initial_timeshift_sec) + "s backshift.");
        } else {
            info.next_packet_read_index = global_timeshift_buffer_.size();
            LOG_TM_DEBUG("Initial timeshift is 0 or buffer empty. Set next_packet_read_index to end of buffer: " + std::to_string(info.next_packet_read_index));
        }
    } // buffer_mutex_ released

    {
        std::lock_guard<std::mutex> targets_lock(targets_mutex_);
        processor_targets_[source_tag][instance_id] = info;
    }
    LOG_TM_INFO("Processor " + instance_id + " registered for source_tag " + source_tag + " with read_idx " + std::to_string(info.next_packet_read_index));
    run_loop_cv_.notify_one(); // New processor might be able to process immediately
}

void TimeshiftManager::unregister_processor(const std::string& instance_id, const std::string& source_tag) {
    LOG_TM_INFO("Unregistering processor: instance_id=" + instance_id + ", source_tag=" + source_tag);
    std::lock_guard<std::mutex> lock(targets_mutex_);
    auto& source_map = processor_targets_[source_tag];
    source_map.erase(instance_id);
    if (source_map.empty()) {
        processor_targets_.erase(source_tag);
        LOG_TM_INFO("Source tag " + source_tag + " removed as no processors are listening to it.");
    }
    LOG_TM_INFO("Processor " + instance_id + " unregistered.");
}

void TimeshiftManager::update_processor_delay(const std::string& instance_id, int delay_ms) {
    LOG_TM_INFO("Updating delay for processor " + instance_id + " to " + std::to_string(delay_ms) + "ms");
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
        LOG_TM_WARN("Attempted to update delay for unknown processor instance_id: " + instance_id);
    }
    run_loop_cv_.notify_one(); // Delay change affects readiness
}

void TimeshiftManager::update_processor_timeshift(const std::string& instance_id, float timeshift_sec) {
    LOG_TM_INFO("Updating timeshift for processor " + instance_id + " to " + std::to_string(timeshift_sec) + "s");
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
                     LOG_TM_DEBUG("Timeshift updated for " + instance_id + ", buffer empty. Read index set to 0.");
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
                    LOG_TM_DEBUG("Timeshift updated for " + instance_id + ". New read_idx: " + std::to_string(new_read_idx) +
                                 " based on " + std::to_string(timeshift_sec) + "s backshift.");
                }
            } // buffer_mutex_ released
            break; 
        }
    }
    if (!found_processor) {
        LOG_TM_WARN("Attempted to update timeshift for unknown processor instance_id: " + instance_id);
    }
    run_loop_cv_.notify_one(); // Timeshift change affects readiness and next packet
}


void TimeshiftManager::run() {
    LOG_TM_INFO("Run loop started.");
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
    LOG_TM_INFO("Run loop exiting.");
}

void TimeshiftManager::processing_loop_iteration() {
    // LOG_TM_DEBUG("Processing loop iteration started.");
    std::lock_guard<std::mutex> targets_lock(targets_mutex_);
    std::lock_guard<std::mutex> buffer_lock(buffer_mutex_);

    if (global_timeshift_buffer_.empty()) {
        // LOG_TM_DEBUG("Global buffer empty, nothing to process.");
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
                    // LOG_TM_DEBUG("Processor " + instance_id + ": Oldest considered packet (idx " + std::to_string(target_info.next_packet_read_index) + ") not ready. Scheduled in " +
                    //              std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(scheduled_time_for_first_potential - current_steady_time).count()) + "ms. Skipping processor.");
                    continue; // Move to the next processor
                }
            } else if (!global_timeshift_buffer_.empty() && target_info.next_packet_read_index >= global_timeshift_buffer_.size()) {
                // Read index is at or past the end of the buffer, but buffer is not empty.
                // This implies all prior packets were processed or skipped. Nothing new to process.
                // LOG_TM_DEBUG("Processor " + instance_id + ": Read index " + std::to_string(target_info.next_packet_read_index) + " is at/past end of non-empty buffer. Nothing to process.");
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
                    // LOG_TM_DEBUG("Processor " + instance_id + " (filter: " + target_info.source_tag_filter + 
                    //              ") skipping packet with tag " + candidate_packet.source_tag + " at index " + std::to_string(target_info.next_packet_read_index));
                    target_info.next_packet_read_index++;
                    continue;
                }
                
                // Calculate scheduled play time (delay_duration, backshift_duration, current_steady_time are from above)
                auto scheduled_play_time = candidate_packet.received_time + delay_duration + backshift_duration;

                if (scheduled_play_time <= current_steady_time) {
                    if (target_info.target_queue) {
                        // LOG_TM_DEBUG("Processor " + instance_id + " pushing packet (idx " + std::to_string(target_info.next_packet_read_index) +
                        //              ", tag " + candidate_packet.source_tag + ", sched_time_ms_ago: " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(current_steady_time - scheduled_play_time).count()) + ") to its queue.");
                        target_info.target_queue->push(TaggedAudioPacket(candidate_packet)); // Push a copy
                    }
                    target_info.next_packet_read_index++;
                } else {
                    // Packet is not yet ready for this processor, break from inner while for this processor
                    // LOG_TM_DEBUG("Processor " + instance_id + ": packet at index " + std::to_string(target_info.next_packet_read_index) + 
                    //              " not ready yet (scheduled in " + 
                    //              std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(scheduled_play_time - current_steady_time).count()) + "ms).");
                    break;
                }
            }
        }
    }
    // LOG_TM_DEBUG("Processing loop iteration finished.");
}


void TimeshiftManager::cleanup_global_buffer() {
    std::lock_guard<std::mutex> buffer_lock(buffer_mutex_); // Protects global_timeshift_buffer_

    if (global_timeshift_buffer_.empty()) {
        LOG_TM_DEBUG("Cleanup: Global buffer is empty.");
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
             LOG_TM_DEBUG("Cleanup: No processors registered. Buffer can be cleaned based on time only.");
        }
    } // targets_mutex_ released

    LOG_TM_DEBUG("Cleanup: oldest_allowed_time_by_duration calculated. Min_read_index_across_all_procs: " + std::to_string(min_read_index_across_all_procs));

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
        LOG_TM_INFO("Cleanup: Removed " + std::to_string(remove_count) + " old packets from global buffer.");
        // Adjust next_packet_read_index for all processors
        std::lock_guard<std::mutex> targets_lock(targets_mutex_);
        for (auto& [tag, source_map] : processor_targets_) {
            for (auto& [id, proc_info] : source_map) {
                if (proc_info.next_packet_read_index >= remove_count) {
                    proc_info.next_packet_read_index -= remove_count;
                } else {
                    // This case implies the processor's read index was pointing to packets that were just removed.
                    // It should be set to 0, as it now needs to start from the new beginning of the (relevant part of) the buffer.
                    LOG_TM_WARN("Cleanup: Processor " + id + " read index (" + std::to_string(proc_info.next_packet_read_index) +
                                 ") was less than remove_count (" + std::to_string(remove_count) + "). Resetting to 0.");
                    proc_info.next_packet_read_index = 0;
                }
            }
        }
        LOG_TM_DEBUG("Cleanup: Adjusted next_packet_read_index for all processors by " + std::to_string(remove_count) + ".");
    } else {
        LOG_TM_DEBUG("Cleanup: No packets removed.");
    }
    LOG_TM_DEBUG("Cleanup: Global buffer size after cleanup: " + std::to_string(global_timeshift_buffer_.size()));
}


} // namespace audio
} // namespace screamrouter
