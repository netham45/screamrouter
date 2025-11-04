/**
 * @file webrtc_sender.h
 * @brief Defines the WebRtcSender class for sending audio data via WebRTC.
 * @details This file contains the definition of the `WebRtcSender` class, which
 *          implements the `INetworkSender` interface to stream audio to a WebRTC peer.
 *          It handles the PeerConnection setup, SDP exchange, ICE candidate gathering,
 *          and Opus encoding of the audio data.
 */
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
#include "../../deps/opus/include/opus_multistream.h"
#include <random>
#include <mutex>
#include <chrono>

namespace screamrouter {
namespace audio {

/**
 * @struct WebRtcSenderStats
 * @brief Holds raw statistics collected from the WebRtcSender.
 */
struct WebRtcSenderStats {
    uint64_t total_packets_sent = 0;
    std::string connection_state;
    size_t pcm_buffer_size = 0;
};

/**
 * @class WebRtcSender
 * @brief An implementation of `INetworkSender` for the WebRTC protocol.
 * @details This class manages a `libdatachannel` PeerConnection to stream audio to a
 *          single remote peer. It takes raw PCM audio, encodes it into the Opus format,
 *          and sends it over an established WebRTC data track. It uses callbacks to
 *          handle the signaling process (SDP and ICE candidates) with the remote peer.
 */
class WebRtcSender : public INetworkSender {
public:
    /**
     * @brief Constructs a WebRtcSender.
     * @param config The configuration for the sink this sender is associated with.
     * @param offer_sdp The SDP offer received from the remote peer.
     * @param on_local_description_callback A callback to send the local SDP answer to the peer.
     * @param on_ice_candidate_callback A callback to send local ICE candidates to the peer.
     */
    WebRtcSender(
        const SinkMixerConfig& config,
        std::string offer_sdp,
        std::function<void(const std::string& sdp)> on_local_description_callback,
        std::function<void(const std::string& candidate, const std::string& sdpMid)> on_ice_candidate_callback
    );
    /**
     * @brief Destructor.
     */
    ~WebRtcSender() noexcept override;

    /** @brief Sets up the PeerConnection and Opus encoder. */
    bool setup() override;
    /** @brief Closes the PeerConnection and cleans up resources. */
    void close() override;
    /**
     * @brief Encodes and sends an audio payload via the WebRTC data track.
     * @param payload_data Pointer to the raw PCM audio data.
     * @param payload_size The size of the audio data in bytes.
     * @param csrcs Contributing source identifiers (ignored by this sender).
     */
    void send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) override;

    /**
     * @brief Sets the remote description on the PeerConnection.
     * @param sdp The SDP string from the remote peer.
     * @param type The type of the SDP ("offer" or "answer").
     */
    void set_remote_description(const std::string& sdp, const std::string& type);
    /**
     * @brief Adds a remote ICE candidate to the PeerConnection.
     * @param candidate The ICE candidate string.
     * @param sdpMid The SDP media line identifier.
     */
    void add_remote_ice_candidate(const std::string& candidate, const std::string& sdpMid);

    /** @brief Checks if the PeerConnection is closed or failed. */
    bool is_closed() const;
    /** @brief Checks if the sender should be cleaned up due to a timeout. */
    bool should_cleanup_due_to_timeout() const;
    /**
     * @brief Sets a callback to be invoked when the sender needs to be cleaned up.
     * @param listener_id The ID of this listener.
     * @param callback The cleanup callback function.
     */
    void set_cleanup_callback(const std::string& listener_id,
                             std::function<void(const std::string&)> callback);

    /**
     * @brief Retrieves the current statistics from the sender.
     * @return A struct containing the current stats.
     */
    WebRtcSenderStats get_stats();

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

    OpusEncoder* opus_encoder_ = nullptr;
    OpusMSEncoder* opus_ms_encoder_ = nullptr;
    std::vector<int16_t> pcm_buffer_;
    std::vector<unsigned char> opus_buffer_;
    
    uint32_t current_timestamp_ = 0;
    static constexpr uint32_t OPUS_SAMPLES_PER_FRAME = 120;
    
    std::function<void(const std::string&)> cleanup_callback_;
    std::string listener_id_;
    std::chrono::steady_clock::time_point disconnect_time_;
    static constexpr std::chrono::seconds CLEANUP_TIMEOUT{30};
    std::atomic<bool> cleanup_requested_{false};
    std::atomic<bool> has_been_connected_{false};
    std::atomic<uint64_t> m_total_packets_sent{0};

    int opus_channels_ = 2;
    bool use_multistream_ = false;
    int opus_streams_ = 0;
    int opus_coupled_streams_ = 0;
    std::vector<unsigned char> opus_mapping_;
    std::string opus_fmtp_profile_;

    bool configure_multistream_layout();
    std::string build_opus_fmtp_profile() const;
};

} // namespace audio
} // namespace screamrouter

#endif // WEBRTC_SENDER_H
