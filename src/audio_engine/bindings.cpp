#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // For automatic conversions of vector, map, string, etc.
#include <pybind11/chrono.h> // If any chrono types were exposed (not currently needed)

#include "audio_manager.h"
// audio_types.h is included by audio_manager.h, but include explicitly for clarity if needed
// #include "../c_utils/audio_types.h"

namespace py = pybind11;
// Import the C++ namespace to shorten type names
using namespace screamrouter::audio;

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
        .def_readwrite("initial_delay_ms", &SourceConfig::initial_delay_ms, "Initial delay in milliseconds (default: 0)");
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

    // --- Bind AudioManager Class ---
    // Expose the main C++ audio engine class to Python

    py::class_<AudioManager>(m, "AudioManager", "Main class for managing the C++ audio engine")
        .def(py::init<>(), "Constructor") // Bind the constructor

        // Lifecycle Methods
        .def("initialize", &AudioManager::initialize,
             py::arg("rtp_listen_port") = 4010, // Provide default value for optional arg
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

    // Define constants if needed (e.g., EQ_BANDS)
    m.attr("EQ_BANDS") = py::int_(EQ_BANDS);

    // Note on GIL: The AudioManager methods are designed to be quick (mostly queue pushes/map lookups).
    // The actual audio processing happens in separate C++ threads not holding the GIL.
    // Therefore, explicit GIL release/acquire is likely not needed within these bound methods initially.
    // If `get_mp3_data` were changed to block, it would need `py::call_guard<py::gil_scoped_release>()`.
}
