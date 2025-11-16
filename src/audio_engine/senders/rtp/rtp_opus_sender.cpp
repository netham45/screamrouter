#include "rtp_opus_sender.h"
#include "../../audio_channel_layout.h"
#include "../../utils/cpp_logger.h"
#include <algorithm>
#include <cstring>
#include <numeric>
#include <utility>

namespace screamrouter {
namespace audio {

namespace {
inline int16_t load_le_i16(const uint8_t* ptr) {
    return static_cast<int16_t>(static_cast<uint16_t>(ptr[0]) | (static_cast<uint16_t>(ptr[1]) << 8));
}

constexpr size_t kOpusMaxPacketBytesPerStream = 1275;
constexpr size_t kOpusPacketSafetyOverhead = 32;

inline size_t compute_max_opus_packet_bytes(int stream_count) {
    if (stream_count <= 0) {
        stream_count = 1;
    }
    return static_cast<size_t>(stream_count) * kOpusMaxPacketBytesPerStream + kOpusPacketSafetyOverhead;
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
      opus_mapping_family_(0),
      channel_remap_(static_cast<size_t>(opus_channels_)),
      needs_channel_reorder_(false) {
    pcm_buffer_.reserve(kDefaultFrameSamplesPerChannel * opus_channels_ * 4);
    opus_buffer_.resize(compute_max_opus_packet_bytes(1));
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
    return use_multistream_ ? "multiopus" : "opus";
}

std::vector<std::string> RtpOpusSender::sdp_format_specific_attributes() const {
    std::vector<std::string> attributes;
    std::string fmtp = "a=fmtp:" + std::to_string(rtp_payload_type()) + " minptime=10; useinbandfec=1";

    const int effective_channels = opus_channels_ > 0 ? opus_channels_ : 2;

    if (!use_multistream_) {
        if (effective_channels == 1) {
            fmtp += "; stereo=0";
        } else if (effective_channels == 2) {
            fmtp += "; stereo=1";
        } else {
            fmtp += "; channels=" + std::to_string(effective_channels);
        }
    } else {
        fmtp += "; channels=" + std::to_string(effective_channels);
        fmtp += "; streams=" + std::to_string(opus_streams_);
        fmtp += "; coupledstreams=" + std::to_string(opus_coupled_streams_);
        fmtp += "; mappingfamily=" + std::to_string(opus_mapping_family_);
        if (!opus_channel_mapping_.empty()) {
            fmtp += "; channelmapping=";
            for (size_t i = 0; i < opus_channel_mapping_.size(); ++i) {
                if (i != 0) {
                    fmtp += ",";
                }
                fmtp += std::to_string(static_cast<int>(opus_channel_mapping_[i]));
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

    pcm_buffer_.clear();
    pcm_buffer_.reserve(kDefaultFrameSamplesPerChannel * opus_channels_ * 4);
    reorder_frame_buffer_.clear();

    opus_streams_ = 0;
    opus_coupled_streams_ = 0;
    opus_channel_mapping_.clear();
    use_multistream_ = opus_channels_ > 2;
    opus_mapping_family_ = use_multistream_ ? 1 : 0;

    initialize_channel_reorder();

    if (!use_multistream_) {
        opus_streams_ = 1;
        opus_coupled_streams_ = (opus_channels_ >= 2) ? 1 : 0;
        opus_channel_mapping_.resize(static_cast<size_t>(opus_channels_));
        for (int ch = 0; ch < opus_channels_; ++ch) {
            opus_channel_mapping_[static_cast<size_t>(ch)] = static_cast<unsigned char>(ch);
        }
    } else {
        if (!derive_multistream_layout(opus_channels_,
                                       kOpusSampleRate,
                                       opus_mapping_family_,
                                       opus_streams_,
                                       opus_coupled_streams_,
                                       opus_channel_mapping_)) {
            LOG_CPP_ERROR("[RtpOpusSender:%s] Unable to derive Opus multistream layout for %d channels",
                          config().sink_id.c_str(), opus_channels_);
            return false;
        }
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
    const int stream_count = use_multistream_ ? opus_streams_ : 1;
    opus_buffer_.resize(compute_max_opus_packet_bytes(stream_count));
    std::fill(opus_buffer_.begin(), opus_buffer_.end(), 0);
    pcm_buffer_.reserve(kDefaultFrameSamplesPerChannel * opus_channels_ * 4);

    LOG_CPP_INFO("[RtpOpusSender:%s] Opus encoder initialized (channels=%d, streams=%d, coupled=%d, bitrate=%d, fec=%s, frame=%d samples)",
                 config().sink_id.c_str(), opus_channels_, opus_streams_, opus_coupled_streams_,
                 target_bitrate_, use_fec_ ? "on" : "off", opus_frame_size_);
    if (use_multistream_) {
        std::string mapping_str;
        for (size_t i = 0; i < opus_channel_mapping_.size(); ++i) {
            if (i != 0) {
                mapping_str += ",";
            }
            mapping_str += std::to_string(static_cast<int>(opus_channel_mapping_[i]));
        }
        LOG_CPP_DEBUG("[RtpOpusSender:%s] Opus layout: family=%d mapping=[%s]",
                      config().sink_id.c_str(),
                      opus_mapping_family_,
                      mapping_str.c_str());
    }
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
    channel_remap_.clear();
    needs_channel_reorder_ = false;
    reorder_frame_buffer_.clear();
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
        const int16_t* encode_input = pcm_buffer_.data();
        if (needs_channel_reorder_) {
            reorder_frame_buffer_.resize(frame_samples);
            for (int sample = 0; sample < opus_frame_size_; ++sample) {
                const int sample_offset = sample * opus_channels_;
                for (int ch = 0; ch < opus_channels_; ++ch) {
                    const int source_index = channel_remap_[static_cast<size_t>(ch)];
                    reorder_frame_buffer_[sample_offset + ch] = pcm_buffer_[sample_offset + source_index];
                }
            }
            encode_input = reorder_frame_buffer_.data();
        }

        int encoded_bytes = 0;
        while (true) {
            if (use_multistream_) {
                encoded_bytes = opus_multistream_encode(
                    opus_ms_encoder_,
                    encode_input,
                    opus_frame_size_,
                    opus_buffer_.data(),
                    static_cast<opus_int32>(opus_buffer_.size()));
            } else {
                encoded_bytes = opus_encode(
                    opus_encoder_,
                    encode_input,
                    opus_frame_size_,
                    opus_buffer_.data(),
                    static_cast<opus_int32>(opus_buffer_.size()));
            }

            if (encoded_bytes == OPUS_BUFFER_TOO_SMALL) {
                const size_t current_streams = static_cast<size_t>(use_multistream_ ? opus_streams_ : 1);
                const size_t min_required = compute_max_opus_packet_bytes(static_cast<int>(current_streams));
                const size_t new_capacity = (std::max)(opus_buffer_.size() * 2, min_required);
                LOG_CPP_WARNING("[RtpOpusSender:%s] Opus packet buffer too small (capacity=%zu). Resizing to %zu bytes.",
                                config().sink_id.c_str(),
                                opus_buffer_.size(),
                                new_capacity);
                opus_buffer_.resize(new_capacity);
                continue;
            }
            break;
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
                                              int mapping_family,
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
    OpusMSEncoder* probe = opus_multistream_surround_encoder_create(
        sample_rate,
        channels,
        mapping_family,
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
    return true;
}

void RtpOpusSender::initialize_channel_reorder() {
    const int channels = opus_channels_;
    if (channels <= 0) {
        channel_remap_.clear();
        needs_channel_reorder_ = false;
        return;
    }

    uint32_t mask = (static_cast<uint32_t>(config().output_chlayout2) << 8) | config().output_chlayout1;
    std::vector<ChannelRole> configured_roles = channel_order_from_mask(mask);
    std::vector<ChannelRole> canonical_roles = family1_canonical_channel_order(channels);

    channel_remap_.resize(static_cast<size_t>(channels));

    if (configured_roles.size() != static_cast<size_t>(channels) ||
        canonical_roles.size() != static_cast<size_t>(channels)) {
        std::iota(channel_remap_.begin(), channel_remap_.end(), 0);
        needs_channel_reorder_ = false;
        if (channels > 2) {
            LOG_CPP_WARNING("[RtpOpusSender:%s] Channel mask/canonical mismatch (mask=%zu, canonical=%zu). Using identity ordering.",
                            config().sink_id.c_str(),
                            configured_roles.size(),
                            canonical_roles.size());
        }
        return;
    }

    bool remap_valid = true;
    for (int i = 0; i < channels; ++i) {
        ChannelRole desired = canonical_roles[static_cast<size_t>(i)];
        auto it = std::find(configured_roles.begin(), configured_roles.end(), desired);
        if (it == configured_roles.end()) {
            remap_valid = false;
            break;
        }
        channel_remap_[static_cast<size_t>(i)] = static_cast<int>(std::distance(configured_roles.begin(), it));
    }

    if (!remap_valid) {
        std::iota(channel_remap_.begin(), channel_remap_.end(), 0);
        needs_channel_reorder_ = false;
        LOG_CPP_WARNING("[RtpOpusSender:%s] Unable to map configured channel mask to Opus canonical order. Using input ordering.",
                        config().sink_id.c_str());
        return;
    }

    needs_channel_reorder_ = false;
    for (int i = 0; i < channels; ++i) {
        if (channel_remap_[static_cast<size_t>(i)] != i) {
            needs_channel_reorder_ = true;
            break;
        }
    }
}

} // namespace audio
} // namespace screamrouter
