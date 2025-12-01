/**
 * @file multi_device_rtp_sender.cpp
 * @brief Implementation of the MultiDeviceRtpSender class.
 */
#include "multi_device_rtp_sender.h"
#include "rtp_constants.h"
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
      total_bytes_sent_(0),
      rtcp_controller_(nullptr) {
    
    LOG_CPP_INFO("[MultiDeviceRtpSender:%s] Initializing with %zu receivers",
                 config_.sink_id.c_str(), config_.rtp_receivers.size());
    
    // Initialize RTP timestamp with random value for security
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis32;
    rtp_timestamp_ = dis32(gen);
    
    LOG_CPP_INFO("[MultiDeviceRtpSender:%s] Creating RTCP controller (time_sync_delay=%dms, forced on)",
                 config_.sink_id.c_str(), config_.time_sync_delay_ms);
    rtcp_controller_ = std::make_unique<RtcpController>(config_.time_sync_delay_ms);
    if (!config_.time_sync_enabled) {
        LOG_CPP_WARNING("[MultiDeviceRtpSender:%s] time_sync_enabled=false but RTCP is always enabled for multi-device RTP.",
                        config_.sink_id.c_str());
    }
    
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

        receiver.sender->set_payload_type(127);
        
        // Pre-allocate buffers (max expected frame count * 2 channels * bytes per sample)
        const size_t max_frame_count = 4096;
        const size_t bytes_per_sample = config_.output_bitdepth / 8;
        const size_t max_stereo_bytes = max_frame_count * 2 * bytes_per_sample;
        
        // Allocate stereo buffer (host byte order)
        receiver.stereo_buffer.resize(max_stereo_bytes);
        
        // Allocate network buffer (network byte order)
        receiver.network_buffer.resize(max_stereo_bytes);
        
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
    
    // Register all streams with RTCP controller if enabled
    if (rtcp_controller_) {
        LOG_CPP_INFO("[MultiDeviceRtpSender:%s] Registering %zu streams with RTCP controller",
                     config_.sink_id.c_str(), active_receivers_.size());
        
        for (const auto& receiver : active_receivers_) {
            RtcpController::StreamInfo info;
            info.stream_id = receiver.config.receiver_id;
            info.dest_ip = receiver.config.ip_address;
            info.rtcp_port = receiver.config.port + 1;  // RTCP uses RTP port + 1
            info.ssrc = receiver.sender->get_ssrc();
            info.sender = receiver.sender.get();  // Non-owning pointer
            
            rtcp_controller_->add_stream(info);
            
            LOG_CPP_DEBUG("[MultiDeviceRtpSender:%s] Registered stream %s (SSRC=0x%08X) for RTCP at %s:%d",
                         config_.sink_id.c_str(),
                         info.stream_id.c_str(),
                         info.ssrc,
                         info.dest_ip.c_str(),
                         info.rtcp_port);
        }
    }
    
    if (active_receivers_.empty()) {
        LOG_CPP_ERROR("[MultiDeviceRtpSender:%s] No active receivers configured",
                     config_.sink_id.c_str());
        return false;
    }
    
    LOG_CPP_INFO("[MultiDeviceRtpSender:%s] Successfully setup %zu active receivers",
                config_.sink_id.c_str(), active_receivers_.size());
    
    // Start RTCP controller thread
    if (rtcp_controller_) {
        if (rtcp_controller_->start()) {
            LOG_CPP_INFO("[MultiDeviceRtpSender:%s] Started RTCP controller for %zu streams",
                         config_.sink_id.c_str(), active_receivers_.size());
        } else {
            LOG_CPP_ERROR("[MultiDeviceRtpSender:%s] Failed to start RTCP controller",
                          config_.sink_id.c_str());
        }
    }
    
    return true;
}

void MultiDeviceRtpSender::close() {
    LOG_CPP_INFO("[MultiDeviceRtpSender:%s] Closing all receivers", config_.sink_id.c_str());
    
    // Stop RTCP controller first (before closing receivers)
    if (rtcp_controller_) {
        LOG_CPP_INFO("[MultiDeviceRtpSender:%s] Stopping RTCP controller",
                     config_.sink_id.c_str());
        rtcp_controller_->stop();
        LOG_CPP_DEBUG("[MultiDeviceRtpSender:%s] RTCP controller stopped",
                      config_.sink_id.c_str());
    }
    
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
    
    std::lock_guard<std::mutex> lock(receivers_mutex_);
    
    // Calculate stereo buffer size (needed for both phases)
    const size_t stereo_bytes = frame_count * 2 * bytes_per_sample;
    
    // Phase 1: Process all streams (CPU work) - use pre-allocated buffers
    for (auto& receiver : active_receivers_) {
        if (!receiver.sender || !receiver.sender->is_ready()) {
            continue;
        }
        
        // Extract stereo channels directly into pre-allocated buffer
        extract_stereo_channels(payload_data,
                              receiver.stereo_buffer.data(),
                              frame_count,
                              receiver.config.channel_map[0],
                              receiver.config.channel_map[1],
                              config_.output_bitdepth);
        
        // Copy to network buffer (we'll convert in-place)
        memcpy(receiver.network_buffer.data(), receiver.stereo_buffer.data(), stereo_bytes);
        
        // Convert to network byte order in-place
        convert_to_network_byte_order(receiver.network_buffer.data(), stereo_bytes, config_.output_bitdepth);
    }
    
    const size_t stereo_frame_bytes = bytes_per_sample * 2;
    if (stereo_frame_bytes == 0) {
        LOG_CPP_ERROR("[MultiDeviceRtpSender:%s] Invalid stereo frame size (bit_depth=%d)",
                      config_.sink_id.c_str(), config_.output_bitdepth);
        return;
    }

    const std::size_t mtu_bytes = kDefaultRtpPayloadMtu;
    size_t slice_cap = mtu_bytes;
    if (slice_cap > 0) {
        const size_t frames_per_slice = std::max<std::size_t>(1, slice_cap / stereo_frame_bytes);
        slice_cap = frames_per_slice * stereo_frame_bytes;
    } else {
        slice_cap = stereo_bytes;
    }

    // Capture timestamp AFTER all processing is complete
    uint32_t current_timestamp = rtp_timestamp_.load();
    
    // Phase 2: Send all packets (I/O work) - now very fast, minimal per-stream delay
    size_t offset = 0;
    while (offset < stereo_bytes) {
        size_t remaining = stereo_bytes - offset;
        size_t slice_size = std::min(remaining, slice_cap);
        const size_t remainder = slice_size % stereo_frame_bytes;
        if (remainder != 0) {
            slice_size -= remainder;
        }
        if (slice_size == 0) {
            slice_size = std::min(remaining, stereo_frame_bytes);
            if (slice_size == 0) {
                break;
            }
        }

        const bool marker = (offset + slice_size) >= stereo_bytes;

        for (auto& receiver : active_receivers_) {
            if (!receiver.sender || !receiver.sender->is_ready()) {
                continue;
            }
            
            if (receiver.sender->send_rtp_packet(receiver.network_buffer.data() + offset,
                                                 slice_size,
                                                 current_timestamp,
                                                 csrcs,
                                                 marker)) {
                total_packets_sent_++;
                total_bytes_sent_ += slice_size;
                
                if (marker) {
                    LOG_CPP_DEBUG("[MultiDeviceRtpSender:%s] Sent final RTP slice (%zu bytes) to receiver %s",
                                  config_.sink_id.c_str(),
                                  slice_size,
                                  receiver.config.receiver_id.c_str());
                }
            } else {
                LOG_CPP_ERROR("[MultiDeviceRtpSender:%s] Failed to send slice (%zu bytes, offset=%zu) to receiver %s",
                              config_.sink_id.c_str(),
                              slice_size,
                              offset,
                              receiver.config.receiver_id.c_str());
            }
        }

        current_timestamp += static_cast<uint32_t>(slice_size / stereo_frame_bytes);
        offset += slice_size;
    }

    // Increment shared timestamp by number of frames sent
    rtp_timestamp_.store(current_timestamp);
    
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
