#include "rtp_pcmu_receiver.h"

#include "../../audio_channel_layout.h"
#include "rtp_receiver_utils.h"

#include <cstddef>
#include <cstdint>

namespace screamrouter {
namespace audio {

bool RtpPcmuReceiver::supports_payload_type(uint8_t payload_type) const {
    return payload_type == kRtpPayloadTypePcmu;
}

bool RtpPcmuReceiver::populate_packet(
    const RtpPacketData& packet,
    const StreamProperties& properties,
    TaggedAudioPacket& out_packet) {
    if (packet.payload.empty()) {
        return false;
    }

    if (properties.codec != StreamCodec::PCMU && properties.codec != StreamCodec::UNKNOWN) {
        return false;
    }

    const int sample_rate = properties.sample_rate > 0 ? properties.sample_rate : kDefaultPcmuSampleRate;
    int channels = properties.channels > 0 ? properties.channels : kDefaultPcmuChannels;
    if (channels <= 0) {
        channels = kDefaultPcmuChannels;
    }

    const size_t sample_count = packet.payload.size();
    out_packet.audio_data.resize(sample_count * sizeof(int16_t));
    auto* decoded = reinterpret_cast<int16_t*>(out_packet.audio_data.data());
    for (size_t i = 0; i < sample_count; ++i) {
        decoded[i] = decode_mulaw_sample(packet.payload[i]);
    }

    out_packet.sample_rate = sample_rate;
    out_packet.channels = channels;
    out_packet.bit_depth = 16;
    const uint32_t channel_mask = default_channel_mask_for_channels(channels);
    out_packet.chlayout1 = static_cast<uint8_t>(channel_mask & 0xFFu);
    out_packet.chlayout2 = static_cast<uint8_t>((channel_mask >> 8) & 0xFFu);

    return true;
}

} // namespace audio
} // namespace screamrouter
