#include "webrtc_manager.h"
#include "../utils/cpp_logger.h"

namespace screamrouter {
namespace audio {

WebRtcManager::WebRtcManager(std::mutex& manager_mutex, SinkManager* sink_manager, std::map<std::string, SinkConfig>& sink_configs)
    : m_manager_mutex(manager_mutex), m_sink_manager(sink_manager), m_sink_configs(sink_configs) {
    LOG_CPP_INFO("WebRtcManager created.");
}

WebRtcManager::~WebRtcManager() {
    LOG_CPP_INFO("WebRtcManager destroyed.");
}

bool WebRtcManager::add_webrtc_listener(
    const std::string& sink_id,
    const std::string& listener_id,
    const std::string& offer_sdp,
    std::function<void(const std::string& sdp)> on_local_description_callback,
    std::function<void(const std::string& candidate, const std::string& sdpMid)> on_ice_candidate_callback,
    bool running,
    const std::string& client_ip)
{
    if (!running) {
        LOG_CPP_ERROR("[WebRtcManager] Cannot add WebRTC listener, manager is not running.");
        return false;
    }

    // Collect listeners to remove (from same IP) without holding the lock
    std::vector<std::pair<std::string, std::string>> listeners_to_remove;
    {
        std::lock_guard<std::mutex> lock(m_manager_mutex);
        for (auto it = m_webrtc_listeners.begin(); it != m_webrtc_listeners.end(); ++it) {
            if (it->second.ip_address == client_ip) {
                LOG_CPP_INFO("[WebRtcManager] Found existing WebRTC listener %s from IP %s. Will remove it.", it->second.listener_id.c_str(), client_ip.c_str());
                listeners_to_remove.push_back({it->second.sink_id, it->second.listener_id});
            }
        }
    }

    // Remove the listeners without holding the lock to avoid deadlock
    for (const auto& [old_sink_id, old_listener_id] : listeners_to_remove) {
        remove_webrtc_listener(old_sink_id, old_listener_id, running);
    }

    // Now proceed with adding the new listener
    std::lock_guard<std::mutex> lock(m_manager_mutex);

    auto config_it = m_sink_configs.find(sink_id);
    if (config_it == m_sink_configs.end()) {
        LOG_CPP_ERROR("[WebRtcManager] Sink config not found for WebRTC listener: %s", sink_id.c_str());
        return false;
    }

    try {
        const SinkConfig& sink_config = config_it->second;
        SinkMixerConfig mixer_config;
        mixer_config.sink_id = sink_config.id;
        mixer_config.output_ip = sink_config.output_ip;
        mixer_config.output_port = sink_config.output_port;
        mixer_config.output_bitdepth = sink_config.bitdepth;
        mixer_config.output_samplerate = sink_config.samplerate;
        mixer_config.output_channels = sink_config.channels;
        mixer_config.output_chlayout1 = sink_config.chlayout1;
        mixer_config.output_chlayout2 = sink_config.chlayout2;
        mixer_config.protocol = sink_config.protocol;
        mixer_config.speaker_layout = sink_config.speaker_layout;

        auto webrtc_sender = std::make_unique<WebRtcSender>(mixer_config, offer_sdp, on_local_description_callback, on_ice_candidate_callback);
        m_sink_manager->add_listener_to_sink(sink_id, listener_id, std::move(webrtc_sender));
        
        // Store listener info
        m_webrtc_listeners[listener_id] = {sink_id, listener_id, client_ip};

        LOG_CPP_INFO("[WebRtcManager] Added WebRTC listener %s to sink %s for IP %s", listener_id.c_str(), sink_id.c_str(), client_ip.c_str());
        return true;
    } catch (const std::exception& e) {
        LOG_CPP_ERROR("[WebRtcManager] Failed to create WebRtcSender for listener %s on sink %s: %s", listener_id.c_str(), sink_id.c_str(), e.what());
        return false;
    }
}

bool WebRtcManager::remove_webrtc_listener(const std::string& sink_id, const std::string& listener_id, bool running) {
    // Step 1: Check if we should proceed and validate listener exists (under lock)
    {
        std::lock_guard<std::mutex> lock(m_manager_mutex);
        if (!running) {
            return true;
        }
        
        // Verify listener exists before proceeding
        INetworkSender* existing_listener = m_sink_manager->get_listener_from_sink(sink_id, listener_id);
        if (!existing_listener) {
            LOG_CPP_DEBUG("[WebRtcManager] WebRTC listener %s already removed from sink %s", listener_id.c_str(), sink_id.c_str());
            // Clean up our tracking map just in case
            m_webrtc_listeners.erase(listener_id);
            return true;
        }
    } // Release m_manager_mutex here to prevent deadlock
    
    // Step 2: Perform the actual removal WITHOUT holding m_manager_mutex
    // This prevents deadlock with SinkAudioMixer's listener_senders_mutex_
    m_sink_manager->remove_listener_from_sink(sink_id, listener_id);
    
    // Step 3: Update our internal state (reacquire lock)
    {
        std::lock_guard<std::mutex> lock(m_manager_mutex);
        m_webrtc_listeners.erase(listener_id);
    }
    
    LOG_CPP_INFO("[WebRtcManager] Removed WebRTC listener %s from sink %s", listener_id.c_str(), sink_id.c_str());
    return true;
}

void WebRtcManager::set_webrtc_remote_description(const std::string& sink_id, const std::string& listener_id, const std::string& sdp, const std::string& type, bool running) {
    std::lock_guard<std::mutex> lock(m_manager_mutex);
    if (!running) return;

    INetworkSender* sender = m_sink_manager->get_listener_from_sink(sink_id, listener_id);
    if (sender) {
        WebRtcSender* webrtc_sender = dynamic_cast<WebRtcSender*>(sender);
        if (webrtc_sender) {
            webrtc_sender->set_remote_description(sdp, type);
        } else {
            LOG_CPP_ERROR("[WebRtcManager] Listener %s on sink %s is not a WebRtcSender.", listener_id.c_str(), sink_id.c_str());
        }
    }
}

void WebRtcManager::add_webrtc_remote_ice_candidate(const std::string& sink_id, const std::string& listener_id, const std::string& candidate, const std::string& sdpMid, bool running) {
    std::lock_guard<std::mutex> lock(m_manager_mutex);
    if (!running) return;

    INetworkSender* sender = m_sink_manager->get_listener_from_sink(sink_id, listener_id);
    if (sender) {
        WebRtcSender* webrtc_sender = dynamic_cast<WebRtcSender*>(sender);
        if (webrtc_sender) {
            webrtc_sender->add_remote_ice_candidate(candidate, sdpMid);
        } else {
            LOG_CPP_ERROR("[WebRtcManager] Listener %s on sink %s is not a WebRtcSender.", listener_id.c_str(), sink_id.c_str());
        }
    }
}

} // namespace audio
} // namespace screamrouter