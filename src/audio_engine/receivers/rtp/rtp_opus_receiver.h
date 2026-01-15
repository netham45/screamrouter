/**
 * @file rtp_opus_receiver.h
 */
#ifndef RTP_OPUS_RECEIVER_H
#define RTP_OPUS_RECEIVER_H

#include "rtp_receiver_base.h"

#include <mutex>
#include <unordered_map>
#include <vector>

struct OpusDecoder;
struct OpusMSDecoder;

namespace screamrouter {
namespace audio {

class RtpOpusReceiver : public RtpPayloadReceiver {
public:
    RtpOpusReceiver();
    ~RtpOpusReceiver() noexcept override;

    bool supports_payload_type(uint8_t payload_type) const override;
    bool populate_packet(
        const RtpPacketData& packet,
        const StreamProperties& properties,
        TaggedAudioPacket& out_packet) override;
    void on_ssrc_state_cleared(uint32_t ssrc) override;
    void on_all_ssrcs_cleared() override;

private:
    struct DecoderState {
        OpusDecoder* handle = nullptr;
        OpusMSDecoder* ms_handle = nullptr;
        int sample_rate = 0;
        int channels = 0;
        int streams = 0;
        int coupled_streams = 0;
        std::vector<unsigned char> mapping;
        uint32_t channel_mask = 0;
    };

    void destroy_decoder(uint32_t ssrc);
    void destroy_all_decoders();

    static int maximum_frame_samples(int sample_rate);

    std::unordered_map<uint32_t, DecoderState> decoder_states_;
    mutable std::mutex decoder_mutex_;
};

} // namespace audio
} // namespace screamrouter

#endif // RTP_OPUS_RECEIVER_H
