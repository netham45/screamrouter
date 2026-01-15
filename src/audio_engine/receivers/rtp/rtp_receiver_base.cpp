#include "rtp_receiver_base.h"

#include "../../audio_channel_layout.h"
#include "../../configuration/audio_engine_settings.h"
#include "../../input_processor/timeshift_manager.h"
#include "../../utils/cpp_logger.h"
#include "../../utils/sentinel_logging.h"
#include "rtp_receiver_utils.h"
#include "rtp_payload_defaults.h"

#include <rtc/rtp.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <cerrno>

#ifndef _WIN32
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/epoll.h>
    #include <sys/socket.h>
    #include <unistd.h>
#else
    #include <BaseTsd.h>
    #include <ws2tcpip.h>
    #ifndef ssize_t
        typedef SSIZE_T ssize_t;
    #endif
#endif

namespace screamrouter {
namespace audio {

RtpReceiverBase::RtpReceiverBase(
    RtpReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager)
    : NetworkAudioReceiver(config.listen_port,
                           notification_queue,
                           timeshift_manager,
                           "[RtpReceiver]",
                           resolve_base_frames_per_chunk(timeshift_manager ? timeshift_manager->get_settings() : nullptr)),
      config_(std::move(config)),
      chunk_size_bytes_(resolve_chunk_size_bytes(timeshift_manager ? timeshift_manager->get_settings() : nullptr))
#ifdef _WIN32
    , max_fd_(NAR_INVALID_SOCKET_VALUE)
#else
    , epoll_fd_(NAR_INVALID_SOCKET_VALUE)
#endif
{
#ifdef _WIN32
    FD_ZERO(&master_read_fds_);
#endif

    sap_listener_ = std::make_unique<SapListener>("[RtpReceiver-SAP]", config_.known_ips);
    if (sap_listener_) {
        sap_listener_->set_session_callback([this](const std::string& ip, int port, const std::string& source_ip) {
            this->open_dynamic_session(ip, port, source_ip);
        });
    }
}

RtpReceiverBase::~RtpReceiverBase() noexcept = default;

std::vector<SapAnnouncement> RtpReceiverBase::get_sap_announcements() {
    if (sap_listener_) {
        return sap_listener_->get_announcements();
    }
    return {};
}

void RtpReceiverBase::set_format_probe_duration_ms(double duration_ms) {
    format_probe_duration_ms_ = duration_ms;
    // Also update any existing probes
    std::lock_guard<std::mutex> lock(format_probes_mutex_);
    for (auto& [ssrc, probe] : format_probes_) {
        if (probe) {
            probe->set_probe_duration_ms(duration_ms);
        }
    }
}

void RtpReceiverBase::set_format_probe_min_bytes(size_t min_bytes) {
    format_probe_min_bytes_ = min_bytes;
    // Also update any existing probes
    std::lock_guard<std::mutex> lock(format_probes_mutex_);
    for (auto& [ssrc, probe] : format_probes_) {
        if (probe) {
            probe->set_probe_min_bytes(min_bytes);
        }
    }
}

std::string RtpReceiverBase::get_source_key(const struct sockaddr_in& addr) const {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), ip_str, INET_ADDRSTRLEN);
    return std::string(ip_str) + ":" + std::to_string(ntohs(addr.sin_port));
}

void RtpReceiverBase::handle_ssrc_changed(uint32_t old_ssrc, uint32_t new_ssrc, const std::string& source_key) {
    char old_ssrc_hex[12];
    char new_ssrc_hex[12];
    snprintf(old_ssrc_hex, sizeof(old_ssrc_hex), "0x%08X", old_ssrc);
    snprintf(new_ssrc_hex, sizeof(new_ssrc_hex), "0x%08X", new_ssrc);

    log_message("SSRC changed for source " + source_key +
                ". Old SSRC: " + std::string(old_ssrc_hex) +
                ", New SSRC: " + std::string(new_ssrc_hex) + ". Clearing state for old SSRC.");

    {
        std::lock_guard<std::mutex> lock(reordering_buffer_mutex_);
        if (reordering_buffers_.count(old_ssrc)) {
            reordering_buffers_.at(old_ssrc).reset();
        }
    }
    {
        std::lock_guard<std::mutex> lock(ssrc_addr_mutex_);
        ssrc_last_addr_.erase(old_ssrc);
    }
    {
        std::lock_guard<std::mutex> lock(format_probes_mutex_);
        format_probes_.erase(old_ssrc);
    }
    {
        std::lock_guard<std::mutex> lock(detected_formats_mutex_);
        detected_formats_.erase(old_ssrc);
    }

    for (auto& receiver : payload_receivers_) {
        receiver->on_ssrc_state_cleared(old_ssrc);
    }

    log_message("State for SSRC " + std::string(old_ssrc_hex) + " cleared due to SSRC change.");
}

bool RtpReceiverBase::setup_socket() {
    log_message("Setting up raw UDP sockets for RTP reception...");

#ifdef _WIN32
    if (max_fd_ != NAR_INVALID_SOCKET_VALUE) {
        log_warning("setup_socket called but sockets already initialized. Closing existing sockets first.");
        close_socket();
    }
    FD_ZERO(&master_read_fds_);
    max_fd_ = NAR_INVALID_SOCKET_VALUE;
#else
    if (epoll_fd_ != NAR_INVALID_SOCKET_VALUE) {
        log_warning("setup_socket called but epoll_fd_ is already valid. Closing existing sockets first.");
        close_socket();
    }

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        log_error("Failed to create epoll file descriptor: " + std::string(strerror(errno)));
        return false;
    }
#endif

    const int default_port = config_.listen_port <= 0 ? 40000 : config_.listen_port;
    open_dynamic_session("0.0.0.0", default_port, "");

    if (socket_fds_.empty()) {
        log_error("Failed to bind the default UDP socket on port " + std::to_string(default_port));
#ifndef _WIN32
        if (epoll_fd_ != NAR_INVALID_SOCKET_VALUE) {
            close(epoll_fd_);
            epoll_fd_ = NAR_INVALID_SOCKET_VALUE;
        }
#endif
        return false;
    }

    log_message("RTP receiver is listening for SAP announcements for dynamic ports.");

    if (sap_listener_) {
        sap_listener_->start();
    }

    return true;
}

void RtpReceiverBase::close_socket() {
    if (sap_listener_) {
        sap_listener_->stop();
    }

    {
        std::lock_guard<std::mutex> lock(reordering_buffer_mutex_);
        reordering_buffers_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(source_ssrc_mutex_);
        source_to_last_ssrc_.clear();
    }

    for (auto& receiver : payload_receivers_) {
        receiver->on_all_ssrcs_cleared();
    }

    std::lock_guard<std::mutex> lock(socket_fds_mutex_);
#ifdef _WIN32
    FD_ZERO(&master_read_fds_);
    max_fd_ = NAR_INVALID_SOCKET_VALUE;
#else
    if (epoll_fd_ != NAR_INVALID_SOCKET_VALUE) {
        log_message("Closing epoll file descriptor (fd: " + std::to_string(epoll_fd_) + ")");
        close(epoll_fd_);
        epoll_fd_ = NAR_INVALID_SOCKET_VALUE;
    }
#endif
    for (socket_t sock_fd : socket_fds_) {
        log_message("Closing raw UDP socket (fd: " + std::to_string(sock_fd) + ")");
#ifdef _WIN32
        closesocket(sock_fd);
#else
        close(sock_fd);
#endif
    }
    socket_fds_.clear();
    socket_sessions_.clear();
    unicast_source_to_socket_.clear();
    log_message("All raw UDP socket resources released.");
}

void RtpReceiverBase::run() {
#ifdef _WIN32
    log_message("RTP receiver thread started using select and libdatachannel parser.");
#else
    log_message("RTP receiver thread started using epoll and libdatachannel parser.");
#endif

#ifdef _WIN32
    if (socket_fds_.empty()) {
        log_error("Sockets are not initialized. Thread cannot run.");
        return;
    }
#else
    if (epoll_fd_ == NAR_INVALID_SOCKET_VALUE || socket_fds_.empty()) {
        log_error("Sockets are not initialized. Thread cannot run.");
        return;
    }
#endif

    unsigned char raw_buffer[kRawReceiveBufferSize];
    struct sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);

#ifndef _WIN32
    const int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];
#endif

    while (is_running()) {

#ifdef _WIN32
        fd_set read_fds = master_read_fds_;
        struct timeval tv;
        tv.tv_sec = get_poll_timeout_ms() / 1000;
        tv.tv_usec = (get_poll_timeout_ms() % 1000) * 1000;

        int n_events = select(max_fd_ + 1, &read_fds, NULL, NULL, &tv);
#else
        int n_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, get_poll_timeout_ms());
#endif

        if (!is_running()) {
            break;
        }

        if (n_events < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) {
                continue;
            }
            log_error("select() error: " + std::to_string(WSAGetLastError()));
#else
            if (errno == EINTR) {
                continue;
            }
            log_error("epoll_wait() error: " + std::string(strerror(errno)));
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (n_events == 0) {
            std::lock_guard<std::mutex> lock(reordering_buffer_mutex_);
            for (auto const& [ssrc, _] : reordering_buffers_) {
                (void)_;
                struct sockaddr_in addr_copy{};
                {
                    std::lock_guard<std::mutex> addr_lock(ssrc_addr_mutex_);
                    auto it = ssrc_last_addr_.find(ssrc);
                    if (it != ssrc_last_addr_.end()) {
                        addr_copy = it->second;
                    }
                }
                if (addr_copy.sin_family == AF_INET) {
                    process_ready_packets_internal(ssrc, addr_copy, false);
                }
            }
            continue;
        }

#ifdef _WIN32
        std::lock_guard<std::mutex> lock(socket_fds_mutex_);
        for (socket_t current_socket_fd : socket_fds_) {
            if (!FD_ISSET(current_socket_fd, &read_fds)) {
                continue;
            }
            ssize_t n_received = recvfrom(current_socket_fd,
                                          reinterpret_cast<char*>(raw_buffer),
                                          static_cast<int>(kRawReceiveBufferSize),
                                          0,
                                          (struct sockaddr *)&cliaddr,
                                          &len);
#else
        for (int i = 0; i < n_events; ++i) {
            if (!(events[i].events & EPOLLIN)) {
                continue;
            }
            socket_t current_socket_fd = events[i].data.fd;
            ssize_t n_received = recvfrom(current_socket_fd,
                                          raw_buffer,
                                          kRawReceiveBufferSize,
                                          0,
                                          (struct sockaddr *)&cliaddr,
                                          &len);
#endif

            if (!is_running()) {
                break;
            }

            if (n_received < 0) {
                if (NAR_GET_LAST_SOCK_ERROR != EAGAIN && NAR_GET_LAST_SOCK_ERROR != EWOULDBLOCK) {
                    log_error("recvfrom() error: " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR)));
                }
                continue;
            }

            if (n_received == 0) {
                log_warning("recvfrom() returned 0 bytes.");
                continue;
            }

            auto received_time = std::chrono::steady_clock::now();
            const bool is_loopback =
                (cliaddr.sin_family == AF_INET && ntohl(cliaddr.sin_addr.s_addr) == INADDR_LOOPBACK);

            if (static_cast<size_t>(n_received) < sizeof(rtc::RtpHeader)) {
                if (is_loopback) {
                    LOG_CPP_INFO("[RtpReceiver] Loopback packet dropped before RTP parse (size=%zd bytes).", n_received);
                }
                log_warning("Received packet too small to be an RTP packet (" + std::to_string(n_received) + " bytes).");
                continue;
            }

            const rtc::RtpHeader* rtp_header = reinterpret_cast<const rtc::RtpHeader*>(raw_buffer);
            if (is_loopback) {
                LOG_CPP_INFO("[RtpReceiver] Loopback recv seq=%u ssrc=0x%08X len=%zd",
                             rtp_header->seqNumber(),
                             rtp_header->ssrc(),
                             n_received);
            }
            uint8_t pt = rtp_header->payloadType();
            uint32_t current_ssrc = rtp_header->ssrc();

            if (!supports_payload_type(pt, current_ssrc)) {
                if (is_loopback) {
                    LOG_CPP_INFO("[RtpReceiver] Loopback packet seq=%u filtered due to unsupported payload %u",
                                 rtp_header->seqNumber(),
                                 pt);
                }
                continue;
            }

            std::string source_key = get_source_key(cliaddr);
            {
                std::lock_guard<std::mutex> lock(source_ssrc_mutex_);
                auto it = source_to_last_ssrc_.find(source_key);
                if (it == source_to_last_ssrc_.end()) {
                    source_to_last_ssrc_[source_key] = current_ssrc;
                    char ssrc_hex[12];
                    snprintf(ssrc_hex, sizeof(ssrc_hex), "0x%08X", current_ssrc);
                    log_message("New RTP source detected: " + source_key + " with SSRC " + std::string(ssrc_hex));
                } else if (it->second != current_ssrc) {
                    uint32_t old_ssrc = it->second;
                    handle_ssrc_changed(old_ssrc, current_ssrc, source_key);
                    it->second = current_ssrc;
                }
            }
            {
                std::lock_guard<std::mutex> lock(ssrc_addr_mutex_);
                ssrc_last_addr_[current_ssrc] = cliaddr;
            }

            RtpPacketData packet_data;
            packet_data.sequence_number = rtp_header->seqNumber();
            packet_data.rtp_timestamp = rtp_header->timestamp();
            packet_data.received_time = received_time;
            packet_data.ssrc = current_ssrc;
            packet_data.payload_type = pt;
            packet_data.ingress_from_loopback = is_loopback;

            size_t header_len = 12 + (rtp_header->csrcCount() * sizeof(uint32_t));
            if (static_cast<size_t>(n_received) < header_len) {
                if (is_loopback) {
                    LOG_CPP_INFO("[RtpReceiver] Loopback packet seq=%u dropped due to truncated header (expected=%zu, actual=%zd)",
                                 rtp_header->seqNumber(),
                                 header_len,
                                 n_received);
                }
                log_warning("Received RTP packet smaller than its own header length. SSRC: 0x" + std::to_string(current_ssrc));
                continue;
            }

            const uint8_t* payload_data = raw_buffer + header_len;
            size_t payload_len = n_received - header_len;
            if (payload_len > 0) {
                packet_data.payload.assign(payload_data, payload_data + payload_len);
            }

            const uint8_t csrc_count = rtp_header->csrcCount();
            if (csrc_count > 0) {
                const uint8_t* csrc_ptr = raw_buffer + 12;
                for (uint8_t c = 0; c < csrc_count; c++) {
                    uint32_t csrc;
                    std::memcpy(&csrc, csrc_ptr, sizeof(uint32_t));
                    packet_data.csrcs.push_back(ntohl(csrc));
                    csrc_ptr += sizeof(uint32_t);
                }
            }

            {
                std::lock_guard<std::mutex> lock(reordering_buffer_mutex_);
                bool is_new_buffer = (reordering_buffers_.find(current_ssrc) == reordering_buffers_.end());
                if (is_new_buffer) {
                    char ssrc_hex[12];
                    snprintf(ssrc_hex, sizeof(ssrc_hex), "0x%08X", current_ssrc);
                    char client_ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(cliaddr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
                    log_message("Creating new reordering buffer for SSRC " + std::string(ssrc_hex) +
                                " from " + std::string(client_ip_str) + ":" + std::to_string(ntohs(cliaddr.sin_port)));
                }
                reordering_buffers_[current_ssrc].add_packet(std::move(packet_data));
            }

            process_ready_packets(current_ssrc, cliaddr);

#ifndef _WIN32
        }
#else
        }
#endif
        maybe_log_telemetry();
    }
    log_message("RTP receiver thread finished.");
}

void RtpReceiverBase::open_dynamic_session(const std::string& ip, int port, const std::string& source_ip) {
    if (port <= 0 || port > 65535) {
        log_warning("Invalid port number received: " + std::to_string(port));
        return;
    }

    std::lock_guard<std::mutex> lock(socket_fds_mutex_);

    for (socket_t existing_fd : socket_fds_) {
        struct sockaddr_in addr{};
        socklen_t addr_len = sizeof(addr);
        if (getsockname(existing_fd, (struct sockaddr *)&addr, &addr_len) == 0) {
            char existing_ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(addr.sin_addr), existing_ip_str, INET_ADDRSTRLEN);
            if (ntohs(addr.sin_port) == port && ip == existing_ip_str) {
                return;
            }
        }
    }

    log_message("Opening new dynamic RTP session on " + ip + ":" + std::to_string(port));

    socket_t sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd == NAR_INVALID_SOCKET_VALUE) {
        log_warning("Failed to create UDP socket for " + ip + ":" + std::to_string(port) + ": " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR)));
        return;
    }

    int optval = 1;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&optval), sizeof(optval)) < 0) {
        log_warning("Failed to set SO_REUSEADDR for " + ip + ":" + std::to_string(port) + ": " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR)));
    }

    const auto desired_buffer_bytes = std::min<std::size_t>(chunk_size_bytes_ * 4000ULL,
                                                            static_cast<std::size_t>(std::numeric_limits<int>::max()));
    const int recv_buf_size = static_cast<int>(desired_buffer_bytes);
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recv_buf_size), sizeof(recv_buf_size)) < 0) {
        log_warning("Failed to set SO_RCVBUF for " + ip + ":" + std::to_string(port) + ": " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR)));
    }

    struct sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr) <= 0) {
        log_error("Invalid IP address string: " + ip);
#ifdef _WIN32
        closesocket(sock_fd);
#else
        close(sock_fd);
#endif
        return;
    }

    if (bind(sock_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        log_message("Could not bind to " + ip + ":" + std::to_string(port) + ": " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR)));
#ifdef _WIN32
        closesocket(sock_fd);
#else
        close(sock_fd);
#endif
        return;
    }

#ifdef _WIN32
    FD_SET(sock_fd, &master_read_fds_);
    if (sock_fd > max_fd_) {
        max_fd_ = sock_fd;
    }
    socket_fds_.push_back(sock_fd);
    log_message("Successfully bound and added new socket for " + ip + ":" + std::to_string(port) + " to select.");
#else
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = sock_fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock_fd, &event) == -1) {
        log_error("Failed to add socket for " + ip + ":" + std::to_string(port) + " to epoll: " + std::string(strerror(errno)));
        close(sock_fd);
        return;
    }

    socket_fds_.push_back(sock_fd);
    log_message("Successfully bound and added new socket for " + ip + ":" + std::to_string(port) + " to epoll.");
#endif

    if (!source_ip.empty()) {
        std::string session_key = source_ip + ":" + ip + ":" + std::to_string(port);
        unicast_source_to_socket_[session_key] = sock_fd;
    }
}

void RtpReceiverBase::process_ready_packets(uint32_t ssrc, const struct sockaddr_in& client_addr) {
    process_ready_packets_internal(ssrc, client_addr, true);
}

bool RtpReceiverBase::resolve_stream_properties(
    uint32_t ssrc,
    const struct sockaddr_in& client_addr,
    uint8_t payload_type,
    StreamProperties& out_properties) const {
    const int packet_port = ntohs(client_addr.sin_port);
    const int listen_port = config_.listen_port <= 0 ? 40000 : config_.listen_port;
    const uint8_t canonical_payload_type = canonicalize_payload_type(payload_type, ssrc);

    if (sap_listener_) {
        if (sap_listener_->get_stream_properties(ssrc, out_properties)) {
            return true;
        }

        char client_ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
        if (sap_listener_->get_stream_properties_by_ip(client_ip_str, packet_port, out_properties)) {
            return true;
        }
        std::string sap_tagged_key = std::string(client_ip_str) + ":" + std::to_string(packet_port) + "#sap-" + std::to_string(packet_port);
        if (sap_listener_->get_stream_properties_by_ip(sap_tagged_key, packet_port, out_properties)) {
            return true;
        }
    }

    if (listen_port != 40000) {
        return false;
    }

    return populate_stream_properties_from_payload(
        payload_type, canonical_payload_type, listen_port, out_properties);
}

uint8_t RtpReceiverBase::canonicalize_payload_type(
    uint8_t payload_type,
    uint32_t ssrc,
    const StreamProperties* props_override) const {
    const StreamProperties* effective_props = props_override;
    StreamProperties temp_props;
    if (!effective_props && sap_listener_) {
        if (sap_listener_->get_stream_properties(ssrc, temp_props)) {
            effective_props = &temp_props;
        }
    }

    if (effective_props &&
        effective_props->payload_type >= 0 &&
        payload_type == static_cast<uint8_t>(effective_props->payload_type)) {
        if (effective_props->codec == StreamCodec::OPUS) {
            return kRtpPayloadTypeOpus;
        }
        if (effective_props->codec == StreamCodec::PCM) {
            return kRtpPayloadTypeL16Stereo;
        }
        if (effective_props->codec == StreamCodec::PCMU) {
            return kRtpPayloadTypePcmu;
        }
        if (effective_props->codec == StreamCodec::PCMA) {
            return kRtpPayloadTypePcma;
        }
    }

    if (payload_type == 10 || payload_type == 11) {
        return kRtpPayloadTypeL16Stereo;
    }

    return payload_type;
}

void RtpReceiverBase::register_payload_receiver(std::unique_ptr<RtpPayloadReceiver> receiver) {
    if (receiver) {
        payload_receivers_.push_back(std::move(receiver));
    }
}

bool RtpReceiverBase::supports_payload_type(uint8_t payload_type, uint32_t ssrc) const {
    const uint8_t canonical_payload_type = canonicalize_payload_type(payload_type, ssrc);
    if (find_handler_for_payload(canonical_payload_type) != nullptr) {
        return true;
    }
    
    // Accept unknown dynamic payload types (96-127) on port 40000 for format probing
    const int listen_port = config_.listen_port <= 0 ? 40000 : config_.listen_port;
    if (listen_port == 40000 && payload_type >= 96 && payload_type <= 127) {
        // Route through PCM handler for format detection
        return find_handler_for_payload(kRtpPayloadTypeL16Stereo) != nullptr;
    }
    
    return false;
}

RtpPayloadReceiver* RtpReceiverBase::find_handler_for_payload(uint8_t canonical_payload_type) const {
    for (const auto& receiver : payload_receivers_) {
        if (receiver && receiver->supports_payload_type(canonical_payload_type)) {
            return receiver.get();
        }
    }
    return nullptr;
}

void RtpReceiverBase::process_ready_packets_internal(uint32_t ssrc, const struct sockaddr_in& client_addr, bool take_lock) {
    std::unique_ptr<std::lock_guard<std::mutex>> lock_ptr;
    if (take_lock) {
        lock_ptr = std::make_unique<std::lock_guard<std::mutex>>(reordering_buffer_mutex_);
    }

    auto buffer_it = reordering_buffers_.find(ssrc);
    if (buffer_it == reordering_buffers_.end()) {
        return;
    }

    auto ready_packets = buffer_it->second.get_ready_packets();
    if (ready_packets.empty()) {
        return;
    }

    if (ready_packets.size() > 1) {
        char ssrc_hex[12];
        snprintf(ssrc_hex, sizeof(ssrc_hex), "0x%08X", ssrc);
        log_message("Processing " + std::to_string(ready_packets.size()) +
                    " ready packets for SSRC " + std::string(ssrc_hex) + " after reordering/recovery");
    }

    const uint8_t payload_type = ready_packets.front().payload_type;
    const uint8_t canonical_payload_type = canonicalize_payload_type(payload_type, ssrc);

    StreamProperties props{};
    const bool has_properties = resolve_stream_properties(ssrc, client_addr, payload_type, props);
    if (!has_properties) {
        const int listen_port = config_.listen_port <= 0 ? 40000 : config_.listen_port;
        if (listen_port == 40000) {
            char fallback_ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(client_addr.sin_addr), fallback_ip_str, INET_ADDRSTRLEN);
            const int fallback_port = ntohs(client_addr.sin_port);
            std::string fallback_tag = std::string(fallback_ip_str) + ":" + std::to_string(fallback_port);
            register_source_tag(fallback_tag);

            if (populate_stream_properties_from_payload(
                    payload_type, canonical_payload_type, listen_port, props)) {
                LOG_CPP_DEBUG("[RtpReceiver] Applying default payload mapping for SSRC 0x%08X on port 40000", ssrc);
            } else {
                // Check for cached detected format first
                {
                    std::lock_guard<std::mutex> fmt_lock(detected_formats_mutex_);
                    auto fmt_it = detected_formats_.find(ssrc);
                    if (fmt_it != detected_formats_.end()) {
                        props = fmt_it->second;
                        props.port = listen_port;
                        LOG_CPP_DEBUG("[RtpReceiver] Using cached auto-detected format for SSRC 0x%08X: %dHz %dch %dbit",
                                      ssrc, props.sample_rate, props.channels, props.bit_depth);
                        goto format_resolved;
                    }
                }

                // Get or create probe for this SSRC
                AudioFormatProbe* probe = nullptr;
                {
                    std::lock_guard<std::mutex> probe_lock(format_probes_mutex_);
                    auto& probe_ptr = format_probes_[ssrc];
                    if (!probe_ptr) {
                        probe_ptr = std::make_unique<AudioFormatProbe>();
                        probe_ptr->set_probe_duration_ms(format_probe_duration_ms_);
                        probe_ptr->set_probe_min_bytes(format_probe_min_bytes_);
                        char ssrc_hex[12];
                        snprintf(ssrc_hex, sizeof(ssrc_hex), "0x%08X", ssrc);
                        LOG_CPP_INFO("[RtpReceiver] Starting format auto-detection for SSRC %s (duration: %.0fms, min_bytes: %zu)", ssrc_hex, format_probe_duration_ms_, format_probe_min_bytes_);
                    }
                    probe = probe_ptr.get();
                }

                // Feed all ready packets to the probe
                for (const auto& packet : ready_packets) {
                    if (!packet.payload.empty()) {
                        probe->add_data(packet.payload, packet.received_time);
                    }
                }

                // Check if detection can finalize
                if (probe->has_sufficient_data() && probe->finalize_detection()) {
                    const StreamProperties& detected = probe->get_detected_format();
                    float confidence = probe->get_confidence();

                    const char* codec_str = "PCM";
                    if (detected.codec == StreamCodec::PCMU) codec_str = "PCMU";
                    else if (detected.codec == StreamCodec::PCMA) codec_str = "PCMA";
                    else if (detected.codec == StreamCodec::OPUS) codec_str = "OPUS";

                    LOG_CPP_INFO("[RtpReceiver] Auto-detected format for SSRC 0x%08X: %s %dHz %dch %dbit %s (confidence: %.1f%%)",
                                 ssrc, codec_str, detected.sample_rate, detected.channels, detected.bit_depth,
                                 detected.endianness == Endianness::BIG ? "BE" : "LE",
                                 confidence * 100.0f);

                    // Cache the detected format
                    {
                        std::lock_guard<std::mutex> fmt_lock(detected_formats_mutex_);
                        detected_formats_[ssrc] = detected;
                    }

                    // Clean up probe since detection is complete
                    {
                        std::lock_guard<std::mutex> probe_lock(format_probes_mutex_);
                        format_probes_.erase(ssrc);
                    }

                    props = detected;
                    props.port = listen_port;
                    props.payload_type = payload_type;
                } else {
                    // Still probing - don't process packets yet
                    char ssrc_hex[12];
                    snprintf(ssrc_hex, sizeof(ssrc_hex), "0x%08X", ssrc);
                    LOG_CPP_DEBUG("[RtpReceiver] Still probing format for SSRC %s (buffered %zu bytes)",
                                  ssrc_hex, probe->has_sufficient_data() ? 0 : 1);
                    return;
                }
            }
        } else {
            char ssrc_hex[12];
            snprintf(ssrc_hex, sizeof(ssrc_hex), "0x%08X", ssrc);
            LOG_CPP_DEBUG(("Ignoring ready packets for unknown SSRC: " + std::string(ssrc_hex) +
                           " - no SAP properties found").c_str());
            return;
        }
    }
    format_resolved:

    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
    const int announced_port = (props.port > 0) ? props.port : ntohs(client_addr.sin_port);
    std::string source_tag = client_ip_str;
    if (sap_listener_) {
        std::string sap_guid;
        std::string sap_session;
        std::string sap_stream_ip;
        int sap_stream_port = 0;
        if (!sap_listener_->get_stream_identity_by_ssrc(ssrc, sap_guid, sap_session, sap_stream_ip, sap_stream_port)) {
            sap_listener_->get_stream_identity(client_ip_str, announced_port, sap_guid, sap_session, sap_stream_ip, sap_stream_port);
        }

        if (!sap_guid.empty()) {
            const std::string port_part = std::to_string(sap_stream_port > 0 ? sap_stream_port : announced_port);
            const std::string ip_part = !sap_stream_ip.empty() ? sap_stream_ip : client_ip_str;
            source_tag = "rtp:" + sap_guid + "#" + ip_part + "." + port_part;
        } else if (!sap_session.empty()) {
            const auto sanitized = sanitize_tag(sap_session);
            if (!sanitized.empty()) {
                const std::string port_part = std::to_string(sap_stream_port > 0 ? sap_stream_port : announced_port);
                const std::string ip_part = !sap_stream_ip.empty() ? sap_stream_ip : client_ip_str;
                source_tag = "rtp:" + sanitized + "#" + ip_part + "." + port_part;
            }
        }
    }

    for (auto& packet_data : ready_packets) {
        const uint8_t packet_canonical_type = canonicalize_payload_type(packet_data.payload_type, packet_data.ssrc, &props);
        RtpPayloadReceiver* handler = find_handler_for_payload(packet_canonical_type);

        if (!handler) {
            if (props.codec == StreamCodec::OPUS) {
                handler = find_handler_for_payload(kRtpPayloadTypeOpus);
            } else if (props.codec == StreamCodec::PCM || props.codec == StreamCodec::UNKNOWN) {
                handler = find_handler_for_payload(kRtpPayloadTypeL16Stereo);
            } else if (props.codec == StreamCodec::PCMU) {
                handler = find_handler_for_payload(kRtpPayloadTypePcmu);
            } else if (props.codec == StreamCodec::PCMA) {
                handler = find_handler_for_payload(kRtpPayloadTypePcma);
            }
        }

        if (!handler) {
            if (packet_data.ingress_from_loopback) {
                LOG_CPP_INFO("[RtpReceiver] Loopback packet seq=%u dropped: no handler for payload=%u",
                             packet_data.sequence_number,
                             packet_data.payload_type);
            }
            char ssrc_hex[12];
            snprintf(ssrc_hex, sizeof(ssrc_hex), "0x%08X", packet_data.ssrc);
            LOG_CPP_WARNING("[RtpReceiver] No handler for payload_type=%u (SSRC=%s). Dropping packet (size=%zu).",
                            packet_data.payload_type,
                            ssrc_hex,
                            packet_data.payload.size());
            continue;
        }

        TaggedAudioPacket packet;
        packet.source_tag = source_tag;
        packet.received_time = packet_data.received_time;
        packet.rtp_timestamp = packet_data.rtp_timestamp;
        packet.rtp_sequence_number = packet_data.sequence_number;
        packet.ingress_from_loopback = packet_data.ingress_from_loopback;
        packet.ssrcs.reserve(1 + packet_data.csrcs.size());
        packet.ssrcs.push_back(packet_data.ssrc);
        packet.ssrcs.insert(packet.ssrcs.end(), packet_data.csrcs.begin(), packet_data.csrcs.end());
        mark_sentinel_if_boundary(packet_data, packet);
        utils::log_sentinel("rtp_ready", packet);

        if (!handler->populate_packet(packet_data, props, packet)) {
            if (packet.ingress_from_loopback && packet.rtp_sequence_number.has_value()) {
                LOG_CPP_INFO("[RtpReceiver] Loopback packet seq=%u dropped: handler parse failure (payload=%u)",
                             packet.rtp_sequence_number.value(),
                             packet_data.payload_type);
            }
            char ssrc_hex[12];
            snprintf(ssrc_hex, sizeof(ssrc_hex), "0x%08X", packet_data.ssrc);
            LOG_CPP_WARNING("[RtpReceiver] Failed to parse payload_type=%u for SSRC=%s (endpoint=%s:%d, size=%zu). Packet dropped.",
                            packet_data.payload_type,
                            ssrc_hex,
                            client_ip_str,
                            announced_port,
                            packet_data.payload.size());
            continue;
        }

        register_source_tag(packet.source_tag);
        if (packet.ingress_from_loopback && packet.rtp_sequence_number.has_value()) {
            LOG_CPP_INFO("[RtpReceiver] Loopback packet seq=%u ready for dispatch (source=%s)",
                         packet.rtp_sequence_number.value(),
                         packet.source_tag.c_str());
        }
        dispatch_ready_packet(std::move(packet));
    }
}

bool RtpReceiverBase::is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) {
    (void)buffer;
    (void)size;
    (void)client_addr;
    return true;
}

bool RtpReceiverBase::process_and_validate_payload(
    const uint8_t* buffer,
    int size,
    const struct sockaddr_in& client_addr,
    std::chrono::steady_clock::time_point received_time,
    TaggedAudioPacket& out_packet,
    std::string& out_source_tag) {
    (void)buffer;
    (void)size;
    (void)client_addr;
    (void)received_time;
    (void)out_packet;
    (void)out_source_tag;
    log_warning("process_and_validate_payload called unexpectedly in raw socket mode.");
    return false;
}

size_t RtpReceiverBase::get_receive_buffer_size() const {
    return std::max<std::size_t>(chunk_size_bytes_ * 4, kMinimumReceiveBufferSize);
}

int RtpReceiverBase::get_poll_timeout_ms() const {
    return 5;
}

void RtpReceiverBase::maybe_log_telemetry() {
    static constexpr auto kTelemetryInterval = std::chrono::seconds(30);

    const auto now = std::chrono::steady_clock::now();
    if (telemetry_last_log_time_.time_since_epoch().count() != 0 &&
        now - telemetry_last_log_time_ < kTelemetryInterval) {
        return;
    }

    telemetry_last_log_time_ = now;

    size_t buffer_count = 0;
    size_t total_packets = 0;
    size_t max_packets = 0;
    {
        std::lock_guard<std::mutex> lock(reordering_buffer_mutex_);
        buffer_count = reordering_buffers_.size();
        for (const auto& [ssrc, buffer] : reordering_buffers_) {
            (void)ssrc;
            const size_t buffered = buffer.size();
            total_packets += buffered;
            if (buffered > max_packets) {
                max_packets = buffered;
            }
        }
    }

    LOG_CPP_INFO(
        "[Telemetry][RtpReceiver] reorder_buffers=%zu total_packets=%zu max_packets=%zu",
        buffer_count,
        total_packets,
        max_packets);
}

bool RtpReceiverBase::mark_sentinel_if_boundary(const RtpPacketData& packet_data, TaggedAudioPacket& packet) {
    const uint32_t bucket = packet_data.rtp_timestamp / 100000u;
    std::lock_guard<std::mutex> lock(sentinel_bucket_mutex_);
    auto [it, inserted] = ssrc_last_sentinel_bucket_.emplace(packet_data.ssrc, bucket);
    if (inserted) {
        return false;
    }
    if (it->second != bucket) {
        it->second = bucket;
        packet.is_sentinel = true;
    }
    return packet.is_sentinel;
}

} // namespace audio
} // namespace screamrouter
