#include "scream_sender.h"
#include "../../utils/cpp_logger.h"
#include <cstring>
#include <stdexcept>
#include <vector>
#include <cmath>

namespace screamrouter {
namespace audio {

ScreamSender::ScreamSender(const SinkMixerConfig& config)
    : config_(config),
      udp_socket_fd_(PLATFORM_INVALID_SOCKET) {
    build_scream_header();
    // Initialize WSA for Windows if not already done
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_CPP_ERROR("[ScreamSender:%s] WSAStartup failed", config_.sink_id.c_str());
        throw std::runtime_error("WSAStartup failed.");
    }
#endif
}

ScreamSender::~ScreamSender() noexcept {
    close();
#ifdef _WIN32
    WSACleanup();
#endif
}

void ScreamSender::build_scream_header() {
    bool output_samplerate_44100_base = (config_.output_samplerate % 44100) == 0;
    uint8_t output_samplerate_mult = (config_.output_samplerate > 0) ?
        ((output_samplerate_44100_base ? 44100 : 48000) / config_.output_samplerate) : 1;

    scream_header_[0] = output_samplerate_mult + (output_samplerate_44100_base << 7);
    scream_header_[1] = static_cast<uint8_t>(config_.output_bitdepth);
    scream_header_[2] = static_cast<uint8_t>(config_.output_channels);
    scream_header_[3] = config_.output_chlayout1;
    scream_header_[4] = config_.output_chlayout2;
    LOG_CPP_INFO("[ScreamSender:%s] Built Scream header for Rate: %d, Depth: %d, Channels: %d",
                 config_.sink_id.c_str(), config_.output_samplerate, config_.output_bitdepth, config_.output_channels);
}

bool ScreamSender::setup() {
    LOG_CPP_INFO("[ScreamSender:%s] Setting up networking...", config_.sink_id.c_str());
    udp_socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket_fd_ == PLATFORM_INVALID_SOCKET) {
        LOG_CPP_ERROR("[ScreamSender:%s] Failed to create UDP socket", config_.sink_id.c_str());
        return false;
    }

#ifndef _WIN32
    // Set socket priority for low latency on Linux
    int priority = 6; // Corresponds to AC_VO (Access Category Voice)
    if (setsockopt(udp_socket_fd_, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority)) < 0) {
        LOG_CPP_WARNING("[ScreamSender:%s] Failed to set socket priority on UDP socket.", config_.sink_id.c_str());
    }
    // Allow reusing the address to avoid issues with lingering sockets
    int reuse = 1;
    if (setsockopt(udp_socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LOG_CPP_WARNING("[ScreamSender:%s] Failed to set SO_REUSEADDR on UDP socket.", config_.sink_id.c_str());
    }
    int dscp = 46; // EF PHB for low latency audio
    int tos_value = dscp << 2;
    if (setsockopt(udp_socket_fd_, IPPROTO_IP, IP_TOS, &tos_value, sizeof(tos_value)) < 0) {
        LOG_CPP_ERROR("[ScreamSender:%s] Failed to set UDP socket TOS/DSCP", config_.sink_id.c_str());
        // Non-fatal, continue anyway
    }
#else
    LOG_CPP_WARNING("[ScreamSender:%s] Skipping TOS/DSCP setting on Windows.", config_.sink_id.c_str());
#endif

    memset(&udp_dest_addr_, 0, sizeof(udp_dest_addr_));
    udp_dest_addr_.sin_family = AF_INET;
    udp_dest_addr_.sin_port = htons(config_.output_port);
    if (inet_pton(AF_INET, config_.output_ip.c_str(), &udp_dest_addr_.sin_addr) <= 0) {
        LOG_CPP_ERROR("[ScreamSender:%s] Invalid UDP destination IP address: %s", config_.sink_id.c_str(), config_.output_ip.c_str());
        platform_close_socket(udp_socket_fd_);
        udp_socket_fd_ = PLATFORM_INVALID_SOCKET;
        return false;
    }

    LOG_CPP_INFO("[ScreamSender:%s] Networking setup complete (UDP target: %s:%d)", config_.sink_id.c_str(), config_.output_ip.c_str(), config_.output_port);
    return true;
}

void ScreamSender::close() {
    if (udp_socket_fd_ != PLATFORM_INVALID_SOCKET) {
        LOG_CPP_INFO("[ScreamSender:%s] Closing UDP socket", config_.sink_id.c_str());
        platform_close_socket(udp_socket_fd_);
        udp_socket_fd_ = PLATFORM_INVALID_SOCKET;
    }
}

void ScreamSender::send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) {
    (void)csrcs; // Unused in ScreamSender
    if (payload_size == 0) {
        LOG_CPP_ERROR("[ScreamSender:%s] Attempted to send empty payload.", config_.sink_id.c_str());
        return;
    }

    // Silence check logic from SinkAudioMixer::send_network_buffer
    bool all_samples_zero = true;
    size_t bytes_per_sample = config_.output_bitdepth / 8;
    if (bytes_per_sample > 0 && (payload_size % bytes_per_sample == 0)) {
        size_t num_payload_samples = payload_size / bytes_per_sample;
        if (num_payload_samples >= 1) {
            size_t indices_to_check[5];
            indices_to_check[0] = 0;
            if (num_payload_samples > 1) {
                indices_to_check[1] = static_cast<size_t>(std::floor(1.0 * (num_payload_samples - 1) / 4.0));
                indices_to_check[2] = static_cast<size_t>(std::floor(2.0 * (num_payload_samples - 1) / 4.0));
                indices_to_check[3] = static_cast<size_t>(std::floor(3.0 * (num_payload_samples - 1) / 4.0));
                indices_to_check[4] = num_payload_samples - 1;
            } else {
                indices_to_check[1] = indices_to_check[2] = indices_to_check[3] = indices_to_check[4] = 0;
            }

            for (int i = 0; i < 5; ++i) {
                const uint8_t* current_sample_ptr = payload_data + (indices_to_check[i] * bytes_per_sample);
                bool current_sample_is_zero = true;
                for (size_t byte_k = 0; byte_k < bytes_per_sample; ++byte_k) {
                    if (current_sample_ptr[byte_k] != 0) { // Simplified check for exact zero
                        current_sample_is_zero = false;
                        break;
                    }
                }
                if (!current_sample_is_zero) {
                    all_samples_zero = false;
                    break;
                }
            }
        }
    } else {
        all_samples_zero = false; // Send if we can't perform a valid check
    }

    if (all_samples_zero) {
        LOG_CPP_DEBUG("[ScreamSender:%s] Packet identified as silent. Skipping send.", config_.sink_id.c_str());
        return;
    }

    // Create final packet with header + payload
    std::vector<uint8_t> packet_buffer(scream_header_.size() + payload_size);
    memcpy(packet_buffer.data(), scream_header_.data(), scream_header_.size());
    memcpy(packet_buffer.data() + scream_header_.size(), payload_data, payload_size);
    
    size_t length = packet_buffer.size();

    if (udp_socket_fd_ != PLATFORM_INVALID_SOCKET) {
        LOG_CPP_DEBUG("[ScreamSender:%s] Sending %zu bytes via UDP", config_.sink_id.c_str(), length);
#ifdef _WIN32
        int sent_bytes = sendto(udp_socket_fd_,
                                reinterpret_cast<const char*>(packet_buffer.data()),
                                static_cast<int>(length),
                                0,
                                (struct sockaddr *)&udp_dest_addr_,
                                sizeof(udp_dest_addr_));
#else
        int sent_bytes = sendto(udp_socket_fd_,
                                packet_buffer.data(),
                                length,
                                0,
                                (struct sockaddr *)&udp_dest_addr_,
                                sizeof(udp_dest_addr_));
#endif
        if (sent_bytes < 0) {
            LOG_CPP_ERROR("[ScreamSender:%s] UDP sendto failed", config_.sink_id.c_str());
        } else if (static_cast<size_t>(sent_bytes) != length) {
            LOG_CPP_ERROR("[ScreamSender:%s] UDP sendto sent partial data: %d/%zu", config_.sink_id.c_str(), sent_bytes, length);
        }
    }
}

} // namespace audio
} // namespace screamrouter