#ifndef WEBRTC_MANAGER_H
#define WEBRTC_MANAGER_H

#include "sink_manager.h"
#include "../senders/webrtc/webrtc_sender.h"
#include <string>
#include <functional>
#include <memory>
#include <mutex>

namespace screamrouter {
namespace audio {

class WebRtcManager {
public:
    WebRtcManager(std::mutex& manager_mutex, SinkManager* sink_manager, std::map<std::string, SinkConfig>& sink_configs);
    ~WebRtcManager();

    bool add_webrtc_listener(
        const std::string& sink_id,
        const std::string& listener_id,
        const std::string& offer_sdp,
        std::function<void(const std::string& sdp)> on_local_description_callback,
        std::function<void(const std::string& candidate, const std::string& sdpMid)> on_ice_candidate_callback,
        bool running
    );

    bool remove_webrtc_listener(const std::string& sink_id, const std::string& listener_id, bool running);
    void set_webrtc_remote_description(const std::string& sink_id, const std::string& listener_id, const std::string& sdp, const std::string& type, bool running);
    void add_webrtc_remote_ice_candidate(const std::string& sink_id, const std::string& listener_id, const std::string& candidate, const std::string& sdpMid, bool running);

private:
    std::mutex& m_manager_mutex;
    SinkManager* m_sink_manager;
    std::map<std::string, SinkConfig>& m_sink_configs;
};

} // namespace audio
} // namespace screamrouter

#endif // WEBRTC_MANAGER_H