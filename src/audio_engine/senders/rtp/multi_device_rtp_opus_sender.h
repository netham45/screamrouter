/**
 * @file multi_device_rtp_opus_sender.h
 * @brief Defines the MultiDeviceRtpOpusSender class for sending Opus-encoded audio to multiple RTP receivers.
 */
#pragma once

#include "../i_network_sender.h"
#include "../../output_mixer/sink_audio_mixer.h"
#include "../../configuration/audio_engine_config_types.h"
#include "rtp_sender_core.h"
#include "rtcp_controller.h"
#include "../../deps/opus/include/opus.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace screamrouter {
namespace audio {

/**
 * @class MultiDeviceRtpOpusSender
 * @brief INetworkSender implementation that Opus-encodes PCM once per receiver and fans packets out.
 * @details Each receiver owns its own Opus encoder and RTP session. A shared RTP timestamp keeps
 *          packets aligned across receivers so downstream endpoints stay synchronized.
 */
class MultiDeviceRtpOpusSender : public INetworkSender {
public:
    explicit MultiDeviceRtpOpusSender(const SinkMixerConfig& config);
    ~MultiDeviceRtpOpusSender() noexcept override;

    bool setup() override;
    void close() override;
    void send_payload(const uint8_t* payload_data, size_t payload_size,
                      const std::vector<uint32_t>& csrcs) override;

private:
    struct ActiveReceiver {
        config::RtpReceiverConfig config;
        std::unique_ptr<RtpSenderCore> sender;
        OpusEncoder* encoder = nullptr;
        std::vector<uint8_t> opus_buffer;
    };

    static constexpr uint8_t kOpusPayloadType = 111;
    static constexpr int kOpusChannels = 2;
    static constexpr int kOpusSampleRate = 48000;
    static constexpr int kDefaultFrameSamplesPerChannel = 960; // 20 ms @ 48 kHz

    SinkMixerConfig config_;
    std::vector<ActiveReceiver> active_receivers_;
    std::mutex receivers_mutex_;

    std::vector<int16_t> pending_samples_;
    size_t consumed_samples_;

    std::atomic<uint32_t> rtp_timestamp_;
    std::unique_ptr<RtcpController> rtcp_controller_;
    std::atomic<uint32_t> total_packets_sent_;
    std::atomic<uint32_t> total_bytes_sent_;

    void teardown_receiver(ActiveReceiver& receiver);
    void destroy_all_receivers();
};

} // namespace audio
} // namespace screamrouter
