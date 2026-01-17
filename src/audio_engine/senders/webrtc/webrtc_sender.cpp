#include "webrtc_sender.h"
#include "../../utils/cpp_logger.h"
#ifndef SCREAMROUTER_TESTING
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/gil.h>
#endif
#include <rtc/rtc.hpp>
#include <rtc/frameinfo.hpp>
#include <chrono>
#include <mutex>
#include <random>
#include <string>
#include <algorithm>
#include <sstream>
#include <cstdlib>
#include <cctype>

namespace screamrouter {
namespace audio {

namespace {

bool resolve_opus_multistream_layout(int channels, int& streams, int& coupled_streams, std::vector<unsigned char>& mapping) {
    mapping.clear();

    switch (channels) {
        case 1:
            streams = 1;
            coupled_streams = 0;
            mapping = {0};
            return true;
        case 2:
            streams = 1;
            coupled_streams = 1;
            mapping = {0, 1};
            return true;
        case 3:
            streams = 2;
            coupled_streams = 1;
            mapping = {0, 2, 1};
            return true;
        case 4:
            streams = 2;
            coupled_streams = 2;
            mapping = {0, 1, 2, 3};
            return true;
        case 5:
            streams = 3;
            coupled_streams = 2;
            mapping = {0, 2, 1, 3, 4};
            return true;
        case 6:
            streams = 4;
            coupled_streams = 2;
            mapping = {0, 2, 1, 5, 3, 4};
            return true;
        case 7:
            streams = 4;
            coupled_streams = 3;
            mapping = {0, 2, 1, 6, 3, 4, 5};
            return true;
        case 8:
            streams = 5;
            coupled_streams = 3;
            mapping = {0, 2, 1, 6, 3, 4, 5, 7};
            return true;
        default:
            return false;
    }
}

} // namespace

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
    opus_channels_ = std::clamp(config_.output_channels > 0 ? config_.output_channels : 2, 1, 8);
    allow_multichannel_output_ = false;
    if (const char* env = std::getenv("SCREAMROUTER_ENABLE_WEBRTC_MULTICHANNEL")) {
        std::string env_value(env);
        std::transform(env_value.begin(), env_value.end(), env_value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (env_value == "1" || env_value == "true" || env_value == "yes" || env_value == "enable") {
            allow_multichannel_output_ = true;
        }
    }
    if (!allow_multichannel_output_ && opus_channels_ > 2) {
        LOG_CPP_INFO("[WebRtcSender:%s] Browser multichannel support limited; forcing stereo answer (requested=%d)",
                     config_.sink_id.c_str(), opus_channels_);
        opus_channels_ = 2;
    }
    LOG_CPP_INFO("[WebRtcSender:%s] Constructing sender (opus_channels=%d samplerate=%d)",
                 config_.sink_id.c_str(), opus_channels_, config_.output_samplerate);
    LOG_CPP_INFO("[WebRtcSender] DEADLOCK_DEBUG: Constructor START for sink: %s", config_.sink_id.c_str());
    initialize_opus_encoder();
    LOG_CPP_INFO("[WebRtcSender] DEADLOCK_DEBUG: Constructor END for sink: %s", config_.sink_id.c_str());
}

WebRtcSender::~WebRtcSender() noexcept {
    close();
    if (opus_encoder_) {
        opus_encoder_destroy(opus_encoder_);
    }
    if (opus_ms_encoder_) {
        opus_multistream_encoder_destroy(opus_ms_encoder_);
    }
}

void WebRtcSender::initialize_opus_encoder() {
    if (opus_encoder_) {
        opus_encoder_destroy(opus_encoder_);
        opus_encoder_ = nullptr;
    }
    if (opus_ms_encoder_) {
        opus_multistream_encoder_destroy(opus_ms_encoder_);
        opus_ms_encoder_ = nullptr;
    }

    opus_fmtp_profile_.clear();

    int sample_rate = config_.output_samplerate > 0 ? config_.output_samplerate : 48000;
    if (sample_rate != 48000) {
        LOG_CPP_WARNING("[WebRtcSender:%s] Opus encoder expects 48kHz, overriding samplerate from %d to 48000.",
                        config_.sink_id.c_str(), sample_rate);
        sample_rate = 48000;
    }

    if (!configure_multistream_layout()) {
        LOG_CPP_WARNING("[WebRtcSender:%s] Unsupported Opus layout for %d channels, reverting to stereo.",
                        config_.sink_id.c_str(), opus_channels_);
        opus_channels_ = 2;
        if (!configure_multistream_layout()) {
            opus_channels_ = 2;
            use_multistream_ = false;
        }
    }

    int error = OPUS_OK;
    if (use_multistream_) {
        opus_ms_encoder_ = opus_multistream_encoder_create(
            sample_rate,
            opus_channels_,
            opus_streams_,
            opus_coupled_streams_,
            opus_mapping_.data(),
            OPUS_APPLICATION_AUDIO,
            &error);
        if (error != OPUS_OK || !opus_ms_encoder_) {
            LOG_CPP_ERROR("[WebRtcSender:%s] Failed to create Opus multistream encoder: %s",
                          config_.sink_id.c_str(), opus_strerror(error));
            opus_ms_encoder_ = nullptr;
            use_multistream_ = false;
        }
    }

    if (!use_multistream_) {
        opus_mapping_.clear();
        opus_streams_ = 0;
        opus_coupled_streams_ = opus_channels_ >= 2 ? 1 : 0;
        opus_encoder_ = opus_encoder_create(sample_rate, opus_channels_, OPUS_APPLICATION_AUDIO, &error);
        if (error != OPUS_OK || !opus_encoder_) {
            LOG_CPP_ERROR("[WebRtcSender:%s] Failed to create Opus encoder: %s", config_.sink_id.c_str(), opus_strerror(error));
            opus_encoder_ = nullptr;
            return;
        }
    }

    if (use_multistream_) {
        opus_multistream_encoder_ctl(opus_ms_encoder_, OPUS_SET_BITRATE(512000));
        opus_multistream_encoder_ctl(opus_ms_encoder_, OPUS_SET_VBR(0));
        opus_multistream_encoder_ctl(opus_ms_encoder_, OPUS_SET_INBAND_FEC(0));
        opus_multistream_encoder_ctl(opus_ms_encoder_, OPUS_SET_COMPLEXITY(10));
        opus_multistream_encoder_ctl(opus_ms_encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    } else {
        opus_encoder_ctl(opus_encoder_, OPUS_SET_BITRATE(512000));
        opus_encoder_ctl(opus_encoder_, OPUS_SET_VBR(0));
        opus_encoder_ctl(opus_encoder_, OPUS_SET_INBAND_FEC(0));
        opus_encoder_ctl(opus_encoder_, OPUS_SET_COMPLEXITY(10));
        opus_encoder_ctl(opus_encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    }

    opus_buffer_.resize(8192);
    opus_fmtp_profile_ = build_opus_fmtp_profile();
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
#ifndef SCREAMROUTER_TESTING
            pybind11::gil_scoped_acquire acquire;
#endif
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
#ifndef SCREAMROUTER_TESTING
            pybind11::gil_scoped_acquire acquire;
#endif
            on_ice_candidate_callback_(candidate_str, sdp_mid_str);
        }
    });

    try {
        if (offer_sdp_.empty()) {
            LOG_CPP_ERROR("[WebRtcSender:%s] Cannot setup peer connection without a remote offer.", config_.sink_id.c_str());
            return;
        }
        LOG_CPP_INFO("[WebRtcSender:%s] Processing remote offer (SDP size=%zu).", config_.sink_id.c_str(), offer_sdp_.size());
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
        LOG_CPP_INFO("[WebRtcSender:%s] Remote description applied", config_.sink_id.c_str());


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
                rtp_map->encParams = std::to_string(opus_channels_);
                rtp_map->fmtps.clear();
                rtp_map->fmtps.push_back(opus_fmtp_profile_);
                LOG_CPP_INFO("[WebRtcSender:%s] Using negotiated payload type: %d, clock rate: %u, format: %s",
                           config_.sink_id.c_str(), negotiated_payload_type, negotiated_clock_rate, rtp_map->format.c_str());
            }
        } else {
            rtc::Description::Media::RtpMap opus_map(negotiated_payload_type);
            opus_map.format = "opus";
            opus_map.clockRate = static_cast<int>(negotiated_clock_rate);
            opus_map.encParams = std::to_string(opus_channels_);
            opus_map.fmtps.push_back(opus_fmtp_profile_);
            audio_description.addRtpMap(std::move(opus_map));
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
        LOG_CPP_INFO("[WebRtcSender:%s] Local description set; awaiting ICE", config_.sink_id.c_str());
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
        LOG_CPP_DEBUG("[WebRtcSender:%s] Dropping payload because sender is closed (size=%zu)", config_.sink_id.c_str(), payload_size);
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
    if (use_multistream_) {
        if (!opus_ms_encoder_) {
            LOG_CPP_ERROR("[WebRtcSender:%s] Opus multistream encoder is null", config_.sink_id.c_str());
            return;
        }
    } else {
        if (!opus_encoder_) {
            LOG_CPP_ERROR("[WebRtcSender:%s] Opus encoder is null", config_.sink_id.c_str());
            return;
        }
    }

    LOG_CPP_DEBUG("[WebRtcSender:%s] Encoding %zu bytes of PCM for listener", config_.sink_id.c_str(), payload_size);
    const int32_t* input = reinterpret_cast<const int32_t*>(payload_data);
    size_t num_samples_interleaved = payload_size / sizeof(int32_t);

    if (num_samples_interleaved % static_cast<size_t>(opus_channels_) != 0) {
        LOG_CPP_ERROR("[WebRtcSender:%s] Payload samples (%zu) not divisible by channel count %d",
                      config_.sink_id.c_str(), num_samples_interleaved, opus_channels_);
        return;
    }

    std::vector<int16_t> pcm16_buffer(num_samples_interleaved);
    for (size_t i = 0; i < num_samples_interleaved; ++i) {
        pcm16_buffer[i] = static_cast<int16_t>(input[i] >> 16);
    }

    pcm_buffer_.insert(pcm_buffer_.end(), pcm16_buffer.begin(), pcm16_buffer.end());

    const size_t frame_samples_per_channel = OPUS_SAMPLES_PER_FRAME; // Per-channel sample count (2.5 ms @ 48kHz)
    const size_t required_samples_for_frame = frame_samples_per_channel * static_cast<size_t>(opus_channels_);

    while (pcm_buffer_.size() >= required_samples_for_frame) {
        int encoded_bytes = 0;
        if (use_multistream_) {
            encoded_bytes = opus_multistream_encode(
                opus_ms_encoder_,
                pcm_buffer_.data(),
                static_cast<int>(frame_samples_per_channel),
                opus_buffer_.data(),
                static_cast<opus_int32>(opus_buffer_.size()));
        } else {
            encoded_bytes = opus_encode(
                opus_encoder_,
                pcm_buffer_.data(),
                static_cast<int>(frame_samples_per_channel),
                opus_buffer_.data(),
                static_cast<opus_int32>(opus_buffer_.size()));
        }

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
            LOG_CPP_DEBUG("[WebRtcSender:%s] Sent Opus frame (encoded_bytes=%d timestamp=%u total_packets=%llu)",
                          config_.sink_id.c_str(), encoded_bytes, current_timestamp_,
                          static_cast<unsigned long long>(m_total_packets_sent.load()));
            current_timestamp_ += static_cast<uint32_t>(frame_samples_per_channel);
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

bool WebRtcSender::configure_multistream_layout() {
    use_multistream_ = false;
    opus_streams_ = 0;
    opus_coupled_streams_ = 0;
    opus_mapping_.clear();

    if (opus_channels_ <= 2) {
        // Mono/stereo use single-stream encoder
        return true;
    }

    int streams = 0;
    int coupled = 0;
    std::vector<unsigned char> mapping;
    if (!resolve_opus_multistream_layout(opus_channels_, streams, coupled, mapping)) {
        return false;
    }

    use_multistream_ = true;
    opus_streams_ = streams;
    opus_coupled_streams_ = coupled;
    opus_mapping_ = std::move(mapping);
    return true;
}

std::string WebRtcSender::build_opus_fmtp_profile() const {
    std::ostringstream ss;
    ss << "minptime=10";
    ss << ";maxaveragebitrate=512000";
    ss << ";useinbandfec=0";

    if (opus_channels_ >= 2) {
        ss << ";stereo=1";
        ss << ";sprop-stereo=1";
    } else {
        ss << ";stereo=0";
        ss << ";sprop-stereo=0";
    }

    ss << ";channels=" << opus_channels_;

    if (use_multistream_) {
        ss << ";streams=" << opus_streams_;
        ss << ";coupledstreams=" << opus_coupled_streams_;
        ss << ";channel_mapping=";
        for (size_t i = 0; i < opus_mapping_.size(); ++i) {
            if (i > 0) {
                ss << ",";
            }
            ss << static_cast<int>(opus_mapping_[i]);
        }
    }

    return ss.str();
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
