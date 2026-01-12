/**
 * @file processor_registry.h
 * @brief Processor registration helper for TimeshiftManager.
 * @details Manages processor lifecycle, delay/timeshift updates, and sink ring attachments.
 */
#ifndef PROCESSOR_REGISTRY_H
#define PROCESSOR_REGISTRY_H

#include "../utils/packet_ring.h"
#include <string>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <cstdint>
#include <chrono>
#include <vector>
#include <functional>

namespace screamrouter {
namespace audio {

// Forward declarations
struct TaggedAudioPacket;
class AudioEngineSettings;

using PacketRing = utils::PacketRing<TaggedAudioPacket>;

/**
 * @struct ProcessorTargetInfo
 * @brief Tracks state for a registered audio processor.
 */
struct ProcessorTargetInfo {
    std::string instance_id;
    std::string source_tag_filter;
    std::string bound_source_tag;
    std::string wildcard_prefix;
    std::string last_logged_mismatch_tag;
    std::set<std::string> matched_concrete_tags;
    
    bool is_wildcard = false;
    int current_delay_ms = 0;
    float current_timeshift_backshift_sec = 0.0f;
    std::size_t next_packet_read_index = 0;
    uint64_t dropped_packets = 0;
    
    // Sink rings associated with this processor
    std::map<std::string, std::weak_ptr<PacketRing>> sink_rings;
};

/**
 * @struct WildcardMatchEvent
 * @brief Records when a wildcard matches a concrete source tag.
 */
struct WildcardMatchEvent {
    std::string instance_id;
    std::string wildcard_filter;
    std::string matched_source_tag;
    bool is_first_match = false;
};

/**
 * @class ProcessorRegistry
 * @brief Manages processor registration and updates for TimeshiftManager.
 */
class ProcessorRegistry {
public:
    using ProcessorMap = std::map<std::string, std::map<std::string, ProcessorTargetInfo>>;
    using StateVersionCallback = std::function<void()>;
    
    ProcessorRegistry() = default;
    ~ProcessorRegistry() = default;
    
    // Non-copyable
    ProcessorRegistry(const ProcessorRegistry&) = delete;
    ProcessorRegistry& operator=(const ProcessorRegistry&) = delete;
    
    /**
     * @brief Sets callback to notify state changes.
     */
    void set_state_change_callback(StateVersionCallback cb) { state_callback_ = std::move(cb); }
    
    /**
     * @brief Registers a new processor.
     */
    void register_processor(const std::string& instance_id,
                           const std::string& source_tag,
                           int initial_delay_ms,
                           float initial_timeshift_sec,
                           std::size_t current_buffer_size);
    
    /**
     * @brief Unregisters a processor.
     */
    void unregister_processor(const std::string& instance_id, const std::string& source_tag);
    
    /**
     * @brief Updates the static delay for a processor.
     */
    void update_delay(const std::string& instance_id, int delay_ms);
    
    /**
     * @brief Updates the timeshift for a processor.
     * @param buffer_access Callback to access buffer for recalculating read index.
     */
    void update_timeshift(const std::string& instance_id, float timeshift_sec,
                         std::function<std::pair<std::size_t, std::chrono::steady_clock::time_point>(std::size_t)> get_packet_time);
    
    /**
     * @brief Attaches a sink ring to a processor.
     */
    void attach_sink_ring(const std::string& instance_id,
                         const std::string& source_tag,
                         const std::string& sink_id,
                         std::shared_ptr<PacketRing> ring);
    
    /**
     * @brief Detaches a sink ring from a processor.
     */
    void detach_sink_ring(const std::string& instance_id,
                         const std::string& source_tag,
                         const std::string& sink_id);
    
    /**
     * @brief Gets processor targets map (read access).
     */
    const ProcessorMap& get_targets() const { return targets_; }
    
    /**
     * @brief Gets mutable processor targets (for dispatch loop).
     */
    ProcessorMap& get_mutable_targets() { return targets_; }
    
    /**
     * @brief Gets the mutex for thread-safe access.
     */
    std::mutex& get_mutex() { return mutex_; }

private:
    ProcessorMap targets_;
    std::mutex mutex_;
    StateVersionCallback state_callback_;
};

} // namespace audio
} // namespace screamrouter

#endif // PROCESSOR_REGISTRY_H
