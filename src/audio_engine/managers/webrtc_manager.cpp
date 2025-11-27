#include "webrtc_manager.h"
#include "../utils/cpp_logger.h"
#include <thread>
#include <chrono>

namespace screamrouter {
namespace audio {

WebRtcManager::WebRtcManager(std::recursive_mutex& manager_mutex, SinkManager* sink_manager, std::map<std::string, SinkConfig>& sink_configs)
    : m_manager_mutex(manager_mutex), m_sink_manager(sink_manager), m_sink_configs(sink_configs) {
    LOG_CPP_INFO("WebRtcManager created.");
}

WebRtcManager::~WebRtcManager() {
    // Signal any deferred setup threads to exit early and join them.
    shutting_down_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(setup_threads_mutex_);
        for (auto &t : setup_threads_) {
            if (t.joinable()) {
                try { t.join(); } catch (const std::system_error& e) {
                    LOG_CPP_ERROR("[WebRtcManager] Error joining setup thread: %s", e.what());
                }
            }
        }
        setup_threads_.clear();
    }
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
        std::scoped_lock lock(m_manager_mutex);
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

    // Step 1: Validate and prepare configuration under lock
    SinkMixerConfig mixer_config;
    {
        std::scoped_lock lock(m_manager_mutex);
        
        auto config_it = m_sink_configs.find(sink_id);
        if (config_it == m_sink_configs.end()) {
            LOG_CPP_ERROR("[WebRtcManager] Sink config not found for WebRTC listener: %s", sink_id.c_str());
            return false;
        }

        // Copy the configuration while holding the lock
        const SinkConfig& sink_config = config_it->second;
        mixer_config.sink_id = sink_config.id;
        mixer_config.friendly_name = sink_config.friendly_name;
        mixer_config.output_ip = sink_config.output_ip;
        mixer_config.output_port = sink_config.output_port;
        mixer_config.output_bitdepth = sink_config.bitdepth;
        mixer_config.output_samplerate = sink_config.samplerate;
        mixer_config.output_channels = sink_config.channels;
        mixer_config.output_chlayout1 = sink_config.chlayout1;
        mixer_config.output_chlayout2 = sink_config.chlayout2;
        mixer_config.protocol = sink_config.protocol;
        mixer_config.sap_target_sink = sink_config.sap_target_sink;
        mixer_config.sap_target_host = sink_config.sap_target_host;
        mixer_config.speaker_layout = sink_config.speaker_layout;
    } // Release m_manager_mutex here to prevent deadlock

    // Step 2: Create WebRtcSender WITHOUT holding m_manager_mutex
    // This prevents deadlock with libdatachannel callbacks that need the GIL
    std::unique_ptr<WebRtcSender> webrtc_sender;
    try {
        webrtc_sender = std::make_unique<WebRtcSender>(mixer_config, offer_sdp, on_local_description_callback, on_ice_candidate_callback);
    } catch (const std::exception& e) {
        LOG_CPP_ERROR("[WebRtcManager] Failed to create WebRtcSender for listener %s on sink %s: %s", listener_id.c_str(), sink_id.c_str(), e.what());
        return false;
    }

    // Step 3: Add the sender to the sink WITHOUT holding m_manager_mutex
    // Note: We do NOT call setup() here - it will be done separately to avoid deadlock
    m_sink_manager->add_listener_to_sink(sink_id, listener_id, std::move(webrtc_sender));
    
    // Step 4: Update internal state (reacquire lock)
    {
        std::scoped_lock lock(m_manager_mutex);
        
        // Double-check the sink still exists
        if (m_sink_configs.find(sink_id) == m_sink_configs.end()) {
            LOG_CPP_WARNING("[WebRtcManager] Sink %s disappeared after adding WebRTC listener %s", sink_id.c_str(), listener_id.c_str());
            // The listener was already added, so we should remove it
            m_sink_manager->remove_listener_from_sink(sink_id, listener_id);
            return false;
        }
        
        // Store listener info
        m_webrtc_listeners[listener_id] = {sink_id, listener_id, client_ip};
        
        LOG_CPP_INFO("[WebRtcManager] Successfully registered WebRTC listener %s for sink %s from IP %s", listener_id.c_str(), sink_id.c_str(), client_ip.c_str());
    }
    
    // Step 5: Defer the WebRTC setup to avoid GIL deadlock
    // CRITICAL: We CANNOT call setup() here because it triggers callbacks that need the Python GIL.
    // Since Python is still in the middle of calling add_webrtc_listener (holding the GIL),
    // calling setup() here would deadlock. Instead, we defer it to a separate thread.
    
    // Launch setup in a managed thread to avoid GIL deadlock and allow clean shutdown
    {
        std::lock_guard<std::mutex> lock(setup_threads_mutex_);
        setup_threads_.emplace_back([this, sink_id, listener_id]() {
            // Small delay to ensure Python has released the GIL
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (shutting_down_.load(std::memory_order_acquire)) {
                return;
            }

            INetworkSender* sender = m_sink_manager->get_listener_from_sink(sink_id, listener_id);
            if (sender) {
                if (!sender->setup()) {
                    LOG_CPP_ERROR("[WebRtcManager] Failed to setup WebRTC connection for listener %s", listener_id.c_str());
                    // Clean up on failure
                    remove_webrtc_listener(sink_id, listener_id, true);
                }
            } else {
                LOG_CPP_ERROR("[WebRtcManager] Could not find sender for listener %s after adding", listener_id.c_str());
            }
        });
    }
    
    return true;
}

bool WebRtcManager::remove_webrtc_listener(const std::string& sink_id, const std::string& listener_id, bool running) {
    // Step 1: Check if we should proceed and validate listener exists (under lock)
    {
        std::scoped_lock lock(m_manager_mutex);
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
        std::scoped_lock lock(m_manager_mutex);
        m_webrtc_listeners.erase(listener_id);
    }
    
    LOG_CPP_INFO("[WebRtcManager] Removed WebRTC listener %s from sink %s", listener_id.c_str(), sink_id.c_str());
    return true;
}

void WebRtcManager::add_webrtc_remote_ice_candidate(const std::string& sink_id, const std::string& listener_id, const std::string& candidate, const std::string& sdpMid, bool running) {
    // Step 1: Validate and get the sender under lock
    WebRtcSender* webrtc_sender = nullptr;
    {
        std::scoped_lock lock(m_manager_mutex);
        if (!running) return;

        INetworkSender* sender = m_sink_manager->get_listener_from_sink(sink_id, listener_id);
        if (sender) {
            webrtc_sender = dynamic_cast<WebRtcSender*>(sender);
            if (!webrtc_sender) {
                LOG_CPP_ERROR("[WebRtcManager] Listener %s on sink %s is not a WebRtcSender.", listener_id.c_str(), sink_id.c_str());
                return;
            }
        } else {
            return;
        }
    } // Release m_manager_mutex here

    // Step 2: Call into WebRTC without holding m_manager_mutex
    // This prevents deadlock with libdatachannel callbacks that need the GIL
    if (webrtc_sender) {
        webrtc_sender->add_remote_ice_candidate(candidate, sdpMid);
    }
}

void WebRtcManager::set_webrtc_remote_description(const std::string& sink_id,
                                                  const std::string& listener_id,
                                                  const std::string& sdp,
                                                  const std::string& type,
                                                  bool running) {
    WebRtcSender* webrtc_sender = nullptr;
    {
        std::scoped_lock lock(m_manager_mutex);
        if (!running) {
            return;
        }

        INetworkSender* sender = m_sink_manager->get_listener_from_sink(sink_id, listener_id);
        if (!sender) {
            return;
        }

        webrtc_sender = dynamic_cast<WebRtcSender*>(sender);
        if (!webrtc_sender) {
            LOG_CPP_ERROR("[WebRtcManager] Listener %s on sink %s is not a WebRtcSender.", listener_id.c_str(), sink_id.c_str());
            return;
        }
    }

    if (webrtc_sender) {
        webrtc_sender->set_remote_description(sdp, type);
    }
}


} // namespace audio
} // namespace screamrouter
