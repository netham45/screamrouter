#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // For automatic conversions of vector, map, string, etc.
#include <pybind11/chrono.h> // If any chrono types were exposed (not currently needed)

#include "audio_manager.h"
// audio_types.h is included by audio_manager.h
#include "audio_engine_config_types.h" // Include new config types
#include "audio_engine_config_applier.h" // Include the applier class header

namespace py = pybind11;
// Import the C++ namespaces to shorten type names
using namespace screamrouter::audio;
// using namespace screamrouter::config; // Add config namespace - Removed for explicit qualification

// Define the Python module using the PYBIND11_MODULE macro
// The first argument is the name of the Python module as it will be imported (e.g., `import screamrouter_audio_engine`)
// The second argument `m` is the py::module_ object representing the module
PYBIND11_MODULE(screamrouter_audio_engine, m) {
    m.doc() = "ScreamRouter C++ Audio Engine Extension"; // Optional module docstring

    // --- Bind Configuration Structs ---
    // These structs are used to pass configuration data from Python to C++

    py::class_<SourceConfig>(m, "SourceConfig", "Configuration for an audio source")
        .def(py::init<>()) // Bind the default constructor
        // Bind member variables as read-write properties in Python
        .def_readwrite("tag", &SourceConfig::tag, "Unique identifier (e.g., IP address or user tag)")
        .def_readwrite("initial_volume", &SourceConfig::initial_volume, "Initial volume level (default: 1.0)")
        .def_readwrite("initial_eq", &SourceConfig::initial_eq, "Initial equalizer settings (list of floats, size EQ_BANDS)")
        .def_readwrite("initial_delay_ms", &SourceConfig::initial_delay_ms, "Initial delay in milliseconds (default: 0)")
        .def_readwrite("protocol_type_hint", &SourceConfig::protocol_type_hint, "Input protocol type (0=RTP_PAYLOAD, 1=RAW_PACKET)")
        .def_readwrite("target_receiver_port", &SourceConfig::target_receiver_port, "Target receiver listen port (used for RAW_PACKET type)");
        // Add __repr__ for better debugging in Python if desired
        // .def("__repr__", [](const SourceConfig &a) {
        //     return "<SourceConfig tag='" + a.tag + "'>";
        // });


    py::class_<SinkConfig>(m, "SinkConfig", "Configuration for an audio sink")
        .def(py::init<>()) // Bind the default constructor
        // Bind member variables as read-write properties
        .def_readwrite("id", &SinkConfig::id, "Unique identifier for this sink instance")
        .def_readwrite("output_ip", &SinkConfig::output_ip, "Destination IP address for UDP output")
        .def_readwrite("output_port", &SinkConfig::output_port, "Destination port for UDP output")
        .def_readwrite("bitdepth", &SinkConfig::bitdepth, "Output bit depth (e.g., 16)")
        .def_readwrite("samplerate", &SinkConfig::samplerate, "Output sample rate (e.g., 48000)")
        .def_readwrite("channels", &SinkConfig::channels, "Output channel count (e.g., 2)")
        .def_readwrite("chlayout1", &SinkConfig::chlayout1, "Scream header channel layout byte 1")
        .def_readwrite("chlayout2", &SinkConfig::chlayout2, "Scream header channel layout byte 2"); // Semicolon moved here
        // .def_readwrite("use_tcp", &SinkConfig::use_tcp, "Whether this sink uses TCP output (managed externally)"); // Removed
        // Removed .def_readwrite("enable_mp3", ...) as MP3 queue is now always created internally
        // Add __repr__
        // .def("__repr__", [](const SinkConfig &a) {
        //     return "<SinkConfig id='" + a.id + "'>";
        // });

    py::class_<RawScreamReceiverConfig>(m, "RawScreamReceiverConfig", "Configuration for a raw Scream receiver")
        .def(py::init<>()) // Default constructor
        .def_readwrite("listen_port", &RawScreamReceiverConfig::listen_port, "UDP port to listen on");
        // Add .def_readwrite("bind_ip", ...) if that field is included

    py::class_<PerProcessScreamReceiverConfig>(m, "PerProcessScreamReceiverConfig", "Configuration for a per-process Scream receiver")
        .def(py::init<>())
        .def_readwrite("listen_port", &PerProcessScreamReceiverConfig::listen_port, "UDP port to listen on for per-process receiver");

    py::enum_<InputProtocolType>(m, "InputProtocolType", "Specifies the expected input packet type")
        .value("RTP_SCREAM_PAYLOAD", InputProtocolType::RTP_SCREAM_PAYLOAD)
        .value("RAW_SCREAM_PACKET", InputProtocolType::RAW_SCREAM_PACKET)
        .value("PER_PROCESS_SCREAM_PACKET", InputProtocolType::PER_PROCESS_SCREAM_PACKET) // Added new type
        .export_values(); // Make enum values accessible as module attributes

    // --- Bind AudioManager Class ---
    // Expose the main C++ audio engine class to Python

    py::class_<AudioManager>(m, "AudioManager", "Main class for managing the C++ audio engine")
        .def(py::init<>(), "Constructor") // Bind the constructor

        // Lifecycle Methods
        .def("initialize", &AudioManager::initialize,
             py::arg("rtp_listen_port") = 40000,
             py::arg("global_timeshift_buffer_duration_sec") = 300, // Added new argument
             "Initializes the audio manager, including TimeshiftManager. Returns true on success.")
        .def("shutdown", &AudioManager::shutdown,
             "Stops all audio components and cleans up resources.")

        // Component Management Methods
        .def("add_sink", &AudioManager::add_sink,
             py::arg("config"), // Name the argument for clarity in Python calls
             "Adds and starts a new audio sink based on the SinkConfig. Returns true on success.")
        .def("remove_sink", &AudioManager::remove_sink,
             py::arg("sink_id"),
             "Stops and removes the audio sink with the given ID. Returns true on success.")
        .def("configure_source", &AudioManager::configure_source,
              py::arg("config"),
              "Creates and configures a new source processor instance. Returns unique instance ID string.")
        .def("remove_source", &AudioManager::remove_source, // Renamed method
             py::arg("instance_id"), // Changed argument
             "Removes the source processor instance with the given ID. Returns true on success.")
        .def("connect_source_sink", &AudioManager::connect_source_sink,
             py::arg("source_instance_id"), py::arg("sink_id"), // Changed arguments
             "Explicitly connects a source instance to a sink. Returns true on success.")
        .def("disconnect_source_sink", &AudioManager::disconnect_source_sink,
             py::arg("source_instance_id"), py::arg("sink_id"), // Changed arguments
             "Explicitly disconnects a source instance from a sink. Returns true on success.")

        // Raw Scream Receiver Management
        .def("add_raw_scream_receiver", &AudioManager::add_raw_scream_receiver,
             py::arg("config"),
             "Adds and starts a new raw Scream receiver. Returns true on success.")
        .def("remove_raw_scream_receiver", &AudioManager::remove_raw_scream_receiver,
             py::arg("listen_port"),
             "Stops and removes the raw Scream receiver listening on the given port. Returns true on success.")
        .def("add_per_process_scream_receiver", &AudioManager::add_per_process_scream_receiver,
             py::arg("config"),
             "Adds and starts a new per-process Scream receiver. Returns true on success.")
        .def("remove_per_process_scream_receiver", &AudioManager::remove_per_process_scream_receiver,
             py::arg("listen_port"),
             "Stops and removes the per-process Scream receiver. Returns true on success.")


         // Control Methods (Updated to use instance_id)
         .def("update_source_volume", &AudioManager::update_source_volume,
             py::arg("instance_id"), py::arg("volume"),
             "Updates the volume for a specific source instance.")
        .def("update_source_equalizer", &AudioManager::update_source_equalizer,
             py::arg("instance_id"), py::arg("eq_values"),
             "Updates the equalizer settings for a specific source instance (expects list/vector of floats).")
        .def("update_source_delay", &AudioManager::update_source_delay,
             py::arg("instance_id"), py::arg("delay_ms"),
             "Updates the delay (in ms) for a specific source instance.")
        .def("update_source_timeshift", &AudioManager::update_source_timeshift,
             py::arg("instance_id"), py::arg("timeshift_sec"),
             "Updates the timeshift playback offset (in seconds) for a specific source instance.")
        // .def("update_source_speaker_mix", &AudioManager::update_source_speaker_mix, // Old binding
        //      py::arg("instance_id"), py::arg("matrix"), py::arg("use_auto"),
        //      "Updates the speaker mix for a specific source instance.")
        .def("update_source_speaker_layout_for_key", &AudioManager::update_source_speaker_layout_for_key,
             py::arg("instance_id"), py::arg("input_channel_key"), py::arg("layout"),
             "Updates the speaker layout for a specific input channel key.")
        .def("update_source_speaker_layouts_map", &AudioManager::update_source_speaker_layouts_map,
             py::arg("instance_id"), py::arg("layouts_map"),
             "Updates the entire speaker layouts map for a source instance.")

        // Data Retrieval Methods
        .def("get_mp3_data", [](AudioManager &self, const std::string& sink_id) -> py::bytes {
                // Call the original C++ method
                std::vector<uint8_t> data_vec = self.get_mp3_data(sink_id);
                // Convert vector<uint8_t> to std::string, then pybind11 converts to bytes
                return py::bytes(reinterpret_cast<const char*>(data_vec.data()), data_vec.size());
            },
            py::arg("sink_id"),
            // No need for return_value_policy::move with py::bytes wrapper
            "Retrieves a chunk of MP3 data (as bytes) from the specified sink's queue if available, otherwise returns empty bytes.")
        .def("get_mp3_data_by_ip", [](AudioManager &self, const std::string& ip_address) -> py::bytes {
                std::vector<uint8_t> data_vec = self.get_mp3_data_by_ip(ip_address);
                return py::bytes(reinterpret_cast<const char*>(data_vec.data()), data_vec.size());
            },
            py::arg("ip_address"),
            "Retrieves a chunk of MP3 data (as bytes) from a sink identified by its output IP address.")
        
        // Receiver Info Methods
        .def("get_rtp_receiver_seen_tags", &AudioManager::get_rtp_receiver_seen_tags,
             "Retrieves the list of seen source tags from the main RTP receiver.")
        .def("get_raw_scream_receiver_seen_tags", &AudioManager::get_raw_scream_receiver_seen_tags,
             py::arg("listen_port"),
             "Retrieves the list of seen source tags from a specific Raw Scream receiver.")
        .def("get_per_process_scream_receiver_seen_tags", &AudioManager::get_per_process_scream_receiver_seen_tags,
             py::arg("listen_port"),
             "Retrieves the list of seen source tags from a specific Per-Process Scream receiver.")

        // Method for plugins to write audio packets directly
        .def("write_plugin_packet",
             [](AudioManager &self,
                const std::string& source_instance_id,
                py::bytes audio_payload_bytes, // Explicitly expect py::bytes from Python
                int channels,
                int sample_rate,
                int bit_depth,
                uint8_t chlayout1,
                uint8_t chlayout2) -> bool {
                 // Convert py::bytes to std::vector<uint8_t>
                 py::buffer_info info = py::buffer(audio_payload_bytes).request();
                 const uint8_t* ptr = static_cast<const uint8_t*>(info.ptr);
                 std::vector<uint8_t> audio_payload_vec(ptr, ptr + info.size);
                 
                 return self.write_plugin_packet(
                     source_instance_id,
                     audio_payload_vec, // Pass the std::vector<uint8_t>
                     channels,
                     sample_rate,
                     bit_depth,
                     chlayout1,
                     chlayout2
                 );
             },
             py::arg("source_instance_id"),
             py::arg("audio_payload"), 
             py::arg("channels"),
             py::arg("sample_rate"),
             py::arg("bit_depth"),
             py::arg("chlayout1"),
             py::arg("chlayout2"),
             "Allows a plugin to inject a pre-formed audio packet (as bytes) into a SourceInputProcessor instance. Returns true on success.");

        // External Control Methods (like setting TCP FD)
        // Removed .def("set_sink_tcp_fd", ...)

    // --- Bind New Configuration Structs (Task 03_01) ---

    // --- Bind CppSpeakerLayout Struct ---
    // CppSpeakerLayout is now in screamrouter::audio namespace
    py::class_<screamrouter::audio::CppSpeakerLayout>(m, "CppSpeakerLayout", "C++ structure for speaker layout configuration")
        .def(py::init<>()) // Default constructor
        .def_readwrite("auto_mode", &screamrouter::audio::CppSpeakerLayout::auto_mode, "True for auto mix, false for custom matrix")
        .def_readwrite("matrix", &screamrouter::audio::CppSpeakerLayout::matrix, "8x8 speaker mix matrix");
    // --- End Bind CppSpeakerLayout Struct ---

    // Bind ControlCommand (ensure this is done after CppSpeakerLayout as it uses it)
    py::class_<ControlCommand>(m, "ControlCommand", "Command structure for SourceInputProcessor")
        .def(py::init<>())
        .def_readwrite("type", &ControlCommand::type)
        .def_readwrite("float_value", &ControlCommand::float_value)
        .def_readwrite("int_value", &ControlCommand::int_value)
        .def_readwrite("eq_values", &ControlCommand::eq_values)
        // New members for SET_SPEAKER_MIX (Task 17.12)
        .def_readwrite("input_channel_key", &ControlCommand::input_channel_key)
        .def_readwrite("speaker_layout_for_key", &ControlCommand::speaker_layout_for_key);
        // Old speaker_mix_matrix and use_auto_speaker_mix are removed from ControlCommand struct

    py::class_<screamrouter::config::AppliedSourcePathParams>(m, "AppliedSourcePathParams", "Parameters for a specific source-to-sink audio path")
        .def(py::init<>()) // Bind default constructor (initializes eq_values)
        .def_readwrite("path_id", &screamrouter::config::AppliedSourcePathParams::path_id, "Unique ID for this path (e.g., source_tag_to_sink_id)")
        .def_readwrite("source_tag", &screamrouter::config::AppliedSourcePathParams::source_tag, "Original source identifier (e.g., IP address)")
        .def_readwrite("target_sink_id", &screamrouter::config::AppliedSourcePathParams::target_sink_id, "ID of the target sink for this path")
        .def_readwrite("volume", &screamrouter::config::AppliedSourcePathParams::volume, "Volume for this path")
        .def_readwrite("eq_values", &screamrouter::config::AppliedSourcePathParams::eq_values, "Equalizer settings for this path (list/vector of floats)")
        .def_readwrite("delay_ms", &screamrouter::config::AppliedSourcePathParams::delay_ms, "Delay in milliseconds for this path")
        .def_readwrite("timeshift_sec", &screamrouter::config::AppliedSourcePathParams::timeshift_sec, "Timeshift in seconds for this path")
        .def_readwrite("target_output_channels", &screamrouter::config::AppliedSourcePathParams::target_output_channels, "Required output channels for the target sink")
        .def_readwrite("target_output_samplerate", &screamrouter::config::AppliedSourcePathParams::target_output_samplerate, "Required output sample rate for the target sink")
        .def_readwrite("generated_instance_id", &screamrouter::config::AppliedSourcePathParams::generated_instance_id, "(Read-only from Python perspective) Instance ID generated by C++")
        // .def_readwrite("speaker_mix_matrix", &screamrouter::config::AppliedSourcePathParams::speaker_mix_matrix, "Speaker mix matrix for this path") // Old
        // .def_readwrite("use_auto_speaker_mix", &screamrouter::config::AppliedSourcePathParams::use_auto_speaker_mix, "Whether to use auto speaker mix for this path") // Old
        .def_readwrite("speaker_layouts_map", &screamrouter::config::AppliedSourcePathParams::speaker_layouts_map, "Map of input channel counts to CppSpeakerLayout objects");


    py::class_<screamrouter::config::AppliedSinkParams>(m, "AppliedSinkParams", "Parameters for a configured sink")
        .def(py::init<>()) // Bind default constructor
        .def_readwrite("sink_id", &screamrouter::config::AppliedSinkParams::sink_id, "Unique identifier for the sink")
        // Bind the nested C++ SinkConfig. Python will need to create/populate this C++ struct instance.
        .def_readwrite("sink_engine_config", &screamrouter::config::AppliedSinkParams::sink_engine_config, "C++ SinkConfig parameters for AudioManager") 
        .def_readwrite("connected_source_path_ids", &screamrouter::config::AppliedSinkParams::connected_source_path_ids, "List of path_ids connected to this sink");

    py::class_<screamrouter::config::DesiredEngineState>(m, "DesiredEngineState", "Represents the complete desired state of the audio engine")
        .def(py::init<>()) // Bind default constructor
        .def_readwrite("source_paths", &screamrouter::config::DesiredEngineState::source_paths, "List of all desired AppliedSourcePathParams")
        .def_readwrite("sinks", &screamrouter::config::DesiredEngineState::sinks, "List of all desired AppliedSinkParams");

    // --- Bind AudioEngineConfigApplier Class (Task 03_02) ---
    py::class_<screamrouter::config::AudioEngineConfigApplier>(m, "AudioEngineConfigApplier", "Applies desired configuration state to the C++ AudioManager")
        .def(py::init<AudioManager&>(),
             py::arg("audio_manager"), 
             "Constructor, takes an AudioManager instance")
        
        .def("apply_state", &screamrouter::config::AudioEngineConfigApplier::apply_state,
             py::arg("desired_state"), 
             "Applies the provided DesiredEngineState to the AudioManager, returns true on success (basic check).");


    // Define constants if needed (e.g., EQ_BANDS)
    m.attr("EQ_BANDS") = py::int_(EQ_BANDS); // Ensure EQ_BANDS is accessible (likely via audio_processor.h included in audio_types.h)

    // Note on GIL: The AudioManager methods are designed to be quick (mostly queue pushes/map lookups).
    // The actual audio processing happens in separate C++ threads not holding the GIL.
    // Therefore, explicit GIL release/acquire is likely not needed within these bound methods initially.
    // If `get_mp3_data` were changed to block, it would need `py::call_guard<py::gil_scoped_release>()`.
}
