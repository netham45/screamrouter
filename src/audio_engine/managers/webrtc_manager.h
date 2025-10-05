/**
 * @file webrtc_manager.h
 * @brief Defines the WebRtcManager class for handling WebRTC connections.
 * @details This class encapsulates the logic for creating, managing, and tearing down
 *          WebRTC peer connections, which act as listeners on audio sinks.
 */
#ifndef WEBRTC_MANAGER_H
#define WEBRTC_MANAGER_H

#include "sink_manager.h"
#include "../senders/webrtc/webrtc_sender.h"
#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <map>

namespace screamrouter {
namespace audio {

/**
 * @struct WebRtcListenerInfo
 * @brief Stores information about an active WebRTC listener.
 */
struct WebRtcListenerInfo {
    std::string sink_id;
    std::string listener_id;
    std::string ip_address;
};

/**
 * @class WebRtcManager
 * @brief Manages WebRTC listeners for audio sinks.
 * @details This class provides an interface to add and remove WebRTC listeners
 *          to/from sinks. It handles the SDP offer/answer exchange and ICE candidate
 *          gathering process by interacting with `WebRtcSender` instances and using
 *          callbacks to communicate with the client.
 */
class WebRtcManager {
public:
    /**
     * @brief Constructs a WebRtcManager.
     * @param manager_mutex A reference to the main AudioManager mutex for thread safety.
     * @param sink_manager A pointer to the SinkManager instance.
     * @param sink_configs A reference to the map of sink configurations.
     */
    WebRtcManager(std::mutex& manager_mutex, SinkManager* sink_manager, std::map<std::string, SinkConfig>& sink_configs);
    /**
     * @brief Destructor.
     */
    ~WebRtcManager();

    /**
     * @brief Adds a new WebRTC listener to a sink.
     * @param sink_id The ID of the sink to listen to.
     * @param listener_id A unique ID for the new listener.
     * @param offer_sdp The SDP offer from the remote peer.
     * @param on_local_description_callback Callback to send the local SDP answer.
     * @param on_ice_candidate_callback Callback to send local ICE candidates.
     * @param running A flag indicating if the audio engine is running.
     * @param client_ip The IP address of the client.
     * @return true if the listener was added successfully, false otherwise.
     */
    bool add_webrtc_listener(
        const std::string& sink_id,
        const std::string& listener_id,
        const std::string& offer_sdp,
        std::function<void(const std::string& sdp)> on_local_description_callback,
        std::function<void(const std::string& candidate, const std::string& sdpMid)> on_ice_candidate_callback,
        bool running,
        const std::string& client_ip
    );

    /**
     * @brief Removes a WebRTC listener from a sink.
     * @param sink_id The ID of the sink.
     * @param listener_id The ID of the listener to remove.
     * @param running A flag indicating if the audio engine is running.
     * @return true if the listener was removed successfully, false otherwise.
     */
    bool remove_webrtc_listener(const std::string& sink_id, const std::string& listener_id, bool running);
    /**
     * @brief Sets the remote SDP description for a WebRTC peer connection.
     * @param sink_id The ID of the sink.
     * @param listener_id The ID of the listener.
     * @param sdp The SDP string from the remote peer.
     * @param type The type of the SDP ("offer" or "answer").
     * @param running A flag indicating if the audio engine is running.
     */
    void set_webrtc_remote_description(const std::string& sink_id, const std::string& listener_id, const std::string& sdp, const std::string& type, bool running);
    /**
     * @brief Adds a remote ICE candidate for a WebRTC peer connection.
     * @param sink_id The ID of the sink.
     * @param listener_id The ID of the listener.
     * @param candidate The ICE candidate string.
     * @param sdpMid The SDP media line identifier.
     * @param running A flag indicating if the audio engine is running.
     */
    void add_webrtc_remote_ice_candidate(const std::string& sink_id, const std::string& listener_id, const std::string& candidate, const std::string& sdpMid, bool running);

private:
    std::mutex& m_manager_mutex;
    SinkManager* m_sink_manager;
    std::map<std::string, SinkConfig>& m_sink_configs;
    std::map<std::string, WebRtcListenerInfo> m_webrtc_listeners;
};

} // namespace audio
} // namespace screamrouter

#endif // WEBRTC_MANAGER_H