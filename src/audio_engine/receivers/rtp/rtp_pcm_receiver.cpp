#include "rtp_pcm_receiver.h"

#include "../../audio_channel_layout.h"
#include "rtp_receiver_utils.h"

namespace screamrouter {
namespace audio {

bool RtpPcmReceiver::supports_payload_type(uint8_t payload_type) const {
    return payload_type == kRtpPayloadTypeL16Stereo;
}

bool RtpPcmReceiver::populate_packet(
    const RtpPacketData& packet,
    const StreamProperties& properties,
    TaggedAudioPacket& out_packet) {
    if (packet.payload.empty()) {
        return false;
    }

    if (properties.codec != StreamCodec::PCM && properties.codec != StreamCodec::UNKNOWN) {
        return false;
    }

    out_packet.audio_data = packet.payload;
    const bool system_is_le = is_system_little_endian();
    if ((properties.endianness == Endianness::BIG && system_is_le) ||
        (properties.endianness == Endianness::LITTLE && !system_is_le)) {
        swap_endianness(out_packet.audio_data.data(), out_packet.audio_data.size(), properties.bit_depth);
    }

    out_packet.sample_rate = properties.sample_rate;
    out_packet.channels = properties.channels;
    out_packet.bit_depth = properties.bit_depth;
    out_packet.chlayout1 = (properties.channels == 2) ? 0x03 : 0x00;
    out_packet.chlayout2 = 0x00;

    return true;
}

} // namespace audio
} // namespace screamrouter
