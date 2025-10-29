#include "per_process_scream_receiver.h"
#include "../../input_processor/timeshift_manager.h" // For TimeshiftManager operations
#include "../../configuration/audio_engine_settings.h"
#include <iostream>             // For std::cout, std::cerr (used by base logger)
#include <vector>
#include <cstring>              // For memcpy, memset, strncpy
#include <stdexcept>            // For runtime_error
#include <utility>              // For std::move
#include <algorithm>            // For std::find, std::string::erase, std::string::find_last_not_of

// Platform-specific includes for inet_ntoa, struct sockaddr_in etc. are in network_audio_receiver.h

namespace screamrouter {
namespace audio {

namespace {
constexpr std::size_t kProgramTagSize = 30;
constexpr std::size_t kScreamHeaderSize = 5;
constexpr std::size_t kMinimumReceiveBufferSize = 2048; // Should exceed expected packet size
constexpr int kPollTimeoutMs = 5;   // Check for stop flag every 5ms to service clock ticks promptly
}

PerProcessScreamReceiver::PerProcessScreamReceiver(
    PerProcessScreamReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager,
    ClockManager* clock_manager,
    std::string logger_prefix)
    : NetworkAudioReceiver(config.listen_port,
                           notification_queue,
                           timeshift_manager,
                           logger_prefix,
                           clock_manager,
                           resolve_chunk_size_bytes(timeshift_manager ? timeshift_manager->get_settings() : nullptr)),
      config_(config),
      chunk_size_bytes_(resolve_chunk_size_bytes(timeshift_manager ? timeshift_manager->get_settings() : nullptr)),
      expected_packet_size_(kProgramTagSize + kScreamHeaderSize + chunk_size_bytes_) {
    if (!clock_manager_) {
        throw std::runtime_error("PerProcessScreamReceiver requires a valid ClockManager instance");
    }
    // Base class constructor handles WSAStartup, null checks for queue/manager, and initial logging.
}

PerProcessScreamReceiver::~PerProcessScreamReceiver() noexcept {
    // Base class destructor handles stopping, joining thread, closing socket, and WSACleanup.
}

// --- NetworkAudioReceiver Pure Virtual Method Implementations ---

bool PerProcessScreamReceiver::is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) {
    (void)buffer;      // Buffer content not checked here, only size
    (void)client_addr; // Client address not used for this basic check
    
    if (static_cast<size_t>(size) != expected_packet_size_) {
        return false;
    }
    return true;
}

bool PerProcessScreamReceiver::validate_per_process_scream_content(
    const uint8_t* buffer, 
    int size, 
    const std::string& sender_ip, // Already converted to string
    TaggedAudioPacket& out_packet, 
    std::string& out_composite_source_tag) {

    // Assumes buffer is not null and size is EXPECTED_PPSR_PACKET_SIZE
     if (static_cast<size_t>(size) != expected_packet_size_) { // Defensive check
        log_warning("validate_per_process_scream_content called with unexpected size: " + std::to_string(size));
        return false;
    }

    // Extract Program Tag
    char program_tag_cstr[kProgramTagSize + 1];
    strncpy(program_tag_cstr, reinterpret_cast<const char*>(buffer), kProgramTagSize);
    program_tag_cstr[kProgramTagSize] = '\0'; // Ensure null termination
    std::string program_tag(program_tag_cstr);
    
    // Trim trailing spaces from program_tag
    size_t last_char = program_tag.find_last_not_of(" \n\r\t");
    if (std::string::npos != last_char) {
        program_tag.erase(last_char + 1);
    } else {
        program_tag.clear(); // All spaces or empty
    }
    
    // Format sender_ip to be 15 chars, space-padded
    std::string fixed_sender_ip(15, ' ');
    if (sender_ip.length() <= 15) {
        fixed_sender_ip.replace(0, sender_ip.length(), sender_ip);
    } else {
        fixed_sender_ip.replace(0, 15, sender_ip.substr(0, 15)); // Truncate if too long
    }
    out_composite_source_tag = fixed_sender_ip + program_tag;

    // --- Parse Header and Set Format ---
    const uint8_t* header = buffer + kProgramTagSize;
    bool is_44100_base = (header[0] >> 7) & 0x01;
    uint8_t samplerate_divisor = header[0] & 0x7F;
    if (samplerate_divisor == 0) samplerate_divisor = 1;

    out_packet.sample_rate = (is_44100_base ? 44100 : 48000) / samplerate_divisor;
    out_packet.bit_depth = static_cast<int>(header[1]);
    out_packet.channels = static_cast<int>(header[2]);
    out_packet.chlayout1 = header[3];
    out_packet.chlayout2 = header[4];

    // Basic validation of parsed format
    if (out_packet.channels <= 0 || out_packet.channels > 64 ||
        (out_packet.bit_depth != 8 && out_packet.bit_depth != 16 && out_packet.bit_depth != 24 && out_packet.bit_depth != 32) ||
        out_packet.sample_rate <= 0) {
        log_warning("Parsed invalid audio format from PerProcess Scream packet for " + out_composite_source_tag +
                      ". SR=" + std::to_string(out_packet.sample_rate) +
                      ", BD=" + std::to_string(out_packet.bit_depth) + ", CH=" + std::to_string(out_packet.channels));
        return false;
    }

    // --- Assign Payload Only ---
    const uint8_t* audio_payload_start = buffer + kProgramTagSize + kScreamHeaderSize;
    out_packet.audio_data.assign(audio_payload_start, audio_payload_start + chunk_size_bytes_);

    if (out_packet.audio_data.size() != chunk_size_bytes_) {
        log_error("Internal error: audio data size mismatch for " + out_composite_source_tag +
                  ". Expected " + std::to_string(chunk_size_bytes_) +
                  ", got " + std::to_string(out_packet.audio_data.size()));
        return false;
    }
    
    return true;
}

bool PerProcessScreamReceiver::process_and_validate_payload(
    const uint8_t* buffer,
    int size,
    const struct sockaddr_in& client_addr,
    std::chrono::steady_clock::time_point received_time,
    TaggedAudioPacket& out_packet,
    std::string& out_source_tag // This will become the composite tag
) {
    // is_valid_packet_structure (size check) should have already been called by the base class.

    char sender_ip_cstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), sender_ip_cstr, INET_ADDRSTRLEN);
    std::string sender_ip_str(sender_ip_cstr);

    // Populate common parts of the TaggedAudioPacket
    out_packet.received_time = received_time;
    // out_source_tag will be set by validate_per_process_scream_content

    if (!validate_per_process_scream_content(buffer, size, sender_ip_str, out_packet, out_source_tag)) {
        // validate_per_process_scream_content already logs details
        log_warning("Invalid PerProcess Scream packet content from " + sender_ip_str + 
                    ". Size: " + std::to_string(size) + " bytes.");
        return false;
    }
    
    // out_source_tag is now the composite tag, assign it to packet.source_tag
    out_packet.source_tag = out_source_tag;
    
    return true;
}

size_t PerProcessScreamReceiver::get_receive_buffer_size() const {
    return std::max<std::size_t>(expected_packet_size_, kMinimumReceiveBufferSize);
}

int PerProcessScreamReceiver::get_poll_timeout_ms() const {
    return kPollTimeoutMs;
}


} // namespace audio
} // namespace screamrouter
