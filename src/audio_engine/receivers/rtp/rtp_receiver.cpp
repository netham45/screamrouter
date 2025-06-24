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
#include <sys/epoll.h>  // For epoll
 
 
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
      config_(config), epoll_fd_(NAR_INVALID_SOCKET_VALUE), last_known_ssrc_(0), ssrc_initialized_(false) {
    pcm_accumulator_.reserve(TARGET_PCM_CHUNK_SIZE * 2);
    sap_listener_ = std::make_unique<SapListener>("[RtpReceiver-SAP]", config_.known_ips);
    if (sap_listener_) {
        sap_listener_->set_session_callback([this](const std::string& ip, int port) {
            this->open_dynamic_session(ip, port);
        });
    }
}

RtpReceiver::~RtpReceiver() noexcept {
    
}


void RtpReceiver::handle_ssrc_changed(uint32_t old_ssrc, uint32_t new_ssrc) {
    char old_ssrc_hex[12];
    char new_ssrc_hex[12];
    snprintf(old_ssrc_hex, sizeof(old_ssrc_hex), "0x%08X", old_ssrc);
    snprintf(new_ssrc_hex, sizeof(new_ssrc_hex), "0x%08X", new_ssrc);

    log_message("SSRC changed. Old SSRC: " + std::string(old_ssrc_hex) +
                ", New SSRC: " + std::string(new_ssrc_hex) + ". Clearing PCM accumulator.");
    pcm_accumulator_.clear();
    log_message("Internal PCM accumulator cleared due to SSRC change.");
}

bool RtpReceiver::setup_socket() {
    log_message("Setting up raw UDP sockets for RTP reception...");
 
    if (epoll_fd_ != NAR_INVALID_SOCKET_VALUE) {
        log_warning("setup_socket called but epoll_fd_ is already valid. Closing existing sockets first.");
        close_socket();
    }
 
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        log_error("Failed to create epoll file descriptor: " + std::string(strerror(errno)));
        return false;
    }
 
    // Open the default port, listening on all interfaces
    const int default_port = 40000;
    open_dynamic_session("0.0.0.0", default_port);
 
    if (socket_fds_.empty()) {
        log_error("Failed to bind the default UDP socket on port " + std::to_string(default_port));
        close(epoll_fd_);
        epoll_fd_ = NAR_INVALID_SOCKET_VALUE;
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
    pcm_accumulator_.clear();

    std::lock_guard<std::mutex> lock(socket_fds_mutex_);

    if (epoll_fd_ != NAR_INVALID_SOCKET_VALUE) {
        log_message("Closing epoll file descriptor (fd: " + std::to_string(epoll_fd_) + ")");
        close(epoll_fd_);
        epoll_fd_ = NAR_INVALID_SOCKET_VALUE;
    }
    for (socket_t sock_fd : socket_fds_) {
        log_message("Closing raw UDP socket (fd: " + std::to_string(sock_fd) + ")");
        close(sock_fd);
    }
    socket_fds_.clear();
    log_message("All raw UDP socket resources released.");
}

void RtpReceiver::run() {
    log_message("RTP receiver thread started using epoll and libdatachannel parser.");
 
    if (epoll_fd_ == NAR_INVALID_SOCKET_VALUE || socket_fds_.empty()) {
        log_error("Sockets are not initialized. Thread cannot run.");
        return;
    }
 
    unsigned char raw_buffer[RAW_RECEIVE_BUFFER_SIZE];
    struct sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);
 
    const int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];
 
    while (is_running()) {
        int n_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, get_poll_timeout_ms());
 
        if (!is_running()) break;
 
        if (n_events < 0) {
            if (errno == EINTR) { // Interrupted by a signal
                continue;
            }
            log_error("epoll_wait() error: " + std::string(strerror(errno)));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
 
        if (n_events == 0) { // Timeout
            continue;
        }
 
        for (int i = 0; i < n_events; ++i) {
            if ((events[i].events & EPOLLIN)) {
                socket_t current_socket_fd = events[i].data.fd;
                ssize_t n_received = recvfrom(current_socket_fd, raw_buffer, RAW_RECEIVE_BUFFER_SIZE, 0, (struct sockaddr *)&cliaddr, &len);
 
                if (!is_running()) break;
 
                if (n_received < 0) {
                // EAGAIN or EWOULDBLOCK might occur if socket is non-blocking, but select should prevent this.
                // However, other errors can occur.
                if (NAR_GET_LAST_SOCK_ERROR != EAGAIN && NAR_GET_LAST_SOCK_ERROR != EWOULDBLOCK) {
                     log_error("recvfrom() error: " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR)));
                }
                continue;
            }

            if (n_received == 0) { // Should not happen for UDP
                log_warning("recvfrom() returned 0 bytes.");
                continue;
            }
            
            // Successfully received data
            auto received_time = std::chrono::steady_clock::now();

            if (static_cast<size_t>(n_received) >= sizeof(rtc::RtpHeader)) {
                const rtc::RtpHeader* rtp_header = reinterpret_cast<const rtc::RtpHeader*>(raw_buffer);
                
                uint8_t pt = rtp_header->payloadType();
                uint32_t current_ssrc = rtp_header->ssrc(); // ntohl is handled by libdatachannel

                if (!ssrc_initialized_) {
                    ssrc_initialized_ = true;
                    last_known_ssrc_ = current_ssrc;
                    char ssrc_hex[12];
                    snprintf(ssrc_hex, sizeof(ssrc_hex), "0x%08X", current_ssrc);
                    log_message("Initial SSRC detected: " + std::string(ssrc_hex));
                } else if (current_ssrc != last_known_ssrc_) {
                    handle_ssrc_changed(last_known_ssrc_, current_ssrc);
                    last_known_ssrc_ = current_ssrc; // Update after handling
                }

                if (pt == RTP_PAYLOAD_TYPE_L16_48K_STEREO) {
                    // Calculate header length manually.
                    // Fixed RTP header size is 12 bytes.
                    size_t header_len = 12;
                    // Add length of CSRC list (each CSRC is 4 bytes).
                    // Assumes rtp_header->csrcCount() is available and correct.
                    header_len += rtp_header->csrcCount() * sizeof(uint32_t);

                    // Manually check for RTP header extension.
                    // The first byte of the RTP header (raw_buffer[0]) contains: V(2) P(1) X(1) CC(4)
                    // X is the extension bit, which is the 4th bit from MSB (mask 0x10).
                    if ((raw_buffer[0] & 0x10) != 0) { // Check if extension bit (X) is set
                        // The RTP header extension structure is:
                        // Defined by profile: 2 bytes
                        // Length: 2 bytes (length of extension data in 32-bit words, EXCLUDING this 4-byte header)
                        
                        size_t min_ext_header_size = 4; // For "defined by profile" and "length" fields
                        if (static_cast<size_t>(n_received) >= header_len + min_ext_header_size) {
                            // Offset to the 16-bit length field of the extension header.
                            // This is after the fixed header (12 bytes), CSRCs, and the 2-byte "defined by profile" field.
                            size_t ext_length_field_offset = 12 + (rtp_header->csrcCount() * sizeof(uint32_t)) + 2;
                            
                            uint16_t ext_data_len_words = ntohs(*reinterpret_cast<const uint16_t*>(raw_buffer + ext_length_field_offset));
                            size_t extension_total_length_bytes = min_ext_header_size + (static_cast<size_t>(ext_data_len_words) * 4);
                            
                            if (static_cast<size_t>(n_received) >= header_len + extension_total_length_bytes) {
                                header_len += extension_total_length_bytes;
                            } else {
                                log_warning("RTP packet indicates extension, but is too short for declared extension data length. SSRC: 0x" + std::to_string(current_ssrc) + ", Declared words: " + std::to_string(ext_data_len_words));
                                // Packet is malformed or truncated, proceed as if no valid extension.
                            }
                        } else {
                            log_warning("RTP packet indicates extension, but is too short for extension header fields. SSRC: 0x" + std::to_string(current_ssrc));
                            // Packet is malformed, proceed as if no valid extension.
                        }
                    }
                    if (static_cast<size_t>(n_received) >= header_len) {
                        const uint8_t* payload_data = raw_buffer + header_len;
                        size_t payload_len = n_received - header_len;

                        if (payload_len > 0) {
                            StreamProperties props;
                            bool has_props = false;
                            if (sap_listener_) {
                                // First, try to get properties by SSRC (preferred)
                                has_props = sap_listener_->get_stream_properties(current_ssrc, props);
                                
                                if (!has_props) {
                                    // If SSRC lookup fails, try to get by sender IP address
                                    char client_ip_str[INET_ADDRSTRLEN];
                                    inet_ntop(AF_INET, &(cliaddr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
                                    has_props = sap_listener_->get_stream_properties_by_ip(client_ip_str, props);
                                }
                            }

                            if (!has_props) {
                                // If both lookups fail, log and ignore the packet
                                if (sap_listener_) {
                                    char client_ip_str[INET_ADDRSTRLEN];
                                    inet_ntop(AF_INET, &(cliaddr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
                                    LOG_CPP_DEBUG(("Ignoring RTP packet from " + std::string(client_ip_str) +
                                                " with unknown SSRC: " + std::to_string(current_ssrc) +
                                                ". No matching SAP announcement found via SSRC or IP.").c_str());
                                }
                                continue;
                            }

                            std::vector<uint8_t> processed_payload(payload_data, payload_data + payload_len);

                            bool system_is_le = is_system_little_endian();
                            if ((props.endianness == Endianness::BIG && system_is_le) ||
                                (props.endianness == Endianness::LITTLE && !system_is_le)) {
                                swap_endianness(processed_payload.data(), processed_payload.size(), props.bit_depth);
                            }

                            pcm_accumulator_.insert(pcm_accumulator_.end(), processed_payload.begin(), processed_payload.end());
                            last_rtp_packet_timestamp_ = received_time;

                            while (pcm_accumulator_.size() >= TARGET_PCM_CHUNK_SIZE) {
                                const rtc::RtpHeader* rtp_header = reinterpret_cast<const rtc::RtpHeader*>(raw_buffer);
                                TaggedAudioPacket packet;
                                packet.received_time = last_rtp_packet_timestamp_;
                                packet.rtp_timestamp = rtp_header->timestamp(); // ntohl() is handled by libdatachannel
                                
                                // Populate SSRC and CSRCs
                                packet.ssrcs.push_back(rtp_header->ssrc());
                                
                                // Manually extract CSRCs from the raw buffer
                                const uint8_t csrc_count = rtp_header->csrcCount();
                                if (csrc_count > 0) {
                                    // The CSRC list starts after the fixed 12-byte header.
                                    const uint8_t* csrc_ptr = raw_buffer + 12;
                                    for (uint8_t i = 0; i < csrc_count; i++) {
                                        uint32_t csrc;
                                        // Copy 4 bytes into a uint32_t.
                                        memcpy(&csrc, csrc_ptr, sizeof(uint32_t));
                                        // The value is in network byte order, convert it to host byte order.
                                        packet.ssrcs.push_back(ntohl(csrc));
                                        csrc_ptr += sizeof(uint32_t);
                                    }
                                }

                                // Use sender's IP address for source_tag
                                char client_ip_str[INET_ADDRSTRLEN];
                                inet_ntop(AF_INET, &(cliaddr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
                                packet.source_tag = client_ip_str;
                                
                                // We already have the stream properties from the check before accumulating,
                                // so we can use the `props` variable.
                                packet.channels = props.channels;
                                packet.sample_rate = props.sample_rate;
                                packet.bit_depth = props.bit_depth;
                                packet.chlayout1 = 0x03; // Stereo L/R
                                packet.chlayout2 = 0x00;

                                packet.audio_data.assign(pcm_accumulator_.begin(), pcm_accumulator_.begin() + TARGET_PCM_CHUNK_SIZE);
                                pcm_accumulator_.erase(pcm_accumulator_.begin(), pcm_accumulator_.begin() + TARGET_PCM_CHUNK_SIZE);

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
                            } // End while pcm_accumulator has enough data
                        } else {
                            log_warning("Received RTP packet (PT=" + std::to_string(pt) + ") with no payload after header.");
                        }
                    } else {
                         log_warning("Received RTP packet (PT=" + std::to_string(pt) + ") smaller than its own header length (" + std::to_string(n_received) + " < " + std::to_string(header_len) + "). SSRC: 0x" + std::to_string(current_ssrc));
                    }
                } else {
                    log_warning("Received packet with unexpected payload type: " + std::to_string(pt) +
                                ", expected " + std::to_string(RTP_PAYLOAD_TYPE_L16_48K_STEREO) + ". SSRC: 0x" + std::to_string(current_ssrc));
                }
            } else {
                log_warning("Received packet too small to be an RTP packet (" + std::to_string(n_received) + " bytes). Source: " + std::string(inet_ntoa(cliaddr.sin_addr)));
            }
            } // End if EPOLLIN
        } // End for n_events
    } // End while is_running
    log_message("RTP receiver thread finished.");
}


void RtpReceiver::open_dynamic_session(const std::string& ip, int port) {
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
        close(sock_fd);
        return;
    }


    if (bind(sock_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        log_message("Could not bind to " + ip + ":" + std::to_string(port) + ": " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR)));
        close(sock_fd);
        return;
    }

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
