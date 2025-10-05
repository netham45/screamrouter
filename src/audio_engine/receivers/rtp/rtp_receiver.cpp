#include "rtp_receiver.h"
#include "../../input_processor/timeshift_manager.h" // For TimeshiftManager operations
#include "../../audio_constants.h"   // For SCREAM_PAYLOAD_TYPE_RTP, etc.
#include <rtc/rtp.hpp> // Added for libdatachannel
#include <iostream>
#include <vector>
#include <cstring>      // For memcpy, memset, strerror
#include <stdexcept>    // For runtime_error
#include <system_error> // For socket error checking
#include <utility>      // For std::move
#include <algorithm>    // For std::find, std::min
#include <cerrno>       // For errno
#ifndef _WIN32
    #include <sys/epoll.h>  // For epoll
#else
    // Define ssize_t for Windows
    #include <BaseTsd.h>
    typedef SSIZE_T ssize_t;
#endif
 
 
 // Platform-specific includes for inet_ntoa, struct sockaddr_in etc. are in network_audio_receiver.h
// NAR_POLL, NAR_GET_LAST_SOCK_ERROR, NAR_INVALID_SOCKET_VALUE are also from network_audio_receiver.h

namespace screamrouter {
namespace audio {

namespace { // Anonymous namespace for helpers
bool is_system_little_endian() {
    int n = 1;
    return (*(char *)&n == 1);
}

void swap_endianness(uint8_t* data, size_t size, int bit_depth) {
    if (bit_depth == 16) {
        for (size_t i = 0; i < size; i += 2) {
            std::swap(data[i], data[i + 1]);
        }
    } else if (bit_depth == 24) {
        for (size_t i = 0; i < size; i += 3) {
            std::swap(data[i], data[i + 2]);
        }
    }
}
} // namespace

const size_t TARGET_PCM_CHUNK_SIZE = 1152;
const size_t RAW_RECEIVE_BUFFER_SIZE = 2048; // Buffer for recvfrom

// RTP constants remain the same
const int RTP_PAYLOAD_TYPE_L16_48K_STEREO = 127;
const int RTP_CLOCK_RATE_L16_48K_STEREO = 48000;
const int RTP_CHANNELS_L16_48K_STEREO = 2;
const int RTP_BITS_PER_SAMPLE_L16_48K_STEREO = 16;


RtpReceiver::RtpReceiver(
    RtpReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager)
    : NetworkAudioReceiver(config.listen_port, notification_queue, timeshift_manager, "[RtpReceiver]"),
      config_(config),
      #ifdef _WIN32
          max_fd_(NAR_INVALID_SOCKET_VALUE),
      #else
          epoll_fd_(NAR_INVALID_SOCKET_VALUE),
      #endif
      last_rtp_timestamp_(0), last_chunk_remainder_samples_(0) {
    #ifdef _WIN32
        FD_ZERO(&master_read_fds_);
    #endif
    // pcm_accumulator_ is now a map, no global reservation
    sap_listener_ = std::make_unique<SapListener>("[RtpReceiver-SAP]", config_.known_ips);
    if (sap_listener_) {
        sap_listener_->set_session_callback([this](const std::string& ip, int port, const std::string& source_ip) {
            this->open_dynamic_session(ip, port, source_ip);
        });
    }
}

RtpReceiver::~RtpReceiver() noexcept {
    
}


// Helper function to generate a unique key for each source
std::string RtpReceiver::get_source_key(const struct sockaddr_in& addr) const {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), ip_str, INET_ADDRSTRLEN);
    return std::string(ip_str) + ":" + std::to_string(ntohs(addr.sin_port));
}

void RtpReceiver::handle_ssrc_changed(uint32_t old_ssrc, uint32_t new_ssrc, const std::string& source_key) {
    char old_ssrc_hex[12];
    char new_ssrc_hex[12];
    snprintf(old_ssrc_hex, sizeof(old_ssrc_hex), "0x%08X", old_ssrc);
    snprintf(new_ssrc_hex, sizeof(new_ssrc_hex), "0x%08X", new_ssrc);

    log_message("SSRC changed for source " + source_key +
                ". Old SSRC: " + std::string(old_ssrc_hex) +
                ", New SSRC: " + std::string(new_ssrc_hex) + ". Clearing state for old SSRC.");
   
   std::lock_guard<std::mutex> lock(reordering_buffer_mutex_);
   if (reordering_buffers_.count(old_ssrc)) {
       reordering_buffers_.at(old_ssrc).reset();
   }
   pcm_accumulators_.erase(old_ssrc);
   is_accumulating_chunk_.erase(old_ssrc);
   chunk_first_packet_received_time_.erase(old_ssrc);
   chunk_first_packet_rtp_timestamp_.erase(old_ssrc);

   log_message("State for SSRC " + std::string(old_ssrc_hex) + " cleared due to SSRC change.");
}

bool RtpReceiver::setup_socket() {
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
 
    // Open the default port, listening on all interfaces
    const int default_port = 40000;
    open_dynamic_session("0.0.0.0", default_port, "");
 
    if (socket_fds_.empty()) {
        log_error("Failed to bind the default UDP socket on port " + std::to_string(default_port));
        #ifndef _WIN32
            close(epoll_fd_);
            epoll_fd_ = NAR_INVALID_SOCKET_VALUE;
        #endif
        return false;
    }
 
    log_message("RTP receiver is listening for SAP announcements for dynamic ports.");
 
    if (sap_listener_) {
        sap_listener_->start();
    }
 
    return true;
}

void RtpReceiver::close_socket() {
    if (sap_listener_) {
        sap_listener_->stop();
    }
   {
       std::lock_guard<std::mutex> lock(reordering_buffer_mutex_);
       reordering_buffers_.clear();
       pcm_accumulators_.clear();
       is_accumulating_chunk_.clear();
       chunk_first_packet_received_time_.clear();
       chunk_first_packet_rtp_timestamp_.clear();
   }
   {
       std::lock_guard<std::mutex> lock(source_ssrc_mutex_);
       source_to_last_ssrc_.clear();
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
    log_message("All raw UDP socket resources released.");
}

void RtpReceiver::run() {
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
 
    unsigned char raw_buffer[RAW_RECEIVE_BUFFER_SIZE];
    struct sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);
 
    #ifndef _WIN32
        const int MAX_EVENTS = 10;
        struct epoll_event events[MAX_EVENTS];
    #endif
 
    while (is_running()) {
        #ifdef _WIN32
            // Windows: Use select()
            fd_set read_fds = master_read_fds_;
            struct timeval tv;
            tv.tv_sec = get_poll_timeout_ms() / 1000;
            tv.tv_usec = (get_poll_timeout_ms() % 1000) * 1000;
            
            int n_events = select(max_fd_ + 1, &read_fds, NULL, NULL, &tv);
        #else
            // Linux: Use epoll
            int n_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, get_poll_timeout_ms());
        #endif
 
        if (!is_running()) break;
 
        if (n_events < 0) {
            #ifdef _WIN32
                if (WSAGetLastError() == WSAEINTR) {
                    continue;
                }
                log_error("select() error: " + std::to_string(WSAGetLastError()));
            #else
                if (errno == EINTR) { // Interrupted by a signal
                    continue;
                }
                log_error("epoll_wait() error: " + std::string(strerror(errno)));
            #endif
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
 
        if (n_events == 0) { // Timeout
            // Timeout: opportunity to process packets from jitter buffer
            // even if no new packets have arrived.
            std::lock_guard<std::mutex> lock(reordering_buffer_mutex_);
            for(auto const& [ssrc, val] : reordering_buffers_) {
                // Call internal version with take_lock=false since we already hold the lock
                process_ready_packets_internal(ssrc, cliaddr, false);
            }
            continue;
        }

        #ifdef _WIN32
            // Windows select: iterate through all sockets
            std::lock_guard<std::mutex> lock(socket_fds_mutex_);
            for (socket_t current_socket_fd : socket_fds_) {
                if (FD_ISSET(current_socket_fd, &read_fds)) {
                    ssize_t n_received = recvfrom(current_socket_fd, (char*)raw_buffer, RAW_RECEIVE_BUFFER_SIZE, 0, (struct sockaddr *)&cliaddr, &len);
        #else
            // Linux epoll: iterate through ready events
            for (int i = 0; i < n_events; ++i) {
                if ((events[i].events & EPOLLIN)) {
                    socket_t current_socket_fd = events[i].data.fd;
                    ssize_t n_received = recvfrom(current_socket_fd, raw_buffer, RAW_RECEIVE_BUFFER_SIZE, 0, (struct sockaddr *)&cliaddr, &len);
        #endif

                if (!is_running()) break;

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
                    log_warning("Received packet too small to be an RTP packet (" + std::to_string(n_received) + " bytes). Source: " + std::string(inet_ntoa(cliaddr.sin_addr)));
                    continue;
                }

                const rtc::RtpHeader* rtp_header = reinterpret_cast<const rtc::RtpHeader*>(raw_buffer);
                uint8_t pt = rtp_header->payloadType();
                uint32_t current_ssrc = rtp_header->ssrc();

                // Per-source SSRC tracking to handle multiple independent RTP streams
                std::string source_key = get_source_key(cliaddr);
                {
                    std::lock_guard<std::mutex> lock(source_ssrc_mutex_);
                    
                    auto it = source_to_last_ssrc_.find(source_key);
                    if (it == source_to_last_ssrc_.end()) {
                        // First packet from this source
                        source_to_last_ssrc_[source_key] = current_ssrc;
                        char ssrc_hex[12];
                        snprintf(ssrc_hex, sizeof(ssrc_hex), "0x%08X", current_ssrc);
                        log_message("New RTP source detected: " + source_key + " with SSRC " + std::string(ssrc_hex));
                    } else if (it->second != current_ssrc) {
                        // SSRC changed for THIS SOURCE ONLY
                        uint32_t old_ssrc = it->second;
                        handle_ssrc_changed(old_ssrc, current_ssrc, source_key);
                        it->second = current_ssrc;
                    }
                }

                if (pt != RTP_PAYLOAD_TYPE_L16_48K_STEREO) {
                    log_warning("Received packet with unexpected payload type: " + std::to_string(pt) +
                                ", expected " + std::to_string(RTP_PAYLOAD_TYPE_L16_48K_STEREO) + ". SSRC: 0x" + std::to_string(current_ssrc));
                    continue;
                }

                // --- Start of Reordering Buffer Logic ---

                // 1. Create RtpPacketData from the raw buffer
                RtpPacketData packet_data;
                packet_data.sequence_number = rtp_header->seqNumber();
                packet_data.rtp_timestamp = rtp_header->timestamp();
                packet_data.received_time = received_time;
                packet_data.ssrc = current_ssrc;

                size_t header_len = 12 + (rtp_header->csrcCount() * sizeof(uint32_t));
                // (Extension handling logic omitted for brevity in this refactor, but would go here)

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
                        memcpy(&csrc, csrc_ptr, sizeof(uint32_t));
                        packet_data.csrcs.push_back(ntohl(csrc));
                        csrc_ptr += sizeof(uint32_t);
                    }
                }

                // 2. Add the packet to the appropriate reordering buffer
                {
                    std::lock_guard<std::mutex> lock(reordering_buffer_mutex_);
                    // This will create a new buffer for the SSRC if one doesn't exist
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

                // 3. Process any packets that are now ready
                process_ready_packets(current_ssrc, cliaddr);

                #ifdef _WIN32
                    } // End if FD_ISSET
                } // End for socket_fds_
            #else
                } // End if EPOLLIN
            } // End for n_events
        #endif
    } // End while is_running
    log_message("RTP receiver thread finished.");
}


void RtpReceiver::open_dynamic_session(const std::string& ip, int port, const std::string& source_ip) {
    if (port <= 0 || port > 65535) {
        log_warning("Invalid port number received: " + std::to_string(port));
        return;
    }

    std::lock_guard<std::mutex> lock(socket_fds_mutex_);

    // Check if we are already listening on this ip:port
    for (socket_t existing_fd : socket_fds_) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        if (getsockname(existing_fd, (struct sockaddr *)&addr, &len) == 0) {
            char existing_ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(addr.sin_addr), existing_ip_str, INET_ADDRSTRLEN);
            if (ntohs(addr.sin_port) == port && ip == existing_ip_str) {
                // log_message("Already listening on " + ip + ":" + std::to_string(port));
                return; // Already listening
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

    int recv_buf_size = 256 * 1024;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recv_buf_size), sizeof(recv_buf_size)) < 0) {
        log_warning("Failed to set SO_RCVBUF for " + ip + ":" + std::to_string(port) + ": " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR)));
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
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
            #ifdef _WIN32
                closesocket(sock_fd);
            #else
                close(sock_fd);
            #endif
            return;
        }

        socket_fds_.push_back(sock_fd);
        log_message("Successfully bound and added new socket for " + ip + ":" + std::to_string(port) + " to epoll.");
    #endif
}

void RtpReceiver::process_ready_packets(uint32_t ssrc, const struct sockaddr_in& client_addr) {
    process_ready_packets_internal(ssrc, client_addr, true);
}

void RtpReceiver::process_ready_packets_internal(uint32_t ssrc, const struct sockaddr_in& client_addr, bool take_lock) {
    std::unique_ptr<std::lock_guard<std::mutex>> lock_ptr;
    if (take_lock) {
        lock_ptr = std::make_unique<std::lock_guard<std::mutex>>(reordering_buffer_mutex_);
    }

    if (reordering_buffers_.find(ssrc) == reordering_buffers_.end()) {
        return; // No buffer for this SSRC
    }

    auto ready_packets = reordering_buffers_.at(ssrc).get_ready_packets();
    if (ready_packets.empty()) {
        return; // No packets ready to process
    }
    
    // Log when we process packets after potential loss
    if (ready_packets.size() > 1) {
        char ssrc_hex[12];
        snprintf(ssrc_hex, sizeof(ssrc_hex), "0x%08X", ssrc);
        log_message("Processing " + std::to_string(ready_packets.size()) +
                    " ready packets for SSRC " + std::string(ssrc_hex) + " after reordering/recovery");
    }

    // Get stream properties for this SSRC
    StreamProperties props;
    bool has_props = false;
    if (sap_listener_) {
        has_props = sap_listener_->get_stream_properties(ssrc, props);
        if (!has_props) {
            char client_ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
            has_props = sap_listener_->get_stream_properties_by_ip(client_ip_str, props);
        }
    }
    if (!has_props) {
        char ssrc_hex[12];
        snprintf(ssrc_hex, sizeof(ssrc_hex), "0x%08X", ssrc);
        LOG_CPP_DEBUG(("Ignoring ready packets for unknown SSRC: " + std::string(ssrc_hex) +
                       " - no SAP properties found").c_str());
        return;
    }

    // Get or create the accumulator for this SSRC
    auto& pcm_accumulator = pcm_accumulators_[ssrc];
    if (pcm_accumulator.capacity() < TARGET_PCM_CHUNK_SIZE * 2) {
        pcm_accumulator.reserve(TARGET_PCM_CHUNK_SIZE * 2);
    }

    for (auto& packet_data : ready_packets) {
        if (!is_accumulating_chunk_[ssrc]) {
            chunk_first_packet_received_time_[ssrc] = packet_data.received_time;
            chunk_first_packet_rtp_timestamp_[ssrc] = packet_data.rtp_timestamp;
            is_accumulating_chunk_[ssrc] = true;
        }

        std::vector<uint8_t> processed_payload = std::move(packet_data.payload);
        bool system_is_le = is_system_little_endian();
        if ((props.endianness == Endianness::BIG && system_is_le) ||
            (props.endianness == Endianness::LITTLE && !system_is_le)) {
            swap_endianness(processed_payload.data(), processed_payload.size(), props.bit_depth);
        }

        pcm_accumulator.insert(pcm_accumulator.end(), processed_payload.begin(), processed_payload.end());

        while (pcm_accumulator.size() >= TARGET_PCM_CHUNK_SIZE) {
            TaggedAudioPacket packet;
            packet.received_time = chunk_first_packet_received_time_[ssrc];
            packet.rtp_timestamp = chunk_first_packet_rtp_timestamp_[ssrc];
            
            size_t bytes_per_sample = props.bit_depth / 8;
            if (bytes_per_sample > 0 && props.channels > 0) {
                size_t samples_in_chunk = (TARGET_PCM_CHUNK_SIZE / bytes_per_sample) / props.channels;
                chunk_first_packet_rtp_timestamp_[ssrc] += samples_in_chunk;
            }

            packet.ssrcs.push_back(packet_data.ssrc);
            packet.ssrcs.insert(packet.ssrcs.end(), packet_data.csrcs.begin(), packet_data.csrcs.end());

            char client_ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
            packet.source_tag = client_ip_str;
            
            packet.channels = props.channels;
            packet.sample_rate = props.sample_rate;
            packet.bit_depth = props.bit_depth;
            packet.chlayout1 = 0x03; // Stereo L/R
            packet.chlayout2 = 0x00;

            packet.audio_data.assign(pcm_accumulator.begin(), pcm_accumulator.begin() + TARGET_PCM_CHUNK_SIZE);
            pcm_accumulator.erase(pcm_accumulator.begin(), pcm_accumulator.begin() + TARGET_PCM_CHUNK_SIZE);
            
            is_accumulating_chunk_[ssrc] = false;

            if (timeshift_manager_) {
                timeshift_manager_->add_packet(std::move(packet));
            }

            bool new_tag = false;
            {
                std::lock_guard<std::mutex> lock(seen_tags_mutex_);
                if (std::find(seen_tags_.begin(), seen_tags_.end(), packet.source_tag) == seen_tags_.end()) {
                    seen_tags_.push_back(packet.source_tag);
                    new_tag = true;
                }
            }
            if (new_tag && notification_queue_) {
                notification_queue_->push({packet.source_tag});
            }
        }
    }
}


// --- Dummied/Simplified NetworkAudioReceiver Pure Virtual Method Implementations ---
 
bool RtpReceiver::is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) {
    (void)buffer; (void)size; (void)client_addr;
    // Basic validation (size >= RTP header size) is done in run().
    // More complex validation could be added if needed.
    return true;
}

bool RtpReceiver::process_and_validate_payload(
    const uint8_t* buffer,
    int size,
    const struct sockaddr_in& client_addr,
    std::chrono::steady_clock::time_point received_time,
    TaggedAudioPacket& out_packet,
    std::string& out_source_tag) {
    
    (void)buffer; (void)size; (void)client_addr; (void)received_time; (void)out_packet; (void)out_source_tag;
    // This method is bypassed by the custom run() loop.
    log_warning("process_and_validate_payload called unexpectedly in raw socket mode.");
    return false;
}

size_t RtpReceiver::get_receive_buffer_size() const {
    // This was for the base class's recvfrom logic, which is not used.
    // Return the size of the buffer used in our run() loop.
    return RAW_RECEIVE_BUFFER_SIZE;
}

int RtpReceiver::get_poll_timeout_ms() const {
    return 100; // ms
}

} // namespace audio
} // namespace screamrouter
