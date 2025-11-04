#include "rtp_opus_sender.h"
#include "../../utils/cpp_logger.h"
#include <algorithm>
#include <cstring>
#include <sstream>

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
      opus_ms_encoder_(nullptr),
      opus_frame_size_(kDefaultFrameSamplesPerChannel),
      target_bitrate_(192000),
      use_fec_(true),
      opus_channels_(std::clamp(
          config.output_channels > 0 ? config.output_channels : kOpusDefaultChannels,
          kOpusMinChannels,
          kOpusMaxChannels)),
      use_multistream_(false),
      opus_streams_(0),
      opus_coupled_streams_(0) {
    if (config.output_channels > 0 && opus_channels_ != config.output_channels) {
        LOG_CPP_WARNING("[RtpOpusSender:%s] Adjusted Opus channel count from %d to %d to stay within supported range.",
                        config.sink_id.c_str(), config.output_channels, opus_channels_);
    }

    if (!configure_multistream_layout()) {
        LOG_CPP_WARNING("[RtpOpusSender:%s] Unsupported Opus channel layout for %d channels. Falling back to stereo.",
                        config.sink_id.c_str(), opus_channels_);
        opus_channels_ = kOpusDefaultChannels;
        configure_multistream_layout();
    }

    use_multistream_ = opus_channels_ > kOpusDefaultChannels;

    pcm_buffer_.reserve(static_cast<size_t>(opus_frame_size_) * static_cast<size_t>(opus_channels_) * 4);
    opus_buffer_.resize(8192);
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
    return static_cast<uint32_t>(opus_channels_);
}

std::string RtpOpusSender::sdp_payload_name() const {
    return "opus";
}

std::vector<std::string> RtpOpusSender::sdp_format_specific_attributes() const {
    std::vector<std::string> attributes;
    std::ostringstream fmtp;
    fmtp << "a=fmtp:" << static_cast<int>(rtp_payload_type())
         << " minptime=10; useinbandfec=" << (use_fec_ ? 1 : 0);
    if (opus_channels_ > 0) {
        fmtp << "; channels=" << opus_channels_;
    }
    if (use_multistream_) {
        fmtp << "; streams=" << opus_streams_
             << "; coupledstreams=" << opus_coupled_streams_
             << "; channelmapping=";
        for (size_t i = 0; i < opus_mapping_.size(); ++i) {
            if (i > 0) {
                fmtp << ",";
            }
            fmtp << static_cast<int>(opus_mapping_[i]);
        }
    }
    attributes.emplace_back(fmtp.str());
    attributes.emplace_back("a=ptime:20");
    return attributes;
}

bool RtpOpusSender::initialize_payload_pipeline() {
    teardown_payload_pipeline();

    opus_encoder_ = nullptr;
    opus_ms_encoder_ = nullptr;

    int error = 0;
    if (use_multistream_) {
        opus_ms_encoder_ = opus_multistream_encoder_create(
            kOpusSampleRate,
            opus_channels_,
            opus_streams_,
            opus_coupled_streams_,
            opus_mapping_.data(),
            OPUS_APPLICATION_AUDIO,
            &error);
        if (error != OPUS_OK || !opus_ms_encoder_) {
            LOG_CPP_ERROR("[RtpOpusSender:%s] Failed to create Opus multistream encoder: %s",
                          config().sink_id.c_str(), opus_strerror(error));
            opus_ms_encoder_ = nullptr;
            return false;
        }

        if (opus_multistream_encoder_ctl(opus_ms_encoder_, OPUS_SET_BITRATE(target_bitrate_)) != OPUS_OK) {
            LOG_CPP_WARNING("[RtpOpusSender:%s] Failed to set Opus bitrate (%d bps)",
                            config().sink_id.c_str(), target_bitrate_);
        }

        if (opus_multistream_encoder_ctl(opus_ms_encoder_, OPUS_SET_COMPLEXITY(3)) != OPUS_OK) {
            LOG_CPP_WARNING("[RtpOpusSender:%s] Failed to set Opus complexity",
                            config().sink_id.c_str());
        }

        if (opus_multistream_encoder_ctl(opus_ms_encoder_, OPUS_SET_INBAND_FEC(use_fec_ ? 1 : 0)) != OPUS_OK) {
            LOG_CPP_WARNING("[RtpOpusSender:%s] Failed to enable Opus in-band FEC",
                            config().sink_id.c_str());
        }

        if (opus_multistream_encoder_ctl(opus_ms_encoder_, OPUS_SET_PACKET_LOSS_PERC(10)) != OPUS_OK) {
            LOG_CPP_WARNING("[RtpOpusSender:%s] Failed to set Opus packet loss percentage",
                            config().sink_id.c_str());
        }
    } else {
        opus_encoder_ = opus_encoder_create(kOpusSampleRate, opus_channels_, OPUS_APPLICATION_AUDIO, &error);
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
    }

    if (config().output_samplerate != kOpusSampleRate) {
        LOG_CPP_WARNING("[RtpOpusSender:%s] Opus output requires 48kHz sample rate, but sink configured for %d Hz. Audio will be treated as 48kHz.",
                        config().sink_id.c_str(), config().output_samplerate);
    }
    if (config().output_channels > 0 && config().output_channels != opus_channels_) {
        LOG_CPP_WARNING("[RtpOpusSender:%s] Opus channel count overridden to %d (requested %d).",
                        config().sink_id.c_str(), opus_channels_, config().output_channels);
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
    if (opus_ms_encoder_) {
        opus_multistream_encoder_destroy(opus_ms_encoder_);
        opus_ms_encoder_ = nullptr;
    }
    pcm_buffer_.clear();
}

bool RtpOpusSender::handle_send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) {
    if (use_multistream_) {
        if (!opus_ms_encoder_) {
            LOG_CPP_ERROR("[RtpOpusSender:%s] Opus multistream encoder is not initialized", config().sink_id.c_str());
            return false;
        }
    } else if (!opus_encoder_) {
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

    const size_t frame_samples = static_cast<size_t>(opus_frame_size_) * static_cast<size_t>(opus_channels_);
    bool sent_any = false;

    while (pcm_buffer_.size() >= frame_samples) {
        int encoded_bytes = 0;
        if (use_multistream_) {
            encoded_bytes = opus_multistream_encode(
                opus_ms_encoder_,
                pcm_buffer_.data(),
                opus_frame_size_,
                opus_buffer_.data(),
                static_cast<opus_int32>(opus_buffer_.size()));
        } else {
            encoded_bytes = opus_encode(
                opus_encoder_,
                pcm_buffer_.data(),
                opus_frame_size_,
                opus_buffer_.data(),
                static_cast<opus_int32>(opus_buffer_.size())
            );
        }

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

bool RtpOpusSender::configure_multistream_layout() {
    opus_mapping_.clear();

    switch (opus_channels_) {
        case 1:
            opus_streams_ = 1;
            opus_coupled_streams_ = 0;
            opus_mapping_ = {0};
            return true;
        case 2:
            opus_streams_ = 1;
            opus_coupled_streams_ = 1;
            opus_mapping_ = {0, 1};
            return true;
        case 3:
            opus_streams_ = 2;
            opus_coupled_streams_ = 1;
            opus_mapping_ = {0, 2, 1};
            return true;
        case 4:
            opus_streams_ = 2;
            opus_coupled_streams_ = 2;
            opus_mapping_ = {0, 1, 2, 3};
            return true;
        case 5:
            opus_streams_ = 3;
            opus_coupled_streams_ = 2;
            opus_mapping_ = {0, 2, 1, 3, 4};
            return true;
        case 6:
            opus_streams_ = 4;
            opus_coupled_streams_ = 2;
            opus_mapping_ = {0, 2, 1, 5, 3, 4};
            return true;
        case 7:
            opus_streams_ = 4;
            opus_coupled_streams_ = 3;
            opus_mapping_ = {0, 2, 1, 6, 3, 4, 5};
            return true;
        case 8:
            opus_streams_ = 5;
            opus_coupled_streams_ = 3;
            opus_mapping_ = {0, 2, 1, 6, 3, 4, 5, 7};
            return true;
        default:
            return false;
    }
}

} // namespace audio
} // namespace screamrouter
