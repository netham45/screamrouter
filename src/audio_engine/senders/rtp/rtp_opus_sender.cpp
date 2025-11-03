#include "rtp_opus_sender.h"
#include "../../utils/cpp_logger.h"
#include <algorithm>
#include <cstring>

namespace screamrouter {
namespace audio {

namespace {
inline int16_t load_le_i16(const uint8_t* ptr) {
    return static_cast<int16_t>(static_cast<uint16_t>(ptr[0]) | (static_cast<uint16_t>(ptr[1]) << 8));
}
} // namespace

RtpOpusSender::RtpOpusSender(const SinkMixerConfig& config)
    : RtpSender(config),
      opus_encoder_(nullptr),
      opus_frame_size_(kDefaultFrameSamplesPerChannel),
      target_bitrate_(192000),
      use_fec_(true) {
    pcm_buffer_.reserve(kDefaultFrameSamplesPerChannel * kOpusChannels * 4);
    opus_buffer_.resize(4096);
}

RtpOpusSender::~RtpOpusSender() noexcept {
    teardown_payload_pipeline();
}

uint8_t RtpOpusSender::rtp_payload_type() const {
    return kOpusPayloadType;
}

uint32_t RtpOpusSender::rtp_clock_rate() const {
    return kOpusSampleRate;
}

uint32_t RtpOpusSender::rtp_channel_count() const {
    return kOpusChannels;
}

std::string RtpOpusSender::sdp_payload_name() const {
    return "opus";
}

std::vector<std::string> RtpOpusSender::sdp_format_specific_attributes() const {
    std::vector<std::string> attributes;
    attributes.emplace_back("a=fmtp:" + std::to_string(rtp_payload_type()) + " minptime=10; useinbandfec=1");
    attributes.emplace_back("a=ptime:20");
    return attributes;
}

bool RtpOpusSender::initialize_payload_pipeline() {
    teardown_payload_pipeline();

    int error = 0;
    opus_encoder_ = opus_encoder_create(kOpusSampleRate, kOpusChannels, OPUS_APPLICATION_AUDIO, &error);
    if (error != OPUS_OK || !opus_encoder_) {
        LOG_CPP_ERROR("[RtpOpusSender:%s] Failed to create Opus encoder: %s",
                      config().sink_id.c_str(), opus_strerror(error));
        opus_encoder_ = nullptr;
        return false;
    }

    if (opus_encoder_ctl(opus_encoder_, OPUS_SET_BITRATE(target_bitrate_)) != OPUS_OK) {
        LOG_CPP_WARNING("[RtpOpusSender:%s] Failed to set Opus bitrate (%d bps)",
                        config().sink_id.c_str(), target_bitrate_);
    }

    if (opus_encoder_ctl(opus_encoder_, OPUS_SET_COMPLEXITY(3)) != OPUS_OK) {
        LOG_CPP_WARNING("[RtpOpusSender:%s] Failed to set Opus complexity",
                        config().sink_id.c_str());
    }

    if (opus_encoder_ctl(opus_encoder_, OPUS_SET_INBAND_FEC(use_fec_ ? 1 : 0)) != OPUS_OK) {
        LOG_CPP_WARNING("[RtpOpusSender:%s] Failed to enable Opus in-band FEC",
                        config().sink_id.c_str());
    }

    if (opus_encoder_ctl(opus_encoder_, OPUS_SET_PACKET_LOSS_PERC(10)) != OPUS_OK) {
        LOG_CPP_WARNING("[RtpOpusSender:%s] Failed to set Opus packet loss percentage",
                        config().sink_id.c_str());
    }

    if (config().output_samplerate != kOpusSampleRate) {
        LOG_CPP_WARNING("[RtpOpusSender:%s] Opus output requires 48kHz sample rate, but sink configured for %d Hz. Audio will be treated as 48kHz.",
                        config().sink_id.c_str(), config().output_samplerate);
    }
    if (config().output_channels != kOpusChannels) {
        LOG_CPP_WARNING("[RtpOpusSender:%s] Opus output expects %d channels, but sink configured for %d. Audio will be mixed as stereo.",
                        config().sink_id.c_str(), kOpusChannels, config().output_channels);
    }
    if (config().output_bitdepth != 16) {
        LOG_CPP_WARNING("[RtpOpusSender:%s] Opus output currently expects 16-bit PCM input, but sink configured for %d bits.",
                        config().sink_id.c_str(), config().output_bitdepth);
    }

    pcm_buffer_.clear();
    std::fill(opus_buffer_.begin(), opus_buffer_.end(), 0);

    LOG_CPP_INFO("[RtpOpusSender:%s] Opus encoder initialized (bitrate=%d, fec=%s, frame=%d samples)",
                 config().sink_id.c_str(), target_bitrate_, use_fec_ ? "on" : "off", opus_frame_size_);
    return true;
}

void RtpOpusSender::teardown_payload_pipeline() {
    if (opus_encoder_) {
        opus_encoder_destroy(opus_encoder_);
        opus_encoder_ = nullptr;
    }
    pcm_buffer_.clear();
}

bool RtpOpusSender::handle_send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) {
    if (!opus_encoder_) {
        LOG_CPP_ERROR("[RtpOpusSender:%s] Opus encoder is not initialized", config().sink_id.c_str());
        return false;
    }

    if (payload_size % sizeof(int16_t) != 0) {
        LOG_CPP_ERROR("[RtpOpusSender:%s] Payload size %zu is not aligned to 16-bit samples",
                      config().sink_id.c_str(), payload_size);
        return false;
    }

    const size_t total_samples = payload_size / sizeof(int16_t);
    pcm_buffer_.reserve(pcm_buffer_.size() + total_samples);

    for (size_t i = 0; i < payload_size; i += sizeof(int16_t)) {
        pcm_buffer_.push_back(load_le_i16(payload_data + i));
    }

    const size_t frame_samples = static_cast<size_t>(opus_frame_size_) * kOpusChannels;
    bool sent_any = false;

    while (pcm_buffer_.size() >= frame_samples) {
        int encoded_bytes = opus_encode(
            opus_encoder_,
            pcm_buffer_.data(),
            opus_frame_size_,
            opus_buffer_.data(),
            static_cast<opus_int32>(opus_buffer_.size())
        );

        if (encoded_bytes < 0) {
            LOG_CPP_ERROR("[RtpOpusSender:%s] Opus encoding failed: %s",
                          config().sink_id.c_str(), opus_strerror(encoded_bytes));
            pcm_buffer_.erase(pcm_buffer_.begin(), pcm_buffer_.begin() + frame_samples);
            continue;
        }

        if (encoded_bytes == 0) {
            LOG_CPP_WARNING("[RtpOpusSender:%s] Opus encoder returned empty frame", config().sink_id.c_str());
            pcm_buffer_.erase(pcm_buffer_.begin(), pcm_buffer_.begin() + frame_samples);
            continue;
        }

        const bool packet_sent = send_rtp_payload(opus_buffer_.data(), static_cast<size_t>(encoded_bytes), csrcs, false);
        // Always advance the RTP timestamp once the frame is encoded so we never reuse timestamps after a send failure.
        advance_rtp_timestamp(static_cast<uint32_t>(opus_frame_size_));
        if (packet_sent) {
            sent_any = true;
        }

        pcm_buffer_.erase(pcm_buffer_.begin(), pcm_buffer_.begin() + frame_samples);
    }

    return sent_any;
}

} // namespace audio
} // namespace screamrouter
