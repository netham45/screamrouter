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
using namespace screamrouter::config; // Add config namespace

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
        .def_readwrite("chlayout2", &SinkConfig::chlayout2, "Scream header channel layout byte 2")
        .def_readwrite("use_tcp", &SinkConfig::use_tcp, "Whether this sink uses TCP output (managed externally)");
        // Removed .def_readwrite("enable_mp3", ...) as MP3 queue is now always created internally
        // Add __repr__
        // .def("__repr__", [](const SinkConfig &a) {
        //     return "<SinkConfig id='" + a.id + "'>";
        // });

    py::class_<RawScreamReceiverConfig>(m, "RawScreamReceiverConfig", "Configuration for a raw Scream receiver")
        .def(py::init<>()) // Default constructor
        .def_readwrite("listen_port", &RawScreamReceiverConfig::listen_port, "UDP port to listen on");
        // Add .def_readwrite("bind_ip", ...) if that field is included

    py::enum_<InputProtocolType>(m, "InputProtocolType", "Specifies the expected input packet type")
        .value("RTP_SCREAM_PAYLOAD", InputProtocolType::RTP_SCREAM_PAYLOAD)
        .value("RAW_SCREAM_PACKET", InputProtocolType::RAW_SCREAM_PACKET)
        .export_values(); // Make enum values accessible as module attributes

    // --- Bind AudioManager Class ---
    // Expose the main C++ audio engine class to Python

    py::class_<AudioManager>(m, "AudioManager", "Main class for managing the C++ audio engine")
        .def(py::init<>(), "Constructor") // Bind the constructor

        // Lifecycle Methods
        .def("initialize", &AudioManager::initialize,
             py::arg("rtp_listen_port") = 40000, // Provide default value for optional arg
             "Initializes the audio manager, starts RTP listener and notification thread. Returns true on success.")
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

        // External Control Methods (like setting TCP FD)
        .def("set_sink_tcp_fd", &AudioManager::set_sink_tcp_fd,
             py::arg("sink_id"), py::arg("fd"),
             "Updates the externally managed TCP file descriptor for a sink.");

    // --- Bind New Configuration Structs (Task 03_01) ---

    py::class_<AppliedSourcePathParams>(m, "AppliedSourcePathParams", "Parameters for a specific source-to-sink audio path")
        .def(py::init<>()) // Bind default constructor (initializes eq_values)
        .def_readwrite("path_id", &AppliedSourcePathParams::path_id, "Unique ID for this path (e.g., source_tag_to_sink_id)")
        .def_readwrite("source_tag", &AppliedSourcePathParams::source_tag, "Original source identifier (e.g., IP address)")
        .def_readwrite("target_sink_id", &AppliedSourcePathParams::target_sink_id, "ID of the target sink for this path")
        .def_readwrite("volume", &AppliedSourcePathParams::volume, "Volume for this path")
        .def_readwrite("eq_values", &AppliedSourcePathParams::eq_values, "Equalizer settings for this path (list/vector of floats)")
        .def_readwrite("delay_ms", &AppliedSourcePathParams::delay_ms, "Delay in milliseconds for this path")
        .def_readwrite("timeshift_sec", &AppliedSourcePathParams::timeshift_sec, "Timeshift in seconds for this path")
        .def_readwrite("target_output_channels", &AppliedSourcePathParams::target_output_channels, "Required output channels for the target sink")
        .def_readwrite("target_output_samplerate", &AppliedSourcePathParams::target_output_samplerate, "Required output sample rate for the target sink")
        .def_readwrite("generated_instance_id", &AppliedSourcePathParams::generated_instance_id, "(Read-only from Python perspective) Instance ID generated by C++"); 

    py::class_<AppliedSinkParams>(m, "AppliedSinkParams", "Parameters for a configured sink")
        .def(py::init<>()) // Bind default constructor
        .def_readwrite("sink_id", &AppliedSinkParams::sink_id, "Unique identifier for the sink")
        // Bind the nested C++ SinkConfig. Python will need to create/populate this C++ struct instance.
        .def_readwrite("sink_engine_config", &AppliedSinkParams::sink_engine_config, "C++ SinkConfig parameters for AudioManager") 
        .def_readwrite("connected_source_path_ids", &AppliedSinkParams::connected_source_path_ids, "List of path_ids connected to this sink");

    py::class_<DesiredEngineState>(m, "DesiredEngineState", "Represents the complete desired state of the audio engine")
        .def(py::init<>()) // Bind default constructor
        .def_readwrite("source_paths", &DesiredEngineState::source_paths, "List of all desired AppliedSourcePathParams")
        .def_readwrite("sinks", &DesiredEngineState::sinks, "List of all desired AppliedSinkParams");

    // --- Bind AudioEngineConfigApplier Class (Task 03_02) ---
    py::class_<AudioEngineConfigApplier>(m, "AudioEngineConfigApplier", "Applies desired configuration state to the C++ AudioManager")
        .def(py::init<AudioManager&>(), 
             py::arg("audio_manager"), 
             "Constructor, takes an AudioManager instance")
        
        .def("apply_state", &AudioEngineConfigApplier::apply_state,
             py::arg("desired_state"), 
             "Applies the provided DesiredEngineState to the AudioManager, returns true on success (basic check).");


    // Define constants if needed (e.g., EQ_BANDS)
    m.attr("EQ_BANDS") = py::int_(EQ_BANDS); // Ensure EQ_BANDS is accessible (likely via audio_processor.h included in audio_types.h)

    // Note on GIL: The AudioManager methods are designed to be quick (mostly queue pushes/map lookups).
    // The actual audio processing happens in separate C++ threads not holding the GIL.
    // Therefore, explicit GIL release/acquire is likely not needed within these bound methods initially.
    // If `get_mp3_data` were changed to block, it would need `py::call_guard<py::gil_scoped_release>()`.
}
