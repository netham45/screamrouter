#include "rtp_opus_sender.h"
#include "../../utils/cpp_logger.h"
#include <algorithm>
#include <cstring>
#include <utility>

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
      opus_channels_(std::max(1, config.output_channels > 0 ? config.output_channels : 2)),
      opus_streams_(0),
      opus_coupled_streams_(0),
      use_multistream_(false),
      opus_mapping_family_(0) {
    pcm_buffer_.reserve(kDefaultFrameSamplesPerChannel * opus_channels_ * 4);
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
    return static_cast<uint32_t>(opus_channels_ > 0 ? opus_channels_ : 2);
}

std::string RtpOpusSender::sdp_payload_name() const {
    return "opus";
}

std::vector<std::string> RtpOpusSender::sdp_format_specific_attributes() const {
    std::vector<std::string> attributes;
    std::string fmtp = "a=fmtp:" + std::to_string(rtp_payload_type()) + " minptime=10; useinbandfec=1";

    const int effective_channels = opus_channels_ > 0 ? opus_channels_ : 2;
    int streams = opus_streams_;
    int coupled_streams = opus_coupled_streams_;
    std::vector<unsigned char> mapping = opus_channel_mapping_;
    int mapping_family = opus_mapping_family_;
    bool multistream = use_multistream_ || effective_channels > 2;

    if (multistream) {
        const bool mapping_valid = mapping.size() == static_cast<size_t>(effective_channels);
        const bool streams_valid = streams > 0;
        if (!mapping_valid || !streams_valid) {
            std::vector<unsigned char> derived_mapping;
            int derived_streams = streams;
            int derived_coupled = coupled_streams;
            int derived_family = mapping_family;
            if (derive_multistream_layout(effective_channels, kOpusSampleRate, derived_family, derived_streams, derived_coupled, derived_mapping)) {
                streams = derived_streams;
                coupled_streams = derived_coupled;
                mapping = std::move(derived_mapping);
                mapping_family = derived_family;
            } else {
                LOG_CPP_WARNING("[RtpOpusSender:%s] Unable to derive SDP multistream layout for %d channels; advertising stereo-compatible fmtp",
                                 config().sink_id.c_str(), effective_channels);
                multistream = false;
            }
        }
    }

    if (!multistream) {
        if (effective_channels == 1) {
            fmtp += "; stereo=0";
        } else if (effective_channels == 2) {
            fmtp += "; stereo=1";
        } else {
            fmtp += "; channels=" + std::to_string(effective_channels);
        }
    } else {
        fmtp += "; channels=" + std::to_string(effective_channels);
        fmtp += "; streams=" + std::to_string(streams);
        fmtp += "; coupledstreams=" + std::to_string(coupled_streams);
        if (!mapping.empty()) {
            fmtp += "; channelmapping=" + std::to_string(mapping_family);
            for (size_t i = 0; i < mapping.size(); ++i) {
                fmtp += "," + std::to_string(static_cast<int>(mapping[i]));
            }
        }
    }

    attributes.emplace_back(std::move(fmtp));
    attributes.emplace_back("a=ptime:20");
    return attributes;
}

bool RtpOpusSender::initialize_payload_pipeline() {
    teardown_payload_pipeline();

    opus_channels_ = config().output_channels > 0 ? config().output_channels : 2;
    if (opus_channels_ <= 0) {
        opus_channels_ = 1;
    }

    opus_streams_ = 0;
    opus_coupled_streams_ = 0;
    opus_channel_mapping_.clear();
    use_multistream_ = opus_channels_ > 2;
    opus_mapping_family_ = 0;

    if (!use_multistream_) {
        opus_streams_ = 1;
        opus_coupled_streams_ = (opus_channels_ >= 2) ? 1 : 0;
        opus_channel_mapping_.resize(static_cast<size_t>(opus_channels_));
        for (int ch = 0; ch < opus_channels_; ++ch) {
            opus_channel_mapping_[static_cast<size_t>(ch)] = static_cast<unsigned char>(ch);
        }
        opus_mapping_family_ = (opus_channels_ <= 2) ? 0 : 255;
    } else {
        if (!derive_multistream_layout(opus_channels_, kOpusSampleRate, opus_mapping_family_, opus_streams_, opus_coupled_streams_, opus_channel_mapping_)) {
            LOG_CPP_ERROR("[RtpOpusSender:%s] Unable to derive Opus multistream layout for %d channels",
                          config().sink_id.c_str(), opus_channels_);
            return false;
        }
        use_multistream_ = true;
    }

    int error = OPUS_OK;
    if (use_multistream_) {
        opus_ms_encoder_ = opus_multistream_encoder_create(
            kOpusSampleRate,
            opus_channels_,
            opus_streams_,
            opus_coupled_streams_,
            opus_channel_mapping_.data(),
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
    if (config().output_channels != opus_channels_) {
        LOG_CPP_WARNING("[RtpOpusSender:%s] Opus output configured for %d channels, but sink reports %d. Using %d channels.",
                        config().sink_id.c_str(), opus_channels_, config().output_channels, opus_channels_);
    }
    if (config().output_bitdepth != 16) {
        LOG_CPP_WARNING("[RtpOpusSender:%s] Opus output currently expects 16-bit PCM input, but sink configured for %d bits.",
                        config().sink_id.c_str(), config().output_bitdepth);
    }

    pcm_buffer_.clear();
    std::fill(opus_buffer_.begin(), opus_buffer_.end(), 0);
    pcm_buffer_.reserve(kDefaultFrameSamplesPerChannel * opus_channels_ * 4);

    LOG_CPP_INFO("[RtpOpusSender:%s] Opus encoder initialized (channels=%d, streams=%d, coupled=%d, bitrate=%d, fec=%s, frame=%d samples)",
                 config().sink_id.c_str(), opus_channels_, opus_streams_, opus_coupled_streams_,
                 target_bitrate_, use_fec_ ? "on" : "off", opus_frame_size_);
    return true;
}

void RtpOpusSender::teardown_payload_pipeline() {
    if (opus_ms_encoder_) {
        opus_multistream_encoder_destroy(opus_ms_encoder_);
        opus_ms_encoder_ = nullptr;
    }
    if (opus_encoder_) {
        opus_encoder_destroy(opus_encoder_);
        opus_encoder_ = nullptr;
    }
    use_multistream_ = false;
    opus_streams_ = 0;
    opus_coupled_streams_ = 0;
    opus_channel_mapping_.clear();
    opus_mapping_family_ = 0;
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
                static_cast<opus_int32>(opus_buffer_.size()));
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

bool RtpOpusSender::derive_multistream_layout(int channels, int sample_rate,
                                              int& mapping_family,
                                              int& streams, int& coupled_streams,
                                              std::vector<unsigned char>& mapping) const {
    mapping.clear();

    if (channels <= 0) {
        return false;
    }

    std::vector<unsigned char> temp(static_cast<size_t>(channels), 0);
    int derived_streams = 0;
    int derived_coupled = 0;
    int error = OPUS_OK;
    int requested_family = (channels <= 2) ? 0 : (mapping_family > 0 ? mapping_family : 1);
    OpusMSEncoder* probe = opus_multistream_surround_encoder_create(
        sample_rate,
        channels,
        requested_family,
        &derived_streams,
        &derived_coupled,
        temp.data(),
        OPUS_APPLICATION_AUDIO,
        &error);

    if (error != OPUS_OK || !probe) {
        if (probe) {
            opus_multistream_encoder_destroy(probe);
        }
        LOG_CPP_ERROR("[RtpOpusSender:%s] Failed to probe Opus layout for %d channels: %s",
                      config().sink_id.c_str(), channels, opus_strerror(error));
        return false;
    }

    opus_multistream_encoder_destroy(probe);

    streams = derived_streams;
    coupled_streams = derived_coupled;
    mapping.assign(temp.begin(), temp.end());
    mapping_family = requested_family;
    return true;
}

} // namespace audio
} // namespace screamrouter
