#include "rtp_sender.h"
#include "rtp_sender_registry.h"
#include "../../utils/cpp_logger.h"
#include "../../audio_constants.h" // For RTP constants
#include <stdexcept>
#include <cstring>
#include <random>
#include <vector>
#include <thread>
#include <chrono>
#include <sstream>

#include <unistd.h> // For close()
#include <arpa/inet.h> // For inet_pton, inet_ntop
#include <sys/socket.h> // For socket, connect, getsockname

namespace screamrouter {
namespace audio {

namespace { // Anonymous namespace for helper function

bool is_multicast(const std::string& ip_address) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_address.c_str(), &addr) != 1) {
        return false; // Invalid address format
    }
    // The multicast range is 224.0.0.0 to 239.255.255.255.
    // In network byte order, this corresponds to checking the first 4 bits of the address.
    // The first byte must be between 224 (0xE0) and 239 (0xEF).
    return (ntohl(addr.s_addr) & 0xF0000000) == 0xE0000000;
}

std::string get_primary_source_ip() {
    // Create a socket
    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == PLATFORM_INVALID_SOCKET) {
        LOG_CPP_ERROR("[RtpSender] Failed to create socket for IP detection.");
        return "127.0.0.1"; // Fallback
    }

    // Connect to a remote host (doesn't send data) to find the egress IP
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(53); // DNS port, can be any port
    if (inet_pton(AF_INET, "8.8.8.8", &serv.sin_addr) <= 0) {
        platform_close_socket(sock);
        LOG_CPP_ERROR("[RtpSender] inet_pton failed for IP detection.");
        return "127.0.0.1"; // Fallback
    }

    if (connect(sock, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        platform_close_socket(sock);
        // This can fail if there's no route, which is not necessarily an error.
        // We can try another way or just return a default.
        LOG_CPP_WARNING("[RtpSender] Failed to connect for IP detection. May indicate no network route.");
        return "127.0.0.1"; // Fallback
    }

    // Get the local address of the socket
    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    if (getsockname(sock, (struct sockaddr*)&name, &namelen) < 0) {
        platform_close_socket(sock);
        LOG_CPP_ERROR("[RtpSender] getsockname failed for IP detection.");
        return "127.0.0.1"; // Fallback
    }

    platform_close_socket(sock);

    char buffer[INET_ADDRSTRLEN];
    const char* p = inet_ntop(AF_INET, &name.sin_addr, buffer, INET_ADDRSTRLEN);
    if (p != nullptr) {
        LOG_CPP_INFO("[RtpSender] Detected primary source IP: %s", buffer);
        return std::string(buffer);
    } else {
        LOG_CPP_ERROR("[RtpSender] inet_ntop failed for IP detection.");
        return "127.0.0.1"; // Fallback
    }
}
} // end anonymous namespace

RtpSender::RtpSender(const SinkMixerConfig& config)
    : config_(config),
      udp_socket_fd_(PLATFORM_INVALID_SOCKET),
      sequence_number_(0),
      rtp_timestamp_(0),
      sap_socket_fd_(PLATFORM_INVALID_SOCKET),
      sap_thread_running_(false) {

    // Initialize RTP state with random values for security
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis32;
    std::uniform_int_distribution<uint16_t> dis16;

    ssrc_ = dis32(gen);
    sequence_number_ = dis16(gen);
    rtp_timestamp_ = dis32(gen);

    LOG_CPP_INFO("[RtpSender:%s] Initialized with SSRC=0x%08X, Seq=%u, TS=%u",
                 config_.sink_id.c_str(), ssrc_, sequence_number_, rtp_timestamp_);

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_CPP_ERROR("[RtpSender:%s] WSAStartup failed", config_.sink_id.c_str());
        throw std::runtime_error("WSAStartup failed.");
    }
#endif
}

RtpSender::~RtpSender() noexcept {
    close();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool RtpSender::setup() {
    LOG_CPP_INFO("[RtpSender:%s] Setting up networking...", config_.sink_id.c_str());
    udp_socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket_fd_ == PLATFORM_INVALID_SOCKET) {
        LOG_CPP_ERROR("[RtpSender:%s] Failed to create UDP socket", config_.sink_id.c_str());
        return false;
    }

    // If the destination is a multicast address, configure the socket accordingly.
    if (is_multicast(config_.output_ip)) {
        LOG_CPP_INFO("[RtpSender:%s] Destination is a multicast address. Configuring socket for multicast.", config_.sink_id.c_str());

        // Set the Time-To-Live (TTL) for multicast packets
        int ttl = 64; // A reasonable TTL for multicast
#ifdef _WIN32
        if (setsockopt(udp_socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl)) < 0) {
#else
        if (setsockopt(udp_socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
#endif
            LOG_CPP_WARNING("[RtpSender:%s] Failed to set multicast TTL on UDP socket.", config_.sink_id.c_str());
        }

        // Set the outgoing interface for multicast packets.
        // This is important on multi-homed systems. We'll use the primary source IP.
        struct in_addr local_interface;
        std::string primary_ip = get_primary_source_ip();
        if (inet_pton(AF_INET, primary_ip.c_str(), &local_interface) != 1) {
            LOG_CPP_WARNING("[RtpSender:%s] Failed to parse primary IP for multicast interface: %s", config_.sink_id.c_str(), primary_ip.c_str());
        } else {
            if (setsockopt(udp_socket_fd_, IPPROTO_IP, IP_MULTICAST_IF, (char *)&local_interface, sizeof(local_interface)) < 0) {
                LOG_CPP_WARNING("[RtpSender:%s] Failed to set multicast outgoing interface for %s.", config_.sink_id.c_str(), primary_ip.c_str());
            }
        }
    }

#ifndef _WIN32
    int dscp = 46; // EF PHB for low latency audio
    int tos_value = dscp << 2;
    if (setsockopt(udp_socket_fd_, IPPROTO_IP, IP_TOS, &tos_value, sizeof(tos_value)) < 0) {
        LOG_CPP_ERROR("[RtpSender:%s] Failed to set UDP socket TOS/DSCP", config_.sink_id.c_str());
    }
#else
    LOG_CPP_WARNING("[RtpSender:%s] Skipping TOS/DSCP setting on Windows.", config_.sink_id.c_str());
#endif

    memset(&udp_dest_addr_, 0, sizeof(udp_dest_addr_));
    udp_dest_addr_.sin_family = AF_INET;
    udp_dest_addr_.sin_port = htons(config_.output_port);
    if (inet_pton(AF_INET, config_.output_ip.c_str(), &udp_dest_addr_.sin_addr) <= 0) {
        LOG_CPP_ERROR("[RtpSender:%s] Invalid UDP destination IP address: %s", config_.sink_id.c_str(), config_.output_ip.c_str());
        platform_close_socket(udp_socket_fd_);
        udp_socket_fd_ = PLATFORM_INVALID_SOCKET;
        return false;
    }

    LOG_CPP_INFO("[RtpSender:%s] Networking setup complete (UDP target: %s:%d)", config_.sink_id.c_str(), config_.output_ip.c_str(), config_.output_port);

    // --- SAP Setup ---
    LOG_CPP_INFO("[RtpSender:%s] Setting up SAP announcements...", config_.sink_id.c_str());
    sap_socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sap_socket_fd_ == PLATFORM_INVALID_SOCKET) {
        LOG_CPP_ERROR("[RtpSender:%s] Failed to create SAP socket", config_.sink_id.c_str());
        // We can still function without SAP, so just log and continue
    } else {
        // Set TTL for multicast packets to allow them to traverse routers (if needed)
        int ttl = 16;
#ifdef _WIN32
        if (setsockopt(sap_socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl)) < 0) {
#else
        if (setsockopt(sap_socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
#endif
            LOG_CPP_WARNING("[RtpSender:%s] Failed to set multicast TTL on SAP socket. Announcements may not work.", config_.sink_id.c_str());
        }

        const std::vector<std::string> sap_ips = {"224.2.127.254", "224.0.0.56"};
        sap_dest_addrs_.clear();

        for (const auto& ip_str : sap_ips) {
            struct sockaddr_in dest_addr;
            memset(&dest_addr, 0, sizeof(dest_addr));
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(9875); // Standard SAP port
            if (inet_pton(AF_INET, ip_str.c_str(), &dest_addr.sin_addr) <= 0) {
                LOG_CPP_ERROR("[RtpSender:%s] Invalid SAP multicast address: %s", config_.sink_id.c_str(), ip_str.c_str());
                printf("[RtpSender:%s] Invalid SAP multicast address: %s\n", config_.sink_id.c_str(), ip_str.c_str());
                continue; // Skip this invalid address
            }
            sap_dest_addrs_.push_back(dest_addr);
            LOG_CPP_INFO("[RtpSender:%s] Added SAP destination: %s:9875", config_.sink_id.c_str(), ip_str.c_str());
        }

        if (!sap_dest_addrs_.empty()) {
            sap_thread_running_ = true;
            sap_thread_ = std::thread(&RtpSender::sap_announcement_loop, this);
        } else {
            LOG_CPP_ERROR("[RtpSender:%s] No valid SAP destinations, SAP thread not started.", config_.sink_id.c_str());
            platform_close_socket(sap_socket_fd_);
            sap_socket_fd_ = PLATFORM_INVALID_SOCKET;
        }
    }

    RtpSenderRegistry::get_instance().add_ssrc(ssrc_);
    return true;
}

void RtpSender::close() {
    if (sap_thread_running_) {
        LOG_CPP_INFO("[RtpSender:%s] Stopping SAP announcement thread...", config_.sink_id.c_str());
        sap_thread_running_ = false;
        if (sap_thread_.joinable()) {
            sap_thread_.join();
        }
        LOG_CPP_INFO("[RtpSender:%s] SAP thread stopped.", config_.sink_id.c_str());
    }

    if (sap_socket_fd_ != PLATFORM_INVALID_SOCKET) {
        LOG_CPP_INFO("[RtpSender:%s] Closing SAP socket", config_.sink_id.c_str());
        platform_close_socket(sap_socket_fd_);
        sap_socket_fd_ = PLATFORM_INVALID_SOCKET;
    }

    if (udp_socket_fd_ != PLATFORM_INVALID_SOCKET) {
        LOG_CPP_INFO("[RtpSender:%s] Closing UDP socket", config_.sink_id.c_str());
        platform_close_socket(udp_socket_fd_);
        udp_socket_fd_ = PLATFORM_INVALID_SOCKET;
    }
    RtpSenderRegistry::get_instance().remove_ssrc(ssrc_);
}

// As seen in rtp_receiver.cpp, this is a common payload type for this format.
const int RTP_PAYLOAD_TYPE_L16_48K_STEREO = 127;
 
 void RtpSender::send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) {
     if (payload_size == 0) {
         return;
     }
 
     const size_t csrc_count = std::min(csrcs.size(), (size_t)15); // Max 15 CSRCs
     const size_t rtp_header_size = 12 + (csrc_count * 4);
     std::vector<uint8_t> packet_buffer(rtp_header_size + payload_size);
 
     // Manually construct the RTP header
     // Version (2 bits), Padding (1), Extension (1), CSRC Count (4)
     packet_buffer[0] = (2 << 6) | (csrc_count & 0x0F); // Version 2, no padding, no extension
     // Marker (1 bit), Payload Type (7)
     packet_buffer[1] = static_cast<uint8_t>(RTP_PAYLOAD_TYPE_L16_48K_STEREO & 0x7F);
    // Sequence Number (16 bits)
    uint16_t seq_num_net = htons(sequence_number_++);
    memcpy(&packet_buffer[2], &seq_num_net, 2);
    // Timestamp (32 bits)
    uint32_t ts_net = htonl(rtp_timestamp_);
    memcpy(&packet_buffer[4], &ts_net, 4);
    // SSRC (32 bits)
    uint32_t ssrc_net = htonl(ssrc_);
    memcpy(&packet_buffer[8], &ssrc_net, 4);

    // Copy CSRCs
    uint8_t* csrc_ptr = packet_buffer.data() + 12;
    for (size_t i = 0; i < csrc_count; ++i) {
        uint32_t csrc_net = htonl(csrcs[i]);
        memcpy(csrc_ptr, &csrc_net, 4);
        csrc_ptr += 4;
    }

    // For debugging, log the constructed header and packet details
    char header_str[12 * 4 + 1] = {0};
    for (int i = 0; i < 12; ++i) {
        snprintf(header_str + i * 3, 4, "%02X ", packet_buffer[i]);
    }
    LOG_CPP_DEBUG("[RtpSender:%s] RTP Send: size=%zu, ts=%u, seq=%u, header=[%s]",
                    config_.sink_id.c_str(), payload_size, rtp_timestamp_, sequence_number_ - 1, header_str);

    // Copy payload data
    memcpy(packet_buffer.data() + rtp_header_size, payload_data, payload_size);

    // Convert payload to network byte order
    uint8_t* rtp_payload_ptr = packet_buffer.data() + rtp_header_size;
    size_t bytes_per_sample = config_.output_bitdepth / 8;

    if (bytes_per_sample > 0 && payload_size % bytes_per_sample != 0) {
        LOG_CPP_WARNING("[RtpSender:%s] Payload size %zu is not a multiple of bytes per sample %zu for bit depth %d. Skipping byte order conversion for this packet.",
                        config_.sink_id.c_str(), payload_size, bytes_per_sample, config_.output_bitdepth);
    } else {
        if (config_.output_bitdepth == 16) {
            for (size_t i = 0; i < payload_size; i += bytes_per_sample) {
                uint16_t* sample_ptr = reinterpret_cast<uint16_t*>(rtp_payload_ptr + i);
                *sample_ptr = htons(*sample_ptr);
            }
        } else if (config_.output_bitdepth == 24) {
            for (size_t i = 0; i < payload_size; i += bytes_per_sample) {
                // Manual byte swap for 24-bit: [0][1][2] -> [2][1][0]
                uint8_t* sample_bytes = rtp_payload_ptr + i;
                std::swap(sample_bytes[0], sample_bytes[2]);
            }
        } else if (config_.output_bitdepth == 32) {
            for (size_t i = 0; i < payload_size; i += bytes_per_sample) {
                uint32_t* sample_ptr = reinterpret_cast<uint32_t*>(rtp_payload_ptr + i);
                *sample_ptr = htonl(*sample_ptr);
            }
        }
        // 8-bit samples (bytes_per_sample == 1) do not require byte order conversion.
    }

    // Increment timestamp by number of samples in the packet
    // A "frame" is a single sample from all channels.
    size_t bytes_per_frame = (config_.output_bitdepth / 8) * config_.output_channels;
    if (bytes_per_frame > 0) {
        rtp_timestamp_ += payload_size / bytes_per_frame;
    }

    size_t length = packet_buffer.size();

    if (udp_socket_fd_ != PLATFORM_INVALID_SOCKET) {
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
            LOG_CPP_ERROR("[RtpSender:%s] UDP sendto failed", config_.sink_id.c_str());
        } else if (static_cast<size_t>(sent_bytes) != length) {
            LOG_CPP_ERROR("[RtpSender:%s] UDP sendto sent partial data: %d/%zu", config_.sink_id.c_str(), sent_bytes, length);
        }
    }
}

void RtpSender::sap_announcement_loop() {
    LOG_CPP_INFO("[RtpSender:%s] SAP announcement thread started.", config_.sink_id.c_str());

    std::string source_ip = get_primary_source_ip();

    while (sap_thread_running_) {
        if (sap_socket_fd_ != PLATFORM_INVALID_SOCKET) {
            // Construct SDP payload
            std::stringstream sdp;
            sdp << "v=0\n";
            sdp << "o=screamrouter " << ssrc_ << " 1 IN IP4 " << source_ip << "\n";
            sdp << "s=" << config_.sink_id << "\n";
            sdp << "c=IN IP4 " << config_.output_ip << "\n";
            sdp << "t=0 0\n";
            sdp << "m=audio " << config_.output_port << " RTP/AVP " << RTP_PAYLOAD_TYPE_L16_48K_STEREO << "\n";
            sdp << "a=rtpmap:" << RTP_PAYLOAD_TYPE_L16_48K_STEREO << " L16/48000/" << config_.output_channels << "\n";
            
            // Add channel map if not in auto mode and channels > 2
            if (!config_.speaker_layout.auto_mode && config_.output_channels > 2) {
                std::stringstream channel_map_ss;
                channel_map_ss << "a=channelmap:" << RTP_PAYLOAD_TYPE_L16_48K_STEREO << " " << config_.output_channels;
                
                std::vector<int> channel_order;
                // This logic assumes a simple 1-to-1 mapping in the matrix.
                // It finds which input channel (column) maps to each output channel (row).
                for (int i = 0; i < config_.output_channels; ++i) {
                    bool found = false;
                    for (int j = 0; j < config_.output_channels; ++j) {
                        if (config_.speaker_layout.matrix[i][j] == 1.0f) {
                            channel_order.push_back(j + 1); // Channel numbers are 1-based
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        channel_order.push_back(0); // Use 0 for unassigned channels
                    }
                }

                for (size_t i = 0; i < channel_order.size(); ++i) {
                    channel_map_ss << (i == 0 ? " " : ",") << channel_order[i];
                }
                sdp << channel_map_ss.str() << "\n";
            }

            std::string sdp_str = sdp.str();

            // Construct SAP Packet (RFC 2974)
            const size_t sap_header_size = 8;
            const std::string content_type = "application/sdp";

            // Allocate space for all components: SAP header, content type + null, and SDP payload + null
            std::vector<uint8_t> sap_packet(sap_header_size + content_type.length() + 1 + sdp_str.length() + 1);

            // SAP Header
            sap_packet[0] = 0x20; // V=1, A=0, R=0, T=0, E=0, C=0
            sap_packet[1] = 0;    // Auth len = 0 (no authentication)
            uint16_t msg_id_hash = sequence_number_ % 65536;
            uint16_t msg_id_hash_net = htons(msg_id_hash);
            memcpy(&sap_packet[2], &msg_id_hash_net, 2);
            struct in_addr src_addr;
            inet_pton(AF_INET, source_ip.c_str(), &src_addr); // Use dynamically detected source IP
            memcpy(&sap_packet[4], &src_addr.s_addr, 4);

            // Copy the content type string, followed by the SDP payload
            size_t offset = sap_header_size;
            memcpy(&sap_packet[offset], content_type.c_str(), content_type.length() + 1); // Include null terminator
            offset += content_type.length() + 1;
            memcpy(&sap_packet[offset], sdp_str.c_str(), sdp_str.length() + 1); // Include null terminator

            LOG_CPP_DEBUG("[RtpSender:%s] Sending SAP Announcement: %s", config_.sink_id.c_str(), sdp_str.c_str());

            for (const auto& dest_addr : sap_dest_addrs_) {
                #ifdef _WIN32
                int sent_bytes = sendto(sap_socket_fd_,
                                        reinterpret_cast<const char*>(sap_packet.data()),
                                        static_cast<int>(sap_packet.size()),
                                        0,
                                        (struct sockaddr *)&dest_addr,
                                        sizeof(dest_addr));
                #else
                int sent_bytes = sendto(sap_socket_fd_,
                                        sap_packet.data(),
                                        sap_packet.size(),
                                        0,
                                        (struct sockaddr *)&dest_addr,
                                        sizeof(dest_addr));
                #endif

                if (sent_bytes < 0) {
                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &dest_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
                    LOG_CPP_ERROR("[RtpSender:%s] SAP sendto failed for %s", config_.sink_id.c_str(), ip_str);
                } else if (static_cast<size_t>(sent_bytes) != sap_packet.size()) {
                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &dest_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
                    LOG_CPP_ERROR("[RtpSender:%s] SAP sendto sent partial data to %s: %d/%zu", config_.sink_id.c_str(), ip_str, sent_bytes, sap_packet.size());
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    }

    LOG_CPP_INFO("[RtpSender:%s] SAP announcement thread finished.", config_.sink_id.c_str());
}


} // namespace audio
} // namespace screamrouter
