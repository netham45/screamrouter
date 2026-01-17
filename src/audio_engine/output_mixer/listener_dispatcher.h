/**
 * @file listener_dispatcher.h
 * @brief Listener management helper class for SinkAudioMixer.
 * @details Encapsulates WebRTC/network listener lifecycle and audio dispatch.
 */
#ifndef LISTENER_DISPATCHER_H
#define LISTENER_DISPATCHER_H

#include "../senders/i_network_sender.h"
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <limits>

namespace screamrouter {
namespace audio {

struct ListenerAudioBuffer {
    const int32_t* data = nullptr;
    size_t sample_count = 0;
    int channels = 0;
};

/**
 * @class ListenerDispatcher
 * @brief Manages network listeners and dispatches audio to them.
 * @details Thread-safe add/remove/dispatch for WebRTC and other network senders.
 */
class ListenerDispatcher {
public:
    /**
     * @brief Constructs a ListenerDispatcher.
     * @param sink_id Identifier for logging.
     */
    explicit ListenerDispatcher(const std::string& sink_id);
    
    ~ListenerDispatcher() = default;
    
    // Non-copyable
    ListenerDispatcher(const ListenerDispatcher&) = delete;
    ListenerDispatcher& operator=(const ListenerDispatcher&) = delete;
    
    /**
     * @brief Adds a network listener.
     * @param listener_id Unique identifier for the listener.
     * @param sender The network sender implementation (ownership transferred).
     * @return true if setup succeeded (for non-WebRTC) or deferred (for WebRTC).
     */
    bool add_listener(const std::string& listener_id, std::unique_ptr<INetworkSender> sender);
    
    /**
     * @brief Removes a network listener.
     * @param listener_id The ID of the listener to remove.
     */
    void remove_listener(const std::string& listener_id);
    
    /**
     * @brief Gets a raw pointer to a listener's sender.
     * @param listener_id The listener ID.
     * @return Pointer to INetworkSender or nullptr if not found.
     */
    INetworkSender* get_listener(const std::string& listener_id);
    
    /**
     * @brief Dispatches audio payload to all active listeners.
     * @param stereo_buffer Preprocessed stereo buffer metadata.
     * @param multichannel_buffer Mixed multichannel buffer metadata.
     */
    void dispatch_to_listeners(const ListenerAudioBuffer& stereo_buffer,
                               const ListenerAudioBuffer& multichannel_buffer);
    
    /**
     * @brief Cleans up closed or timed-out listeners.
     */
    void cleanup_closed_listeners();
    
    /**
     * @brief Closes all listeners during shutdown.
     */
    void close_all();
    
    /**
     * @brief Gets list of current listener IDs.
     * @return Vector of listener ID strings.
     */
    std::vector<std::string> get_listener_ids() const;
    
    /**
     * @brief Gets count of active listeners.
     * @return Number of listeners.
     */
    size_t count() const;
    
    // Profiling accessors
    uint64_t get_dispatch_calls() const { return dispatch_calls_; }
    long double get_dispatch_ns_sum() const { return dispatch_ns_sum_; }
    uint64_t get_dispatch_ns_max() const { return dispatch_ns_max_; }
    uint64_t get_dispatch_ns_min() const { return dispatch_ns_min_; }
    
    void reset_profiling_counters();

private:
    std::string sink_id_;
    
    std::map<std::string, std::unique_ptr<INetworkSender>> listeners_;
    mutable std::mutex mutex_;
    
    // Profiling
    uint64_t dispatch_calls_{0};
    long double dispatch_ns_sum_{0.0L};
    uint64_t dispatch_ns_max_{0};
    uint64_t dispatch_ns_min_{std::numeric_limits<uint64_t>::max()};
};

} // namespace audio
} // namespace screamrouter

#endif // LISTENER_DISPATCHER_H
