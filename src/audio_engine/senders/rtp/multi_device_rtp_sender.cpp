/**
 * @file multi_device_rtp_sender.cpp
 * @brief Implementation of the MultiDeviceRtpSender class.
 */
#include "multi_device_rtp_sender.h"
#include "../../utils/cpp_logger.h"
#include <cstring>
#include <random>
#include <algorithm>

namespace screamrouter {
namespace audio {

MultiDeviceRtpSender::MultiDeviceRtpSender(const SinkMixerConfig& config)
    : config_(config),
      rtp_timestamp_(0),
      total_packets_sent_(0),
      total_bytes_sent_(0) {
    
    LOG_CPP_INFO("[MultiDeviceRtpSender:%s] Initializing with %zu receivers",
                 config_.sink_id.c_str(), config_.rtp_receivers.size());
    
    // Initialize RTP timestamp with random value for security
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis32;
    rtp_timestamp_ = dis32(gen);
    
    // Pre-allocate receivers
    active_receivers_.reserve(config_.rtp_receivers.size());
}

MultiDeviceRtpSender::~MultiDeviceRtpSender() noexcept {
    close();
}

bool MultiDeviceRtpSender::setup() {
    LOG_CPP_INFO("[MultiDeviceRtpSender:%s] Setting up %zu receivers",
                 config_.sink_id.c_str(), config_.rtp_receivers.size());
    
    std::lock_guard<std::mutex> lock(receivers_mutex_);
    
    // Generate unique SSRCs for each receiver
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis32;
    
    for (const auto& receiver_config : config_.rtp_receivers) {
        
        ActiveReceiver receiver;
        receiver.config = receiver_config;
        
        // Create RTP sender core with unique SSRC
        uint32_t ssrc = dis32(gen);
        receiver.sender = std::make_unique<RtpSenderCore>(ssrc);
        
        // Setup the sender
        if (!receiver.sender->setup(receiver_config.ip_address, receiver_config.port)) {
            LOG_CPP_ERROR("[MultiDeviceRtpSender:%s] Failed to setup receiver %s at %s:%d",
                         config_.sink_id.c_str(), 
                         receiver_config.receiver_id.c_str(),
                         receiver_config.ip_address.c_str(),
                         receiver_config.port);
            // Continue with other receivers even if one fails
            continue;
        }
        
        // Pre-allocate stereo buffer (max expected frame count * 2 channels)
        receiver.stereo_buffer.reserve(4096 * 2);
        
        active_receivers_.push_back(std::move(receiver));
        
        LOG_CPP_INFO("[MultiDeviceRtpSender:%s] Setup receiver %s (SSRC=0x%08X) at %s:%d, channels=[%d,%d]",
                    config_.sink_id.c_str(),
                    receiver_config.receiver_id.c_str(),
                    ssrc,
                    receiver_config.ip_address.c_str(),
                    receiver_config.port,
                    receiver_config.channel_map[0],
                    receiver_config.channel_map[1]);
    }
    
    if (active_receivers_.empty()) {
        LOG_CPP_ERROR("[MultiDeviceRtpSender:%s] No active receivers configured",
                     config_.sink_id.c_str());
        return false;
    }
    
    LOG_CPP_INFO("[MultiDeviceRtpSender:%s] Successfully setup %zu active receivers",
                config_.sink_id.c_str(), active_receivers_.size());
    return true;
}

void MultiDeviceRtpSender::close() {
    LOG_CPP_INFO("[MultiDeviceRtpSender:%s] Closing all receivers", config_.sink_id.c_str());
    
    std::lock_guard<std::mutex> lock(receivers_mutex_);
    
    for (auto& receiver : active_receivers_) {
        if (receiver.sender) {
            receiver.sender->close();
        }
    }
    
    active_receivers_.clear();
    
    LOG_CPP_INFO("[MultiDeviceRtpSender:%s] All receivers closed. Total packets sent: %u, bytes: %u",
                config_.sink_id.c_str(), 
                total_packets_sent_.load(),
                total_bytes_sent_.load());
}

void MultiDeviceRtpSender::send_payload(const uint8_t* payload_data, size_t payload_size,
                                        const std::vector<uint32_t>& csrcs) {
    if (payload_size == 0) {
        return;
    }
    
    // Get the number of channels from config
    const int channels = config_.output_channels;
    
    const int bytes_per_sample = config_.output_bitdepth / 8;
    const size_t bytes_per_frame = bytes_per_sample * channels;
    const size_t frame_count = payload_size / bytes_per_frame;
    
    if (payload_size % bytes_per_frame != 0) {
        LOG_CPP_WARNING("[MultiDeviceRtpSender:%s] Payload size %zu not aligned to frame boundary (%zu bytes/frame)",
                       config_.sink_id.c_str(), payload_size, bytes_per_frame);
        return;
    }
    
    // Get current RTP timestamp (shared across all streams for sync)
    uint32_t current_timestamp = rtp_timestamp_.load();
    
    std::lock_guard<std::mutex> lock(receivers_mutex_);
    
    for (auto& receiver : active_receivers_) {
        if (!receiver.sender || !receiver.sender->is_ready()) {
            continue;
        }
        
        // Prepare stereo buffer
        const size_t stereo_bytes = frame_count * 2 * bytes_per_sample;
        std::vector<uint8_t> stereo_data(stereo_bytes);
        
        // Extract stereo channels
        extract_stereo_channels(payload_data, 
                              stereo_data.data(),
                              frame_count,
                              receiver.config.channel_map[0],
                              receiver.config.channel_map[1],
                              config_.output_bitdepth);
        
        // Convert to network byte order
        convert_to_network_byte_order(stereo_data.data(), stereo_bytes, config_.output_bitdepth);
        
        // Send via RTP
        if (receiver.sender->send_rtp_packet(stereo_data.data(), stereo_bytes, 
                                            current_timestamp, csrcs)) {
            total_packets_sent_++;
            total_bytes_sent_ += stereo_bytes;
            
            LOG_CPP_DEBUG("[MultiDeviceRtpSender:%s] Sent %zu bytes to receiver %s",
                         config_.sink_id.c_str(), stereo_bytes,
                         receiver.config.receiver_id.c_str());
        } else {
            LOG_CPP_ERROR("[MultiDeviceRtpSender:%s] Failed to send to receiver %s",
                         config_.sink_id.c_str(),
                         receiver.config.receiver_id.c_str());
        }
    }
    
    // Increment shared timestamp by number of frames
    rtp_timestamp_ += frame_count;
    
    // Log statistics periodically
    if (total_packets_sent_ % 100 == 0) {
        LOG_CPP_DEBUG("[MultiDeviceRtpSender:%s] Stats: %u packets, %u bytes sent to %zu receivers",
                     config_.sink_id.c_str(),
                     total_packets_sent_.load(),
                     total_bytes_sent_.load(),
                     active_receivers_.size());
    }
}

void MultiDeviceRtpSender::extract_stereo_channels(const uint8_t* input_data,
                                                   uint8_t* output_data,
                                                   size_t frame_count,
                                                   uint8_t left_channel,
                                                   uint8_t right_channel,
                                                   int bit_depth) {
    const int bytes_per_sample = bit_depth / 8;
    const int input_channels = config_.output_channels;  // Use actual channel count from config
    
    // Validate channel indices
    if (left_channel >= input_channels || right_channel >= input_channels) {
        LOG_CPP_ERROR("[MultiDeviceRtpSender:%s] Invalid channel indices: left=%d, right=%d (max=%d)",
                     config_.sink_id.c_str(), left_channel, right_channel, input_channels - 1);
        return;
    }
    
    switch (bit_depth) {
        case 16: {
            const int16_t* input = reinterpret_cast<const int16_t*>(input_data);
            int16_t* output = reinterpret_cast<int16_t*>(output_data);
            
            for (size_t frame = 0; frame < frame_count; frame++) {
                const int16_t* frame_ptr = input + (frame * input_channels);
                output[frame * 2] = frame_ptr[left_channel];
                output[frame * 2 + 1] = frame_ptr[right_channel];
            }
            break;
        }
        
        case 24: {
            for (size_t frame = 0; frame < frame_count; frame++) {
                const uint8_t* frame_ptr = input_data + (frame * input_channels * 3);
                uint8_t* out_ptr = output_data + (frame * 2 * 3);
                
                // Copy left channel (3 bytes)
                memcpy(out_ptr, frame_ptr + (left_channel * 3), 3);
                // Copy right channel (3 bytes)
                memcpy(out_ptr + 3, frame_ptr + (right_channel * 3), 3);
            }
            break;
        }
        
        case 32: {
            const int32_t* input = reinterpret_cast<const int32_t*>(input_data);
            int32_t* output = reinterpret_cast<int32_t*>(output_data);
            
            for (size_t frame = 0; frame < frame_count; frame++) {
                const int32_t* frame_ptr = input + (frame * input_channels);
                output[frame * 2] = frame_ptr[left_channel];
                output[frame * 2 + 1] = frame_ptr[right_channel];
            }
            break;
        }
        
        default:
            LOG_CPP_ERROR("[MultiDeviceRtpSender:%s] Unsupported bit depth: %d",
                         config_.sink_id.c_str(), bit_depth);
            break;
    }
}

void MultiDeviceRtpSender::convert_to_network_byte_order(uint8_t* data, size_t size, int bit_depth) {
    const int bytes_per_sample = bit_depth / 8;
    
    if (size % bytes_per_sample != 0) {
        LOG_CPP_WARNING("[MultiDeviceRtpSender:%s] Data size %zu not aligned to sample boundary",
                       config_.sink_id.c_str(), size);
        return;
    }
    
    switch (bit_depth) {
        case 16: {
            uint16_t* samples = reinterpret_cast<uint16_t*>(data);
            size_t sample_count = size / 2;
            for (size_t i = 0; i < sample_count; i++) {
                samples[i] = htons(samples[i]);
            }
            break;
        }
        
        case 24: {
            // Manual byte swap for 24-bit: [0][1][2] -> [2][1][0]
            for (size_t i = 0; i < size; i += 3) {
                std::swap(data[i], data[i + 2]);
            }
            break;
        }
        
        case 32: {
            uint32_t* samples = reinterpret_cast<uint32_t*>(data);
            size_t sample_count = size / 4;
            for (size_t i = 0; i < sample_count; i++) {
                samples[i] = htonl(samples[i]);
            }
            break;
        }
        
        case 8:
            // 8-bit samples don't need byte order conversion
            break;
            
        default:
            LOG_CPP_ERROR("[MultiDeviceRtpSender:%s] Unsupported bit depth for byte order conversion: %d",
                         config_.sink_id.c_str(), bit_depth);
            break;
    }
}

} // namespace audio
} // namespace screamrouter