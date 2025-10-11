/**
 * @file multi_device_rtp_sender.h
 * @brief Defines the MultiDeviceRtpSender class for sending audio to multiple RTP receivers.
 * @details This file contains the definition of the MultiDeviceRtpSender class, which
 *          implements the INetworkSender interface for distributing audio to multiple
 *          RTP receivers with channel separation performed after mixing.
 */
#pragma once

#include "../i_network_sender.h"
#include "../../output_mixer/sink_audio_mixer.h"
#include "../../configuration/audio_engine_config_types.h"
#include "rtp_sender_core.h"
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>

namespace screamrouter {
namespace audio {

/**
 * @class MultiDeviceRtpSender
 * @brief An implementation of INetworkSender for multi-device RTP output.
 * @details This class manages multiple RTP streams, each sending a stereo pair
 *          extracted from an 8-channel mixed audio stream. It ensures perfect
 *          synchronization across all receivers by using a shared RTP timestamp.
 */
class MultiDeviceRtpSender : public INetworkSender {
public:
    /**
     * @brief Constructs a MultiDeviceRtpSender.
     * @param config The configuration for the sink this sender is associated with.
     */
    explicit MultiDeviceRtpSender(const SinkMixerConfig& config);
    
    /**
     * @brief Destructor.
     */
    ~MultiDeviceRtpSender() noexcept override;

    /**
     * @brief Sets up all RTP senders for the configured receivers.
     * @return true if all senders were set up successfully, false otherwise.
     */
    bool setup() override;
    
    /**
     * @brief Closes all RTP senders.
     */
    void close() override;
    
    /**
     * @brief Sends audio payload to all configured receivers with channel separation.
     * @param payload_data Pointer to the 8-channel interleaved audio data.
     * @param payload_size The size of the audio data in bytes.
     * @param csrcs A vector of CSRC identifiers to include in the RTP headers.
     */
    void send_payload(const uint8_t* payload_data, size_t payload_size, 
                     const std::vector<uint32_t>& csrcs) override;

private:
    /**
     * @struct ActiveReceiver
     * @brief Represents an active RTP receiver with its associated sender.
     */
    struct ActiveReceiver {
        config::RtpReceiverConfig config;
        std::unique_ptr<RtpSenderCore> sender;
        std::vector<int16_t> stereo_buffer; // Pre-allocated buffer for stereo extraction
    };
    
    SinkMixerConfig config_;
    std::vector<ActiveReceiver> active_receivers_;
    std::mutex receivers_mutex_;
    
    // Shared RTP timestamp for synchronization
    std::atomic<uint32_t> rtp_timestamp_;
    
    // Statistics
    std::atomic<uint32_t> total_packets_sent_;
    std::atomic<uint32_t> total_bytes_sent_;
    
    /**
     * @brief Extracts a stereo pair from 8-channel interleaved audio.
     * @param input_data Pointer to 8-channel interleaved audio data.
     * @param output_data Pointer to output buffer for stereo data.
     * @param frame_count Number of audio frames to process.
     * @param left_channel Index of the left channel (0-7).
     * @param right_channel Index of the right channel (0-7).
     * @param bit_depth Bit depth of the audio samples.
     */
    void extract_stereo_channels(const uint8_t* input_data, 
                                 uint8_t* output_data,
                                 size_t frame_count,
                                 uint8_t left_channel, 
                                 uint8_t right_channel,
                                 int bit_depth);
    
    /**
     * @brief Converts audio samples to network byte order.
     * @param data Pointer to audio data to convert in-place.
     * @param size Size of the data in bytes.
     * @param bit_depth Bit depth of the audio samples.
     */
    void convert_to_network_byte_order(uint8_t* data, size_t size, int bit_depth);
};

} // namespace audio
} // namespace screamrouter