#include "raw_scream_receiver.h"
#include "../../input_processor/timeshift_manager.h" // For TimeshiftManager operations
#include "../../configuration/audio_engine_settings.h"
#include <iostream>             // For std::cout, std::cerr (used by base logger)
#include <vector>
#include <cstring>              // For memcpy, memset
#include <stdexcept>            // For runtime_error
#include <utility>              // For std::move
#include <algorithm>            // For std::find

// Platform-specific includes for inet_ntoa, struct sockaddr_in etc. are in network_audio_receiver.h

namespace screamrouter {
namespace audio {

namespace {
constexpr std::size_t kRawScreamHeaderSize = 5;
constexpr std::size_t kRawScreamPayloadBytes = 1152;
constexpr std::size_t kMinimumReceiveBufferSize = 2048; // Should be larger than packet size
constexpr int kRawPollTimeoutMs = 5;   // Check for stop flag every 5ms to service clock ticks promptly
}

RawScreamReceiver::RawScreamReceiver(
    RawScreamReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager,
    std::string logger_prefix)
    : NetworkAudioReceiver(config.listen_port,
                           notification_queue,
                           timeshift_manager,
                           logger_prefix,
                           resolve_base_frames_per_chunk(timeshift_manager ? timeshift_manager->get_settings() : nullptr)),
      config_(config),
      expected_packet_size_(kRawScreamHeaderSize + kRawScreamPayloadBytes) {
    // Base class constructor handles WSAStartup, null checks for queue/manager, and initial logging.
}

RawScreamReceiver::~RawScreamReceiver() noexcept {
    // Base class destructor handles stopping, joining thread, closing socket, and WSACleanup.
}

// --- NetworkAudioReceiver Pure Virtual Method Implementations ---

bool RawScreamReceiver::is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) {
    (void)buffer; // Buffer content not checked here, only size
    (void)client_addr; // Client address not used for this basic check
    
    if (static_cast<size_t>(size) != expected_packet_size_) {
        // Logging this here might be too verbose if many non-matching packets arrive.
        // The base class's run loop can log a generic "invalid packet structure" if this returns false.
        // Or, process_and_validate_payload can log if it also finds this to be an issue.
        // For now, keep it silent here, relying on higher-level logging.
        return false;
    }
    return true;
}

bool RawScreamReceiver::populate_tagged_packet(const uint8_t* buffer,
                                               int size,
                                               TaggedAudioPacket& packet) {
    if (static_cast<size_t>(size) != expected_packet_size_) {
        log_warning("populate_tagged_packet called with unexpected size: " + std::to_string(size));
        return false;
    }

    const uint8_t* header = buffer;
    bool is_44100_base = (header[0] >> 7) & 0x01;
    uint8_t samplerate_divisor = header[0] & 0x7F;
    if (samplerate_divisor == 0) {
        samplerate_divisor = 1;
    }

    packet.sample_rate = (is_44100_base ? 44100 : 48000) / samplerate_divisor;
    packet.bit_depth = static_cast<int>(header[1]);
    packet.channels = static_cast<int>(header[2]);
    packet.chlayout1 = header[3];
    packet.chlayout2 = header[4];

    if (packet.channels <= 0 || packet.channels > 64 ||
        (packet.bit_depth != 8 && packet.bit_depth != 16 && packet.bit_depth != 24 && packet.bit_depth != 32) ||
        packet.sample_rate <= 0) {
        log_warning("Parsed invalid audio format from Raw Scream packet. SR=" + std::to_string(packet.sample_rate) +
                    ", BD=" + std::to_string(packet.bit_depth) + ", CH=" + std::to_string(packet.channels));
        return false;
    }

    packet.audio_data.assign(buffer + kRawScreamHeaderSize,
                             buffer + kRawScreamHeaderSize + kRawScreamPayloadBytes);
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
    // If we are here, size should be expected_packet_size_.

    // Extract source tag (IP address for Raw Scream)
    char sender_ip_cstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), sender_ip_cstr, INET_ADDRSTRLEN);
    out_source_tag = std::string(sender_ip_cstr);

    out_packet.source_tag = out_source_tag;
    out_packet.received_time = received_time;

    if (!populate_tagged_packet(buffer, size, out_packet)) {
        log_warning("Invalid Raw Scream packet content from " + out_source_tag +
                    ". Size: " + std::to_string(size) + " bytes.");
        return false;
    }

    register_source_tag(out_source_tag);
    return true;
}

size_t RawScreamReceiver::get_receive_buffer_size() const {
    return std::max<std::size_t>(expected_packet_size_, kMinimumReceiveBufferSize);
}

int RawScreamReceiver::get_poll_timeout_ms() const {
    return kRawPollTimeoutMs;
}

} // namespace audio
} // namespace screamrouter
