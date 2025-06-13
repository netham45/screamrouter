#ifndef AUDIO_TYPES_H
#define AUDIO_TYPES_H

#include <vector>
#include <string>
#include <chrono>
#include <cstdint> // For fixed-width integers like uint8_t, uint32_t
#include <optional> // For std::optional
// #include "audio_processor.h" // For EQ_BANDS - No longer needed here if defines are moved
#include "audio_constants.h" // Include the new constants file
// #include "../configuration/audio_engine_config_types.h" // For CppSpeakerLayout - No longer needed, CppSpeakerLayout moved here

// MAX_CHANNELS and EQ_BANDS are now in audio_constants.h
// CHUNK_SIZE is still in audio_processor.h as it's more specific to its processing logic

namespace screamrouter {

// Forward declare config namespace for CppSpeakerLayout if it's kept there,
// but we are moving CppSpeakerLayout into the audio namespace for simplicity here.
// namespace config { struct CppSpeakerLayout; }

namespace audio {

// --- CppSpeakerLayout Struct Definition (Moved from audio_engine_config_types.h) ---
// This struct is used by ControlCommand in this file, and by AppliedSourcePathParams in audio_engine_config_types.h
// For AppliedSourcePathParams to use it, it will need to refer to screamrouter::audio::CppSpeakerLayout
// or audio_engine_config_types.h will need to include audio_types.h (which it does).
struct CppSpeakerLayout {
    bool auto_mode = true;
    std::vector<std::vector<float>> matrix;

    CppSpeakerLayout() : auto_mode(true) {
        matrix.assign(MAX_CHANNELS, std::vector<float>(MAX_CHANNELS, 0.0f));
        for (int i = 0; i < MAX_CHANNELS; ++i) {
            if (i < MAX_CHANNELS) { 
                matrix[i][i] = 1.0f; 
            }
        }
    }

    // Equality operator
    bool operator==(const CppSpeakerLayout& other) const {
        if (auto_mode != other.auto_mode) {
            return false;
        }
        // If both are in auto_mode, they are equal regardless of matrix content (as matrix is default/ignored)
        if (auto_mode) {
            return true; 
        }
        // If not in auto_mode, compare matrices
        return matrix == other.matrix;
    }
};
// --- End CppSpeakerLayout Struct Definition ---

// --- Data Structures for Inter-Thread Communication ---

/**
 * @brief Represents a raw audio packet received from the network, tagged with its source.
 *        Passed from RtpReceiver to the corresponding SourceInputProcessor.
 */
struct TaggedAudioPacket {
    std::string source_tag;                     // Identifier for the source (e.g., IP address)
    std::vector<uint8_t> audio_data;            // Audio payload (ALWAYS 1152 bytes of PCM)
    std::chrono::steady_clock::time_point received_time; // Timestamp for timeshifting/jitter
    std::optional<uint32_t> rtp_timestamp;      // Optional RTP timestamp for dejittering
    std::vector<uint32_t> ssrcs;                // SSRC and CSRCs
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
    std::vector<uint32_t> ssrcs;     // SSRC and CSRCs
    // std::string source_tag; // Optional: Could be added if mixer needs to know origin beyond the queue
};

/**
 * @brief Enum defining types of control commands for SourceInputProcessor.
 */
enum class CommandType {
    SET_VOLUME,
    SET_EQ,
    SET_DELAY,
    SET_TIMESHIFT, // Controls the 'backshift' amount
    // --- New Command Type ---
    SET_SPEAKER_MIX
};

/**
 * @brief Represents a command sent from AudioManager to a SourceInputProcessor.
 */
struct ControlCommand {
    CommandType type;
    // Using separate members for simplicity over std::variant for now
    float float_value;         // For volume, timeshift - Initialized in constructor
    int int_value;             // For delay_ms - Initialized in constructor
    std::vector<float> eq_values;     // For EQ bands (size should match AudioProcessor expectation, e.g., 18)

    // --- Updated Speaker Layout Command Members ---
    int input_channel_key;                                  // NEW: Specifies which input channel config this layout is for
    CppSpeakerLayout speaker_layout_for_key; // NEW: The actual layout for that key (now in this namespace)
    // Old members removed:
    // std::vector<std::vector<float>> speaker_mix_matrix; 
    // bool use_auto_speaker_mix;                         

    // Default constructor
    ControlCommand() : type(CommandType::SET_VOLUME), float_value(0.0f), int_value(0), input_channel_key(0) {
        // eq_values will be default constructed (empty)
        // speaker_layout_for_key will be default constructed (auto_mode=true, identity matrix)
    }

    // Consider adding specific constructors or static factory methods for each command type
    // e.g., static ControlCommand CreateSetSpeakerLayoutCommand(int key, const screamrouter::config::CppSpeakerLayout& layout);
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
};


// --- Configuration Structs (Simplified versions for C++ internal use) ---
// These might mirror or be derived from Python-side descriptions

struct SourceConfig {
    std::string tag;
    float initial_volume = 1.0f;
    std::vector<float> initial_eq; // Size EQ_BANDS expected by AudioProcessor
    int initial_delay_ms = 0;
    float initial_timeshift_sec = 0.0f; // Added for TimeshiftManager integration
    int target_output_channels = 2;    // Target output channels for this source path
    int target_output_samplerate = 48000; // Target output samplerate for this source path
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
    // bool use_tcp = false; // Removed
    bool enable_mp3 = false; // Flag to enable MP3 output queue
    // int mp3_bitrate = 192; // Example MP3 setting if needed here
    std::string protocol = "scream"; // "scream" or "rtp"
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

// Configuration for PerProcessScreamReceiver component
struct PerProcessScreamReceiverConfig {
    int listen_port = 16402; // Port for this receiver
    // Add other config options if needed, e.g., interface_ip
};

// Enum to specify the expected input data format for a SourceInputProcessor
enum class InputProtocolType {
    RTP_SCREAM_PAYLOAD,       // Expects raw PCM audio data (RTP payload)
    RAW_SCREAM_PACKET,        // Expects full 5-byte header + PCM audio data
    PER_PROCESS_SCREAM_PACKET // Expects Program Tag + 5-byte header + PCM audio data
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
    float initial_timeshift_sec = 0.0f; // Added for TimeshiftManager integration
    int timeshift_buffer_duration_sec = 5; // Default timeshift buffer duration

    // --- New Speaker Mix Members ---
    std::vector<std::vector<float>> speaker_mix_matrix; // For custom 8x8 matrix
    bool use_auto_speaker_mix;                         // True to use existing dynamic logic

    // Constructor to initialize eq_values if not done by default vector behavior
    // Consider adding a default constructor or ensuring initialization elsewhere
    SourceProcessorConfig() : // output_channels, output_samplerate, initial_volume, initial_delay_ms, initial_timeshift_sec
                              // are already initialized by class member defaults.
                              // The task example re-initializes them here for explicitness.
                              output_channels(2), output_samplerate(48000),
                              initial_volume(1.0f), initial_delay_ms(0), initial_timeshift_sec(0.0f),
                              use_auto_speaker_mix(true) {
        initial_eq.assign(EQ_BANDS, 1.0f); // Default flat EQ
        // Initialize speaker_mix_matrix to an 8x8 identity matrix
        speaker_mix_matrix.resize(MAX_CHANNELS, std::vector<float>(MAX_CHANNELS, 0.0f));
        for (size_t i = 0; i < MAX_CHANNELS; ++i) { // Changed int to size_t
            if (i < speaker_mix_matrix.size() && i < speaker_mix_matrix[i].size()) { // Basic bounds check
                speaker_mix_matrix[i][i] = 1.0f;
            }
        }
    }
};

// Configuration for AudioManager (C++ specific settings)
struct AudioManagerConfigCpp {
    int rtp_listen_port = 4010; // Default from current AudioManager::initialize
    int global_timeshift_buffer_duration_sec = 300; // Default 5 minutes
    // Add other C++ specific AudioManager settings here if needed
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
    std::string protocol = "scream"; // "scream" or "rtp"
};


} // namespace audio
} // namespace screamrouter

#endif // AUDIO_TYPES_H
