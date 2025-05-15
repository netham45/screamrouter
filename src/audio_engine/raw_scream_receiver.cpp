#include "raw_scream_receiver.h"
#include "timeshift_manager.h" // For TimeshiftManager operations
#include <iostream>             // For std::cout, std::cerr (used by base logger)
#include <vector>
#include <cstring>              // For memcpy, memset
#include <stdexcept>            // For runtime_error
#include <utility>              // For std::move
#include <algorithm>            // For std::find

// Platform-specific includes for inet_ntoa, struct sockaddr_in etc. are in network_audio_receiver.h

namespace screamrouter {
namespace audio {

// Define constants
const size_t RAW_SCREAM_HEADER_SIZE = 5;
const size_t RAW_CHUNK_SIZE = 1152;
const size_t EXPECTED_RAW_PACKET_SIZE = RAW_SCREAM_HEADER_SIZE + RAW_CHUNK_SIZE; // 5 + 1152 = 1157
const size_t RAW_RECEIVE_BUFFER_SIZE_CONFIG = 2048; // Should be larger than EXPECTED_RAW_PACKET_SIZE
const int RAW_POLL_TIMEOUT_MS_CONFIG = 100;   // Check for stop flag every 100ms

RawScreamReceiver::RawScreamReceiver(
    RawScreamReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager)
    : NetworkAudioReceiver(config.listen_port, notification_queue, timeshift_manager, "[RawScreamReceiver]"),
      config_(config) {
    // Base class constructor handles WSAStartup, null checks for queue/manager, and initial logging.
}

RawScreamReceiver::~RawScreamReceiver() noexcept {
    // Base class destructor handles stopping, joining thread, closing socket, and WSACleanup.
}

// --- NetworkAudioReceiver Pure Virtual Method Implementations ---

bool RawScreamReceiver::is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) {
    (void)buffer; // Buffer content not checked here, only size
    (void)client_addr; // Client address not used for this basic check
    
    if (static_cast<size_t>(size) != EXPECTED_RAW_PACKET_SIZE) {
        // Logging this here might be too verbose if many non-matching packets arrive.
        // The base class's run loop can log a generic "invalid packet structure" if this returns false.
        // Or, process_and_validate_payload can log if it also finds this to be an issue.
        // For now, keep it silent here, relying on higher-level logging.
        return false;
    }
    return true;
}

bool RawScreamReceiver::validate_raw_scream_content(const uint8_t* buffer, int size, TaggedAudioPacket& out_packet) {
    // Assumes buffer is not null and size is EXPECTED_RAW_PACKET_SIZE based on is_valid_packet_structure
    if (static_cast<size_t>(size) != EXPECTED_RAW_PACKET_SIZE) { // Defensive check
        log_warning("validate_raw_scream_content called with unexpected size: " + std::to_string(size));
        return false;
    }

    const uint8_t* header = buffer;
    bool is_44100_base = (header[0] >> 7) & 0x01;
    uint8_t samplerate_divisor = header[0] & 0x7F;
    if (samplerate_divisor == 0) samplerate_divisor = 1; // As per original logic

    out_packet.sample_rate = (is_44100_base ? 44100 : 48000) / samplerate_divisor;
    out_packet.bit_depth = static_cast<int>(header[1]);
    out_packet.channels = static_cast<int>(header[2]);
    out_packet.chlayout1 = header[3];
    out_packet.chlayout2 = header[4];

    // Basic validation of parsed format
    if (out_packet.channels <= 0 || out_packet.channels > 64 || // Max channels reasonable limit
        (out_packet.bit_depth != 8 && out_packet.bit_depth != 16 && out_packet.bit_depth != 24 && out_packet.bit_depth != 32) ||
        out_packet.sample_rate <= 0) {
        log_warning("Parsed invalid audio format from Raw Scream packet. SR=" + std::to_string(out_packet.sample_rate) +
                      ", BD=" + std::to_string(out_packet.bit_depth) + ", CH=" + std::to_string(out_packet.channels));
        return false;
    }

    // Assign Payload Only
    out_packet.audio_data.assign(buffer + RAW_SCREAM_HEADER_SIZE,
                                 buffer + RAW_SCREAM_HEADER_SIZE + RAW_CHUNK_SIZE);
    
    if (out_packet.audio_data.size() != RAW_CHUNK_SIZE) { // Should always be true if size == EXPECTED_RAW_PACKET_SIZE
        log_error("Internal error: audio data size mismatch. Expected " + std::to_string(RAW_CHUNK_SIZE) +
                  ", got " + std::to_string(out_packet.audio_data.size()));
        return false;
    }
    
    return true;
}


bool RawScreamReceiver::process_and_validate_payload(
    const uint8_t* buffer,
    int size,
    const struct sockaddr_in& client_addr,
    std::chrono::steady_clock::time_point received_time,
    TaggedAudioPacket& out_packet,
    std::string& out_source_tag) {

    // is_valid_packet_structure (size check) should have already been called by the base class.
    // If we are here, size should be EXPECTED_RAW_PACKET_SIZE.

    // Extract source tag (IP address for Raw Scream)
    char sender_ip_cstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), sender_ip_cstr, INET_ADDRSTRLEN);
    out_source_tag = std::string(sender_ip_cstr);

    // Populate common parts of the TaggedAudioPacket
    out_packet.source_tag = out_source_tag;
    out_packet.received_time = received_time;

    // Parse header, set format, and assign payload
    if (!validate_raw_scream_content(buffer, size, out_packet)) {
        log_warning("Invalid Raw Scream packet content from " + out_source_tag + 
                    ". Size: " + std::to_string(size) + " bytes.");
        return false;
    }
    
    return true;
}

size_t RawScreamReceiver::get_receive_buffer_size() const {
    return RAW_RECEIVE_BUFFER_SIZE_CONFIG;
}

int RawScreamReceiver::get_poll_timeout_ms() const {
    return RAW_POLL_TIMEOUT_MS_CONFIG;
}

} // namespace audio
} // namespace screamrouter
