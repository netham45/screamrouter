#include "configuration/audio_engine_config_types.h"
/**
 * @file audio_types.h
 * @brief Defines core data structures, types, and enumerations for the audio engine.
 * @details This file contains the definitions for various data structures used for inter-thread
 *          communication, configuration, and control within the ScreamRouter audio engine. It also
 *          includes pybind11 bindings for exposing these types to Python.
 */
#ifndef AUDIO_TYPES_H
#define AUDIO_TYPES_H

#include <vector>
#include <string>
#include <chrono>
#include <cstdint> // For fixed-width integers like uint8_t, uint32_t
#include <optional> // For std::optional
#include "audio_constants.h"
#include <map>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/operators.h> // For operator overloads
#include "utils/thread_safe_queue.h"

namespace screamrouter {

// Forward declaration
namespace config {
    struct RtpReceiverConfig;
}

namespace audio {

/**
 * @struct CppSpeakerLayout
 * @brief Defines a speaker mixing matrix and its operational mode.
 * @details This struct is used to configure how input channels are mapped to output channels,
 *          either through an automatic pass-through or a custom user-defined matrix.
 */
struct CppSpeakerLayout {
    /** @brief True for auto mix (default), false for using the custom matrix. */
    bool auto_mode = true;
    /** @brief 8x8 speaker mix matrix, defining gain from input (row) to output (col). */
    std::vector<std::vector<float>> matrix;

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
     * @param other The other CppSpeakerLayout to compare against.
     * @return True if the layouts are equal, false otherwise.
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
 * @struct TaggedAudioPacket
 * @brief Represents a raw audio packet received from the network, tagged with its source.
 * @details Passed from a receiver component to the corresponding SourceInputProcessor.
 */
struct TaggedAudioPacket {
    /** @brief Identifier for the source (e.g., IP address or user tag). */
    std::string source_tag;
    /** @brief Raw audio payload sized according to the configured chunk size. */
    std::vector<uint8_t> audio_data;
    /** @brief Timestamp for timeshifting/jitter buffer. */
    std::chrono::steady_clock::time_point received_time;
    /** @brief Optional RTP timestamp for dejittering. */
    std::optional<uint32_t> rtp_timestamp;
    /** @brief List of SSRC and CSRCs from the RTP header. */
    std::vector<uint32_t> ssrcs;
    // --- Audio Format Info ---
    /** @brief Number of audio channels in the payload. */
    int channels = 0;
    /** @brief Sample rate of the audio in the payload. */
    int sample_rate = 0;
    /** @brief Bit depth of the audio in the payload. */
    int bit_depth = 0;
    /** @brief Scream channel layout byte 1. */
    uint8_t chlayout1 = 0;
    /** @brief Scream channel layout byte 2. */
    uint8_t chlayout2 = 0;
    /** @brief Playback rate adjustment factor (1.0 is normal speed). */
    double playback_rate = 1.0;
    /** @brief Marks packets that should trigger sentinel logging. */
    bool is_sentinel = false;
};

/**
 * @struct ProcessedAudioChunk
 * @brief Represents a chunk of audio data after processing by a SourceInputProcessor.
 * @details Passed from a SourceInputProcessor to one or more SinkAudioMixer(s).
 */
struct ProcessedAudioChunk {
    /** @brief Processed audio data as 32-bit signed integers. */
    std::vector<int32_t> audio_data;
    /** @brief SSRC and CSRCs, forwarded from the original packet. */
    std::vector<uint32_t> ssrcs;
    /** @brief Timestamp recorded when the chunk was produced by the source processor. */
    std::chrono::steady_clock::time_point produced_time{};
    /** @brief Timestamp recorded when the audio content first entered the system. */
    std::chrono::steady_clock::time_point origin_time{};
    /** @brief Playback rate applied by the source processor for this chunk. */
    double playback_rate = 1.0;
    /** @brief Sentinel flag propagated from the originating packet. */
    bool is_sentinel = false;
};

/**
 * @enum CommandType
 * @brief Defines types of control commands for a SourceInputProcessor.
 */
enum class CommandType {
    SET_VOLUME,                 ///< Change the volume level.
    SET_EQ,                     ///< Set the equalizer band values.
    SET_DELAY,                  ///< Set the delay in integer milliseconds.
    SET_TIMESHIFT,              ///< Set the timeshift delay in float seconds.
    SET_EQ_NORMALIZATION,       ///< Enable or disable equalizer normalization.
    SET_VOLUME_NORMALIZATION,   ///< Enable or disable volume normalization.
    SET_SPEAKER_MIX,            ///< Set the speaker layout mapping.
    SET_PLAYBACK_RATE_SCALE     ///< Apply an additional playback rate multiplier.
};

/**
 * @struct ControlCommand
 * @brief Represents a command sent from AudioManager to a SourceInputProcessor.
 */
struct ControlCommand {
    /** @brief The type of command to execute. */
    CommandType type;
    /** @brief Value for volume, timeshift. */
    float float_value;
    /** @brief Value for delay_ms. */
    int int_value;
    /** @brief Values for all EQ bands. */
    std::vector<float> eq_values;
    /** @brief Key for the speaker layout map (e.g., number of input channels). */
    int input_channel_key;
    /** @brief The speaker layout to apply for the given key. */
    CppSpeakerLayout speaker_layout_for_key;

    /**
     * @brief Default constructor.
     */
    ControlCommand() : type(CommandType::SET_VOLUME), float_value(0.0f), int_value(0), input_channel_key(0) {}
};

/**
 * @enum DeviceDirection
 * @brief Indicates whether a system device is an input (capture) or output (playback).
 */
enum class DeviceDirection {
    CAPTURE,
    PLAYBACK
};

/**
 * @struct DeviceCapabilityRange
 * @brief Describes the supported range for a particular device capability.
 */
struct DeviceCapabilityRange {
    unsigned int min = 0;
    unsigned int max = 0;

    bool operator==(const DeviceCapabilityRange& other) const {
        return min == other.min && max == other.max;
    }
};

/**
 * @struct SystemDeviceInfo
 * @brief Metadata describing a discoverable system audio endpoint.
 */
struct SystemDeviceInfo {
    std::string tag;
    std::string friendly_name;
    std::string hw_id;
    std::string endpoint_id;
    int card_index = -1;
    int device_index = -1;
    DeviceDirection direction = DeviceDirection::CAPTURE;
    DeviceCapabilityRange channels;
    DeviceCapabilityRange sample_rates;
    unsigned int bit_depth = 0;
    bool present = false;

    bool operator==(const SystemDeviceInfo& other) const {
        return tag == other.tag &&
               friendly_name == other.friendly_name &&
               hw_id == other.hw_id &&
               endpoint_id == other.endpoint_id &&
               card_index == other.card_index &&
               device_index == other.device_index &&
               direction == other.direction &&
               channels == other.channels &&
               sample_rates == other.sample_rates &&
               bit_depth == other.bit_depth &&
               present == other.present;
    }
};

/**
 * @struct DeviceDiscoveryNotification
 * @brief Notification emitted when a discoverable system audio device changes state.
 */
struct DeviceDiscoveryNotification {
    std::string tag;
    DeviceDirection direction = DeviceDirection::CAPTURE;
    bool present = false;
};

/**
 * @struct EncodedMP3Data
 * @brief Represents a chunk of MP3 encoded audio data.
 * @details Passed from a SinkAudioMixer to the AudioManager for consumption by Python.
 */
struct EncodedMP3Data {
    /** @brief A chunk of MP3-encoded bytes. */
    std::vector<uint8_t> mp3_data;
};

// --- Statistics Structs ---

struct BufferMetrics {
    size_t size = 0;
    size_t high_watermark = 0;
    double depth_ms = 0.0;
    double fill_percent = 0.0;
    double push_rate_per_second = 0.0;
    double pop_rate_per_second = 0.0;
};

struct StreamStats {
    double jitter_estimate_ms = 0.0;
    double packets_per_second = 0.0;
    double playback_rate = 1.0;
    size_t timeshift_buffer_size = 0;
    uint64_t timeshift_buffer_late_packets = 0;
    uint64_t timeshift_buffer_lagging_events = 0;
    uint64_t tm_buffer_underruns = 0;
    uint64_t tm_packets_discarded = 0;
    double last_arrival_time_error_ms = 0.0;
    double total_anchor_adjustment_ms = 0.0;
    uint64_t total_packets_in_stream = 0;
    double target_buffer_level_ms = 0.0;
    double buffer_target_fill_percentage = 0.0;
    double avg_arrival_error_ms = 0.0;
    double avg_abs_arrival_error_ms = 0.0;
    double max_arrival_error_ms = 0.0;
    double min_arrival_error_ms = 0.0;
    uint64_t arrival_error_sample_count = 0;
    double avg_playout_deviation_ms = 0.0;
    double avg_abs_playout_deviation_ms = 0.0;
    double max_playout_deviation_ms = 0.0;
    double min_playout_deviation_ms = 0.0;
    uint64_t playout_deviation_sample_count = 0;
    double avg_head_playout_lag_ms = 0.0;
    double max_head_playout_lag_ms = 0.0;
    uint64_t head_playout_lag_sample_count = 0;
    double last_head_playout_lag_ms = 0.0;
    double clock_offset_ms = 0.0;
    double clock_drift_ppm = 0.0;
    double clock_last_innovation_ms = 0.0;
    double clock_avg_abs_innovation_ms = 0.0;
    double clock_last_measured_offset_ms = 0.0;
    double system_jitter_ms = 0.0;
    double last_system_delay_ms = 0.0;
    BufferMetrics timeshift_buffer;
};

struct SourceStats {
    std::string instance_id;
    std::string source_tag;
    size_t input_queue_size = 0;
    size_t output_queue_size = 0;
    double packets_processed_per_second = 0.0;
    uint64_t reconfigurations = 0;
    double playback_rate = 1.0;
    double input_samplerate = 0.0;
    double output_samplerate = 0.0;
    double resample_ratio = 0.0;
    BufferMetrics input_buffer;
    BufferMetrics output_buffer;
    BufferMetrics process_buffer;
    BufferMetrics timeshift_buffer;
    double last_packet_age_ms = 0.0;
    double last_origin_age_ms = 0.0;
    uint64_t chunks_pushed = 0;
    uint64_t discarded_packets = 0;
    double avg_processing_ms = 0.0;
    size_t peak_process_buffer_samples = 0;
};

struct WebRtcListenerStats {
    std::string listener_id;
    std::string connection_state; // e.g., "Connected", "Failed"
    size_t pcm_buffer_size = 0;
    double packets_sent_per_second = 0.0;
};

struct SinkInputLaneStats {
    std::string instance_id;
    BufferMetrics source_output_queue;
    BufferMetrics ready_queue;
    double last_chunk_dwell_ms = 0.0;
    double avg_chunk_dwell_ms = 0.0;
    uint64_t underrun_events = 0;
    uint64_t ready_total_received = 0;
    uint64_t ready_total_popped = 0;
    uint64_t ready_total_dropped = 0;
};

struct SinkStats {
    std::string sink_id;
    size_t active_input_streams = 0;
    size_t total_input_streams = 0;
    double packets_mixed_per_second = 0.0;
    uint64_t sink_buffer_underruns = 0;
    uint64_t sink_buffer_overflows = 0;
    uint64_t mp3_buffer_overflows = 0;
    BufferMetrics payload_buffer;
    BufferMetrics mp3_output_buffer;
    BufferMetrics mp3_pcm_buffer;
    double last_chunk_dwell_ms = 0.0;
    double avg_chunk_dwell_ms = 0.0;
    double last_send_gap_ms = 0.0;
    double avg_send_gap_ms = 0.0;
    std::vector<SinkInputLaneStats> inputs;
    std::vector<WebRtcListenerStats> webrtc_listeners;
};

struct GlobalStats {
    size_t timeshift_buffer_total_size = 0;
    double packets_added_to_timeshift_per_second = 0.0;
    BufferMetrics timeshift_inbound_buffer;
};

struct AudioEngineStats {
    GlobalStats global_stats;
    std::map<std::string, StreamStats> stream_stats; // Keyed by stream_tag
    std::vector<SourceStats> source_stats;
    std::vector<SinkStats> sink_stats;
};

// --- Configuration Structs (For C++ internal use) ---


/**
 * @struct SourceConfig
 * @brief Initial configuration for a single audio source path.
 */
struct SourceConfig {
    /** @brief Unique identifier for the source (e.g., IP address). */
    std::string tag;
    /** @brief Initial volume level (0.0 to 1.0+). */
    float initial_volume = 1.0f;
    /** @brief Initial equalizer settings (size EQ_BANDS). */
    std::vector<float> initial_eq;
    /** @brief Initial delay in milliseconds. */
    int initial_delay_ms = 0;
    /** @brief Initial timeshift in seconds. */
    float initial_timeshift_sec = 0.0f;
    /** @brief Required output channels for this source's processing path. */
    int target_output_channels = 2;
    /** @brief Required output sample rate for this source's processing path. */
    int target_output_samplerate = 48000;
};

/**
 * @struct SinkConfig
 * @brief Configuration for a single audio sink (output).
 */
struct SinkConfig {
    /** @brief Unique ID for this sink instance. */
    std::string id;
    /** @brief Human-friendly display name for this sink. */
    std::string friendly_name;
    /** @brief Destination IP address for UDP output. */
    std::string output_ip;
    /** @brief Destination port for UDP output. */
    int output_port;
    /** @brief Output bit depth (e.g., 16, 24, 32). */
    int bitdepth = 16;
    /** @brief Output sample rate (e.g., 44100, 48000). */
    int samplerate = 48000;
    /** @brief Output channel count. */
    int channels = 2;
    /** @brief Scream header channel layout byte 1. */
    uint8_t chlayout1 = 0x03;
    /** @brief Scream header channel layout byte 2. */
    uint8_t chlayout2 = 0x00;
    /** @brief Flag to enable the MP3 output queue for this sink. */
    bool enable_mp3 = false;
    /** @brief Output protocol ("scream", "rtp", "web_receiver", "system_audio"). */
    std::string protocol = "scream";
    /** @brief Optional target sink name for SAP-directed remote routing. */
    std::string sap_target_sink;
    /** @brief Optional target host for SAP-directed remote routing. */
    std::string sap_target_host;
    /** @brief Speaker layout configuration for this sink. */
    CppSpeakerLayout speaker_layout;
    /** @brief Enable time synchronization for RTP streams. */
    bool time_sync_enabled = false;
    /** @brief Time synchronization delay in milliseconds. */
    int time_sync_delay_ms = 0;
    /** @brief List of RTP receivers for multi-device mode. */
    std::vector<config::RtpReceiverConfig> rtp_receivers;
    /** @brief Enable multi-device RTP mode. */
    bool multi_device_mode = false;
};

/**
 * @struct CaptureParams
 * @brief Configuration parameters for a system capture endpoint.
 */
struct CaptureParams {
    /** @brief ALSA hardware identifier (e.g., "hw:0,0"). */
    std::string hw_id;
    /** @brief WASAPI endpoint identifier. */
    std::string endpoint_id;
    /** @brief Desired channel count for capture. */
    unsigned int channels = 2;
    /** @brief Desired sample rate in Hz. */
    unsigned int sample_rate = 48000;
    /** @brief Desired period size in frames (0 = use driver default). */
    unsigned int period_frames = 1024;
    /** @brief Desired buffer size in frames (0 = derive from period size). */
    unsigned int buffer_frames = 0;
    /** @brief Bit depth per sample. Supported values: 16 or 32. */
    unsigned int bit_depth = 16;
    /** @brief Request loopback capture (WASAPI). */
    bool loopback = false;
    /** @brief Request exclusive mode stream (WASAPI). */
    bool exclusive_mode = false;
    /** @brief Requested buffer duration in milliseconds (WASAPI shared mode). */
    unsigned int buffer_duration_ms = 0;
};

/**
 * @struct RtpReceiverConfig
 * @brief Configuration for the main RTP receiver component.
 */
struct RtpReceiverConfig {
    /** @brief Default UDP port to listen on for RTP packets. */
    int listen_port = 40000;
    /** @brief Pre-configured list of known source IPs. */
    std::vector<std::string> known_ips;
};

/**
 * @struct RawScreamReceiverConfig
 * @brief Configuration for a raw Scream receiver component.
 */
struct RawScreamReceiverConfig {
    /** @brief UDP port to listen on for raw Scream packets. */
    int listen_port = 4010;
};

/**
 * @struct PerProcessScreamReceiverConfig
 * @brief Configuration for a per-process Scream receiver component.
 */
struct PerProcessScreamReceiverConfig {
    /** @brief UDP port to listen on for per-process Scream packets. */
    int listen_port = 16402;
};

/**
 * @struct SourceProcessorConfig
 * @brief Configuration for a SourceInputProcessor component.
 */
struct SourceProcessorConfig {
    /** @brief Unique identifier for this specific processor instance. */
    std::string instance_id;
    /** @brief Identifier of the source this processor handles (IP or user tag). */
    std::string source_tag;
    /** @brief Target output channels, populated from SinkConfig. */
    int output_channels = 2;
    /** @brief Target output sample rate, populated from SinkConfig. */
    int output_samplerate = 48000;
    /** @brief Initial volume level. */
    float initial_volume = 1.0f;
    /** @brief Initial equalizer settings. */
    std::vector<float> initial_eq;
    /** @brief Initial delay in milliseconds. */
    int initial_delay_ms = 0;
    /** @brief Initial timeshift in seconds. */
    float initial_timeshift_sec = 0.0f;
    /** @brief Duration of the internal timeshift buffer in seconds. */
    int timeshift_buffer_duration_sec = 5;
    /** @brief Custom 8x8 speaker mix matrix. */
    std::vector<std::vector<float>> speaker_mix_matrix;
    /** @brief True to use dynamic mixing logic, false to use the matrix. */
    bool use_auto_speaker_mix;

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
 * @struct AudioManagerConfigCpp
 * @brief C++ specific configuration for the AudioManager.
 */
struct AudioManagerConfigCpp {
    /** @brief Default main RTP listen port. */
    int rtp_listen_port = 4010;
    /** @brief Default global timeshift buffer duration (5 minutes). */
    int global_timeshift_buffer_duration_sec = 300;
};

/**
 * @struct AdaptivePlaybackSettings
 * @brief Parameters for adaptive playback control when targeting a local system device.
 */
struct AdaptivePlaybackSettings {
    /** @brief Desired buffered audio in milliseconds. */
    double target_delay_ms = 80.0;
    /** @brief Acceptable variation from the target in milliseconds. */
    double tolerance_ms = 10.0;
    /** @brief Maximum frames to drop or inject during a single adjustment. */
    size_t max_compensation_frames = 256;
    /** @brief Smoothing factor (0..1) for measured delay samples. */
    double smoothing_alpha = 0.2;
    /** @brief Minimum interval between adaptive log statements, in milliseconds (0 disables throttling). */
    int log_interval_ms = 1000;
};

/**
 * @struct SinkMixerConfig
 * @brief Configuration for a SinkAudioMixer component.
 */
struct SinkMixerConfig {
    /** @brief Unique identifier for the sink this mixer serves. */
    std::string sink_id;
    /** @brief Human-friendly display name for the sink. */
    std::string friendly_name;
    /** @brief Destination IP address. */
    std::string output_ip;
    /** @brief Destination port. */
    int output_port;
    /** @brief Output bit depth. */
    int output_bitdepth;
    /** @brief Output sample rate. */
    int output_samplerate;
    /** @brief Output channel count. */
    int output_channels;
    /** @brief Scream header channel layout byte 1. */
    uint8_t output_chlayout1;
    /** @brief Scream header channel layout byte 2. */
    uint8_t output_chlayout2;
    /** @brief Output protocol ("scream", "rtp", "web_receiver", "system_audio"). */
    std::string protocol = "scream";
    /** @brief Optional target sink for SAP-directed routing. */
    std::string sap_target_sink;
    /** @brief Optional target host for SAP-directed routing. */
    std::string sap_target_host;
    /** @brief Speaker layout for RTP channel mapping. */
    CppSpeakerLayout speaker_layout;
    /** @brief Enable time synchronization for RTP streams. */
    bool time_sync_enabled = false;
    /** @brief Time synchronization delay in milliseconds. */
    int time_sync_delay_ms = 0;
    /** @brief List of RTP receivers for multi-device mode. */
    std::vector<config::RtpReceiverConfig> rtp_receivers;
    /** @brief Enable multi-device RTP mode. */
    bool multi_device_mode = false;
    /** @brief Adaptive playback parameters for local system sinks. */
    AdaptivePlaybackSettings adaptive_playback;
};


/**
 * @struct SourceParameterUpdates
 * @brief A struct to hold a batch of parameter updates for a source.
 * @details Using std::optional allows for updating only specified parameters atomically.
 */
struct SourceParameterUpdates {
    /** @brief Optional new volume level. */
    std::optional<float> volume;
    /** @brief Optional new EQ band values. */
    std::optional<std::vector<float>> eq_values;
    /** @brief Optional new EQ normalization state. */
    std::optional<bool> eq_normalization;
    /** @brief Optional new volume normalization state. */
    std::optional<bool> volume_normalization;
    /** @brief Optional new delay in milliseconds. */
    std::optional<int> delay_ms;
    /** @brief Optional new timeshift in seconds. */
    std::optional<float> timeshift_sec;
    /** @brief Optional new map of speaker layouts. */
    std::optional<std::map<int, CppSpeakerLayout>> speaker_layouts_map;
    };
    
// --- Queue Type Aliases ---
/** @brief A thread-safe queue for passing raw audio packets between threads. */
using PacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>;
/** @brief A thread-safe queue for passing processed audio chunks between threads. */
using ChunkQueue = utils::ThreadSafeQueue<ProcessedAudioChunk>;
/** @brief A thread-safe queue for sending control commands to audio processors. */
using CommandQueue = utils::ThreadSafeQueue<ControlCommand>;
/** @brief A thread-safe queue for passing encoded MP3 data. */
using Mp3Queue = utils::ThreadSafeQueue<EncodedMP3Data>;
/** @brief Registry mapping device tags to their metadata. */
using SystemDeviceRegistry = std::map<std::string, SystemDeviceInfo>;
/** @brief A thread-safe queue for system device discovery notifications. */
using NotificationQueue = utils::ThreadSafeQueue<DeviceDiscoveryNotification>;
/** @brief A type definition for a request to remove a listener, containing a source and sink ID pair. */
using ListenerRemovalRequest = std::pair<std::string, std::string>;
/** @brief A thread-safe queue for handling requests to remove listeners. */
using ListenerRemovalQueue = utils::ThreadSafeQueue<ListenerRemovalRequest>;

    /**
     * @brief Binds the C++ audio types to the given Python module.
     * @param m The pybind11 module to which the types will be bound.
     */
    inline void bind_audio_types(pybind11::module_ &m) {
        namespace py = pybind11;
    
        py::class_<SinkConfig>(m, "SinkConfig", "Configuration for an audio sink")
            .def(py::init<>()) // Bind the default constructor
            .def_readwrite("id", &SinkConfig::id, "Unique identifier for this sink instance")
            .def_readwrite("friendly_name", &SinkConfig::friendly_name, "Human-friendly display name for this sink")
            .def_readwrite("output_ip", &SinkConfig::output_ip, "Destination IP address for UDP output")
            .def_readwrite("output_port", &SinkConfig::output_port, "Destination port for UDP output")
            .def_readwrite("bitdepth", &SinkConfig::bitdepth, "Output bit depth (e.g., 16)")
            .def_readwrite("samplerate", &SinkConfig::samplerate, "Output sample rate (e.g., 48000)")
            .def_readwrite("channels", &SinkConfig::channels, "Output channel count (e.g., 2)")
            .def_readwrite("chlayout1", &SinkConfig::chlayout1, "Scream header channel layout byte 1")
            .def_readwrite("chlayout2", &SinkConfig::chlayout2, "Scream header channel layout byte 2")
            .def_readwrite("protocol", &SinkConfig::protocol, "Network protocol (e.g., 'scream', 'rtp')")
            .def_readwrite("sap_target_sink", &SinkConfig::sap_target_sink, "Optional target sink name for SAP-directed routing")
            .def_readwrite("sap_target_host", &SinkConfig::sap_target_host, "Optional target host for SAP-directed routing")
            .def_readwrite("time_sync_enabled", &SinkConfig::time_sync_enabled, "Enable time synchronization for RTP streams")
            .def_readwrite("time_sync_delay_ms", &SinkConfig::time_sync_delay_ms, "Time synchronization delay in milliseconds")
            .def_readwrite("rtp_receivers", &SinkConfig::rtp_receivers, "List of RTP receivers for multi-device mode")
            .def_readwrite("multi_device_mode", &SinkConfig::multi_device_mode, "Enable multi-device RTP mode");

        py::class_<CppSpeakerLayout>(m, "CppSpeakerLayout", "C++ structure for speaker layout configuration")
            .def(py::init<>()) // Default constructor
            .def_readwrite("auto_mode", &CppSpeakerLayout::auto_mode, "True for auto mix, false for custom matrix")
            .def_readwrite("matrix", &CppSpeakerLayout::matrix, "8x8 speaker mix matrix");

        py::enum_<DeviceDirection>(m, "DeviceDirection", "Direction for system audio devices")
            .value("CAPTURE", DeviceDirection::CAPTURE)
            .value("PLAYBACK", DeviceDirection::PLAYBACK)
            .export_values();

        py::class_<DeviceCapabilityRange>(m, "DeviceCapabilityRange", "Range description for device capabilities")
            .def(py::init<>())
            .def_readwrite("min", &DeviceCapabilityRange::min)
            .def_readwrite("max", &DeviceCapabilityRange::max);

        py::class_<SystemDeviceInfo>(m, "SystemDeviceInfo", "Metadata for a discoverable system audio device")
            .def(py::init<>())
            .def_readwrite("tag", &SystemDeviceInfo::tag)
            .def_readwrite("friendly_name", &SystemDeviceInfo::friendly_name)
            .def_readwrite("hw_id", &SystemDeviceInfo::hw_id)
            .def_readwrite("endpoint_id", &SystemDeviceInfo::endpoint_id)
            .def_readwrite("card_index", &SystemDeviceInfo::card_index)
            .def_readwrite("device_index", &SystemDeviceInfo::device_index)
            .def_readwrite("direction", &SystemDeviceInfo::direction)
            .def_readwrite("channels", &SystemDeviceInfo::channels)
            .def_readwrite("sample_rates", &SystemDeviceInfo::sample_rates)
            .def_readwrite("bit_depth", &SystemDeviceInfo::bit_depth)
            .def_readwrite("present", &SystemDeviceInfo::present);

        py::class_<DeviceDiscoveryNotification>(m, "DeviceDiscoveryNotification", "Notification emitted when a system device changes state")
            .def(py::init<>())
            .def_readwrite("tag", &DeviceDiscoveryNotification::tag)
            .def_readwrite("direction", &DeviceDiscoveryNotification::direction)
            .def_readwrite("present", &DeviceDiscoveryNotification::present);
    
        py::class_<BufferMetrics>(m, "BufferMetrics", "Generic buffer occupancy and rate information")
            .def(py::init<>())
            .def_readwrite("size", &BufferMetrics::size)
            .def_readwrite("high_watermark", &BufferMetrics::high_watermark)
            .def_readwrite("depth_ms", &BufferMetrics::depth_ms)
            .def_readwrite("fill_percent", &BufferMetrics::fill_percent)
            .def_readwrite("push_rate_per_second", &BufferMetrics::push_rate_per_second)
            .def_readwrite("pop_rate_per_second", &BufferMetrics::pop_rate_per_second);

        py::class_<StreamStats>(m, "StreamStats", "Statistics for a single audio stream")
            .def(py::init<>())
            .def_readwrite("jitter_estimate_ms", &StreamStats::jitter_estimate_ms)
            .def_readwrite("packets_per_second", &StreamStats::packets_per_second)
            .def_readwrite("playback_rate", &StreamStats::playback_rate)
            .def_readwrite("timeshift_buffer_size", &StreamStats::timeshift_buffer_size)
            .def_readwrite("timeshift_buffer_late_packets", &StreamStats::timeshift_buffer_late_packets)
            .def_readwrite("timeshift_buffer_lagging_events", &StreamStats::timeshift_buffer_lagging_events)
            .def_readwrite("tm_buffer_underruns", &StreamStats::tm_buffer_underruns)
            .def_readwrite("tm_packets_discarded", &StreamStats::tm_packets_discarded)
            .def_readwrite("last_arrival_time_error_ms", &StreamStats::last_arrival_time_error_ms)
            .def_readwrite("total_anchor_adjustment_ms", &StreamStats::total_anchor_adjustment_ms)
            .def_readwrite("total_packets_in_stream", &StreamStats::total_packets_in_stream)
            .def_readwrite("target_buffer_level_ms", &StreamStats::target_buffer_level_ms)
            .def_readwrite("buffer_target_fill_percentage", &StreamStats::buffer_target_fill_percentage)
            .def_readwrite("avg_arrival_error_ms", &StreamStats::avg_arrival_error_ms)
            .def_readwrite("avg_abs_arrival_error_ms", &StreamStats::avg_abs_arrival_error_ms)
            .def_readwrite("max_arrival_error_ms", &StreamStats::max_arrival_error_ms)
            .def_readwrite("min_arrival_error_ms", &StreamStats::min_arrival_error_ms)
            .def_readwrite("arrival_error_sample_count", &StreamStats::arrival_error_sample_count)
            .def_readwrite("avg_playout_deviation_ms", &StreamStats::avg_playout_deviation_ms)
            .def_readwrite("avg_abs_playout_deviation_ms", &StreamStats::avg_abs_playout_deviation_ms)
            .def_readwrite("max_playout_deviation_ms", &StreamStats::max_playout_deviation_ms)
            .def_readwrite("min_playout_deviation_ms", &StreamStats::min_playout_deviation_ms)
            .def_readwrite("playout_deviation_sample_count", &StreamStats::playout_deviation_sample_count)
            .def_readwrite("avg_head_playout_lag_ms", &StreamStats::avg_head_playout_lag_ms)
            .def_readwrite("max_head_playout_lag_ms", &StreamStats::max_head_playout_lag_ms)
            .def_readwrite("head_playout_lag_sample_count", &StreamStats::head_playout_lag_sample_count)
            .def_readwrite("last_head_playout_lag_ms", &StreamStats::last_head_playout_lag_ms)
            .def_readwrite("clock_offset_ms", &StreamStats::clock_offset_ms)
            .def_readwrite("clock_drift_ppm", &StreamStats::clock_drift_ppm)
            .def_readwrite("clock_last_innovation_ms", &StreamStats::clock_last_innovation_ms)
            .def_readwrite("clock_avg_abs_innovation_ms", &StreamStats::clock_avg_abs_innovation_ms)
            .def_readwrite("clock_last_measured_offset_ms", &StreamStats::clock_last_measured_offset_ms)
            .def_readwrite("system_jitter_ms", &StreamStats::system_jitter_ms)
            .def_readwrite("last_system_delay_ms", &StreamStats::last_system_delay_ms)
            .def_readwrite("timeshift_buffer", &StreamStats::timeshift_buffer);

        py::class_<SourceStats>(m, "SourceStats", "Statistics for a single source processor")
            .def(py::init<>())
            .def_readwrite("instance_id", &SourceStats::instance_id)
            .def_readwrite("source_tag", &SourceStats::source_tag)
            .def_readwrite("input_queue_size", &SourceStats::input_queue_size)
            .def_readwrite("output_queue_size", &SourceStats::output_queue_size)
            .def_readwrite("packets_processed_per_second", &SourceStats::packets_processed_per_second)
            .def_readwrite("reconfigurations", &SourceStats::reconfigurations)
            .def_readwrite("playback_rate", &SourceStats::playback_rate)
            .def_readwrite("input_samplerate", &SourceStats::input_samplerate)
            .def_readwrite("output_samplerate", &SourceStats::output_samplerate)
            .def_readwrite("resample_ratio", &SourceStats::resample_ratio)
            .def_readwrite("input_buffer", &SourceStats::input_buffer)
            .def_readwrite("output_buffer", &SourceStats::output_buffer)
            .def_readwrite("process_buffer", &SourceStats::process_buffer)
            .def_readwrite("timeshift_buffer", &SourceStats::timeshift_buffer)
            .def_readwrite("last_packet_age_ms", &SourceStats::last_packet_age_ms)
            .def_readwrite("last_origin_age_ms", &SourceStats::last_origin_age_ms)
            .def_readwrite("chunks_pushed", &SourceStats::chunks_pushed)
            .def_readwrite("discarded_packets", &SourceStats::discarded_packets)
            .def_readwrite("avg_processing_ms", &SourceStats::avg_processing_ms)
            .def_readwrite("peak_process_buffer_samples", &SourceStats::peak_process_buffer_samples);

        py::class_<WebRtcListenerStats>(m, "WebRtcListenerStats", "Statistics for a single WebRTC listener")
            .def(py::init<>())
            .def_readwrite("listener_id", &WebRtcListenerStats::listener_id)
            .def_readwrite("connection_state", &WebRtcListenerStats::connection_state)
            .def_readwrite("pcm_buffer_size", &WebRtcListenerStats::pcm_buffer_size)
            .def_readwrite("packets_sent_per_second", &WebRtcListenerStats::packets_sent_per_second);

        py::class_<SinkInputLaneStats>(m, "SinkInputLaneStats", "Per-source buffer stats within a sink mixer")
            .def(py::init<>())
            .def_readwrite("instance_id", &SinkInputLaneStats::instance_id)
            .def_readwrite("source_output_queue", &SinkInputLaneStats::source_output_queue)
            .def_readwrite("ready_queue", &SinkInputLaneStats::ready_queue)
            .def_readwrite("last_chunk_dwell_ms", &SinkInputLaneStats::last_chunk_dwell_ms)
            .def_readwrite("avg_chunk_dwell_ms", &SinkInputLaneStats::avg_chunk_dwell_ms)
            .def_readwrite("underrun_events", &SinkInputLaneStats::underrun_events)
            .def_readwrite("ready_total_received", &SinkInputLaneStats::ready_total_received)
            .def_readwrite("ready_total_popped", &SinkInputLaneStats::ready_total_popped)
            .def_readwrite("ready_total_dropped", &SinkInputLaneStats::ready_total_dropped);

        py::class_<SinkStats>(m, "SinkStats", "Statistics for a single sink mixer")
            .def(py::init<>())
            .def_readwrite("sink_id", &SinkStats::sink_id)
            .def_readwrite("active_input_streams", &SinkStats::active_input_streams)
            .def_readwrite("total_input_streams", &SinkStats::total_input_streams)
            .def_readwrite("packets_mixed_per_second", &SinkStats::packets_mixed_per_second)
            .def_readwrite("sink_buffer_underruns", &SinkStats::sink_buffer_underruns)
            .def_readwrite("sink_buffer_overflows", &SinkStats::sink_buffer_overflows)
            .def_readwrite("mp3_buffer_overflows", &SinkStats::mp3_buffer_overflows)
            .def_readwrite("payload_buffer", &SinkStats::payload_buffer)
            .def_readwrite("mp3_output_buffer", &SinkStats::mp3_output_buffer)
            .def_readwrite("mp3_pcm_buffer", &SinkStats::mp3_pcm_buffer)
            .def_readwrite("last_chunk_dwell_ms", &SinkStats::last_chunk_dwell_ms)
            .def_readwrite("avg_chunk_dwell_ms", &SinkStats::avg_chunk_dwell_ms)
            .def_readwrite("last_send_gap_ms", &SinkStats::last_send_gap_ms)
            .def_readwrite("avg_send_gap_ms", &SinkStats::avg_send_gap_ms)
            .def_readwrite("inputs", &SinkStats::inputs)
            .def_readwrite("webrtc_listeners", &SinkStats::webrtc_listeners);

        py::class_<GlobalStats>(m, "GlobalStats", "Global statistics for the audio engine")
            .def(py::init<>())
            .def_readwrite("timeshift_buffer_total_size", &GlobalStats::timeshift_buffer_total_size)
            .def_readwrite("packets_added_to_timeshift_per_second", &GlobalStats::packets_added_to_timeshift_per_second)
            .def_readwrite("timeshift_inbound_buffer", &GlobalStats::timeshift_inbound_buffer);

        py::class_<AudioEngineStats>(m, "AudioEngineStats", "A collection of all statistics from the audio engine")
            .def(py::init<>())
            .def_readwrite("global_stats", &AudioEngineStats::global_stats)
            .def_readwrite("stream_stats", &AudioEngineStats::stream_stats)
            .def_readwrite("source_stats", &AudioEngineStats::source_stats)
            .def_readwrite("sink_stats", &AudioEngineStats::sink_stats);
    }
    
    } // namespace audio
    } // namespace screamrouter

#endif // AUDIO_TYPES_H
