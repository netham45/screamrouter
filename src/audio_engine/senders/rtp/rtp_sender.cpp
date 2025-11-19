#include "rtp_sender.h"
#include "rtp_sender_registry.h"
#include "../../audio_channel_layout.h"
#include "../../utils/cpp_logger.h"
#include "../../audio_constants.h" // For RTP constants
#include <stdexcept>
#include <cstring>
#include <random>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <sstream>
#include <errno.h> // For errno, EAGAIN, EWOULDBLOCK, ETIMEDOUT

#ifndef _WIN32
    #include <unistd.h> // For close()
    #include <arpa/inet.h> // For inet_pton, inet_ntop
    #include <sys/socket.h> // For socket, connect, getsockname
#endif

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
      rtp_core_(nullptr),
      sap_socket_fd_(PLATFORM_INVALID_SOCKET),
      sap_thread_running_(false),
      rtcp_socket_fd_(PLATFORM_INVALID_SOCKET),
      rtcp_thread_running_(false),
      packet_count_(0),
      octet_count_(0),
      time_sync_delay_ms_(config.time_sync_delay_ms) {

    LOG_CPP_INFO("[RtpSender:%s] ===== CONSTRUCTOR START =====", config_.sink_id.c_str());
    LOG_CPP_INFO("[RtpSender:%s] CONFIG VALUES:", config_.sink_id.c_str());
    LOG_CPP_INFO("[RtpSender:%s]   sink_id='%s'", config_.sink_id.c_str(), config_.sink_id.c_str());
    LOG_CPP_INFO("[RtpSender:%s]   protocol='%s'", config_.sink_id.c_str(), config_.protocol.c_str());
    LOG_CPP_INFO("[RtpSender:%s]   time_sync_enabled=%s", config_.sink_id.c_str(), config_.time_sync_enabled ? "TRUE" : "FALSE");
    LOG_CPP_INFO("[RtpSender:%s]   time_sync_delay_ms=%d", config_.sink_id.c_str(), config_.time_sync_delay_ms);
    LOG_CPP_INFO("[RtpSender:%s]   output_ip='%s'", config_.sink_id.c_str(), config_.output_ip.c_str());
    LOG_CPP_INFO("[RtpSender:%s]   output_port=%d", config_.sink_id.c_str(), config_.output_port);
    LOG_CPP_INFO("[RtpSender:%s]   output_channels=%d", config_.sink_id.c_str(), config_.output_channels);
    LOG_CPP_INFO("[RtpSender:%s]   output_bitdepth=%d", config_.sink_id.c_str(), config_.output_bitdepth);
    LOG_CPP_INFO("[RtpSender:%s] CALCULATED time_sync_delay_ms_=%d", config_.sink_id.c_str(), time_sync_delay_ms_);
    LOG_CPP_INFO("[RtpSender:%s] RTCP is always enabled for this sender (time_sync_enabled flag is ignored for RTCP enablement)",
                 config_.sink_id.c_str());

    // Initialize RTP state with random values for security
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis32;

    ssrc_ = dis32(gen);
    rtp_timestamp_ = dis32(gen);
    
    // Create RTP core with the generated SSRC
    rtp_core_ = std::make_unique<RtpSenderCore>(ssrc_);
    
    // Store initial RTP timestamp and stream start time for synchronization
    stream_start_rtp_timestamp_ = rtp_timestamp_;
    stream_start_time_ = std::chrono::system_clock::now();

    LOG_CPP_INFO("[RtpSender:%s] Initialized with SSRC=0x%08X, TS=%u",
                 config_.sink_id.c_str(), ssrc_, rtp_timestamp_);
    
    // Log RTCP configuration
    LOG_CPP_INFO("[RtpSender:%s] RTCP Configuration: protocol=%s, time_sync_enabled=%s, time_sync_delay_ms=%d",
                 config_.sink_id.c_str(),
                 config_.protocol.c_str(),
                 config_.time_sync_enabled ? "true" : "false",
                 time_sync_delay_ms_);

    LOG_CPP_INFO("[RtpSender:%s] ===== CONSTRUCTOR END =====", config_.sink_id.c_str());

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
    LOG_CPP_INFO("[RtpSender:%s] ===== SETUP() START =====", config_.sink_id.c_str());
    LOG_CPP_INFO("[RtpSender:%s] SETUP CONFIG CHECK:", config_.sink_id.c_str());
    LOG_CPP_INFO("[RtpSender:%s]   config_.protocol='%s'", config_.sink_id.c_str(), config_.protocol.c_str());
    LOG_CPP_INFO("[RtpSender:%s]   config_.time_sync_enabled=%s", config_.sink_id.c_str(), config_.time_sync_enabled ? "TRUE" : "FALSE");
    LOG_CPP_INFO("[RtpSender:%s]   config_.time_sync_delay_ms=%d", config_.sink_id.c_str(), config_.time_sync_delay_ms);
    LOG_CPP_INFO("[RtpSender:%s]   time_sync_delay_ms_=%d", config_.sink_id.c_str(), time_sync_delay_ms_);
    LOG_CPP_INFO("[RtpSender:%s]   config_.output_ip='%s'", config_.sink_id.c_str(), config_.output_ip.c_str());
    LOG_CPP_INFO("[RtpSender:%s]   config_.output_port=%d", config_.sink_id.c_str(), config_.output_port);
    LOG_CPP_INFO("[RtpSender:%s] RTCP will be configured regardless of protocol (time_sync_enabled=%s)",
                 config_.sink_id.c_str(),
                 config_.time_sync_enabled ? "true" : "false");
    
    LOG_CPP_INFO("[RtpSender:%s] Setting up networking (protocol=%s, time_sync=%s, delay=%dms)...",
                 config_.sink_id.c_str(),
                 config_.protocol.c_str(),
                 config_.time_sync_enabled ? "true" : "false",
                 time_sync_delay_ms_);
    
    // Setup RTP core
    if (!rtp_core_->setup(config_.output_ip, config_.output_port)) {
        LOG_CPP_ERROR("[RtpSender:%s] Failed to setup RTP core", config_.sink_id.c_str());
        return false;
    }

    rtp_core_->set_payload_type(rtp_payload_type());

    if (!initialize_payload_pipeline()) {
        LOG_CPP_ERROR("[RtpSender:%s] Failed to initialize payload pipeline", config_.sink_id.c_str());
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
#ifndef _WIN32
       // Set socket priority for low latency on Linux
       int priority = 6;
       if (setsockopt(sap_socket_fd_, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority)) < 0) {
           LOG_CPP_WARNING("[RtpSender:%s] Failed to set socket priority on SAP socket.", config_.sink_id.c_str());
       }
       // Allow reusing the address
       int reuse = 1;
       if (setsockopt(sap_socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
           LOG_CPP_WARNING("[RtpSender:%s] Failed to set SO_REUSEADDR on SAP socket.", config_.sink_id.c_str());
       }
#endif
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

    // --- RTCP Setup (always attempt) ---
    LOG_CPP_INFO("[RtpSender:%s] ===== RTCP SETUP SECTION START =====", config_.sink_id.c_str());
    LOG_CPP_INFO("[RtpSender:%s] Configuring RTCP socket on port %d (RTP+1)",
                 config_.sink_id.c_str(), config_.output_port + 1);
    
    rtcp_socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (rtcp_socket_fd_ == PLATFORM_INVALID_SOCKET) {
        LOG_CPP_ERROR("[RtpSender:%s] RTCP SETUP FAILED - Failed to create RTCP socket (errno=%d: %s)",
                      config_.sink_id.c_str(), errno, strerror(errno));
        LOG_CPP_ERROR("[RtpSender:%s] RTCP remains unavailable for this sender due to socket failure.",
                      config_.sink_id.c_str());
    } else {
        LOG_CPP_INFO("[RtpSender:%s] RTCP socket created successfully (fd=%d)",
                    config_.sink_id.c_str(), rtcp_socket_fd_);
#ifndef _WIN32
        // Set socket priority for low latency on Linux
        int priority = 6; // Corresponds to AC_VO (Access Category Voice)
        if (setsockopt(rtcp_socket_fd_, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority)) < 0) {
            LOG_CPP_WARNING("[RtpSender:%s] Failed to set socket priority on RTCP socket (errno=%d: %s)",
                           config_.sink_id.c_str(), errno, strerror(errno));
        } else {
            LOG_CPP_DEBUG("[RtpSender:%s] RTCP socket priority set to %d", config_.sink_id.c_str(), priority);
        }
        // Allow reusing the address
        int reuse = 1;
        if (setsockopt(rtcp_socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            LOG_CPP_WARNING("[RtpSender:%s] Failed to set SO_REUSEADDR on RTCP socket (errno=%d: %s)",
                           config_.sink_id.c_str(), errno, strerror(errno));
        } else {
            LOG_CPP_DEBUG("[RtpSender:%s] RTCP socket SO_REUSEADDR enabled", config_.sink_id.c_str());
        }
#endif

        // Set up RTCP destination address (RTP port + 1)
        LOG_CPP_INFO("[RtpSender:%s] Configuring RTCP destination address: %s:%d",
                    config_.sink_id.c_str(), config_.output_ip.c_str(), config_.output_port + 1);
        
        memset(&rtcp_dest_addr_, 0, sizeof(rtcp_dest_addr_));
        rtcp_dest_addr_.sin_family = AF_INET;
        rtcp_dest_addr_.sin_port = htons(config_.output_port + 1);
        
        int pton_result = inet_pton(AF_INET, config_.output_ip.c_str(), &rtcp_dest_addr_.sin_addr);
        if (pton_result <= 0) {
            if (pton_result == 0) {
                LOG_CPP_ERROR("[RtpSender:%s] Invalid RTCP destination IP address format: %s",
                             config_.sink_id.c_str(), config_.output_ip.c_str());
            } else {
                LOG_CPP_ERROR("[RtpSender:%s] inet_pton failed for RTCP destination IP: %s (errno=%d: %s)",
                             config_.sink_id.c_str(), config_.output_ip.c_str(), errno, strerror(errno));
            }
            platform_close_socket(rtcp_socket_fd_);
            rtcp_socket_fd_ = PLATFORM_INVALID_SOCKET;
        } else {
            LOG_CPP_INFO("[RtpSender:%s] RTCP socket setup complete (target: %s:%d)",
                        config_.sink_id.c_str(), config_.output_ip.c_str(), config_.output_port + 1);
            
            // Start RTCP thread
            LOG_CPP_INFO("[RtpSender:%s] Starting RTCP thread...", config_.sink_id.c_str());
            rtcp_thread_running_ = true;
            rtcp_thread_ = std::thread(&RtpSender::rtcp_thread_loop, this);
            LOG_CPP_INFO("[RtpSender:%s] RTCP thread started successfully", config_.sink_id.c_str());
        }
    }
    LOG_CPP_INFO("[RtpSender:%s] ===== RTCP SETUP SECTION END =====", config_.sink_id.c_str());

    // Log final RTCP status
    LOG_CPP_ERROR("[RtpSender:%s] ===== FINAL RTCP STATUS =====", config_.sink_id.c_str());
    LOG_CPP_ERROR("[RtpSender:%s] RTCP socket_fd=%d (INVALID=%d)",
                config_.sink_id.c_str(), rtcp_socket_fd_, PLATFORM_INVALID_SOCKET);
    LOG_CPP_ERROR("[RtpSender:%s] RTCP thread_running=%s",
                config_.sink_id.c_str(), rtcp_thread_running_.load() ? "TRUE" : "FALSE");
    LOG_CPP_ERROR("[RtpSender:%s] RTCP target=%s:%d",
                config_.sink_id.c_str(), config_.output_ip.c_str(), config_.output_port + 1);
    LOG_CPP_INFO("[RtpSender:%s] Setup complete - RTCP status: socket_fd=%d, thread_running=%s, target=%s:%d",
                config_.sink_id.c_str(),
                rtcp_socket_fd_,
                rtcp_thread_running_.load() ? "true" : "false",
                config_.output_ip.c_str(),
                config_.output_port + 1);
    LOG_CPP_ERROR("[RtpSender:%s] ===== SETUP() END =====", config_.sink_id.c_str());
    
    RtpSenderRegistry::get_instance().add_ssrc(ssrc_);
    return true;
}

void RtpSender::close() {
    // Stop RTCP thread first
    if (rtcp_thread_running_) {
        LOG_CPP_INFO("[RtpSender:%s] Stopping RTCP thread (was running=%s)...",
                    config_.sink_id.c_str(), rtcp_thread_running_.load() ? "true" : "false");
        rtcp_thread_running_ = false;
        if (rtcp_thread_.joinable()) {
            LOG_CPP_INFO("[RtpSender:%s] Waiting for RTCP thread to join...", config_.sink_id.c_str());
            rtcp_thread_.join();
        }
        LOG_CPP_INFO("[RtpSender:%s] RTCP thread stopped successfully", config_.sink_id.c_str());
    } else {
        LOG_CPP_DEBUG("[RtpSender:%s] RTCP thread was not running", config_.sink_id.c_str());
    }

    if (sap_thread_running_) {
        LOG_CPP_INFO("[RtpSender:%s] Stopping SAP announcement thread...", config_.sink_id.c_str());
        sap_thread_running_ = false;
        if (sap_thread_.joinable()) {
            sap_thread_.join();
        }
        LOG_CPP_INFO("[RtpSender:%s] SAP thread stopped.", config_.sink_id.c_str());
    }

    if (rtcp_socket_fd_ != PLATFORM_INVALID_SOCKET) {
        LOG_CPP_INFO("[RtpSender:%s] Closing RTCP socket", config_.sink_id.c_str());
        platform_close_socket(rtcp_socket_fd_);
        rtcp_socket_fd_ = PLATFORM_INVALID_SOCKET;
    }

    if (sap_socket_fd_ != PLATFORM_INVALID_SOCKET) {
        LOG_CPP_INFO("[RtpSender:%s] Closing SAP socket", config_.sink_id.c_str());
        platform_close_socket(sap_socket_fd_);
        sap_socket_fd_ = PLATFORM_INVALID_SOCKET;
    }

    teardown_payload_pipeline();

    if (rtp_core_) {
        LOG_CPP_INFO("[RtpSender:%s] Closing RTP core", config_.sink_id.c_str());
        rtp_core_->close();
    }
    RtpSenderRegistry::get_instance().remove_ssrc(ssrc_);
}

// As seen in rtp_receiver.cpp, this is a common payload type for this format.
const int RTP_PAYLOAD_TYPE_L16_48K_STEREO = 127;
constexpr std::size_t kDefaultRtpPayloadMtu = 1152;

void RtpSender::send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) {
    if (payload_size == 0 || !rtp_core_) {
        return;
    }

    handle_send_payload(payload_data, payload_size, csrcs);
}

bool RtpSender::handle_send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) {
    if (payload_size == 0) {
        return true;
    }

    // Convert payload to network byte order (entire buffer once).
    std::vector<uint8_t> network_payload(payload_size);
    memcpy(network_payload.data(), payload_data, payload_size);

    const size_t bytes_per_sample = static_cast<size_t>(std::max(1, config_.output_bitdepth / 8));
    const size_t bytes_per_frame = bytes_per_sample * static_cast<size_t>(std::max(1, config_.output_channels));

    if (bytes_per_sample > 0 && payload_size % bytes_per_sample == 0) {
        uint8_t* rtp_payload_ptr = network_payload.data();

        if (config_.output_bitdepth == 16) {
            for (size_t i = 0; i < payload_size; i += bytes_per_sample) {
                uint16_t* sample_ptr = reinterpret_cast<uint16_t*>(rtp_payload_ptr + i);
                *sample_ptr = htons(*sample_ptr);
            }
        } else if (config_.output_bitdepth == 24) {
            for (size_t i = 0; i < payload_size; i += bytes_per_sample) {
                uint8_t* sample_bytes = rtp_payload_ptr + i;
                std::swap(sample_bytes[0], sample_bytes[2]);
            }
        } else if (config_.output_bitdepth == 32) {
            for (size_t i = 0; i < payload_size; i += bytes_per_sample) {
                uint32_t* sample_ptr = reinterpret_cast<uint32_t*>(rtp_payload_ptr + i);
                *sample_ptr = htonl(*sample_ptr);
            }
        }
    }

    const std::size_t mtu_bytes = kDefaultRtpPayloadMtu;
    std::size_t slice_cap = mtu_bytes;
    if (bytes_per_frame > 0 && slice_cap > 0) {
        const std::size_t frames_per_slice = std::max<std::size_t>(1, slice_cap / bytes_per_frame);
        slice_cap = frames_per_slice * bytes_per_frame;
    } else {
        slice_cap = payload_size;
    }

    size_t offset = 0;
    while (offset < payload_size) {
        const size_t remaining = payload_size - offset;
        size_t slice_size = std::min(remaining, slice_cap);
        if (bytes_per_frame > 0) {
            const size_t remainder = slice_size % bytes_per_frame;
            if (remainder != 0) {
                // Ensure we never send partial frames.
                slice_size -= remainder;
            }
        }
        if (slice_size == 0) {
            // Fallback: send one frame to avoid infinite loop.
            slice_size = std::min(remaining, bytes_per_frame > 0 ? bytes_per_frame : remaining);
        }

        const bool marker = (offset + slice_size) >= payload_size;
        if (!send_rtp_payload(network_payload.data() + offset, slice_size, csrcs, marker)) {
            return false;
        }

        if (bytes_per_frame > 0) {
            advance_rtp_timestamp(static_cast<uint32_t>(slice_size / bytes_per_frame));
        }

        offset += slice_size;
    }

    return true;
}

bool RtpSender::initialize_payload_pipeline() {
    return true;
}

void RtpSender::teardown_payload_pipeline() {
    // No-op for PCM sender
}

uint8_t RtpSender::rtp_payload_type() const {
    return static_cast<uint8_t>(RTP_PAYLOAD_TYPE_L16_48K_STEREO);
}

uint32_t RtpSender::rtp_clock_rate() const {
    return static_cast<uint32_t>(config_.output_samplerate > 0 ? config_.output_samplerate : 48000);
}

uint32_t RtpSender::rtp_channel_count() const {
    return static_cast<uint32_t>(config_.output_channels > 0 ? config_.output_channels : 2);
}

std::string RtpSender::sdp_payload_name() const {
    return "L16";
}

std::vector<std::string> RtpSender::sdp_format_specific_attributes() const {
    std::vector<std::string> attributes;
    attributes.emplace_back("a=fmtp:" + std::to_string(rtp_payload_type()) + " buffer-time=20");
    return attributes;
}

bool RtpSender::send_rtp_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs, bool marker) {
    if (!rtp_core_) {
        return false;
    }

    if (!rtp_core_->send_rtp_packet(payload_data, payload_size, rtp_timestamp_, csrcs, marker)) {
        LOG_CPP_ERROR("[RtpSender:%s] Failed to send RTP packet", config_.sink_id.c_str());
        return false;
    }

    uint32_t old_packet_count = packet_count_.fetch_add(1);
    uint32_t old_octet_count = octet_count_.fetch_add(payload_size);

    if ((old_packet_count + 1) % 100 == 0) {
        LOG_CPP_DEBUG("[RtpSender:%s] RTP Statistics: packets=%u, octets=%u, RTCP enabled=%s",
                      config_.sink_id.c_str(),
                      old_packet_count + 1,
                      old_octet_count + payload_size,
                      (rtcp_thread_running_.load() ? "true" : "false"));
    }

    return true;
}

void RtpSender::advance_rtp_timestamp(uint32_t samples_per_channel) {
    rtp_timestamp_ += samples_per_channel;
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
            const uint8_t payload_type = rtp_payload_type();
            const uint32_t effective_clock_rate = rtp_clock_rate() == 0 ? 48000 : rtp_clock_rate();
            const uint32_t channel_count = rtp_channel_count();
            const std::string codec_name = sdp_payload_name();
            const auto extra_attributes = sdp_format_specific_attributes();

            sdp << "m=audio " << config_.output_port << " RTP/AVP " << static_cast<int>(payload_type) << "\n"; 
            sdp << "a=rtpmap:" << static_cast<int>(payload_type) << " " << codec_name << "/" << effective_clock_rate;
            if (channel_count > 0) {
                sdp << "/" << channel_count;
            }
            sdp << "\n";
            for (const auto& attribute : extra_attributes) {
                if (attribute.empty()) {
                    continue;
                }
                sdp << attribute;
                if (attribute.back() != '\n') {
                    sdp << "\n";
                }
            }

            // Add channel map if channels > 2, using the scream channel layout
            if (config_.output_channels > 2 && codec_name != "opus") {
                const uint32_t ch_mask = (static_cast<uint32_t>(config_.output_chlayout2) << 8) | config_.output_chlayout1;
                std::vector<ChannelRole> layout_roles = channel_order_from_mask(ch_mask);

                if (layout_roles.size() == static_cast<size_t>(config_.output_channels)) {
                    std::vector<int> channel_order = roles_to_indices(layout_roles);
                    std::stringstream channel_map_ss;
                    channel_map_ss << "a=channelmap:" << static_cast<int>(payload_type) << " " << config_.output_channels;
                    for (size_t i = 0; i < channel_order.size(); ++i) {
                        channel_map_ss << (i == 0 ? " " : ",") << channel_order[i];
                    }
                    sdp << channel_map_ss.str() << "\n";
                } else {
                    LOG_CPP_WARNING("[RtpSender:%s] Channel mask layout does not match channel count. Mask: %02X%02X, Count: %d. Skipping channelmap.",
                                    config_.sink_id.c_str(), config_.output_chlayout2, config_.output_chlayout1, config_.output_channels);
                }
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
            uint16_t msg_id_hash = rtp_core_->get_sequence_number() % 65536;
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
uint64_t RtpSender::get_ntp_timestamp_with_delay() {
    // Get current time
    auto now = std::chrono::system_clock::now();
    
    // Add delay if configured
    if (time_sync_delay_ms_ != 0) {
        LOG_CPP_DEBUG("[RtpSender:%s] Adding time sync delay of %d ms to NTP timestamp",
                     config_.sink_id.c_str(), time_sync_delay_ms_);
        now += std::chrono::milliseconds(time_sync_delay_ms_);
    }
    
    // Convert to time since Unix epoch
    // Explicitly use uint64_t to avoid sign issues
    uint64_t unix_time_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()
        ).count()
    );
    
    // NTP epoch is January 1, 1900, Unix epoch is January 1, 1970
    // The difference is 70 years (2208988800 seconds)
    const uint64_t NTP_UNIX_EPOCH_DIFF = 2208988800ULL;
    
    // Convert microseconds to seconds and fraction
    uint64_t seconds = (unix_time_us / 1000000) + NTP_UNIX_EPOCH_DIFF;
    uint64_t microseconds = unix_time_us % 1000000;
    
    // Convert fraction to NTP format (32-bit fraction of a second)
    // NTP fraction = (microseconds * 2^32) / 10^6
    // To avoid overflow, we can use: (microseconds * 4294967296) / 1000000
    // Or more efficiently: (microseconds * 4295) / 1000 (approximately)
    uint64_t fraction = (microseconds * 4294967296ULL) / 1000000ULL;
    
    // Combine seconds (upper 32 bits) and fraction (lower 32 bits)
    uint64_t ntp_timestamp = (seconds << 32) | (fraction & 0xFFFFFFFF);
    
    LOG_CPP_DEBUG("[RtpSender:%s] Generated NTP timestamp: 0x%016llX (seconds=%u, fraction=%u, delay=%dms)",
                  config_.sink_id.c_str(),
                  (unsigned long long)ntp_timestamp,
                  (unsigned)seconds,
                  (unsigned)fraction,
                  time_sync_delay_ms_);
    
    return ntp_timestamp;
}

void RtpSender::rtcp_thread_loop() {
    LOG_CPP_INFO("[RtpSender:%s] RTCP thread loop started (socket_fd=%d, target=%s:%d)",
                config_.sink_id.c_str(),
                rtcp_socket_fd_,
                config_.output_ip.c_str(),
                config_.output_port + 1);
    
    // Set up socket for receiving with timeout
    if (rtcp_socket_fd_ != PLATFORM_INVALID_SOCKET) {
        LOG_CPP_INFO("[RtpSender:%s] Configuring RTCP socket for receiving...", config_.sink_id.c_str());
        // Bind to receive RTCP packets
        struct sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = INADDR_ANY;
        local_addr.sin_port = htons(config_.output_port + 1);
        
        if (bind(rtcp_socket_fd_, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
            LOG_CPP_WARNING("[RtpSender:%s] Failed to bind RTCP socket to port %d for receiving (errno=%d: %s)",
                          config_.sink_id.c_str(), config_.output_port + 1, errno, strerror(errno));
        } else {
            LOG_CPP_INFO("[RtpSender:%s] RTCP socket bound successfully to port %d",
                        config_.sink_id.c_str(), config_.output_port + 1);
        }
        
        // Set socket timeout for non-blocking receive
        #ifdef _WIN32
            DWORD timeout = 100; // 100ms timeout
            if (setsockopt(rtcp_socket_fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
                LOG_CPP_WARNING("[RtpSender:%s] Failed to set receive timeout on RTCP socket (error=%d)",
                              config_.sink_id.c_str(), WSAGetLastError());
        #else
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000; // 100ms timeout
            if (setsockopt(rtcp_socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
                LOG_CPP_WARNING("[RtpSender:%s] Failed to set receive timeout on RTCP socket (errno=%d: %s)",
                              config_.sink_id.c_str(), errno, strerror(errno));
        #endif
        } else {
            LOG_CPP_INFO("[RtpSender:%s] RTCP socket receive timeout set to 100ms", config_.sink_id.c_str());
        }
    } else {
        LOG_CPP_ERROR("[RtpSender:%s] RTCP socket is invalid in thread loop!", config_.sink_id.c_str());
    }
    
    auto last_sr_time = std::chrono::steady_clock::now();
    const auto sr_interval = std::chrono::seconds(5);
    
    // Buffer for receiving RTCP packets
    uint8_t recv_buffer[2048];
    
    int loop_count = 0;
    LOG_CPP_INFO("[RtpSender:%s] RTCP thread entering main loop (SR interval=%ld seconds)",
                config_.sink_id.c_str(), sr_interval.count());
    
    while (rtcp_thread_running_) {
        auto now = std::chrono::steady_clock::now();
        
        // Log every 100 iterations for debugging
        if (++loop_count % 100 == 0) {
            LOG_CPP_DEBUG("[RtpSender:%s] RTCP thread loop iteration %d, thread_running=%s",
                         config_.sink_id.c_str(), loop_count, rtcp_thread_running_.load() ? "true" : "false");
        }
        
        // Send periodic Sender Reports
        if (now - last_sr_time >= sr_interval) {
            LOG_CPP_INFO("[RtpSender:%s] RTCP SR interval reached, sending Sender Report...",
                        config_.sink_id.c_str());
            send_rtcp_sr();
            last_sr_time = now;
        }
        
        // Check for incoming RTCP packets
        if (rtcp_socket_fd_ != PLATFORM_INVALID_SOCKET) {
            struct sockaddr_in sender_addr;
            socklen_t sender_addr_len = sizeof(sender_addr);
            
            #ifdef _WIN32
                int recv_len = recvfrom(rtcp_socket_fd_,
                                       (char*)recv_buffer,
                                       sizeof(recv_buffer),
                                       0,
                                       (struct sockaddr*)&sender_addr,
                                       &sender_addr_len);
            #else
                ssize_t recv_len = recvfrom(rtcp_socket_fd_,
                                           recv_buffer,
                                           sizeof(recv_buffer),
                                           0,
                                           (struct sockaddr*)&sender_addr,
                                           &sender_addr_len);
            #endif
            
            if (recv_len > 0) {
                char sender_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, INET_ADDRSTRLEN);
                LOG_CPP_INFO("[RtpSender:%s] Received RTCP packet: %zd bytes from %s:%d",
                            config_.sink_id.c_str(), recv_len,
                            sender_ip, ntohs(sender_addr.sin_port));
                // Process the received RTCP packet
                process_incoming_rtcp(recv_buffer, recv_len, sender_addr);
            } else if (recv_len < 0) {
                // Check if it's a timeout (EAGAIN/EWOULDBLOCK) or a real error
                #ifdef _WIN32
                int error = WSAGetLastError();
                if (error != WSAETIMEDOUT && error != WSAEWOULDBLOCK) {
                    LOG_CPP_ERROR("[RtpSender:%s] RTCP recvfrom error: %d",
                                config_.sink_id.c_str(), error);
                }
                #else
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ETIMEDOUT) {
                    LOG_CPP_ERROR("[RtpSender:%s] RTCP recvfrom error: %s",
                                config_.sink_id.c_str(), strerror(errno));
                }
                #endif
            }
        }
        
        // Small sleep to prevent busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    LOG_CPP_INFO("[RtpSender:%s] RTCP thread loop exited (loop_count=%d)",
                config_.sink_id.c_str(), loop_count);
}

void RtpSender::send_rtcp_sr() {
    LOG_CPP_INFO("[RtpSender:%s] send_rtcp_sr() called (socket_fd=%d)",
                config_.sink_id.c_str(), rtcp_socket_fd_);
    
    if (rtcp_socket_fd_ == PLATFORM_INVALID_SOCKET) {
        LOG_CPP_ERROR("[RtpSender:%s] Cannot send RTCP SR - socket is invalid!", config_.sink_id.c_str());
        return;
    }

    // RTCP Sender Report packet structure
    struct rtcp_sr {
        // RTCP header
        uint8_t version_p_rc;  // Version (2 bits), Padding (1 bit), Reception report count (5 bits)
        uint8_t packet_type;    // Packet type (SR = 200)
        uint16_t length;        // Length in 32-bit words minus one

        // Sender info
        uint32_t ssrc;              // Synchronization source identifier
        uint32_t ntp_timestamp_msw; // NTP timestamp, most significant word
        uint32_t ntp_timestamp_lsw; // NTP timestamp, least significant word
        uint32_t rtp_timestamp;     // RTP timestamp
        uint32_t packet_count;      // Sender's packet count
        uint32_t octet_count;       // Sender's octet count
    }
    #ifndef _WIN32
        __attribute__((packed))
    #endif
    ;

    struct rtcp_sr sr;
    memset(&sr, 0, sizeof(sr));

    // Fill RTCP header
    sr.version_p_rc = 0x80;  // Version = 2, Padding = 0, RC = 0
    sr.packet_type = 200;     // SR packet type
    sr.length = htons(6);     // Length = 6 (7 words - 1)

    // Fill sender info
    sr.ssrc = htonl(ssrc_);
    
    // Get NTP timestamp with delay
    uint64_t ntp_ts = get_ntp_timestamp_with_delay();
    // NTP timestamp needs to be in network byte order
    uint32_t ntp_seconds = (ntp_ts >> 32) & 0xFFFFFFFF;
    uint32_t ntp_fraction = ntp_ts & 0xFFFFFFFF;
    sr.ntp_timestamp_msw = htonl(ntp_seconds);
    sr.ntp_timestamp_lsw = htonl(ntp_fraction);
    
    // Use the current RTP timestamp - RTCP SR pairs wall clock time (NTP)
    // with media time (RTP timestamp) for synchronization
    sr.rtp_timestamp = htonl(rtp_timestamp_);
    
    // Add packet and octet counts
    uint32_t pkt_count = packet_count_.load();
    uint32_t oct_count = octet_count_.load();
    sr.packet_count = htonl(pkt_count);
    sr.octet_count = htonl(oct_count);
    
    LOG_CPP_DEBUG("[RtpSender:%s] RTCP SR packet prepared: SSRC=0x%08X, NTP_sec=%u, NTP_frac=%u, RTP_ts=%u, pkts=%u, octets=%u",
                 config_.sink_id.c_str(), ssrc_, ntp_seconds, ntp_fraction, rtp_timestamp_, pkt_count, oct_count);

    // Log packet details before sending
    LOG_CPP_INFO("[RtpSender:%s] Attempting to send RTCP SR packet (size=%zu bytes) to %s:%d",
                config_.sink_id.c_str(), sizeof(sr),
                config_.output_ip.c_str(), config_.output_port + 1);
    
    // Send the RTCP packet
    #ifdef _WIN32
        int sent_bytes = sendto(rtcp_socket_fd_,
                               (const char*)&sr,
                               sizeof(sr),
                               0,
                               (struct sockaddr *)&rtcp_dest_addr_,
                               sizeof(rtcp_dest_addr_));
    #else
        int sent_bytes = sendto(rtcp_socket_fd_,
                               &sr,
                               sizeof(sr),
                               0,
                               (struct sockaddr *)&rtcp_dest_addr_,
                               sizeof(rtcp_dest_addr_));
    #endif

    if (sent_bytes < 0) {
        int error_code = errno;
        LOG_CPP_ERROR("[RtpSender:%s] FAILED to send RTCP SR packet (errno=%d: %s)",
                     config_.sink_id.c_str(), error_code, strerror(error_code));
    } else if (static_cast<size_t>(sent_bytes) != sizeof(sr)) {
        LOG_CPP_WARNING("[RtpSender:%s] RTCP SR sent partial data: %d/%zu bytes",
                       config_.sink_id.c_str(), sent_bytes, sizeof(sr));
    } else {
        LOG_CPP_INFO("[RtpSender:%s] SUCCESS - Sent RTCP SR (%d bytes): NTP=0x%016llX, RTP=%u, packets=%u, octets=%u to %s:%d",
                    config_.sink_id.c_str(),
                    sent_bytes,
                    (unsigned long long)ntp_ts,
                    ntohl(sr.rtp_timestamp),
                    packet_count_.load(),
                    octet_count_.load(),
                    config_.output_ip.c_str(),
                    config_.output_port + 1);
    }
}

void RtpSender::process_incoming_rtcp(const uint8_t* data, size_t size,
                                      const struct sockaddr_in& sender_addr) {
    if (size < 4) {
        LOG_CPP_WARNING("[RtpSender:%s] RTCP packet too small: %zu bytes (minimum 4 required)",
                       config_.sink_id.c_str(), size);
        return;
    }
    
    char sender_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, INET_ADDRSTRLEN);
    uint16_t sender_port = ntohs(sender_addr.sin_port);
    
    LOG_CPP_INFO("[RtpSender:%s] Processing RTCP packet: size=%zu bytes from %s:%d",
                 config_.sink_id.c_str(), size, sender_ip, sender_port);
    
    // Log first few bytes for debugging
    if (size >= 4) {
        LOG_CPP_DEBUG("[RtpSender:%s] RTCP packet header bytes: %02X %02X %02X %02X",
                     config_.sink_id.c_str(), data[0], data[1], data[2], data[3]);
    }
    
    // Process compound RTCP packets
    size_t offset = 0;
    while (offset + 4 <= size) {
        // Parse RTCP header
        const uint8_t* packet = data + offset;
        uint8_t version = (packet[0] >> 6) & 0x03;
        uint8_t padding = (packet[0] >> 5) & 0x01;
        uint8_t count = packet[0] & 0x1F;
        uint8_t packet_type = packet[1];
        uint16_t length = ntohs(*reinterpret_cast<const uint16_t*>(packet + 2));
        
        // Validate version
        if (version != 2) {
            LOG_CPP_WARNING("[RtpSender:%s] Invalid RTCP version: %d",
                          config_.sink_id.c_str(), version);
            break;
        }
        
        // Calculate packet size in bytes (length is in 32-bit words minus one)
        size_t packet_size = (length + 1) * 4;
        
        if (offset + packet_size > size) {
            LOG_CPP_WARNING("[RtpSender:%s] RTCP packet size exceeds buffer: %zu > %zu",
                          config_.sink_id.c_str(), offset + packet_size, size);
            break;
        }
        
        // Process based on packet type
        LOG_CPP_INFO("[RtpSender:%s] RTCP packet type=%d, version=%d, padding=%d, count=%d, length=%d words",
                    config_.sink_id.c_str(), packet_type, version, padding, count, length + 1);
        
        switch (packet_type) {
            case 200: // SR (Sender Report)
                LOG_CPP_INFO("[RtpSender:%s] Received RTCP SR (Sender Report) from %s:%d",
                            config_.sink_id.c_str(), sender_ip, sender_port);
                // We typically don't process SR packets as a sender
                break;
                
            case 201: // RR (Receiver Report)
                LOG_CPP_INFO("[RtpSender:%s] Received RTCP RR (Receiver Report) from %s:%d",
                            config_.sink_id.c_str(), sender_ip, sender_port);
                process_rtcp_rr(packet, sender_addr);
                break;
                
            case 202: // SDES (Source Description)
                LOG_CPP_INFO("[RtpSender:%s] Received RTCP SDES (Source Description) from %s:%d",
                            config_.sink_id.c_str(), sender_ip, sender_port);
                process_rtcp_sdes(packet, sender_addr);
                break;
                
            case 203: // BYE
                LOG_CPP_INFO("[RtpSender:%s] Received RTCP BYE from %s:%d",
                            config_.sink_id.c_str(), sender_ip, sender_port);
                process_rtcp_bye(packet, sender_addr);
                break;
                
            case 204: // APP (Application-defined)
                LOG_CPP_INFO("[RtpSender:%s] Received RTCP APP (Application-defined) packet from %s:%d",
                            config_.sink_id.c_str(), sender_ip, sender_port);
                break;
                
            default:
                LOG_CPP_WARNING("[RtpSender:%s] Received unknown RTCP packet type %d from %s:%d",
                               config_.sink_id.c_str(), packet_type, sender_ip, sender_port);
                break;
        }
        
        offset += packet_size;
    }
}

void RtpSender::process_rtcp_rr(const void* rr, const struct sockaddr_in& sender_addr) {
    const uint8_t* packet = static_cast<const uint8_t*>(rr);
    
    // Parse RR header
    uint8_t count = packet[0] & 0x1F; // Number of reception report blocks
    uint16_t length = ntohs(*reinterpret_cast<const uint16_t*>(packet + 2));
    
    // Skip header (4 bytes) and get SSRC of packet sender
    uint32_t reporter_ssrc = ntohl(*reinterpret_cast<const uint32_t*>(packet + 4));
    
    char sender_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, INET_ADDRSTRLEN);
    
    LOG_CPP_INFO("[RtpSender:%s] Processing RTCP RR from SSRC 0x%08X (%s) with %d report blocks",
                config_.sink_id.c_str(), reporter_ssrc, sender_ip, count);
    
    // Process each reception report block
    const uint8_t* report_block = packet + 8; // Start of first report block
    
    for (uint8_t i = 0; i < count; i++) {
        if (report_block + 24 > packet + (length + 1) * 4) {
            LOG_CPP_WARNING("[RtpSender:%s] RR packet truncated", config_.sink_id.c_str());
            break;
        }
        
        // Parse report block
        uint32_t source_ssrc = ntohl(*reinterpret_cast<const uint32_t*>(report_block));
        uint8_t fraction_lost = report_block[4];
        uint32_t cumulative_lost = (report_block[5] << 16) | (report_block[6] << 8) | report_block[7];
        uint32_t extended_seq = ntohl(*reinterpret_cast<const uint32_t*>(report_block + 8));
        uint32_t jitter = ntohl(*reinterpret_cast<const uint32_t*>(report_block + 12));
        uint32_t lsr = ntohl(*reinterpret_cast<const uint32_t*>(report_block + 16));
        uint32_t dlsr = ntohl(*reinterpret_cast<const uint32_t*>(report_block + 20));
        
        // Check if this report is about our stream
        if (source_ssrc == ssrc_) {
            float fraction_lost_pct = (fraction_lost / 255.0f) * 100.0f;
            
            LOG_CPP_INFO("[RtpSender:%s] RR for our stream (SSRC 0x%08X): "
                        "fraction_lost=%.1f%%, cumulative_lost=%u, jitter=%u, seq=%u",
                        config_.sink_id.c_str(), ssrc_,
                        fraction_lost_pct, cumulative_lost, jitter, extended_seq);
            
            // Calculate RTT if we have sent an SR before
            if (lsr != 0 && dlsr != 0) {
                // Get current NTP timestamp
                uint64_t now_ntp = get_ntp_timestamp_with_delay();
                uint32_t now_ntp_middle = (now_ntp >> 16) & 0xFFFFFFFF;
                
                // RTT = now - lsr - dlsr (in units of 1/65536 seconds)
                if (now_ntp_middle > lsr + dlsr) {
                    uint32_t rtt_units = now_ntp_middle - lsr - dlsr;
                    float rtt_ms = (rtt_units / 65.536f); // Convert to milliseconds
                    
                    LOG_CPP_INFO("[RtpSender:%s] Calculated RTT: %.2f ms",
                               config_.sink_id.c_str(), rtt_ms);
                }
            }
        }
        
        report_block += 24; // Move to next report block
    }
}

void RtpSender::process_rtcp_sdes(const void* sdes, const struct sockaddr_in& sender_addr) {
    const uint8_t* packet = static_cast<const uint8_t*>(sdes);
    
    // Parse SDES header
    uint8_t source_count = packet[0] & 0x1F;
    uint16_t length = ntohs(*reinterpret_cast<const uint16_t*>(packet + 2));
    
    char sender_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, INET_ADDRSTRLEN);
    
    LOG_CPP_DEBUG("[RtpSender:%s] Processing RTCP SDES from %s with %d sources",
                 config_.sink_id.c_str(), sender_ip, source_count);
    
    const uint8_t* chunk = packet + 4; // Start of first SSRC/CSRC chunk
    const uint8_t* packet_end = packet + (length + 1) * 4;
    
    for (uint8_t i = 0; i < source_count && chunk < packet_end; i++) {
        if (chunk + 4 > packet_end) break;
        
        uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(chunk));
        chunk += 4;
        
        // Process SDES items for this source
        while (chunk < packet_end && *chunk != 0) { // 0 = END item
            uint8_t item_type = chunk[0];
            uint8_t item_length = chunk[1];
            
            if (chunk + 2 + item_length > packet_end) break;
            
            std::string item_value(reinterpret_cast<const char*>(chunk + 2), item_length);
            
            switch (item_type) {
                case 1: // CNAME (Canonical name)
                    LOG_CPP_INFO("[RtpSender:%s] SDES CNAME for SSRC 0x%08X: %s",
                               config_.sink_id.c_str(), ssrc, item_value.c_str());
                    break;
                case 2: // NAME (User name)
                    LOG_CPP_DEBUG("[RtpSender:%s] SDES NAME for SSRC 0x%08X: %s",
                                config_.sink_id.c_str(), ssrc, item_value.c_str());
                    break;
                case 3: // EMAIL
                    LOG_CPP_DEBUG("[RtpSender:%s] SDES EMAIL for SSRC 0x%08X: %s",
                                config_.sink_id.c_str(), ssrc, item_value.c_str());
                    break;
                case 4: // PHONE
                    LOG_CPP_DEBUG("[RtpSender:%s] SDES PHONE for SSRC 0x%08X: %s",
                                config_.sink_id.c_str(), ssrc, item_value.c_str());
                    break;
                case 5: // LOC (Location)
                    LOG_CPP_DEBUG("[RtpSender:%s] SDES LOC for SSRC 0x%08X: %s",
                                config_.sink_id.c_str(), ssrc, item_value.c_str());
                    break;
                case 6: // TOOL (Application/tool name)
                    LOG_CPP_DEBUG("[RtpSender:%s] SDES TOOL for SSRC 0x%08X: %s",
                                config_.sink_id.c_str(), ssrc, item_value.c_str());
                    break;
                case 7: // NOTE
                    LOG_CPP_DEBUG("[RtpSender:%s] SDES NOTE for SSRC 0x%08X: %s",
                                config_.sink_id.c_str(), ssrc, item_value.c_str());
                    break;
                default:
                    LOG_CPP_DEBUG("[RtpSender:%s] SDES unknown item type %d for SSRC 0x%08X",
                                config_.sink_id.c_str(), item_type, ssrc);
                    break;
            }
            
            chunk += 2 + item_length;
        }
        
        // Skip to next 32-bit boundary
        while ((chunk - packet) % 4 != 0 && chunk < packet_end) {
            chunk++;
        }
    }
}

void RtpSender::process_rtcp_bye(const void* bye, const struct sockaddr_in& sender_addr) {
    const uint8_t* packet = static_cast<const uint8_t*>(bye);
    
    // Parse BYE header
    uint8_t source_count = packet[0] & 0x1F;
    uint16_t length = ntohs(*reinterpret_cast<const uint16_t*>(packet + 2));
    
    char sender_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, INET_ADDRSTRLEN);
    
    LOG_CPP_INFO("[RtpSender:%s] Processing RTCP BYE from %s with %d sources",
                config_.sink_id.c_str(), sender_ip, source_count);
    
    // Process SSRC/CSRC identifiers
    const uint8_t* ssrc_ptr = packet + 4;
    for (uint8_t i = 0; i < source_count; i++) {
        if (ssrc_ptr + 4 > packet + (length + 1) * 4) break;
        
        uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(ssrc_ptr));
        LOG_CPP_INFO("[RtpSender:%s] Receiver with SSRC 0x%08X is leaving",
                    config_.sink_id.c_str(), ssrc);
        ssrc_ptr += 4;
    }
    
    // Check for optional reason string
    size_t ssrc_bytes = source_count * 4;
    const uint8_t* reason_ptr = packet + 4 + ssrc_bytes;
    
    if (reason_ptr < packet + (length + 1) * 4) {
        uint8_t reason_length = *reason_ptr;
        if (reason_ptr + 1 + reason_length <= packet + (length + 1) * 4) {
            std::string reason(reinterpret_cast<const char*>(reason_ptr + 1), reason_length);
            LOG_CPP_INFO("[RtpSender:%s] BYE reason: %s",
                        config_.sink_id.c_str(), reason.c_str());
        }
    }
}



} // namespace audio
} // namespace screamrouter
