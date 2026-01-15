/**
 * @file processor_registry.cpp
 * @brief Implementation of processor registration for TimeshiftManager.
 */
#include "processor_registry.h"
#include "../utils/cpp_logger.h"

namespace screamrouter {
namespace audio {

void ProcessorRegistry::register_processor(const std::string& instance_id,
                                           const std::string& source_tag,
                                           int initial_delay_ms,
                                           float initial_timeshift_sec,
                                           std::size_t current_buffer_size) {
    LOG_CPP_INFO("[ProcessorRegistry] Registering processor: instance_id=%s, source_tag=%s, delay=%dms, timeshift=%.2fs",
                 instance_id.c_str(), source_tag.c_str(), initial_delay_ms, initial_timeshift_sec);
    
    ProcessorTargetInfo info;
    info.instance_id = instance_id;
    info.current_delay_ms = initial_delay_ms;
    info.current_timeshift_backshift_sec = initial_timeshift_sec;
    info.source_tag_filter = source_tag;
    info.is_wildcard = !source_tag.empty() && source_tag.back() == '*';
    
    if (info.is_wildcard) {
        info.wildcard_prefix = source_tag.substr(0, source_tag.size() - 1);
        LOG_CPP_INFO("[ProcessorRegistry] Processor %s registered with wildcard prefix '%s'",
                     instance_id.c_str(), info.wildcard_prefix.c_str());
    } else {
        info.bound_source_tag = source_tag;
        info.matched_concrete_tags.insert(source_tag);
    }
    
    // For non-zero timeshift, would need buffer access to find correct position
    // For now, start at end of buffer (caller should handle timeshift positioning)
    info.next_packet_read_index = current_buffer_size;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        targets_[source_tag][instance_id] = std::move(info);
    }
    
    LOG_CPP_INFO("[ProcessorRegistry] Processor %s registered with read_idx %zu",
                 instance_id.c_str(), current_buffer_size);
    
    if (state_callback_) state_callback_();
}

void ProcessorRegistry::unregister_processor(const std::string& instance_id, const std::string& source_tag) {
    LOG_CPP_INFO("[ProcessorRegistry] Unregistering processor: instance_id=%s, source_tag=%s",
                 instance_id.c_str(), source_tag.c_str());
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& source_map = targets_[source_tag];
        source_map.erase(instance_id);
        if (source_map.empty()) {
            targets_.erase(source_tag);
            LOG_CPP_INFO("[ProcessorRegistry] Source tag %s removed as no processors are listening.", source_tag.c_str());
        }
    }
    
    LOG_CPP_INFO("[ProcessorRegistry] Processor %s unregistered.", instance_id.c_str());
    if (state_callback_) state_callback_();
}

void ProcessorRegistry::update_delay(const std::string& instance_id, int delay_ms) {
    LOG_CPP_INFO("[ProcessorRegistry] Updating delay for processor %s to %dms", instance_id.c_str(), delay_ms);
    
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [tag, source_map] : targets_) {
            auto it = source_map.find(instance_id);
            if (it != source_map.end()) {
                it->second.current_delay_ms = delay_ms;
                found = true;
                break;
            }
        }
    }
    
    if (!found) {
        LOG_CPP_WARNING("[ProcessorRegistry] Attempted to update delay for unknown processor: %s", instance_id.c_str());
    }
    
    if (state_callback_) state_callback_();
}

void ProcessorRegistry::update_timeshift(const std::string& instance_id, float timeshift_sec,
                                         std::function<std::pair<std::size_t, std::chrono::steady_clock::time_point>(std::size_t)> get_packet_time) {
    LOG_CPP_INFO("[ProcessorRegistry] Updating timeshift for processor %s to %.2fs", instance_id.c_str(), timeshift_sec);
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [tag, source_map] : targets_) {
        auto proc_it = source_map.find(instance_id);
        if (proc_it != source_map.end()) {
            proc_it->second.current_timeshift_backshift_sec = timeshift_sec;
            
            if (get_packet_time) {
                auto [buffer_size, now] = get_packet_time(0);
                if (buffer_size == 0) {
                    proc_it->second.next_packet_read_index = 0;
                } else {
                    auto target_past_time = now - 
                        std::chrono::milliseconds(proc_it->second.current_delay_ms) - 
                        std::chrono::duration<double>(timeshift_sec);
                    
                    std::size_t new_idx = buffer_size;
                    for (std::size_t i = 0; i < buffer_size; ++i) {
                        auto [_, packet_time] = get_packet_time(i);
                        if (packet_time >= target_past_time) {
                            new_idx = i;
                            break;
                        }
                    }
                    proc_it->second.next_packet_read_index = new_idx;
                }
            }
            
            if (state_callback_) state_callback_();
            return;
        }
    }
    
    LOG_CPP_WARNING("[ProcessorRegistry] Attempted to update timeshift for unknown processor: %s", instance_id.c_str());
}

void ProcessorRegistry::attach_sink_ring(const std::string& instance_id,
                                         const std::string& source_tag,
                                         const std::string& sink_id,
                                         std::shared_ptr<PacketRing> ring) {
    LOG_CPP_INFO("[ProcessorRegistry] Attaching sink ring: instance=%s source=%s sink=%s",
                 instance_id.c_str(), source_tag.c_str(), sink_id.c_str());
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto source_it = targets_.find(source_tag);
    if (source_it == targets_.end()) {
        LOG_CPP_WARNING("[ProcessorRegistry] Source tag not found: %s", source_tag.c_str());
        return;
    }
    
    auto proc_it = source_it->second.find(instance_id);
    if (proc_it == source_it->second.end()) {
        LOG_CPP_WARNING("[ProcessorRegistry] Processor not found: %s", instance_id.c_str());
        return;
    }
    
    proc_it->second.sink_rings[sink_id] = ring;
    LOG_CPP_INFO("[ProcessorRegistry] Sink ring attached.");
}

void ProcessorRegistry::detach_sink_ring(const std::string& instance_id,
                                         const std::string& source_tag,
                                         const std::string& sink_id) {
    LOG_CPP_INFO("[ProcessorRegistry] Detaching sink ring: instance=%s source=%s sink=%s",
                 instance_id.c_str(), source_tag.c_str(), sink_id.c_str());
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto source_it = targets_.find(source_tag);
    if (source_it == targets_.end()) return;
    
    auto proc_it = source_it->second.find(instance_id);
    if (proc_it == source_it->second.end()) return;
    
    proc_it->second.sink_rings.erase(sink_id);
    LOG_CPP_INFO("[ProcessorRegistry] Sink ring detached.");
}

} // namespace audio
} // namespace screamrouter
