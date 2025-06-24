#ifndef WEBRTC_SENDER_H
#define WEBRTC_SENDER_H

#include "../i_network_sender.h"
#include "../../audio_types.h"
#include <rtc/peerconnection.hpp>
#include <rtc/rtppacketizer.hpp>
#include <rtc/rtcpsrreporter.hpp>
#include <functional>
#include <memory>
#include <vector>
#include <atomic>
#include "../../deps/opus/include/opus.h"
#include <random>
#include <mutex>
#include <chrono>

namespace screamrouter {
namespace audio {

class WebRtcSender : public INetworkSender {
public:
    WebRtcSender(
        const SinkMixerConfig& config,
        std::string offer_sdp,
        std::function<void(const std::string& sdp)> on_local_description_callback,
        std::function<void(const std::string& candidate, const std::string& sdpMid)> on_ice_candidate_callback
    );
    ~WebRtcSender() noexcept override;

    // INetworkSender interface
    bool setup() override;
    void close() override;
    void send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) override;

    // Signaling methods
    void set_remote_description(const std::string& sdp, const std::string& type);
    void add_remote_ice_candidate(const std::string& candidate, const std::string& sdpMid);

    bool is_closed() const;
    bool should_cleanup_due_to_timeout() const;
    void set_cleanup_callback(const std::string& listener_id,
                             std::function<void(const std::string&)> callback);

private:
    void trigger_cleanup_if_needed();
    const std::string DEFAULT_OPUS_AUDIO_PROFILE = "minptime=10;maxaveragebitrate=512000;stereo=1;sprop-stereo=1;useinbandfec=0";
    void setup_peer_connection();
    void initialize_opus_encoder();

    SinkMixerConfig config_;
    std::string offer_sdp_;
    std::unique_ptr<rtc::PeerConnection> peer_connection_;
    
    std::function<void(const std::string& sdp)> on_local_description_callback_;
    std::function<void(const std::string& candidate, const std::string& sdpMid)> on_ice_candidate_callback_;

    std::atomic<rtc::PeerConnection::State> state_;
    std::shared_ptr<rtc::Track> audio_track_;

    // For Opus encoding
    OpusEncoder* opus_encoder_ = nullptr;
    std::vector<int16_t> pcm_buffer_;
    std::vector<unsigned char> opus_buffer_;
    
    // RTP timestamp tracking
    uint32_t current_timestamp_ = 0;
    static constexpr uint32_t OPUS_SAMPLES_PER_FRAME = 120;
    
    // Cleanup mechanism
    std::function<void(const std::string&)> cleanup_callback_;
    std::string listener_id_;
    std::chrono::steady_clock::time_point disconnect_time_;
    static constexpr std::chrono::seconds CLEANUP_TIMEOUT{30};
    std::atomic<bool> cleanup_requested_{false};
    std::atomic<bool> has_been_connected_{false};
};

} // namespace audio
} // namespace screamrouter

#endif // WEBRTC_SENDER_H