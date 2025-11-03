/**
 * @file rtp_sender_core.cpp
 * @brief Implementation of the RtpSenderCore class.
 */
#include "rtp_sender_core.h"
#include "../../utils/cpp_logger.h"
#include <cstring>
#include <random>

#ifndef _WIN32
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
#endif

namespace screamrouter {
namespace audio {

namespace {
    // RTP payload type for L16 48kHz stereo
    const int RTP_PAYLOAD_TYPE_L16_48K_STEREO = 127;
    
    bool is_multicast(const std::string& ip_address) {
        struct in_addr addr;
        if (inet_pton(AF_INET, ip_address.c_str(), &addr) != 1) {
            return false;
        }
        // Multicast range is 224.0.0.0 to 239.255.255.255
        return (ntohl(addr.s_addr) & 0xF0000000) == 0xE0000000;
    }
    
    std::string get_primary_source_ip() {
        socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == PLATFORM_INVALID_SOCKET) {
            LOG_CPP_ERROR("[RtpSenderCore] Failed to create socket for IP detection.");
            return "127.0.0.1";
        }

        struct sockaddr_in serv;
        memset(&serv, 0, sizeof(serv));
        serv.sin_family = AF_INET;
        serv.sin_port = htons(53);
        if (inet_pton(AF_INET, "8.8.8.8", &serv.sin_addr) <= 0) {
            platform_close_socket(sock);
            LOG_CPP_ERROR("[RtpSenderCore] inet_pton failed for IP detection.");
            return "127.0.0.1";
        }

        if (connect(sock, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
            platform_close_socket(sock);
            LOG_CPP_WARNING("[RtpSenderCore] Failed to connect for IP detection.");
            return "127.0.0.1";
        }

        struct sockaddr_in name;
        socklen_t namelen = sizeof(name);
        if (getsockname(sock, (struct sockaddr*)&name, &namelen) < 0) {
            platform_close_socket(sock);
            LOG_CPP_ERROR("[RtpSenderCore] getsockname failed for IP detection.");
            return "127.0.0.1";
        }

        platform_close_socket(sock);

        char buffer[INET_ADDRSTRLEN];
        const char* p = inet_ntop(AF_INET, &name.sin_addr, buffer, INET_ADDRSTRLEN);
        if (p != nullptr) {
            LOG_CPP_INFO("[RtpSenderCore] Detected primary source IP: %s", buffer);
            return std::string(buffer);
        } else {
            LOG_CPP_ERROR("[RtpSenderCore] inet_ntop failed for IP detection.");
            return "127.0.0.1";
        }
    }
}

RtpSenderCore::RtpSenderCore(uint32_t ssrc)
    : udp_socket_fd_(PLATFORM_INVALID_SOCKET),
      ssrc_(ssrc),
      sequence_number_(0),
      packet_count_(0),
      octet_count_(0),
      dest_port_(0) {
    
    // Initialize with random sequence number for security
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint16_t> dis16;
    sequence_number_ = dis16(gen);
    
    LOG_CPP_INFO("[RtpSenderCore] Initialized with SSRC=0x%08X, initial seq=%u",
                 ssrc_, sequence_number_.load());

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_CPP_ERROR("[RtpSenderCore] WSAStartup failed");
    }
#endif
}

RtpSenderCore::~RtpSenderCore() noexcept {
    close();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool RtpSenderCore::setup(const std::string& dest_ip, uint16_t dest_port, bool is_multicast_addr) {
    LOG_CPP_INFO("[RtpSenderCore] Setting up UDP socket for %s:%d (multicast=%s)",
                 dest_ip.c_str(), dest_port, is_multicast_addr ? "true" : "false");
    
    dest_ip_ = dest_ip;
    dest_port_ = dest_port;
    
    udp_socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket_fd_ == PLATFORM_INVALID_SOCKET) {
        LOG_CPP_ERROR("[RtpSenderCore] Failed to create UDP socket");
        return false;
    }

#ifndef _WIN32
    // Set socket priority for low latency on Linux
    int priority = 6; // AC_VO
    if (setsockopt(udp_socket_fd_, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority)) < 0) {
        LOG_CPP_WARNING("[RtpSenderCore] Failed to set socket priority");
    }
    
    // Allow reusing the address
    int reuse = 1;
    if (setsockopt(udp_socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LOG_CPP_WARNING("[RtpSenderCore] Failed to set SO_REUSEADDR");
    }
#endif

    // Configure for multicast if needed
    if (is_multicast_addr || is_multicast(dest_ip)) {
        LOG_CPP_INFO("[RtpSenderCore] Configuring socket for multicast");
        
        int ttl = 64;
#ifdef _WIN32
        if (setsockopt(udp_socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, 
                      (const char*)&ttl, sizeof(ttl)) < 0) {
#else
        if (setsockopt(udp_socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, 
                      &ttl, sizeof(ttl)) < 0) {
#endif
            LOG_CPP_WARNING("[RtpSenderCore] Failed to set multicast TTL");
        }
        
        // Set outgoing interface for multicast
        struct in_addr local_interface;
        std::string primary_ip = get_primary_source_ip();
        if (inet_pton(AF_INET, primary_ip.c_str(), &local_interface) == 1) {
            if (setsockopt(udp_socket_fd_, IPPROTO_IP, IP_MULTICAST_IF,
                          (char*)&local_interface, sizeof(local_interface)) < 0) {
                LOG_CPP_WARNING("[RtpSenderCore] Failed to set multicast interface");
            }
        }
    }

#ifndef _WIN32
    // Set DSCP for low latency
    int dscp = 46; // EF PHB
    int tos_value = dscp << 2;
    if (setsockopt(udp_socket_fd_, IPPROTO_IP, IP_TOS, &tos_value, sizeof(tos_value)) < 0) {
        LOG_CPP_WARNING("[RtpSenderCore] Failed to set TOS/DSCP");
    }
#endif

    // Set up destination address
    memset(&udp_dest_addr_, 0, sizeof(udp_dest_addr_));
    udp_dest_addr_.sin_family = AF_INET;
    udp_dest_addr_.sin_port = htons(dest_port);
    if (inet_pton(AF_INET, dest_ip.c_str(), &udp_dest_addr_.sin_addr) <= 0) {
        LOG_CPP_ERROR("[RtpSenderCore] Invalid destination IP address: %s", dest_ip.c_str());
        platform_close_socket(udp_socket_fd_);
        udp_socket_fd_ = PLATFORM_INVALID_SOCKET;
        return false;
    }

    LOG_CPP_INFO("[RtpSenderCore] Setup complete for %s:%d", dest_ip.c_str(), dest_port);
    return true;
}

void RtpSenderCore::close() {
    if (udp_socket_fd_ != PLATFORM_INVALID_SOCKET) {
        LOG_CPP_INFO("[RtpSenderCore] Closing UDP socket");
        platform_close_socket(udp_socket_fd_);
        udp_socket_fd_ = PLATFORM_INVALID_SOCKET;
    }
}

bool RtpSenderCore::send_rtp_packet(const uint8_t* payload_data, size_t payload_size,
                                    uint32_t timestamp, const std::vector<uint32_t>& csrcs,
                                    bool marker) {
    if (udp_socket_fd_ == PLATFORM_INVALID_SOCKET || payload_size == 0) {
        return false;
    }
    
    const size_t csrc_count = (std::min)(csrcs.size(), size_t(15)); // Max 15 CSRCs
    const size_t rtp_header_size = 12 + (csrc_count * 4);
    std::vector<uint8_t> packet_buffer(rtp_header_size + payload_size);
    
    // Construct RTP header
    // Version (2 bits), Padding (1), Extension (1), CSRC Count (4)
    packet_buffer[0] = (2 << 6) | (csrc_count & 0x0F);
    // Marker (1 bit), Payload Type (7)
    packet_buffer[1] = static_cast<uint8_t>((marker ? 0x80 : 0x00) | (payload_type_ & 0x7F));
    
    // Sequence Number
    uint16_t seq_num = get_next_sequence_number();
    uint16_t seq_num_net = htons(seq_num);
    memcpy(&packet_buffer[2], &seq_num_net, 2);
    
    // Timestamp
    uint32_t ts_net = htonl(timestamp);
    memcpy(&packet_buffer[4], &ts_net, 4);
    
    // SSRC
    uint32_t ssrc_net = htonl(ssrc_);
    memcpy(&packet_buffer[8], &ssrc_net, 4);
    
    // CSRCs
    uint8_t* csrc_ptr = packet_buffer.data() + 12;
    for (size_t i = 0; i < csrc_count; ++i) {
        uint32_t csrc_net = htonl(csrcs[i]);
        memcpy(csrc_ptr, &csrc_net, 4);
        csrc_ptr += 4;
    }
    
    // Copy payload
    memcpy(packet_buffer.data() + rtp_header_size, payload_data, payload_size);
    
    // Send packet
#ifdef _WIN32
    int sent_bytes = sendto(udp_socket_fd_,
                           reinterpret_cast<const char*>(packet_buffer.data()),
                           static_cast<int>(packet_buffer.size()),
                           0,
                           (struct sockaddr*)&udp_dest_addr_,
                           sizeof(udp_dest_addr_));
#else
    int sent_bytes = sendto(udp_socket_fd_,
                           packet_buffer.data(),
                           packet_buffer.size(),
                           0,
                           (struct sockaddr*)&udp_dest_addr_,
                           sizeof(udp_dest_addr_));
#endif
    
    if (sent_bytes < 0) {
        LOG_CPP_ERROR("[RtpSenderCore] UDP sendto failed");
        return false;
    } else if (static_cast<size_t>(sent_bytes) != packet_buffer.size()) {
        LOG_CPP_ERROR("[RtpSenderCore] UDP sendto sent partial data: %d/%zu",
                     sent_bytes, packet_buffer.size());
        return false;
    }
    
    // Update statistics
    packet_count_++;
    octet_count_ += payload_size;
    
    LOG_CPP_DEBUG("[RtpSenderCore] Sent RTP packet: seq=%u, ts=%u, size=%zu, marker=%d",
                 seq_num, timestamp, payload_size, marker ? 1 : 0);
    
    return true;
}

} // namespace audio
} // namespace screamrouter
