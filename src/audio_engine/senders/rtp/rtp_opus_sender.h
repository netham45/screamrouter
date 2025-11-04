/**
 * @file rtp_opus_sender.h
 * @brief Defines the RtpOpusSender class for sending Opus-encoded RTP audio.
 */
#pragma once

#include "rtp_sender.h"
#include "../../deps/opus/include/opus.h"
#include "../../deps/opus/include/opus_multistream.h"
#include <vector>

namespace screamrouter {
namespace audio {

/**
 * @class RtpOpusSender
 * @brief RTP sender that encodes PCM audio to Opus before transmission.
 */
class RtpOpusSender : public RtpSender {
public:
    explicit RtpOpusSender(const SinkMixerConfig& config);
    ~RtpOpusSender() noexcept override;

protected:
    uint8_t rtp_payload_type() const override;
    uint32_t rtp_clock_rate() const override;
    uint32_t rtp_channel_count() const override;
    std::string sdp_payload_name() const override;
    std::vector<std::string> sdp_format_specific_attributes() const override;
    bool initialize_payload_pipeline() override;
    void teardown_payload_pipeline() override;
    bool handle_send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) override;

private:
    static constexpr uint8_t kOpusPayloadType = 111;
    static constexpr int kDefaultFrameSamplesPerChannel = 960; // 20ms @ 48kHz
    static constexpr int kOpusMinChannels = 1;
    static constexpr int kOpusDefaultChannels = 2;
    static constexpr int kOpusMaxChannels = 8;
    static constexpr int kOpusSampleRate = 48000;

    OpusEncoder* opus_encoder_;
    OpusMSEncoder* opus_ms_encoder_;
    int opus_frame_size_;
    int target_bitrate_;
    bool use_fec_;
    int opus_channels_;
    bool use_multistream_;
    int opus_streams_;
    int opus_coupled_streams_;
    std::vector<unsigned char> opus_mapping_;
    std::vector<int16_t> pcm_buffer_;
    std::vector<uint8_t> opus_buffer_;

    bool configure_multistream_layout();
};

} // namespace audio
} // namespace screamrouter
