#ifndef AUDIO_TYPES_H
#define AUDIO_TYPES_H

#include <vector>
#include <string>
#include <chrono>
#include <cstdint> // For fixed-width integers like uint8_t, uint32_t
#include "../c_utils/audio_processor.h" // For EQ_BANDS

namespace screamrouter {
namespace audio {

// --- Data Structures for Inter-Thread Communication ---

/**
 * @brief Represents a raw audio packet received from the network, tagged with its source.
 *        Passed from RtpReceiver to the corresponding SourceInputProcessor.
 */
struct TaggedAudioPacket {
    std::string source_tag;                     // Identifier for the source (e.g., IP address)
    std::vector<uint8_t> audio_data;            // Audio payload (ALWAYS 1152 bytes of PCM)
    std::chrono::steady_clock::time_point received_time; // Timestamp for timeshifting/jitter
    // --- Added Format Info ---
    int channels = 0;                           // Number of audio channels in payload
    int sample_rate = 0;                        // Sample rate of audio in payload
    int bit_depth = 0;                          // Bit depth of audio in payload
    uint8_t chlayout1 = 0;                      // Scream channel layout byte 1
    uint8_t chlayout2 = 0;                      // Scream channel layout byte 2
};

/**
 * @brief Represents a chunk of audio data after processing by a SourceInputProcessor.
 *        Passed from SourceInputProcessor to SinkAudioMixer(s).
 */
struct ProcessedAudioChunk {
    // Changed to int32_t to match the signed nature of processed PCM data
    std::vector<int32_t> audio_data; // Processed audio data (e.g., 288 samples of int32_t for 1152 bytes)
    // std::string source_tag; // Optional: Could be added if mixer needs to know origin beyond the queue
};

/**
 * @brief Enum defining types of control commands for SourceInputProcessor.
 */
enum class CommandType {
    SET_VOLUME,
    SET_EQ,
    SET_DELAY,
    SET_TIMESHIFT // Controls the 'backshift' amount
};

/**
 * @brief Represents a command sent from AudioManager to a SourceInputProcessor.
 */
struct ControlCommand {
    CommandType type;
    // Using separate members for simplicity over std::variant for now
    float float_value = 0.0f;         // For volume, timeshift
    int int_value = 0;                // For delay_ms
    std::vector<float> eq_values;     // For EQ bands (size should match AudioProcessor expectation, e.g., 18)
};

/**
 * @brief Notification sent from RtpReceiver to AudioManager when a new source is detected.
 */
struct NewSourceNotification {
    std::string source_tag; // Identifier (IP address) of the new source
};

/**
 * @brief Represents a chunk of MP3 encoded audio data.
 *        Passed from SinkAudioMixer to AudioManager/Python layer.
 */
struct EncodedMP3Data {
    std::vector<uint8_t> mp3_data; // Chunk of MP3 bytes
    // std::string sink_id; // Optional: Could be added if consumer needs to know origin sink
};


// --- Configuration Structs (Simplified versions for C++ internal use) ---
// These might mirror or be derived from Python-side descriptions

struct SourceConfig {
    std::string tag;
    float initial_volume = 1.0f;
    std::vector<float> initial_eq; // Size EQ_BANDS expected by AudioProcessor
    int initial_delay_ms = 0;
    // Timeshift duration is often global or sink-related, managed in SourceInputProcessor config

    // --- NEW FIELDS ---
    int target_output_channels = 2;    // Target output channels for this source path
    int target_output_samplerate = 48000; // Target output samplerate for this source path
    // --- END NEW FIELDS ---
    int protocol_type_hint = 0; // Add this line (0 for RTP_SCREAM_PAYLOAD, 1 for RAW_SCREAM_PACKET)
    int target_receiver_port = -1; // Add this line
};

struct SinkConfig {
    std::string id; // Unique ID for this sink instance
    std::string output_ip;
    int output_port;
    int bitdepth = 16;
    int samplerate = 48000;
    int channels = 2;
    uint8_t chlayout1 = 0x03; // Default Stereo L/R
    uint8_t chlayout2 = 0x00;
    bool use_tcp = false;
    bool enable_mp3 = false; // Flag to enable MP3 output queue
    // int mp3_bitrate = 192; // Example MP3 setting if needed here
};

// Configuration for RtpReceiver component
struct RtpReceiverConfig {
    int listen_port = 40000; // Default port
    // std::string bind_ip = "0.0.0.0"; // Optional: Interface IP to bind to
};

// Configuration for RawScreamReceiver component
struct RawScreamReceiverConfig {
    int listen_port = 4010; // Default port (adjust if needed)
    // std::string bind_ip = "0.0.0.0"; // Optional: Interface IP to bind to
};

// Enum to specify the expected input data format for a SourceInputProcessor
enum class InputProtocolType {
    RTP_SCREAM_PAYLOAD, // Expects raw PCM audio data (RTP payload)
    RAW_SCREAM_PACKET   // Expects full 5-byte header + PCM audio data
};

// Configuration for SourceInputProcessor component
struct SourceProcessorConfig {
    std::string instance_id; // Unique identifier for this specific processor instance
    std::string source_tag; // Identifier (IP or user tag) - potentially shared
    // Target format (from Sink) - Needed for AudioProcessor initialization
    int output_channels = 2;         // This will be populated from SourceConfig.target_output_channels
    int output_samplerate = 48000;   // This will be populated from SourceConfig.target_output_samplerate
    // Initial settings (from SourceConfig)
    float initial_volume = 1.0f; // Corrected default to 1.0f to match typical usage
    std::vector<float> initial_eq; // Default constructor will handle empty, or AudioManager can default
    int initial_delay_ms = 0;
    int timeshift_buffer_duration_sec = 5; // Default timeshift buffer duration

    // Constructor to initialize eq_values if not done by default vector behavior
    SourceProcessorConfig() : initial_eq(EQ_BANDS, 1.0f) {} // Ensure EQ is initialized
    InputProtocolType protocol_type = InputProtocolType::RTP_SCREAM_PAYLOAD; // Add this line
    int target_receiver_port = -1; // Add this line
    // Input format hints (if needed, otherwise assume standard like 16-bit, 48kHz, 2ch)
    // int input_channels = 2;
    // int input_samplerate = 48000;
    // int input_bitdepth = 16;
};

// Configuration for SinkAudioMixer component
struct SinkMixerConfig {
    std::string sink_id; // Unique identifier (e.g., IP:Port or name)
    std::string output_ip;
    int output_port;
    int output_bitdepth;
    int output_samplerate;
    int output_channels;
    uint8_t output_chlayout1;
    uint8_t output_chlayout2;
    bool use_tcp;
    // MP3 Encoding settings (if applicable)
    // bool enable_mp3_output; // Determined by whether mp3_output_queue is provided
    // int mp3_bitrate = 192; // Example setting for LAME
};


} // namespace audio
} // namespace screamrouter

#endif // AUDIO_TYPES_H
