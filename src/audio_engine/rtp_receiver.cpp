#include "rtp_receiver.h"
#include "timeshift_manager.h" // For TimeshiftManager operations
#include <iostream>             // For std::cout, std::cerr (used by base logger)
#include <vector>
#include <cstring>              // For memcpy, memset
#include <stdexcept>            // For runtime_error
#include <system_error>         // For socket error checking (though base handles most)
#include <utility>              // For std::move
#include <algorithm>            // For std::find

// Platform-specific includes for inet_ntoa, struct sockaddr_in etc. are in network_audio_receiver.h
// NAR_POLL, NAR_GET_LAST_SOCK_ERROR, NAR_INVALID_SOCKET_VALUE are also from network_audio_receiver.h

namespace screamrouter {
namespace audio {

// Define constants based on original code and RTP standard
const size_t RTP_HEADER_SIZE = 12;
const size_t EXPECTED_CHUNK_SIZE_RTP = 1152; // Specific to RTP receiver's expectation
const size_t EXPECTED_PAYLOAD_SIZE_RTP = RTP_HEADER_SIZE + EXPECTED_CHUNK_SIZE_RTP;
const uint8_t SCREAM_PAYLOAD_TYPE_RTP = 127;

// Define a reasonable buffer size for recvfrom
const size_t RECEIVE_BUFFER_SIZE_RTP = 2048; // Should be larger than EXPECTED_PAYLOAD_SIZE_RTP
const int POLL_TIMEOUT_MS_RTP = 100;   // Check for stop flag every 100ms

RtpReceiver::RtpReceiver(
    RtpReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager)
    : NetworkAudioReceiver(config.listen_port, notification_queue, timeshift_manager, "[RtpReceiver]"),
      config_(config) {
    // Base class constructor handles WSAStartup, null checks for queue/manager, and initial logging.
    // Additional RtpReceiver-specific initialization can go here if needed.
}

RtpReceiver::~RtpReceiver() noexcept {
    // Base class destructor handles stopping, joining thread, closing socket, and WSACleanup.
    // RtpReceiver-specific cleanup can go here if needed.
}

// start(), stop(), run(), setup_socket(), close_socket(), get_seen_tags() are now handled by NetworkAudioReceiver.

bool RtpReceiver::is_valid_rtp_header_payload(const uint8_t* buffer, int size) {
    // Basic size check
    if (static_cast<size_t>(size) < RTP_HEADER_SIZE) {
        log_warning("Packet too small for RTP header (" + std::to_string(size) + " bytes)");
        return false;
    }
    // Check payload type (byte 1, lower 7 bits)
    uint8_t payloadType = buffer[1] & 0x7F;
    if (payloadType != SCREAM_PAYLOAD_TYPE_RTP) {
        log_warning("Invalid RTP payload type: " + std::to_string(payloadType) + ", expected " + std::to_string(SCREAM_PAYLOAD_TYPE_RTP));
        return false;
    }
    // Check if packet size matches expected Scream payload size
    if (static_cast<size_t>(size) != EXPECTED_PAYLOAD_SIZE_RTP) {
        log_warning("Unexpected RTP packet size: " + std::to_string(size) + " bytes, expected " + std::to_string(EXPECTED_PAYLOAD_SIZE_RTP) + " bytes");
        return false;
    }
    return true;
}

// --- NetworkAudioReceiver Pure Virtual Method Implementations ---

bool RtpReceiver::is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) {
    (void)client_addr; // client_addr not used for this basic check in RTP
    // For RTP, the primary structural check is if it's large enough for an RTP header.
    // More detailed validation (payload type, expected size) happens in process_and_validate_payload.
    if (static_cast<size_t>(size) < RTP_HEADER_SIZE) {
        // This might be too noisy if logged here, as process_and_validate_payload will also check.
        // Consider if logging is needed at this stage or if is_valid_rtp_header_payload's logging is sufficient.
        // For now, let the more specific function handle detailed logging.
        return false;
    }
    return true;
}

bool RtpReceiver::process_and_validate_payload(
    const uint8_t* buffer,
    int size,
    const struct sockaddr_in& client_addr,
    std::chrono::steady_clock::time_point received_time,
    TaggedAudioPacket& out_packet,
    std::string& out_source_tag) {

    if (!is_valid_rtp_header_payload(buffer, size)) {
        // Detailed reason logged by is_valid_rtp_header_payload
        char sender_ip_cstr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), sender_ip_cstr, INET_ADDRSTRLEN);
        log_warning("Invalid RTP header/payload from " + std::string(sender_ip_cstr));
        return false;
    }

    // Extract source tag (IP address for RTP)
    char sender_ip_cstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), sender_ip_cstr, INET_ADDRSTRLEN);
    out_source_tag = std::string(sender_ip_cstr);

    // Populate the TaggedAudioPacket
    out_packet.source_tag = out_source_tag;
    out_packet.received_time = received_time;

    // --- Set Default Format for RTP (as per original RtpReceiver logic) ---
    out_packet.channels = 2;
    out_packet.sample_rate = 48000;
    out_packet.bit_depth = 16;
    out_packet.chlayout1 = 0x03; // Stereo L/R default
    out_packet.chlayout2 = 0x00;

    // --- Assign Payload (audio data after RTP header) ---
    out_packet.audio_data.assign(buffer + RTP_HEADER_SIZE,
                                 buffer + size); // size is EXPECTED_PAYLOAD_SIZE_RTP here

    // Additional validation specific to RTP payload content could go here if needed.
    // For now, matching size and payload type is the main criteria.

    return true;
}

size_t RtpReceiver::get_receive_buffer_size() const {
    return RECEIVE_BUFFER_SIZE_RTP;
}

int RtpReceiver::get_poll_timeout_ms() const {
    return POLL_TIMEOUT_MS_RTP;
}

} // namespace audio
} // namespace screamrouter
