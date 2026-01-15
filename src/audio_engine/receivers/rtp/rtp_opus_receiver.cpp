#include "rtp_opus_receiver.h"

#include "../../audio_channel_layout.h"
#include "../../utils/cpp_logger.h"
#include "rtp_receiver_utils.h"

#include <opus/opus.h>
#include <opus/opus_multistream.h>

#include <cstdint>
#include <cstring>

namespace screamrouter {
namespace audio {

RtpOpusReceiver::RtpOpusReceiver() = default;

RtpOpusReceiver::~RtpOpusReceiver() noexcept {
    destroy_all_decoders();
}

bool RtpOpusReceiver::supports_payload_type(uint8_t payload_type) const {
    return payload_type == kRtpPayloadTypeOpus;
}

bool RtpOpusReceiver::populate_packet(
    const RtpPacketData& packet,
    const StreamProperties& properties,
    TaggedAudioPacket& out_packet) {
    if (packet.payload.empty()) {
        return false;
    }

    if (properties.codec != StreamCodec::OPUS && properties.codec != StreamCodec::UNKNOWN) {
        return false;
    }

    const int sample_rate = properties.sample_rate > 0 ? properties.sample_rate : kDefaultOpusSampleRate;
    const int channels = properties.channels > 0 ? properties.channels : kDefaultOpusChannels;
    int streams = properties.opus_streams;
    int coupled_streams = properties.opus_coupled_streams;
    std::vector<unsigned char> mapping;
    mapping.reserve(properties.opus_channel_mapping.size());
    for (uint8_t value : properties.opus_channel_mapping) {
        mapping.push_back(static_cast<unsigned char>(value));
    }

    const bool mapping_valid = !mapping.empty() && mapping.size() == static_cast<size_t>(channels);
    bool require_multistream = channels > 2;

    auto layout_matches = [&](int stream_count, int coupled_count) {
        if (stream_count <= 0 || coupled_count < 0 || coupled_count > stream_count) {
            return false;
        }
        const int decoded_channels = (coupled_count * 2) + (stream_count - coupled_count);
        return decoded_channels == channels;
    };

    if (require_multistream) {
        bool tuple_valid = mapping_valid && layout_matches(streams, coupled_streams);

        if (!tuple_valid) {
            int derived_streams = streams;
            int derived_coupled = coupled_streams;
            std::vector<unsigned char> derived_mapping;
            if (!resolve_opus_multistream_layout(channels,
                                                 sample_rate,
                                                 properties.opus_mapping_family,
                                                 derived_streams,
                                                 derived_coupled,
                                                 derived_mapping)) {
                LOG_CPP_ERROR("[RtpOpusReceiver] Unable to resolve Opus multistream layout for %d channels", channels);
                return false;
            }

            streams = derived_streams;
            coupled_streams = derived_coupled;
            mapping = std::move(derived_mapping);
            tuple_valid = !mapping.empty() && mapping.size() == static_cast<size_t>(channels) &&
                          layout_matches(streams, coupled_streams);
        }

        if (!tuple_valid) {
            LOG_CPP_ERROR("[RtpOpusReceiver] Invalid Opus stream configuration for %d channels (streams=%d, coupled=%d)",
                          channels, streams, coupled_streams);
            return false;
        }
    } else {
        streams = 0;
        coupled_streams = 0;
        mapping.clear();
    }

    OpusDecoder* decoder_handle = nullptr;
    OpusMSDecoder* ms_decoder_handle = nullptr;
    uint32_t negotiated_mask = 0;
    {
        std::lock_guard<std::mutex> lock(decoder_mutex_);
        DecoderState& state = decoder_states_[packet.ssrc];
        const bool needs_reconfigure =
            state.sample_rate != sample_rate ||
            state.channels != channels ||
            (require_multistream && (!state.ms_handle || state.streams != streams || state.coupled_streams != coupled_streams || state.mapping != mapping)) ||
            (!require_multistream && !state.handle) ||
            (require_multistream && state.handle != nullptr);

        if (needs_reconfigure) {
            if (state.handle) {
                opus_decoder_destroy(state.handle);
                state.handle = nullptr;
            }
            if (state.ms_handle) {
                opus_multistream_decoder_destroy(state.ms_handle);
                state.ms_handle = nullptr;
            }

            int error = 0;
            if (require_multistream) {
                state.ms_handle = opus_multistream_decoder_create(
                    sample_rate,
                    channels,
                    streams,
                    coupled_streams,
                    mapping.data(),
                    &error);
                if (error != OPUS_OK || !state.ms_handle) {
                    LOG_CPP_ERROR("[RtpOpusReceiver] Failed to create Opus multistream decoder: %s", opus_strerror(error));
                    state.ms_handle = nullptr;
                }
            } else {
                state.handle = opus_decoder_create(sample_rate, channels, &error);
                if (error != OPUS_OK || !state.handle) {
                    LOG_CPP_ERROR("[RtpOpusReceiver] Failed to create Opus decoder: %s", opus_strerror(error));
                    state.handle = nullptr;
                }
            }

            if ((require_multistream && !state.ms_handle) || (!require_multistream && !state.handle)) {
                state.sample_rate = 0;
                state.channels = 0;
                state.streams = 0;
                state.coupled_streams = 0;
                state.mapping.clear();
                state.channel_mask = 0;
                return false;
            }

            state.sample_rate = sample_rate;
            state.channels = channels;
            state.streams = require_multistream ? streams : 0;
            state.coupled_streams = require_multistream ? coupled_streams : 0;
            if (require_multistream) {
                state.mapping = mapping;
            } else {
                state.mapping.clear();
            }
            state.channel_mask = default_channel_mask_for_channels(channels);
            LOG_CPP_DEBUG("[RtpOpusReceiver] Configured decoder for SSRC %u (rate=%d, channels=%d, streams=%d, coupled=%d, mask=0x%04X)",
                          packet.ssrc,
                          state.sample_rate,
                          state.channels,
                          state.streams,
                          state.coupled_streams,
                          state.channel_mask);
        }

        decoder_handle = state.handle;
        ms_decoder_handle = state.ms_handle;
        negotiated_mask = state.channel_mask;
    }

    if (!decoder_handle && !ms_decoder_handle) {
        LOG_CPP_ERROR("[RtpOpusReceiver] No Opus decoder available for SSRC %u", packet.ssrc);
        return false;
    }

    const int max_samples_per_channel = maximum_frame_samples(sample_rate);
    std::vector<opus_int16> decode_buffer(static_cast<size_t>(max_samples_per_channel) * channels);

    int decoded_samples = 0;
    if (ms_decoder_handle) {
        decoded_samples = opus_multistream_decode(
            ms_decoder_handle,
            packet.payload.data(),
            static_cast<opus_int32>(packet.payload.size()),
            decode_buffer.data(),
            max_samples_per_channel,
            0);
    } else {
        decoded_samples = opus_decode(
            decoder_handle,
            packet.payload.data(),
            static_cast<opus_int32>(packet.payload.size()),
            decode_buffer.data(),
            max_samples_per_channel,
            0);
    }

    if (decoded_samples < 0) {
        LOG_CPP_ERROR("[RtpOpusReceiver] Opus decoding failed for SSRC %u: %s", packet.ssrc, opus_strerror(decoded_samples));
        return false;
    }

    const size_t pcm_bytes = static_cast<size_t>(decoded_samples) * static_cast<size_t>(channels) * sizeof(opus_int16);
    out_packet.audio_data.resize(pcm_bytes);
    std::memcpy(out_packet.audio_data.data(), decode_buffer.data(), pcm_bytes);

    out_packet.sample_rate = sample_rate;
    out_packet.channels = channels;
    out_packet.bit_depth = 16;
    const uint32_t channel_mask = negotiated_mask != 0
                                      ? negotiated_mask
                                      : default_channel_mask_for_channels(channels);
    out_packet.chlayout1 = static_cast<uint8_t>(channel_mask & 0xFFu);
    out_packet.chlayout2 = static_cast<uint8_t>((channel_mask >> 8) & 0xFFu);

    return true;
}

void RtpOpusReceiver::on_ssrc_state_cleared(uint32_t ssrc) {
    destroy_decoder(ssrc);
}

void RtpOpusReceiver::on_all_ssrcs_cleared() {
    destroy_all_decoders();
}

void RtpOpusReceiver::destroy_decoder(uint32_t ssrc) {
    std::lock_guard<std::mutex> lock(decoder_mutex_);
    auto it = decoder_states_.find(ssrc);
    if (it != decoder_states_.end()) {
        if (it->second.handle) {
            opus_decoder_destroy(it->second.handle);
        }
        if (it->second.ms_handle) {
            opus_multistream_decoder_destroy(it->second.ms_handle);
        }
        decoder_states_.erase(it);
    }
}

void RtpOpusReceiver::destroy_all_decoders() {
    std::lock_guard<std::mutex> lock(decoder_mutex_);
    for (auto& [ssrc, state] : decoder_states_) {
        (void)ssrc;
        if (state.handle) {
            opus_decoder_destroy(state.handle);
        }
        if (state.ms_handle) {
            opus_multistream_decoder_destroy(state.ms_handle);
        }
    }
    decoder_states_.clear();
}

int RtpOpusReceiver::maximum_frame_samples(int sample_rate) {
    if (sample_rate <= 0) {
        return 0;
    }
    const int64_t samples = (static_cast<int64_t>(sample_rate) * 120 + 999) / 1000;
    return static_cast<int>(samples);
}

} // namespace audio
} // namespace screamrouter
