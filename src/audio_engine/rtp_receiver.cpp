#include "rtp_receiver.h"
#include "timeshift_manager.h" // For TimeshiftManager operations
#include "audio_constants.h"   // For SCREAM_PAYLOAD_TYPE_RTP, etc.
// #include <ortp/ortp.h> // Removed
#include <rtc/rtp.hpp> // Added for libdatachannel
#include <iostream>
#include <vector>
#include <cstring>      // For memcpy, memset, strerror
#include <stdexcept>    // For runtime_error
#include <system_error> // For socket error checking
#include <utility>      // For std::move
#include <algorithm>    // For std::find, std::min
#include <cerrno>       // For errno

// POSIX socket includes are now in rtp_receiver.h
// #include <sys/types.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>
// #include <unistd.h> // For close()
#include <sys/select.h> // For select()


// Platform-specific includes for inet_ntoa, struct sockaddr_in etc. are in network_audio_receiver.h
// NAR_POLL, NAR_GET_LAST_SOCK_ERROR, NAR_INVALID_SOCKET_VALUE are also from network_audio_receiver.h

namespace screamrouter {
namespace audio {

const size_t TARGET_PCM_CHUNK_SIZE = 1152;
const size_t RAW_RECEIVE_BUFFER_SIZE = 2048; // Buffer for recvfrom

// Static member initialization for oRTP removed
// std::mutex RtpReceiver::ortp_init_mutex_;
// bool RtpReceiver::ortp_initialized_ = false;
// int RtpReceiver::ortp_ref_count_ = 0;

// RTP constants remain the same
const int RTP_PAYLOAD_TYPE_L16_48K_STEREO = 127;
const int RTP_CLOCK_RATE_L16_48K_STEREO = 48000;
const int RTP_CHANNELS_L16_48K_STEREO = 2;
const int RTP_BITS_PER_SAMPLE_L16_48K_STEREO = 16;


// global_ortp_init and global_ortp_deinit removed
// void RtpReceiver::global_ortp_init() { ... }
// void RtpReceiver::global_ortp_deinit() { ... }

RtpReceiver::RtpReceiver(
    RtpReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager)
    : NetworkAudioReceiver(config.listen_port, notification_queue, timeshift_manager, "[RtpReceiver]"),
      config_(config), last_known_ssrc_(0), ssrc_initialized_(false) {
    // session_ and profile_ (oRTP specific) removed from initializer list
    // global_ortp_init() call removed
    pcm_accumulator_.reserve(TARGET_PCM_CHUNK_SIZE * 2);
}

RtpReceiver::~RtpReceiver() noexcept {
    // oRTP specific cleanup (session_, profile_) removed.
    // Base class destructor handles stopping thread, which calls close_socket().
    // global_ortp_deinit() call removed
}

// oRTP SSRC Changed Callback (on_ssrc_changed_callback_static) removed.

void RtpReceiver::handle_ssrc_changed(uint32_t old_ssrc, uint32_t new_ssrc) {
    char old_ssrc_hex[12];
    char new_ssrc_hex[12];
    snprintf(old_ssrc_hex, sizeof(old_ssrc_hex), "0x%08X", old_ssrc);
    snprintf(new_ssrc_hex, sizeof(new_ssrc_hex), "0x%08X", new_ssrc);

    log_message("SSRC changed. Old SSRC: " + std::string(old_ssrc_hex) +
                ", New SSRC: " + std::string(new_ssrc_hex) + ". Clearing PCM accumulator.");
    // rtp_session_reset(session_) removed.
    pcm_accumulator_.clear();
    log_message("Internal PCM accumulator cleared due to SSRC change.");
}


// --- Overridden NetworkAudioReceiver Methods ---

bool RtpReceiver::setup_socket() {
    log_message("Setting up raw UDP socket for RTP reception on port " + std::to_string(listen_port_));

    if (socket_fd_ != NAR_INVALID_SOCKET_VALUE) {
        log_warning("setup_socket called but socket_fd_ is already valid. Closing existing socket first.");
        close(socket_fd_);
        socket_fd_ = NAR_INVALID_SOCKET_VALUE;
    }

    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ == NAR_INVALID_SOCKET_VALUE) {
        log_error("Failed to create UDP socket: " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR())));
        return false;
    }

    // Set socket options (e.g., SO_REUSEADDR, SO_RCVBUF)
    int optval = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&optval), sizeof(optval)) < 0) {
        log_warning("Failed to set SO_REUSEADDR: " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR())));
        // Continue anyway
    }

    // Set receive buffer size (e.g., 65536, similar to previous oRTP config)
    // Increased to 256KB to handle potential bursts, similar to other receivers
    int recv_buf_size = 256 * 1024;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recv_buf_size), sizeof(recv_buf_size)) < 0) {
        log_warning("Failed to set SO_RCVBUF to " + std::to_string(recv_buf_size) + ": " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR())));
    } else {
        socklen_t optlen = sizeof(recv_buf_size);
        getsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&recv_buf_size), &optlen);
        log_message("Socket SO_RCVBUF set to: " + std::to_string(recv_buf_size));
    }


    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(listen_port_);

    if (bind(socket_fd_, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        log_error("Failed to bind UDP socket to port " + std::to_string(listen_port_) + ": " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR())));
        close(socket_fd_);
        socket_fd_ = NAR_INVALID_SOCKET_VALUE;
        return false;
    }

    log_message("Raw UDP socket successfully set up and bound to port " + std::to_string(listen_port_));
    return true;
}

void RtpReceiver::close_socket() {
    pcm_accumulator_.clear();
    if (socket_fd_ != NAR_INVALID_SOCKET_VALUE) {
        log_message("Closing raw UDP socket (fd: " + std::to_string(socket_fd_) + ")");
        close(socket_fd_);
        socket_fd_ = NAR_INVALID_SOCKET_VALUE;
    }
    log_message("Raw UDP socket resources released.");
}

void RtpReceiver::run() {
    log_message("RTP receiver thread started using raw socket and libdatachannel parser.");

    if (socket_fd_ == NAR_INVALID_SOCKET_VALUE) {
        log_error("Socket is not initialized. Thread cannot run.");
        return;
    }

    unsigned char raw_buffer[RAW_RECEIVE_BUFFER_SIZE];
    struct sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);

    while (is_running()) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socket_fd_, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0; // Use a shorter timeout for responsiveness
        timeout.tv_usec = get_poll_timeout_ms() * 1000; // Convert ms to us, e.g., 100ms

        int activity = select(socket_fd_ + 1, &readfds, NULL, NULL, &timeout);

        if (!is_running()) break;

        if (activity < 0) {
            if (errno == EINTR) { // Interrupted by a signal
                continue;
            }
            log_error("select() error: " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR())));
            // Consider a short sleep to prevent busy-looping on persistent select errors
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (activity == 0) { // Timeout
            continue;
        }

        if (FD_ISSET(socket_fd_, &readfds)) {
            ssize_t n_received = recvfrom(socket_fd_, raw_buffer, RAW_RECEIVE_BUFFER_SIZE, 0, (struct sockaddr *)&cliaddr, &len);

            if (!is_running()) break;

            if (n_received < 0) {
                // EAGAIN or EWOULDBLOCK might occur if socket is non-blocking, but select should prevent this.
                // However, other errors can occur.
                if (NAR_GET_LAST_SOCK_ERROR() != EAGAIN && NAR_GET_LAST_SOCK_ERROR() != EWOULDBLOCK) {
                     log_error("recvfrom() error: " + std::string(strerror(NAR_GET_LAST_SOCK_ERROR())));
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
                    size_t header_len = rtp_header->headerLength(); // Includes CSRC list if present
                    if (static_cast<size_t>(n_received) >= header_len) {
                        const uint8_t* payload_data = raw_buffer + header_len;
                        size_t payload_len = n_received - header_len;

                        if (payload_len > 0) {
                            pcm_accumulator_.insert(pcm_accumulator_.end(), payload_data, payload_data + payload_len);
                            last_rtp_packet_timestamp_ = received_time;

                            while (pcm_accumulator_.size() >= TARGET_PCM_CHUNK_SIZE) {
                                TaggedAudioPacket packet;
                                packet.received_time = last_rtp_packet_timestamp_;
                                
                                // Use sender's IP address for source_tag
                                char client_ip_str[INET_ADDRSTRLEN];
                                inet_ntop(AF_INET, &(cliaddr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
                                packet.source_tag = client_ip_str;

                                packet.channels = RTP_CHANNELS_L16_48K_STEREO;
                                packet.sample_rate = RTP_CLOCK_RATE_L16_48K_STEREO;
                                packet.bit_depth = RTP_BITS_PER_SAMPLE_L16_48K_STEREO;
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
        } // End if FD_ISSET
    } // End while is_running
    log_message("RTP receiver thread finished.");
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
    // This is used by the base class's poll() if its run() is active,
    // and now also used by select() in our custom run() loop.
    return 100; // ms
}

// The old is_valid_rtp_header_payload method is obsolete.

} // namespace audio
} // namespace screamrouter
