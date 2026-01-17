/**
 * @file listener_dispatcher.cpp
 * @brief Implementation of listener management for SinkAudioMixer.
 */
#include "listener_dispatcher.h"
#include "../utils/cpp_logger.h"
#include "../utils/profiler.h"
#include "../senders/webrtc/webrtc_sender.h"
#include <chrono>

namespace screamrouter {
namespace audio {

ListenerDispatcher::ListenerDispatcher(const std::string& sink_id)
    : sink_id_(sink_id)
{
}

bool ListenerDispatcher::add_listener(const std::string& listener_id, std::unique_ptr<INetworkSender> sender) {
    if (!sender) {
        LOG_CPP_ERROR("[ListenerDispatcher:%s] Attempted to add null sender for ID: %s",
                      sink_id_.c_str(), listener_id.c_str());
        return false;
    }
    
    // Setup cleanup callback for WebRTC senders
    if (WebRtcSender* webrtc_sender = dynamic_cast<WebRtcSender*>(sender.get())) {
        LOG_CPP_INFO("[ListenerDispatcher:%s] Registering WebRTC listener '%s'", sink_id_.c_str(), listener_id.c_str());
        webrtc_sender->set_cleanup_callback(listener_id, [this](const std::string& id) {
            LOG_CPP_INFO("[ListenerDispatcher:%s] Cleanup callback triggered for listener: %s",
                         sink_id_.c_str(), id.c_str());
        });
    }
    
    // IMPORTANT: Do NOT call setup() here for WebRTC senders!
    // WebRtcSender::setup() triggers callbacks that need the Python GIL.
    // If we call it here while Python is still in the middle of add_webrtc_listener,
    // it will deadlock. Instead, setup() is called separately after Python releases GIL.
    bool needs_deferred_setup = (dynamic_cast<WebRtcSender*>(sender.get()) != nullptr);
    
    if (!needs_deferred_setup) {
        // For non-WebRTC senders, setup immediately
        if (!sender->setup()) {
            LOG_CPP_ERROR("[ListenerDispatcher:%s] Failed to setup sender for ID: %s",
                          sink_id_.c_str(), listener_id.c_str());
            return false;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        listeners_[listener_id] = std::move(sender);
        LOG_CPP_INFO("[ListenerDispatcher:%s] Added listener: %s (setup %s)",
                     sink_id_.c_str(), listener_id.c_str(),
                     needs_deferred_setup ? "deferred" : "completed");
    }
    
    return true;
}

void ListenerDispatcher::remove_listener(const std::string& listener_id) {
    std::unique_ptr<INetworkSender> sender_to_remove;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = listeners_.find(listener_id);
        if (it != listeners_.end()) {
            sender_to_remove = std::move(it->second);
            listeners_.erase(it);
            LOG_CPP_INFO("[ListenerDispatcher:%s] Removed listener: %s",
                         sink_id_.c_str(), listener_id.c_str());
        } else {
            LOG_CPP_DEBUG("[ListenerDispatcher:%s] Listener not found: %s",
                          sink_id_.c_str(), listener_id.c_str());
            return;
        }
    } // Release mutex before calling close()
    
    // Close the sender WITHOUT holding the mutex to prevent deadlock
    // close() can trigger libdatachannel callbacks that need the GIL
    if (sender_to_remove) {
        if (dynamic_cast<WebRtcSender*>(sender_to_remove.get())) {
            LOG_CPP_INFO("[ListenerDispatcher:%s] Force closing WebRTC connection: %s",
                         sink_id_.c_str(), listener_id.c_str());
        }
        sender_to_remove->close();
    }
}

INetworkSender* ListenerDispatcher::get_listener(const std::string& listener_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = listeners_.find(listener_id);
    if (it != listeners_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void ListenerDispatcher::dispatch_to_listeners(const ListenerAudioBuffer& stereo_buffer,
                                               const ListenerAudioBuffer& multichannel_buffer) {
    PROFILE_FUNCTION();
    auto t0 = std::chrono::steady_clock::now();
    
    std::vector<std::string> closed_listeners;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool has_stereo = stereo_buffer.data && stereo_buffer.sample_count > 0;
        const bool has_multichannel = multichannel_buffer.data && multichannel_buffer.sample_count > 0;
        if (listeners_.empty() || (!has_stereo && !has_multichannel)) {
            return;
        }
        LOG_CPP_DEBUG("[ListenerDispatcher:%s] Dispatching stereo=%zu samples multichannel=%zu samples to %zu listeners",
                      sink_id_.c_str(),
                      has_stereo ? stereo_buffer.sample_count : 0,
                      has_multichannel ? multichannel_buffer.sample_count : 0,
                      listeners_.size());
        
        const uint8_t* stereo_payload = has_stereo
            ? reinterpret_cast<const uint8_t*>(stereo_buffer.data)
            : nullptr;
        const size_t stereo_payload_size = has_stereo
            ? stereo_buffer.sample_count * sizeof(int32_t)
            : 0;
        const uint8_t* multichannel_payload = has_multichannel
            ? reinterpret_cast<const uint8_t*>(multichannel_buffer.data)
            : nullptr;
        const size_t multichannel_payload_size = has_multichannel
            ? multichannel_buffer.sample_count * sizeof(int32_t)
            : 0;
        std::vector<uint32_t> empty_csrcs;
        
        for (const auto& [id, sender] : listeners_) {
            if (sender) {
                const uint8_t* payload_data = stereo_payload;
                size_t payload_size = stereo_payload_size;
                
                if (WebRtcSender* webrtc_sender = dynamic_cast<WebRtcSender*>(sender.get())) {
                    if (webrtc_sender->is_closed()) {
                        closed_listeners.push_back(id);
                        LOG_CPP_INFO("[ListenerDispatcher:%s] Found closed listener: %s",
                                     sink_id_.c_str(), id.c_str());
                        continue;
                    }

                    if (webrtc_sender->wants_multichannel_audio()) {
                        if (has_multichannel &&
                            multichannel_buffer.channels == webrtc_sender->channel_count()) {
                            payload_data = multichannel_payload;
                            payload_size = multichannel_payload_size;
                        } else if (!has_multichannel) {
                            LOG_CPP_WARNING("[ListenerDispatcher:%s] Multichannel requested by %s but no buffer available; falling back to stereo",
                                            sink_id_.c_str(), id.c_str());
                        } else {
                            LOG_CPP_WARNING("[ListenerDispatcher:%s] Multichannel buffer channels (%d) mismatch WebRTC sender expectation (%d) for %s; falling back to stereo",
                                            sink_id_.c_str(),
                                            multichannel_buffer.channels,
                                            webrtc_sender->channel_count(),
                                            id.c_str());
                        }
                    }
                }

                if (payload_data && payload_size > 0) {
                    sender->send_payload(payload_data, payload_size, empty_csrcs);
                }
            }
        }
    } // Release mutex before removing closed listeners
    
    for (const auto& listener_id : closed_listeners) {
        LOG_CPP_INFO("[ListenerDispatcher:%s] Listener '%s' closed during dispatch; removing.",
                     sink_id_.c_str(), listener_id.c_str());
        remove_listener(listener_id);
        LOG_CPP_INFO("[ListenerDispatcher:%s] Removed closed listener: %s",
                     sink_id_.c_str(), listener_id.c_str());
    }
    
    auto t1 = std::chrono::steady_clock::now();
    uint64_t dt = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    dispatch_calls_++;
    dispatch_ns_sum_ += static_cast<long double>(dt);
    if (dt > dispatch_ns_max_) dispatch_ns_max_ = dt;
    if (dt < dispatch_ns_min_) dispatch_ns_min_ = dt;
}

void ListenerDispatcher::cleanup_closed_listeners() {
    PROFILE_FUNCTION();
    std::vector<std::string> to_remove;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, sender] : listeners_) {
            if (WebRtcSender* webrtc_sender = dynamic_cast<WebRtcSender*>(sender.get())) {
                if (webrtc_sender->is_closed() || webrtc_sender->should_cleanup_due_to_timeout()) {
                    to_remove.push_back(id);
                    LOG_CPP_INFO("[ListenerDispatcher:%s] Found listener to cleanup: %s",
                                 sink_id_.c_str(), id.c_str());
                }
            }
        }
    }
    
    for (const auto& listener_id : to_remove) {
        remove_listener(listener_id);
        LOG_CPP_INFO("[ListenerDispatcher:%s] Cleaned up listener: %s",
                     sink_id_.c_str(), listener_id.c_str());
    }
    
    if (!to_remove.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        LOG_CPP_INFO("[ListenerDispatcher:%s] Cleanup complete. Remaining: %zu",
                     sink_id_.c_str(), listeners_.size());
    }
}

void ListenerDispatcher::close_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, sender] : listeners_) {
        if (sender) {
            LOG_CPP_INFO("[ListenerDispatcher:%s] Closing listener: %s",
                         sink_id_.c_str(), id.c_str());
            sender->close();
        }
    }
    listeners_.clear();
    LOG_CPP_INFO("[ListenerDispatcher:%s] All listeners closed.", sink_id_.c_str());
}

std::vector<std::string> ListenerDispatcher::get_listener_ids() const {
    std::vector<std::string> ids;
    std::lock_guard<std::mutex> lock(mutex_);
    ids.reserve(listeners_.size());
    for (const auto& [id, sender] : listeners_) {
        ids.push_back(id);
    }
    return ids;
}

size_t ListenerDispatcher::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return listeners_.size();
}

void ListenerDispatcher::reset_profiling_counters() {
    dispatch_calls_ = 0;
    dispatch_ns_sum_ = 0.0L;
    dispatch_ns_max_ = 0;
    dispatch_ns_min_ = std::numeric_limits<uint64_t>::max();
}

} // namespace audio
} // namespace screamrouter
