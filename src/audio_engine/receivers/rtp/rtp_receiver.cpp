#include "rtp_receiver.h"
#include "../../configuration/audio_engine_settings.h"
#include "../../input_processor/timeshift_manager.h"
#include "../../utils/cpp_logger.h"

#include <rtc/rtp.hpp>
#include <opus/opus.h>
#include <opus/opus_multistream.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>
#include <string>
#include <utility>
#include <memory>

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

namespace {
bool is_system_little_endian() {
    int n = 1;
    return (*(char *)&n == 1);
}

void swap_endianness(uint8_t* data, size_t size, int bit_depth) {
    if (bit_depth == 16) {
        for (size_t i = 0; i + 1 < size; i += 2) {
            std::swap(data[i], data[i + 1]);
        }
    } else if (bit_depth == 24) {
        for (size_t i = 0; i + 2 < size; i += 3) {
            std::swap(data[i], data[i + 2]);
        }
    }
}

bool resolve_opus_multistream_layout(int channels, int& streams, int& coupled_streams, std::vector<unsigned char>& mapping) {
    mapping.clear();

    switch (channels) {
        case 1:
            streams = 1;
            coupled_streams = 0;
            mapping = {0};
            return true;
        case 2:
            streams = 1;
            coupled_streams = 1;
            mapping = {0, 1};
            return true;
        case 3:
            streams = 2;
            coupled_streams = 1;
            mapping = {0, 2, 1};
            return true;
        case 4:
            streams = 2;
            coupled_streams = 2;
            mapping = {0, 1, 2, 3};
            return true;
        case 5:
            streams = 3;
            coupled_streams = 2;
            mapping = {0, 2, 1, 3, 4};
            return true;
        case 6:
            streams = 4;
            coupled_streams = 2;
            mapping = {0, 2, 1, 5, 3, 4};
            return true;
        case 7:
            streams = 4;
            coupled_streams = 3;
            mapping = {0, 2, 1, 6, 3, 4, 5};
            return true;
        case 8:
            streams = 5;
            coupled_streams = 3;
            mapping = {0, 2, 1, 6, 3, 4, 5, 7};
            return true;
        default:
            return false;
    }
}
} // namespace

namespace screamrouter {
namespace audio {

constexpr std::size_t kMinimumReceiveBufferSize = 2048;
constexpr std::size_t kRawReceiveBufferSize = 2048;

const uint8_t kRtpPayloadTypeL16Stereo = 127;
const uint8_t kRtpPayloadTypeOpus = 111;
const int kDefaultOpusSampleRate = 48000;
const int kDefaultOpusChannels = 2;

RtpReceiverBase::RtpReceiverBase(
    RtpReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager,
    ClockManager* clock_manager)
    : NetworkAudioReceiver(config.listen_port,
                           notification_queue,
                           timeshift_manager,
                           "[RtpReceiver]",
                           clock_manager,
                           resolve_chunk_size_bytes(timeshift_manager ? timeshift_manager->get_settings() : nullptr)),
      config_(std::move(config)),
      chunk_size_bytes_(resolve_chunk_size_bytes(timeshift_manager ? timeshift_manager->get_settings() : nullptr))
#ifdef _WIN32
    , max_fd_(NAR_INVALID_SOCKET_VALUE)
#else
    , epoll_fd_(NAR_INVALID_SOCKET_VALUE)
#endif
{
    if (!clock_manager_) {
        throw std::runtime_error("RtpReceiver requires a valid ClockManager instance");
    }

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

std::string RtpReceiverBase::get_source_key(const struct sockaddr_in& addr) const {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), ip_str, INET_ADDRSTRLEN);
    return std::string(ip_str) + ":" + std::to_string(ntohs(addr.sin_port));
}

std::string RtpReceiverBase::make_pcm_accumulator_key(uint32_t ssrc) const {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "ssrc:%u", ssrc);
    return std::string(buffer);
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

    reset_pcm_accumulator(make_pcm_accumulator_key(old_ssrc));
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
        for (const auto& [_, ssrc] : source_to_last_ssrc_) {
            (void)_; // unused key
            reset_pcm_accumulator(make_pcm_accumulator_key(ssrc));
        }
        source_to_last_ssrc_.clear();
    }

    reset_all_pcm_accumulators();
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
        service_clock_manager();

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
                process_ready_packets_internal(ssrc, cliaddr, false);
            }
            service_clock_manager();
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

            if (static_cast<size_t>(n_received) < sizeof(rtc::RtpHeader)) {
                log_warning("Received packet too small to be an RTP packet (" + std::to_string(n_received) + " bytes).");
                continue;
            }

            const rtc::RtpHeader* rtp_header = reinterpret_cast<const rtc::RtpHeader*>(raw_buffer);
            uint8_t pt = rtp_header->payloadType();
            uint32_t current_ssrc = rtp_header->ssrc();

            if (!supports_payload_type(pt, current_ssrc)) {
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

            RtpPacketData packet_data;
            packet_data.sequence_number = rtp_header->seqNumber();
            packet_data.rtp_timestamp = rtp_header->timestamp();
            packet_data.received_time = received_time;
            packet_data.ssrc = current_ssrc;
            packet_data.payload_type = pt;

            size_t header_len = 12 + (rtp_header->csrcCount() * sizeof(uint32_t));
            if (static_cast<size_t>(n_received) < header_len) {
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
    if (sap_listener_) {
        if (sap_listener_->get_stream_properties(ssrc, out_properties)) {
            return true;
        }

        char client_ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
        if (sap_listener_->get_stream_properties_by_ip(client_ip_str, out_properties)) {
            return true;
        }
    }

    const int listen_port = config_.listen_port <= 0 ? 40000 : config_.listen_port;
    const int packet_port = ntohs(client_addr.sin_port);

    if (payload_type == kRtpPayloadTypeOpus && packet_port == listen_port && listen_port == 40000) {
        out_properties.sample_rate = kDefaultOpusSampleRate;
        out_properties.channels = kDefaultOpusChannels;
        out_properties.bit_depth = 16;
        out_properties.endianness = Endianness::LITTLE;
        out_properties.port = packet_port;
        out_properties.codec = StreamCodec::OPUS;
        out_properties.opus_streams = 0;
        out_properties.opus_coupled_streams = 0;
        out_properties.opus_channel_mapping.clear();
        return true;
    }

    return false;
}

void RtpReceiverBase::register_payload_receiver(std::unique_ptr<RtpPayloadReceiver> receiver) {
    if (receiver) {
        payload_receivers_.push_back(std::move(receiver));
    }
}

bool RtpReceiverBase::supports_payload_type(uint8_t payload_type, uint32_t /*ssrc*/) const {
    for (const auto& receiver : payload_receivers_) {
        if (receiver && receiver->supports_payload_type(payload_type)) {
            return true;
        }
    }
    return false;
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

    StreamProperties props{};
    if (!resolve_stream_properties(ssrc, client_addr, payload_type, props)) {
        char ssrc_hex[12];
        snprintf(ssrc_hex, sizeof(ssrc_hex), "0x%08X", ssrc);
        LOG_CPP_DEBUG(("Ignoring ready packets for unknown SSRC: " + std::string(ssrc_hex) +
                       " - no SAP properties found").c_str());
        return;
    }

    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
    std::string source_tag = client_ip_str;

    const std::string accumulator_key = make_pcm_accumulator_key(ssrc);

    for (auto& packet_data : ready_packets) {
        RtpPayloadReceiver* handler = nullptr;
        for (const auto& receiver : payload_receivers_) {
            if (receiver && receiver->supports_payload_type(packet_data.payload_type)) {
                handler = receiver.get();
                break;
            }
        }

        if (!handler) {
            if (props.codec == StreamCodec::OPUS) {
                for (const auto& receiver : payload_receivers_) {
                    if (receiver && receiver->supports_payload_type(kRtpPayloadTypeOpus)) {
                        handler = receiver.get();
                        break;
                    }
                }
            } else if (props.codec == StreamCodec::PCM || props.codec == StreamCodec::UNKNOWN) {
                for (const auto& receiver : payload_receivers_) {
                    if (receiver && receiver->supports_payload_type(kRtpPayloadTypeL16Stereo)) {
                        handler = receiver.get();
                        break;
                    }
                }
            }
        }

        if (!handler) {
            continue;
        }

        NetworkAudioReceiver::PcmAppendContext pcm_context;
        pcm_context.accumulator_key = accumulator_key;
        pcm_context.source_tag = source_tag;
        pcm_context.received_time = packet_data.received_time;
        pcm_context.rtp_timestamp = packet_data.rtp_timestamp;
        pcm_context.ssrcs.reserve(1 + packet_data.csrcs.size());
        pcm_context.ssrcs.push_back(packet_data.ssrc);
        pcm_context.ssrcs.insert(pcm_context.ssrcs.end(), packet_data.csrcs.begin(), packet_data.csrcs.end());

        if (!handler->populate_append_context(packet_data, props, pcm_context)) {
            continue;
        }

        auto completed_chunks = append_pcm_payload(std::move(pcm_context));
        for (auto& chunk : completed_chunks) {
            bool new_tag = false;
            {
                std::lock_guard<std::mutex> lock(seen_tags_mutex_);
                if (std::find(seen_tags_.begin(), seen_tags_.end(), chunk.source_tag) == seen_tags_.end()) {
                    seen_tags_.push_back(chunk.source_tag);
                    new_tag = true;
                }
            }
            if (new_tag && notification_queue_) {
                notification_queue_->push({chunk.source_tag});
            }

            dispatch_ready_packet(std::move(chunk));
        }
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

// ---- RtpPcmReceiver -----------------------------------------------------------------

bool RtpPcmReceiver::supports_payload_type(uint8_t payload_type) const {
    return payload_type == kRtpPayloadTypeL16Stereo;
}

bool RtpPcmReceiver::populate_append_context(
    const RtpPacketData& packet,
    const StreamProperties& properties,
    NetworkAudioReceiver::PcmAppendContext& context) {
    if (packet.payload.empty()) {
        return false;
    }

    if (properties.codec != StreamCodec::PCM && properties.codec != StreamCodec::UNKNOWN) {
        return false;
    }

    context.payload = packet.payload;
    const bool system_is_le = is_system_little_endian();
    if ((properties.endianness == Endianness::BIG && system_is_le) ||
        (properties.endianness == Endianness::LITTLE && !system_is_le)) {
        swap_endianness(context.payload.data(), context.payload.size(), properties.bit_depth);
    }

    context.sample_rate = properties.sample_rate;
    context.channels = properties.channels;
    context.bit_depth = properties.bit_depth;
    context.chlayout1 = (properties.channels == 2) ? 0x03 : 0x00;
    context.chlayout2 = 0x00;

    return true;
}

// ---- RtpOpusReceiver -----------------------------------------------------------------

RtpOpusReceiver::RtpOpusReceiver() = default;

RtpOpusReceiver::~RtpOpusReceiver() noexcept {
    destroy_all_decoders();
}

bool RtpOpusReceiver::supports_payload_type(uint8_t payload_type) const {
    return payload_type == kRtpPayloadTypeOpus;
}

bool RtpOpusReceiver::populate_append_context(
    const RtpPacketData& packet,
    const StreamProperties& properties,
    NetworkAudioReceiver::PcmAppendContext& context) {
    if (packet.payload.empty()) {
        return false;
    }

    if (properties.codec != StreamCodec::OPUS && properties.codec != StreamCodec::UNKNOWN) {
        return false;
    }

    const int sample_rate = properties.sample_rate > 0 ? properties.sample_rate : kDefaultOpusSampleRate;
    const int channels = properties.channels > 0 ? properties.channels : kDefaultOpusChannels;
    int streams = properties.opus_streams;
    int coupled_streams = properties.opus_coupled_streams;
    std::vector<unsigned char> mapping;
    mapping.reserve(properties.opus_channel_mapping.size());
    for (uint8_t value : properties.opus_channel_mapping) {
        mapping.push_back(static_cast<unsigned char>(value));
    }

    const bool require_multistream = (channels > 2) || (streams > 0) || !mapping.empty();

    if (require_multistream) {
        if (streams <= 0 || mapping.size() != static_cast<size_t>(channels)) {
            if (!resolve_opus_multistream_layout(channels, streams, coupled_streams, mapping)) {
                LOG_CPP_ERROR("[RtpOpusReceiver] Unable to resolve Opus multistream layout for %d channels", channels);
                return false;
            }
        }
    }

    OpusDecoder* decoder_handle = nullptr;
    OpusMSDecoder* ms_decoder_handle = nullptr;
    {
        std::lock_guard<std::mutex> lock(decoder_mutex_);
        DecoderState& state = decoder_states_[packet.ssrc];
        const bool needs_reconfigure =
            state.sample_rate != sample_rate ||
            state.channels != channels ||
            (require_multistream && (!state.ms_handle || state.streams != streams || state.coupled_streams != coupled_streams || state.mapping != mapping)) ||
            (!require_multistream && !state.handle) ||
            (require_multistream && state.handle != nullptr);

        if (needs_reconfigure) {
            if (state.handle) {
                opus_decoder_destroy(state.handle);
                state.handle = nullptr;
            }
            if (state.ms_handle) {
                opus_multistream_decoder_destroy(state.ms_handle);
                state.ms_handle = nullptr;
            }

            int error = 0;
            if (require_multistream) {
                state.ms_handle = opus_multistream_decoder_create(
                    sample_rate,
                    channels,
                    streams,
                    coupled_streams,
                    mapping.data(),
                    &error);
                if (error != OPUS_OK || !state.ms_handle) {
                    LOG_CPP_ERROR("[RtpOpusReceiver] Failed to create Opus multistream decoder: %s", opus_strerror(error));
                    state.ms_handle = nullptr;
                }
            } else {
                state.handle = opus_decoder_create(sample_rate, channels, &error);
                if (error != OPUS_OK || !state.handle) {
                    LOG_CPP_ERROR("[RtpOpusReceiver] Failed to create Opus decoder: %s", opus_strerror(error));
                    state.handle = nullptr;
                }
            }

            if ((require_multistream && !state.ms_handle) || (!require_multistream && !state.handle)) {
                state.sample_rate = 0;
                state.channels = 0;
                state.streams = 0;
                state.coupled_streams = 0;
                state.mapping.clear();
                return false;
            }

            state.sample_rate = sample_rate;
            state.channels = channels;
            state.streams = require_multistream ? streams : 0;
            state.coupled_streams = require_multistream ? coupled_streams : 0;
            if (require_multistream) {
                state.mapping = mapping;
            } else {
                state.mapping.clear();
            }
        }

        decoder_handle = state.handle;
        ms_decoder_handle = state.ms_handle;
    }

    if (!decoder_handle && !ms_decoder_handle) {
        LOG_CPP_ERROR("[RtpOpusReceiver] No Opus decoder available for SSRC %u", packet.ssrc);
        return false;
    }

    const int max_samples_per_channel = maximum_frame_samples(sample_rate);
    std::vector<opus_int16> decode_buffer(static_cast<size_t>(max_samples_per_channel) * channels);

    int decoded_samples = 0;
    if (ms_decoder_handle) {
        decoded_samples = opus_multistream_decode(
            ms_decoder_handle,
            packet.payload.data(),
            static_cast<opus_int32>(packet.payload.size()),
            decode_buffer.data(),
            max_samples_per_channel,
            0);
    } else {
        decoded_samples = opus_decode(
            decoder_handle,
            packet.payload.data(),
            static_cast<opus_int32>(packet.payload.size()),
            decode_buffer.data(),
            max_samples_per_channel,
            0);
    }

    if (decoded_samples < 0) {
        LOG_CPP_ERROR("[RtpOpusReceiver] Opus decoding failed for SSRC %u: %s", packet.ssrc, opus_strerror(decoded_samples));
        return false;
    }

    const size_t pcm_bytes = static_cast<size_t>(decoded_samples) * static_cast<size_t>(channels) * sizeof(opus_int16);
    context.payload.resize(pcm_bytes);
    std::memcpy(context.payload.data(), decode_buffer.data(), pcm_bytes);

    context.sample_rate = sample_rate;
    context.channels = channels;
    context.bit_depth = 16;
    context.chlayout1 = (channels == 2) ? 0x03 : 0x00;
    context.chlayout2 = 0x00;

    return true;
}

void RtpOpusReceiver::on_ssrc_state_cleared(uint32_t ssrc) {
    destroy_decoder(ssrc);
}

void RtpOpusReceiver::on_all_ssrcs_cleared() {
    destroy_all_decoders();
}

void RtpOpusReceiver::destroy_decoder(uint32_t ssrc) {
    std::lock_guard<std::mutex> lock(decoder_mutex_);
    auto it = decoder_states_.find(ssrc);
    if (it != decoder_states_.end()) {
        if (it->second.handle) {
            opus_decoder_destroy(it->second.handle);
        }
        if (it->second.ms_handle) {
            opus_multistream_decoder_destroy(it->second.ms_handle);
        }
        decoder_states_.erase(it);
    }
}

void RtpOpusReceiver::destroy_all_decoders() {
    std::lock_guard<std::mutex> lock(decoder_mutex_);
    for (auto& [ssrc, state] : decoder_states_) {
        (void)ssrc;
        if (state.handle) {
            opus_decoder_destroy(state.handle);
        }
        if (state.ms_handle) {
            opus_multistream_decoder_destroy(state.ms_handle);
        }
    }
    decoder_states_.clear();
}

int RtpOpusReceiver::maximum_frame_samples(int sample_rate) {
    // Opus frames can be up to 120 ms.
    if (sample_rate <= 0) {
        return 0;
    }
    const int64_t samples = (static_cast<int64_t>(sample_rate) * 120 + 999) / 1000;
    return static_cast<int>(samples);
}

// ---- RtpReceiver (composite) ---------------------------------------------------------

RtpReceiver::RtpReceiver(
    RtpReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager,
    ClockManager* clock_manager)
    : RtpReceiverBase(std::move(config), std::move(notification_queue), timeshift_manager, clock_manager) {
    register_payload_receiver(std::make_unique<RtpPcmReceiver>());
    register_payload_receiver(std::make_unique<RtpOpusReceiver>());
}

} // namespace audio
} // namespace screamrouter
