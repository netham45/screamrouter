#include "webrtc_sender.h"
#include "../../utils/cpp_logger.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/gil.h>
#include <rtc/rtc.hpp>
#include <rtc/frameinfo.hpp>
#include <chrono>
#include <mutex>
#include <random>
#include <string>
#include <algorithm>

namespace screamrouter {
namespace audio {

WebRtcSender::WebRtcSender(
    const SinkMixerConfig& config,
    std::string offer_sdp,
    std::function<void(const std::string& sdp)> on_local_description_callback,
    std::function<void(const std::string& candidate, const std::string& sdpMid)> on_ice_candidate_callback)
    : config_(config),
      offer_sdp_(std::move(offer_sdp)),
      on_local_description_callback_(on_local_description_callback),
      on_ice_candidate_callback_(on_ice_candidate_callback),
      state_(rtc::PeerConnection::State::New),
      audio_track_(nullptr),
      current_timestamp_(0) {
    LOG_CPP_INFO("[WebRtcSender] DEADLOCK_DEBUG: Constructor START for sink: %s", config_.sink_id.c_str());
    initialize_opus_encoder();
    LOG_CPP_INFO("[WebRtcSender] DEADLOCK_DEBUG: Constructor END for sink: %s", config_.sink_id.c_str());
}

WebRtcSender::~WebRtcSender() noexcept {
    close();
    if (opus_encoder_) {
        opus_encoder_destroy(opus_encoder_);
    }
}

void WebRtcSender::initialize_opus_encoder() {
    int error;
    opus_encoder_ = opus_encoder_create(config_.output_samplerate, 2, OPUS_APPLICATION_AUDIO, &error);
    if (error != OPUS_OK) {
        LOG_CPP_ERROR("[WebRtcSender:%s] Failed to create Opus encoder: %s", config_.sink_id.c_str(), opus_strerror(error));
        opus_encoder_ = nullptr;
        return;
    }
    opus_encoder_ctl(opus_encoder_, OPUS_SET_BITRATE(512000));
    opus_encoder_ctl(opus_encoder_, OPUS_SET_VBR(0));
    opus_encoder_ctl(opus_encoder_, OPUS_SET_INBAND_FEC(0));
    opus_encoder_ctl(opus_encoder_, OPUS_SET_COMPLEXITY(10));
    opus_encoder_ctl(opus_encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    opus_buffer_.resize(4000); // Max opus packet size
}

bool WebRtcSender::setup() {
    LOG_CPP_INFO("[WebRtcSender:%s] DEADLOCK_DEBUG: setup() START", config_.sink_id.c_str());
    try {
        setup_peer_connection();
        LOG_CPP_INFO("[WebRtcSender:%s] DEADLOCK_DEBUG: setup() END - success", config_.sink_id.c_str());
        return true;
    } catch (const std::exception& e) {
        LOG_CPP_ERROR("[WebRtcSender:%s] Exception during setup: %s", config_.sink_id.c_str(), e.what());
        LOG_CPP_INFO("[WebRtcSender:%s] DEADLOCK_DEBUG: setup() END - failed", config_.sink_id.c_str());
        return false;
    }
}

void WebRtcSender::setup_peer_connection() {
    LOG_CPP_INFO("[WebRtcSender:%s] DEADLOCK_DEBUG: setup_peer_connection() START", config_.sink_id.c_str());
    rtc::Configuration rtc_config;
    rtc_config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    rtc_config.iceServers.emplace_back("turn:screamrouter:screamrouter@192.168.3.201:3478");
    rtc_config.disableAutoNegotiation = true;

    LOG_CPP_INFO("[WebRtcSender:%s] DEADLOCK_DEBUG: Creating PeerConnection", config_.sink_id.c_str());
    peer_connection_ = std::make_unique<rtc::PeerConnection>(rtc_config);
    LOG_CPP_INFO("[WebRtcSender:%s] DEADLOCK_DEBUG: PeerConnection created", config_.sink_id.c_str());

    peer_connection_->onStateChange([this](rtc::PeerConnection::State state) {
        state_ = state;
        std::string state_str;
        bool should_close = false;
        switch (state) {
            case rtc::PeerConnection::State::New: state_str = "New"; break;
            case rtc::PeerConnection::State::Connecting: state_str = "Connecting"; break;
            case rtc::PeerConnection::State::Connected:
                state_str = "Connected";
                has_been_connected_.store(true);
                break;
            case rtc::PeerConnection::State::Disconnected:
                state_str = "Disconnected";
                should_close = true;
                break;
            case rtc::PeerConnection::State::Failed:
                state_str = "Failed";
                should_close = true;
                break;
            case rtc::PeerConnection::State::Closed:
                state_str = "Closed";
                should_close = true;
                break;
            default: state_str = "Unknown"; break;
        }
        LOG_CPP_INFO("[WebRtcSender:%s] PeerConnection state changed to: %s", config_.sink_id.c_str(), state_str.c_str());
        if (should_close) {
            LOG_CPP_INFO("[WebRtcSender:%s] Connection state is now terminal. Triggering cleanup.", config_.sink_id.c_str());
            disconnect_time_ = std::chrono::steady_clock::now();
            trigger_cleanup_if_needed();
        }
    });

    peer_connection_->onIceStateChange([this](rtc::PeerConnection::IceState ice_state) {
        std::string ice_state_str;
        bool should_close = false;
        switch (ice_state) {
            case rtc::PeerConnection::IceState::New: ice_state_str = "New"; break;
            case rtc::PeerConnection::IceState::Checking: ice_state_str = "Checking"; break;
            case rtc::PeerConnection::IceState::Connected:
                ice_state_str = "Connected";
                state_ = rtc::PeerConnection::State::Connected;
                has_been_connected_.store(true);
                break;
            case rtc::PeerConnection::IceState::Completed:
                ice_state_str = "Completed";
                state_ = rtc::PeerConnection::State::Connected;
                has_been_connected_.store(true);
                break;
            case rtc::PeerConnection::IceState::Failed:
                ice_state_str = "Failed";
                should_close = true;
                break;
            case rtc::PeerConnection::IceState::Disconnected:
                ice_state_str = "Disconnected";
                should_close = true;
                break;
            case rtc::PeerConnection::IceState::Closed:
                ice_state_str = "Closed";
                should_close = true;
                break;
            default: ice_state_str = "Unknown"; break;
        }
        LOG_CPP_INFO("[WebRtcSender:%s] ICE state changed to: %s", config_.sink_id.c_str(), ice_state_str.c_str());
        
        if (should_close) {
            LOG_CPP_INFO("[WebRtcSender:%s] ICE state is now terminal. Triggering cleanup.", config_.sink_id.c_str());
            if (disconnect_time_ == std::chrono::steady_clock::time_point{}) {
                disconnect_time_ = std::chrono::steady_clock::now();
            }
            trigger_cleanup_if_needed();
        }
    });

    peer_connection_->onLocalDescription([this](rtc::Description desc) {
        LOG_CPP_ERROR("[WebRtcSender:%s] DEADLOCK_DEBUG: onLocalDescription triggered, type=%s",
                      config_.sink_id.c_str(), desc.typeString().c_str());
        if (on_local_description_callback_) {
            std::string sdp_string = std::string(desc);
            LOG_CPP_ERROR("[WebRtcSender:%s] DEADLOCK_DEBUG: About to acquire GIL and call Python callback", config_.sink_id.c_str());
            pybind11::gil_scoped_acquire acquire;
            LOG_CPP_ERROR("[WebRtcSender:%s] DEADLOCK_DEBUG: GIL acquired, calling Python callback", config_.sink_id.c_str());
            on_local_description_callback_(sdp_string);
            LOG_CPP_ERROR("[WebRtcSender:%s] DEADLOCK_DEBUG: Python callback completed", config_.sink_id.c_str());
        }
    });

    peer_connection_->onLocalCandidate([this](rtc::Candidate cand) {
        if (on_ice_candidate_callback_) {
            std::string candidate_str = std::string(cand);
            std::string sdp_mid_str = cand.mid();
            LOG_CPP_INFO("[WebRtcSender:%s] Generated local ICE candidate. Forwarding to Python.", config_.sink_id.c_str());
            pybind11::gil_scoped_acquire acquire;
            on_ice_candidate_callback_(candidate_str, sdp_mid_str);
        }
    });

    try {
        if (offer_sdp_.empty()) {
            LOG_CPP_ERROR("[WebRtcSender:%s] Cannot setup peer connection without a remote offer.", config_.sink_id.c_str());
            return;
        }
        LOG_CPP_INFO("[WebRtcSender:%s] Processing remote offer.", config_.sink_id.c_str());
        rtc::Description offer(offer_sdp_, "offer");

        // Find the audio media description in the client's offer
        rtc::Description::Media* remote_audio_media = nullptr;
        for (int i = 0; i < offer.mediaCount(); ++i) {
            auto media = offer.media(i);
            if (auto* media_ptr = std::get_if<rtc::Description::Media*>(&media)) {
                if ((*media_ptr)->type() == "audio") {
                    remote_audio_media = *media_ptr;
                    break;
                }
            }
        }

        if (!remote_audio_media) {
            throw std::runtime_error("Could not find audio media description in remote offer");
        }

        // Create a reciprocal media description to answer the client's offer.
        // This correctly copies the mid and codecs, while inverting the direction.
        auto audio_description = remote_audio_media->reciprocate();

        // Explicitly set the direction to sendonly to conform to WHEP spec
        audio_description.setDirection(rtc::Description::Direction::SendOnly);

        // Set the remote description first.
        peer_connection_->setRemoteDescription(offer);


        // Add our track using the reciprocated description.
        audio_track_ = peer_connection_->addTrack(audio_description);
        
        if (!audio_track_) {
            throw std::runtime_error("Failed to add audio track to peer connection");
        }
        
        LOG_CPP_INFO("[WebRtcSender:%s] Audio track created, initial state: %s",
                     config_.sink_id.c_str(),
                     audio_track_->isOpen() ? "open" : "closed");
        
        // Set up track state monitoring
        audio_track_->onOpen([this]() {
            LOG_CPP_INFO("[WebRtcSender:%s] Audio track opened", config_.sink_id.c_str());
        });
        
        audio_track_->onClosed([this]() {
            LOG_CPP_INFO("[WebRtcSender:%s] Audio track closed", config_.sink_id.c_str());
        });
        
        // Extract negotiated values from the audio description instead of using hardcoded values
        uint8_t negotiated_payload_type = 111; // Default fallback
        uint32_t negotiated_clock_rate = 48000; // Default fallback
        
        // Get the negotiated payload types from the audio description
        auto payload_types = audio_description.payloadTypes();
        if (!payload_types.empty()) {
            // Use the first payload type (should be Opus)
            negotiated_payload_type = static_cast<uint8_t>(payload_types[0]);
            
            // Get the RTP map for this payload type to extract clock rate
            auto* rtp_map = audio_description.rtpMap(payload_types[0]);
            if (rtp_map) {
                negotiated_clock_rate = static_cast<uint32_t>(rtp_map->clockRate);
                LOG_CPP_INFO("[WebRtcSender:%s] Using negotiated payload type: %d, clock rate: %u, format: %s",
                           config_.sink_id.c_str(), negotiated_payload_type, negotiated_clock_rate, rtp_map->format.c_str());
            }
        }
        
        // 1. Generate a new, unique SSRC for our sending stream.
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dis(1, UINT32_MAX);
        uint32_t new_server_ssrc = dis(gen);
        LOG_CPP_INFO("[WebRtcSender:%s] Generated unique SSRC for sending stream: %u", config_.sink_id.c_str(), new_server_ssrc);

        // 2. Get the media description from the track we just created.
        auto media_description_for_answer = audio_track_->description();

        // 3. Add our new SSRC to this description so it will be included in the SDP answer.
        const std::string cname = "screamrouter-audio";
        media_description_for_answer.addSSRC(new_server_ssrc, cname, "screamrouter-stream", config_.sink_id);

        // 4. Set the modified description back onto the track. This is the critical step.
        audio_track_->setDescription(media_description_for_answer);

        // 5. Use this same SSRC for the RtpPacketizationConfig.
        uint32_t negotiated_ssrc = new_server_ssrc;
        
        LOG_CPP_INFO("[WebRtcSender:%s] Using SSRC: %u, PayloadType: %d, ClockRate: %u",
                   config_.sink_id.c_str(), negotiated_ssrc, negotiated_payload_type, negotiated_clock_rate);
        
        // Add Opus RTP packetizer with negotiated values
        auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
            negotiated_ssrc,
            "screamrouter-audio",
            negotiated_payload_type,
            negotiated_clock_rate
        );
        auto opusPacketizer = std::make_shared<rtc::OpusRtpPacketizer>(rtpConfig);
        audio_track_->setMediaHandler(opusPacketizer);
        
        LOG_CPP_INFO("[WebRtcSender:%s] Audio track setup complete, state after handler: %s",
                     config_.sink_id.c_str(),
                     audio_track_->isOpen() ? "open" : "closed");

        // With auto-negotiation disabled, we manually generate the answer *after* adding the track.
        peer_connection_->setLocalDescription();
    } catch (const std::exception& e) {
        LOG_CPP_ERROR("[WebRtcSender:%s] Exception during peer connection setup: %s", config_.sink_id.c_str(), e.what());
        throw;
    }
}

void WebRtcSender::close() {
    if (peer_connection_) {
        LOG_CPP_INFO("[WebRtcSender:%s] Closing peer connection.", config_.sink_id.c_str());
        audio_track_.reset();
        peer_connection_->close();
    }
    peer_connection_.reset();
}

void WebRtcSender::send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& /*csrcs*/) {
    //LOG_CPP_INFO("[WebRtcSender:%s] send_payload called with %zu bytes", config_.sink_id.c_str(), payload_size);
    
    // Early return if this sender is closed or marked for cleanup
    if (is_closed()) {
        return;
    }
    
    if (state_ != rtc::PeerConnection::State::Connected) {
        std::string state_str;
        switch (state_) {
            case rtc::PeerConnection::State::New: state_str = "New"; break;
            case rtc::PeerConnection::State::Connecting: state_str = "Connecting"; break;
            case rtc::PeerConnection::State::Connected: state_str = "Connected"; break;
            case rtc::PeerConnection::State::Disconnected: state_str = "Disconnected"; break;
            case rtc::PeerConnection::State::Failed: state_str = "Failed"; break;
            case rtc::PeerConnection::State::Closed: state_str = "Closed"; break;
            default: state_str = "Unknown"; break;
        }
        LOG_CPP_DEBUG("[WebRtcSender:%s] Not connected, state: %s", config_.sink_id.c_str(), state_str.c_str());
        return;
    }
    if (!audio_track_) {
        LOG_CPP_ERROR("[WebRtcSender:%s] Audio track is null", config_.sink_id.c_str());
        return;
    }
    if (!audio_track_->isOpen()) {
        LOG_CPP_ERROR("[WebRtcSender:%s] Audio track is not open", config_.sink_id.c_str());
        return;
    }
    if (!opus_encoder_) {
        LOG_CPP_ERROR("[WebRtcSender:%s] Opus encoder is null", config_.sink_id.c_str());
        return;
    }

    const int32_t* input = reinterpret_cast<const int32_t*>(payload_data);
    size_t num_samples_interleaved = payload_size / sizeof(int32_t);
    
    std::vector<int16_t> pcm16_buffer(num_samples_interleaved);
    for (size_t i = 0; i < num_samples_interleaved; ++i) {
        pcm16_buffer[i] = static_cast<int16_t>(input[i] >> 16);
    }

    pcm_buffer_.insert(pcm_buffer_.end(), pcm16_buffer.begin(), pcm16_buffer.end());

    const size_t OPUS_FRAME_SAMPLES_PER_CHANNEL = 120; // STOP CHANGING THIS! IF I WANTED 960 I WOULD HAVE IT SET TO 960!
    const size_t required_samples_for_frame = OPUS_FRAME_SAMPLES_PER_CHANNEL * 2; // 20ms at 48kHz * 2 channels

    while (pcm_buffer_.size() >= required_samples_for_frame) {
        int encoded_bytes = opus_encode(
            opus_encoder_,
            pcm_buffer_.data(),
            OPUS_FRAME_SAMPLES_PER_CHANNEL,
            opus_buffer_.data(),
            opus_buffer_.size()
        );

        if (encoded_bytes < 0) {
            LOG_CPP_ERROR("[WebRtcSender:%s] Failed to encode Opus packet: %s", config_.sink_id.c_str(), opus_strerror(encoded_bytes));
            pcm_buffer_.clear();
            return;
        }

        if (audio_track_->isOpen()) {
            rtc::FrameInfo frame_info(current_timestamp_);
            const auto* opus_data_byte_ptr = reinterpret_cast<const std::byte*>(opus_buffer_.data());
            audio_track_->sendFrame(rtc::binary(opus_data_byte_ptr, opus_data_byte_ptr + encoded_bytes), frame_info);
            m_total_packets_sent++;
            current_timestamp_ += OPUS_FRAME_SAMPLES_PER_CHANNEL;
        }
        
        pcm_buffer_.erase(pcm_buffer_.begin(), pcm_buffer_.begin() + required_samples_for_frame);
    }
}

void WebRtcSender::set_remote_description(const std::string& sdp, const std::string& type) {
    // This function is not used in the WHEP server flow, but is kept for completeness.
    if (peer_connection_) {
        try {
            LOG_CPP_INFO("[WebRtcSender:%s] Setting remote description from Python.", config_.sink_id.c_str());
            peer_connection_->setRemoteDescription(rtc::Description(sdp, type));
        } catch (const std::exception& e) {
            LOG_CPP_ERROR("[WebRtcSender:%s] Exception setting remote description: %s", config_.sink_id.c_str(), e.what());
        }
    }
}

void WebRtcSender::add_remote_ice_candidate(const std::string& candidate, const std::string& sdpMid) {
    if (peer_connection_) {
        try {
            LOG_CPP_INFO("[WebRtcSender:%s] Adding remote ICE candidate from Python.", config_.sink_id.c_str());
            peer_connection_->addRemoteCandidate(rtc::Candidate(candidate, sdpMid));
        } catch (const std::exception& e) {
            LOG_CPP_ERROR("[WebRtcSender:%s] Exception adding remote ICE candidate: %s", config_.sink_id.c_str(), e.what());
        }
    }
}
bool WebRtcSender::is_closed() const {
    rtc::PeerConnection::State current_state = state_.load();
    
    // Only consider truly terminal states as closed
    bool peer_connection_closed = current_state == rtc::PeerConnection::State::Disconnected ||
                                 current_state == rtc::PeerConnection::State::Failed ||
                                 current_state == rtc::PeerConnection::State::Closed;
    
    // Don't check audio track state - it can cause false positives
    // The peer connection state is the authoritative source
    
    return peer_connection_closed || cleanup_requested_.load();
}

bool WebRtcSender::should_cleanup_due_to_timeout() const {
    if (is_closed() && disconnect_time_ != std::chrono::steady_clock::time_point{}) {
        auto elapsed = std::chrono::steady_clock::now() - disconnect_time_;
        return elapsed > CLEANUP_TIMEOUT;
    }
    return false;
}

void WebRtcSender::set_cleanup_callback(const std::string& listener_id,
                                       std::function<void(const std::string&)> callback) {
    listener_id_ = listener_id;
    cleanup_callback_ = callback;
}

void WebRtcSender::trigger_cleanup_if_needed() {
    // Only trigger cleanup once
    if (is_closed() && cleanup_callback_ && !listener_id_.empty() && !cleanup_requested_.load()) {
        LOG_CPP_INFO("[WebRtcSender:%s] Triggering cleanup for listener: %s", config_.sink_id.c_str(), listener_id_.c_str());
        cleanup_requested_.store(true);
        cleanup_callback_(listener_id_);
    }
}

WebRtcSenderStats WebRtcSender::get_stats() {
    WebRtcSenderStats stats;
    stats.total_packets_sent = m_total_packets_sent.load();
    stats.pcm_buffer_size = pcm_buffer_.size();

    std::string state_str;
    switch (state_.load()) {
        case rtc::PeerConnection::State::New: state_str = "New"; break;
        case rtc::PeerConnection::State::Connecting: state_str = "Connecting"; break;
        case rtc::PeerConnection::State::Connected: state_str = "Connected"; break;
        case rtc::PeerConnection::State::Disconnected: state_str = "Disconnected"; break;
        case rtc::PeerConnection::State::Failed: state_str = "Failed"; break;
        case rtc::PeerConnection::State::Closed: state_str = "Closed"; break;
        default: state_str = "Unknown"; break;
    }
    stats.connection_state = state_str;

    return stats;
}

} // namespace audio
} // namespace screamrouter
