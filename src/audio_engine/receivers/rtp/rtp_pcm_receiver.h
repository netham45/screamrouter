/**
 * @file rtp_pcm_receiver.h
 */
#ifndef RTP_PCM_RECEIVER_H
#define RTP_PCM_RECEIVER_H

#include "rtp_receiver_base.h"

namespace screamrouter {
namespace audio {

class RtpPcmReceiver : public RtpPayloadReceiver {
public:
    bool supports_payload_type(uint8_t payload_type) const override;
    bool populate_packet(
        const RtpPacketData& packet,
        const StreamProperties& properties,
        TaggedAudioPacket& out_packet) override;
};

} // namespace audio
} // namespace screamrouter

#endif // RTP_PCM_RECEIVER_H
