#ifndef AUDIO_TYPES_H
#define AUDIO_TYPES_H

#include <vector>
#include <string>
#include <chrono>
#include <cstdint> // For fixed-width integers like uint8_t, uint32_t
#include <optional> // For std::optional
#include "audio_constants.h" // Include the new constants file
#include <map>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/operators.h> // For operator overloads
#include "utils/thread_safe_queue.h"

// MAX_CHANNELS and EQ_BANDS are now in audio_constants.h
// CHUNK_SIZE is still in audio_processor.h as it's more specific to its processing logic

namespace screamrouter {
namespace audio {

/**
 * @brief Defines a speaker mixing matrix and its operational mode.
 * @details This struct is used to configure how input channels are mapped to output channels,
 *          either through an automatic pass-through or a custom user-defined matrix.
 */
struct CppSpeakerLayout {
    bool auto_mode = true;                                  // True for auto mix (default), false for using the custom matrix.
    std::vector<std::vector<float>> matrix;                 // 8x8 speaker mix matrix, defining gain from input (row) to output (col).

    /**
     * @brief Default constructor.
     * @details Initializes in auto_mode with an identity matrix.
     */
    CppSpeakerLayout() : auto_mode(true) {
        matrix.assign(MAX_CHANNELS, std::vector<float>(MAX_CHANNELS, 0.0f));
        for (int i = 0; i < MAX_CHANNELS; ++i) {
            if (i < MAX_CHANNELS) { 
                matrix[i][i] = 1.0f; 
            }
        }
    }

    /**
     * @brief Equality operator for comparing two speaker layouts.
     */
    bool operator==(const CppSpeakerLayout& other) const {
        if (auto_mode != other.auto_mode) {
            return false;
        }
        // If both are in auto_mode, they are considered equal.
        if (auto_mode) {
            return true; 
        }
        // If not in auto_mode, the matrices must match.
        return matrix == other.matrix;
    }
};

// --- Data Structures for Inter-Thread Communication ---

/**
 * @brief Represents a raw audio packet received from the network, tagged with its source.
 * @details Passed from a receiver component to the corresponding SourceInputProcessor.
 */
struct TaggedAudioPacket {
    std::string source_tag;                     // Identifier for the source (e.g., IP address or user tag).
    std::vector<uint8_t> audio_data;            // Raw audio payload (e.g., 1152 bytes of PCM).
    std::chrono::steady_clock::time_point received_time; // Timestamp for timeshifting/jitter buffer.
    std::optional<uint32_t> rtp_timestamp;      // Optional RTP timestamp for dejittering.
    std::vector<uint32_t> ssrcs;                // List of SSRC and CSRCs from the RTP header.
    // --- Audio Format Info ---
    int channels = 0;                           // Number of audio channels in the payload.
    int sample_rate = 0;                        // Sample rate of the audio in the payload.
    int bit_depth = 0;                          // Bit depth of the audio in the payload.
    uint8_t chlayout1 = 0;                      // Scream channel layout byte 1.
    uint8_t chlayout2 = 0;                      // Scream channel layout byte 2.
};

/**
 * @brief Represents a chunk of audio data after processing by a SourceInputProcessor.
 * @details Passed from a SourceInputProcessor to one or more SinkAudioMixer(s).
 */
struct ProcessedAudioChunk {
    std::vector<int32_t> audio_data; // Processed audio data as 32-bit signed integers.
    std::vector<uint32_t> ssrcs;     // SSRC and CSRCs, forwarded from the original packet.
};

/**
 * @brief Enum defining types of control commands for a SourceInputProcessor.
 */
enum class CommandType {
    SET_VOLUME,                 // Change the volume level.
    SET_EQ,                     // Set the equalizer band values.
    SET_DELAY,                  // Set the delay in integer milliseconds.
    SET_TIMESHIFT,              // Set the timeshift delay in float seconds.
    SET_EQ_NORMALIZATION,       // Enable or disable equalizer normalization.
    SET_VOLUME_NORMALIZATION,   // Enable or disable volume normalization.
    SET_SPEAKER_MIX             // Set the speaker layout mapping.
};

/**
 * @brief Represents a command sent from AudioManager to a SourceInputProcessor.
 */
struct ControlCommand {
    CommandType type;                                       // The type of command to execute.
    float float_value;                                      // Value for volume, timeshift.
    int int_value;                                          // Value for delay_ms.
    std::vector<float> eq_values;                           // Values for all EQ bands.
    int input_channel_key;                                  // Key for the speaker layout map (e.g., number of input channels).
    CppSpeakerLayout speaker_layout_for_key;                // The speaker layout to apply for the given key.

    /**
     * @brief Default constructor.
     */
    ControlCommand() : type(CommandType::SET_VOLUME), float_value(0.0f), int_value(0), input_channel_key(0) {}
};

/**
 * @brief Notification sent from a receiver to AudioManager when a new source is detected.
 */
struct NewSourceNotification {
    std::string source_tag; // Identifier (e.g., IP address) of the new source.
};

/**
 * @brief Represents a chunk of MP3 encoded audio data.
 * @details Passed from a SinkAudioMixer to the AudioManager for consumption by Python.
 */
struct EncodedMP3Data {
    std::vector<uint8_t> mp3_data; // A chunk of MP3-encoded bytes.
};


// --- Configuration Structs (For C++ internal use) ---

/**
 * @brief Initial configuration for a single audio source path.
 */
struct SourceConfig {
    std::string tag;                            // Unique identifier for the source (e.g., IP address).
    float initial_volume = 1.0f;                // Initial volume level (0.0 to 1.0+).
    std::vector<float> initial_eq;              // Initial equalizer settings (size EQ_BANDS).
    int initial_delay_ms = 0;                   // Initial delay in milliseconds.
    float initial_timeshift_sec = 0.0f;         // Initial timeshift in seconds.
    int target_output_channels = 2;             // Required output channels for this source's processing path.
    int target_output_samplerate = 48000;       // Required output sample rate for this source's processing path.
};

/**
 * @brief Configuration for a single audio sink (output).
 */
struct SinkConfig {
    std::string id;                             // Unique ID for this sink instance.
    std::string output_ip;                      // Destination IP address for UDP output.
    int output_port;                            // Destination port for UDP output.
    int bitdepth = 16;                          // Output bit depth (e.g., 16, 24, 32).
    int samplerate = 48000;                     // Output sample rate (e.g., 44100, 48000).
    int channels = 2;                           // Output channel count.
    uint8_t chlayout1 = 0x03;                   // Scream header channel layout byte 1.
    uint8_t chlayout2 = 0x00;                   // Scream header channel layout byte 2.
    bool enable_mp3 = false;                    // Flag to enable the MP3 output queue for this sink.
    std::string protocol = "scream";            // Network protocol ("scream", "rtp", "web_receiver").
    CppSpeakerLayout speaker_layout;            // Speaker layout configuration for this sink.
};

/**
 * @brief Configuration for the main RTP receiver component.
 */
struct RtpReceiverConfig {
    int listen_port = 40000;                    // Default UDP port to listen on for RTP packets.
    std::vector<std::string> known_ips;         // Pre-configured list of known source IPs.
};

/**
 * @brief Configuration for a raw Scream receiver component.
 */
struct RawScreamReceiverConfig {
    int listen_port = 4010;                     // UDP port to listen on for raw Scream packets.
};

/**
 * @brief Configuration for a per-process Scream receiver component.
 */
struct PerProcessScreamReceiverConfig {
    int listen_port = 16402;                    // UDP port to listen on for per-process Scream packets.
};

/**
 * @brief Enum to specify the expected input data format for a SourceInputProcessor.
 */
enum class InputProtocolType {
    RTP_SCREAM_PAYLOAD,       // Expects raw PCM audio data (from an RTP payload).
    RAW_SCREAM_PACKET,        // Expects a full Scream packet (5-byte header + PCM).
    PER_PROCESS_SCREAM_PACKET // Expects a per-process packet (Program Tag + 5-byte header + PCM).
};

/**
 * @brief Configuration for a SourceInputProcessor component.
 */
struct SourceProcessorConfig {
    std::string instance_id;                    // Unique identifier for this specific processor instance.
    std::string source_tag;                     // Identifier of the source this processor handles (IP or user tag).
    int output_channels = 2;                    // Target output channels, populated from SinkConfig.
    int output_samplerate = 48000;              // Target output sample rate, populated from SinkConfig.
    float initial_volume = 1.0f;                // Initial volume level.
    std::vector<float> initial_eq;              // Initial equalizer settings.
    int initial_delay_ms = 0;                   // Initial delay in milliseconds.
    float initial_timeshift_sec = 0.0f;         // Initial timeshift in seconds.
    int timeshift_buffer_duration_sec = 5;      // Duration of the internal timeshift buffer in seconds.
    std::vector<std::vector<float>> speaker_mix_matrix; // Custom 8x8 speaker mix matrix.
    bool use_auto_speaker_mix;                  // True to use dynamic mixing logic, false to use the matrix.

    /**
     * @brief Default constructor.
     */
   SourceProcessorConfig() : use_auto_speaker_mix(true) {
       initial_eq.assign(screamrouter::audio::EQ_BANDS, 1.0f); // Default to a flat EQ.
       // Initialize speaker_mix_matrix to an 8x8 identity matrix.
       speaker_mix_matrix.resize(screamrouter::audio::MAX_CHANNELS, std::vector<float>(screamrouter::audio::MAX_CHANNELS, 0.0f));
       for (size_t i = 0; i < screamrouter::audio::MAX_CHANNELS; ++i) {
           if (i < speaker_mix_matrix.size() && i < speaker_mix_matrix[i].size()) {
               speaker_mix_matrix[i][i] = 1.0f;
           }
        }
    }
};

/**
 * @brief C++ specific configuration for the AudioManager.
 */
struct AudioManagerConfigCpp {
    int rtp_listen_port = 4010;                 // Default main RTP listen port.
    int global_timeshift_buffer_duration_sec = 300; // Default global timeshift buffer duration (5 minutes).
};

/**
 * @brief Configuration for a SinkAudioMixer component.
 */
struct SinkMixerConfig {
    std::string sink_id;                        // Unique identifier for the sink this mixer serves.
    std::string output_ip;                      // Destination IP address.
    int output_port;                            // Destination port.
    int output_bitdepth;                        // Output bit depth.
    int output_samplerate;                      // Output sample rate.
    int output_channels;                        // Output channel count.
    uint8_t output_chlayout1;                   // Scream header channel layout byte 1.
    uint8_t output_chlayout2;                   // Scream header channel layout byte 2.
    std::string protocol = "scream";            // Network protocol ("scream" or "rtp").
    CppSpeakerLayout speaker_layout;            // Speaker layout for RTP channel mapping.
};


/**
 * @brief A struct to hold a batch of parameter updates for a source.
 * @details Using std::optional allows for updating only specified parameters atomically.
 */
struct SourceParameterUpdates {
    std::optional<float> volume;                                    // Optional new volume level.
    std::optional<std::vector<float>> eq_values;                    // Optional new EQ band values.
    std::optional<bool> eq_normalization;                           // Optional new EQ normalization state.
    std::optional<bool> volume_normalization;                       // Optional new volume normalization state.
    std::optional<int> delay_ms;                                    // Optional new delay in milliseconds.
    std::optional<float> timeshift_sec;                             // Optional new timeshift in seconds.
    std::optional<std::map<int, CppSpeakerLayout>> speaker_layouts_map; // Optional new map of speaker layouts.
    };
    
// --- Queue Type Aliases ---
using PacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>;
using ChunkQueue = utils::ThreadSafeQueue<ProcessedAudioChunk>;
using CommandQueue = utils::ThreadSafeQueue<ControlCommand>;
using Mp3Queue = utils::ThreadSafeQueue<EncodedMP3Data>;
using NotificationQueue = utils::ThreadSafeQueue<NewSourceNotification>;
using ListenerRemovalRequest = std::pair<std::string, std::string>;
using ListenerRemovalQueue = utils::ThreadSafeQueue<ListenerRemovalRequest>;

    inline void bind_audio_types(pybind11::module_ &m) {
        namespace py = pybind11;
    
        py::class_<SourceConfig>(m, "SourceConfig", "Configuration for an audio source")
            .def(py::init<>())
            .def_readwrite("tag", &SourceConfig::tag, "Unique identifier (e.g., IP address or user tag)")
            .def_readwrite("initial_volume", &SourceConfig::initial_volume, "Initial volume level (default: 1.0)")
            .def_readwrite("initial_eq", &SourceConfig::initial_eq, "Initial equalizer settings (list of floats, size EQ_BANDS)")
            .def_readwrite("initial_delay_ms", &SourceConfig::initial_delay_ms, "Initial delay in milliseconds (default: 0)");
    
        py::class_<SinkConfig>(m, "SinkConfig", "Configuration for an audio sink")
            .def(py::init<>()) // Bind the default constructor
            .def_readwrite("id", &SinkConfig::id, "Unique identifier for this sink instance")
            .def_readwrite("output_ip", &SinkConfig::output_ip, "Destination IP address for UDP output")
            .def_readwrite("output_port", &SinkConfig::output_port, "Destination port for UDP output")
            .def_readwrite("bitdepth", &SinkConfig::bitdepth, "Output bit depth (e.g., 16)")
            .def_readwrite("samplerate", &SinkConfig::samplerate, "Output sample rate (e.g., 48000)")
            .def_readwrite("channels", &SinkConfig::channels, "Output channel count (e.g., 2)")
            .def_readwrite("chlayout1", &SinkConfig::chlayout1, "Scream header channel layout byte 1")
            .def_readwrite("chlayout2", &SinkConfig::chlayout2, "Scream header channel layout byte 2")
            .def_readwrite("protocol", &SinkConfig::protocol, "Network protocol (e.g., 'scream', 'rtp')");
    
        py::class_<RawScreamReceiverConfig>(m, "RawScreamReceiverConfig", "Configuration for a raw Scream receiver")
            .def(py::init<>()) // Default constructor
            .def_readwrite("listen_port", &RawScreamReceiverConfig::listen_port, "UDP port to listen on");
    
        py::class_<PerProcessScreamReceiverConfig>(m, "PerProcessScreamReceiverConfig", "Configuration for a per-process Scream receiver")
            .def(py::init<>())
            .def_readwrite("listen_port", &PerProcessScreamReceiverConfig::listen_port, "UDP port to listen on for per-process receiver");
    
        py::enum_<InputProtocolType>(m, "InputProtocolType", "Specifies the expected input packet type")
            .value("RTP_SCREAM_PAYLOAD", InputProtocolType::RTP_SCREAM_PAYLOAD)
            .value("RAW_SCREAM_PACKET", InputProtocolType::RAW_SCREAM_PACKET)
            .value("PER_PROCESS_SCREAM_PACKET", InputProtocolType::PER_PROCESS_SCREAM_PACKET)
            .export_values();
    
        py::class_<CppSpeakerLayout>(m, "CppSpeakerLayout", "C++ structure for speaker layout configuration")
            .def(py::init<>()) // Default constructor
            .def_readwrite("auto_mode", &CppSpeakerLayout::auto_mode, "True for auto mix, false for custom matrix")
            .def_readwrite("matrix", &CppSpeakerLayout::matrix, "8x8 speaker mix matrix");
    
        py::enum_<CommandType>(m, "CommandType", "Type of control command for a source")
            .value("SET_VOLUME", CommandType::SET_VOLUME)
            .value("SET_EQ", CommandType::SET_EQ)
            .value("SET_DELAY", CommandType::SET_DELAY)
            .value("SET_TIMESHIFT", CommandType::SET_TIMESHIFT)
            .value("SET_EQ_NORMALIZATION", CommandType::SET_EQ_NORMALIZATION)
            .value("SET_VOLUME_NORMALIZATION", CommandType::SET_VOLUME_NORMALIZATION)
            .value("SET_SPEAKER_MIX", CommandType::SET_SPEAKER_MIX)
            .export_values();
    
        py::class_<SourceParameterUpdates>(m, "SourceParameterUpdates", "Holds optional parameter updates for a source")
            .def(py::init<>())
            .def_readwrite("volume", &SourceParameterUpdates::volume, "Optional volume level")
            .def_readwrite("eq_values", &SourceParameterUpdates::eq_values, "Optional list of EQ values")
            .def_readwrite("eq_normalization", &SourceParameterUpdates::eq_normalization, "Optional EQ normalization flag")
            .def_readwrite("volume_normalization", &SourceParameterUpdates::volume_normalization, "Optional volume normalization flag")
            .def_readwrite("delay_ms", &SourceParameterUpdates::delay_ms, "Optional delay in milliseconds")
            .def_readwrite("timeshift_sec", &SourceParameterUpdates::timeshift_sec, "Optional timeshift in seconds")
            .def_readwrite("speaker_layouts_map", &SourceParameterUpdates::speaker_layouts_map, "Optional map of input channel counts to speaker layouts");
    
        py::class_<ControlCommand>(m, "ControlCommand", "Command structure for SourceInputProcessor")
            .def(py::init<>())
            .def_readwrite("type", &ControlCommand::type, "The type of the command")
            .def_readwrite("float_value", &ControlCommand::float_value, "Floating point value for the command")
            .def_readwrite("int_value", &ControlCommand::int_value, "Integer value for the command")
            .def_readwrite("eq_values", &ControlCommand::eq_values, "List of float values for EQ")
            .def_readwrite("input_channel_key", &ControlCommand::input_channel_key, "Input channel key for speaker mix")
            .def_readwrite("speaker_layout_for_key", &ControlCommand::speaker_layout_for_key, "Speaker layout for the given key");
    }
    
    } // namespace audio
    } // namespace screamrouter

#endif // AUDIO_TYPES_H
