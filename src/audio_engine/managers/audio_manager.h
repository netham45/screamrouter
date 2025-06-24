#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include "../audio_types.h"
#include "source_manager.h"
#include "sink_manager.h"
#include "connection_manager.h"
#include "control_api_manager.h"
#include "data_api_manager.h"
#include "webrtc_manager.h"
#include "receiver_manager.h"
#include "../input_processor/timeshift_manager.h"
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

// #include <pybind11/pybind11.h> // For pybind11 core types like py::bytes // Temporarily commented out for linter diagnosis
 
 namespace screamrouter {
namespace audio {

// Forward declarations
class SipManager;

// Forward declarations for queue types used internally

/**
 * @brief Central orchestrator for the C++ audio engine.
 * Manages the lifecycle of audio components (RTP Receiver, Source Processors, Sink Mixers),
 * sets up communication queues, and provides the primary interface for the Python layer.
 */
class AudioManager : public std::enable_shared_from_this<AudioManager> {
public:
    AudioManager();
    ~AudioManager();

    // Prevent copying/moving
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;
    AudioManager(AudioManager&&) = delete;
    AudioManager& operator=(AudioManager&&) = delete;

    // --- Lifecycle Management ---
    /**
     * @brief Initializes the audio manager, starts the RTP receiver, TimeshiftManager, and notification processing.
     * @param rtp_listen_port The UDP port for the RTP receiver to listen on.
     * @param global_timeshift_buffer_duration_sec The maximum duration for the global timeshift buffer in seconds.
     * @return true on success, false on failure (e.g., component failed to start).
     */
    bool initialize(int rtp_listen_port = 4010, int global_timeshift_buffer_duration_sec = 300);

    /**
     * @brief Shuts down the audio manager, stopping all components and threads gracefully.
     */
    void shutdown();

    // --- Component Management API (for Python via pybind11) ---
    /**
     * @brief Adds a new audio sink based on the provided configuration.
     * Creates, configures, and starts a SinkAudioMixer instance.
     * Connects all currently active sources to the new sink.
     * @param config Configuration for the sink.
     * @return true if the sink was added successfully, false otherwise.
     */
    bool add_sink(SinkConfig config);

    /**
     * @brief Removes an existing audio sink.
     * Stops and destroys the corresponding SinkAudioMixer instance.
     * @param sink_id The unique identifier of the sink to remove.
     * @return true if the sink was removed successfully, false if not found.
     */
    bool remove_sink(const std::string& sink_id);

    /**
     * @brief Creates and configures a new SourceInputProcessor instance based on the provided settings.
     *        Generates and returns a unique instance ID for this processor.
     * @param config Configuration settings for the source (tag, initial volume, EQ, delay).
     * @return std::string The unique instance ID generated for this source processor, or an empty string on failure.
     */
    std::string configure_source(SourceConfig config);

    /**
     * @brief Removes an active source processor instance identified by its unique ID.
     *        Stops the processor, removes its queues, and disconnects it from sinks and the RTP receiver.
     * @param instance_id The unique identifier of the source processor instance to remove.
     * @return true if the instance was found and removed successfully, false otherwise.
     */
    bool remove_source(const std::string& instance_id); // Renamed from remove_source_config

    /**
     * @brief Explicitly connects a source processor instance to an existing sink.
     * @param source_instance_id The unique ID of the source processor instance.
     * @param sink_id The unique ID of the sink.
     * @return true if the connection was successful, false otherwise (e.g., source/sink not found).
     */
    bool connect_source_sink(const std::string& source_instance_id, const std::string& sink_id);

    /**
     * @brief Explicitly disconnects a source processor instance from a sink.
     * @param source_instance_id The unique ID of the source processor instance.
     * @param sink_id The unique ID of the sink.
     * @return true if the disconnection was successful, false otherwise (e.g., sink not found).
     */
    bool disconnect_source_sink(const std::string& source_instance_id, const std::string& sink_id);



    // --- Control API (for Python via pybind11) ---
    /**
     * @brief Atomically updates multiple parameters for a specific source processor instance.
     * @param instance_id Identifier of the source processor instance.
     * @param params A struct containing optional values for the parameters to update.
     */
    void update_source_parameters(const std::string& instance_id, SourceParameterUpdates params);


    // --- Data Retrieval API (for Python via pybind11) ---
    /**
     * @brief Retrieves a chunk of encoded MP3 data from a specific sink's output queue.
     * This is a polling method (non-blocking).
     * @param sink_id Identifier of the sink.
     * @return A vector of bytes containing the MP3 data, or an empty vector if no data is available or sink/queue not found.
     */
    std::vector<uint8_t> get_mp3_data(const std::string& sink_id);

    /**
     * @brief Retrieves a chunk of encoded MP3 data from a sink identified by its output IP address.
     * This method will find the sink_id associated with the given IP and then call get_mp3_data(sink_id).
     * This is a polling method (non-blocking).
     * @param ip_address The output IP address of the sink.
     * @return A vector of bytes containing the MP3 data, or an empty vector if no data is available, sink/queue not found, or IP not associated with any sink.
     */
    std::vector<uint8_t> get_mp3_data_by_ip(const std::string& ip_address);

    // --- Receiver Info API ---
    /**
     * @brief Retrieves the list of seen source tags from the main RTP receiver.
     * @return A vector of strings, where each string is a source tag (typically IP address).
     */
    std::vector<std::string> get_rtp_receiver_seen_tags();

    /**
     * @brief Retrieves the list of seen source tags from a specific Raw Scream receiver.
     * @param listen_port The port of the Raw Scream receiver.
     * @return A vector of strings (source tags), or an empty vector if the receiver is not found.
     */
    std::vector<std::string> get_raw_scream_receiver_seen_tags(int listen_port);

    /**
     * @brief Retrieves the list of seen source tags from a specific Per-Process Scream receiver.
     * @param listen_port The port of the Per-Process Scream receiver.
     * @return A vector of strings (composite source tags), or an empty vector if the receiver is not found.
     */
    std::vector<std::string> get_per_process_scream_receiver_seen_tags(int listen_port);

    /**
     * @brief Allows external components (e.g., Python plugins) to inject pre-formed audio packets
     * directly into a specific SourceInputProcessor instance.
     *
     * @param source_instance_id The unique ID of the target SourceInputProcessor.
     * @param audio_payload The raw audio data (e.g., 1152 bytes of PCM).
     * @param channels Number of audio channels.
     * @param sample_rate Sample rate in Hz.
     * @param bit_depth Bit depth (e.g., 16, 24, 32).
     * @param chlayout1 Scream channel layout byte 1.
     * @param chlayout2 Scream channel layout byte 2.
     * @return true if the packet was successfully passed to the SourceInputProcessor, false otherwise
     *         (e.g., source_instance_id not found, or processor not ready).
     */
    bool write_plugin_packet(
        const std::string& source_instance_tag,
        const std::vector<uint8_t>& audio_payload,
        int channels,
        int sample_rate,
        int bit_depth,
        uint8_t chlayout1,
        uint8_t chlayout2
    );

    void inject_plugin_packet_globally(
        const std::string& source_tag, // This tag will be used by TimeshiftManager for filtering
        const std::vector<uint8_t>& audio_payload,
        int channels,
        int sample_rate,
        int bit_depth,
        uint8_t chlayout1,
        uint8_t chlayout2
    );

    // Removed set_sink_tcp_fd

    // --- WebRTC Listener Management ---
    bool add_webrtc_listener(
        const std::string& sink_id,
        const std::string& listener_id,
        const std::string& offer_sdp,
        std::function<void(const std::string& sdp)> on_local_description_callback,
        std::function<void(const std::string& candidate, const std::string& sdpMid)> on_ice_candidate_callback
    );
    bool remove_webrtc_listener(const std::string& sink_id, const std::string& listener_id);
    void set_webrtc_remote_description(const std::string& sink_id, const std::string& listener_id, const std::string& sdp, const std::string& type);
    void add_webrtc_remote_ice_candidate(const std::string& sink_id, const std::string& listener_id, const std::string& candidate, const std::string& sdpMid);

private:
    // --- Internal State ---
    std::atomic<bool> m_running{false};
    std::mutex m_manager_mutex;

    // --- Managers ---
    std::unique_ptr<TimeshiftManager> m_timeshift_manager;
    std::unique_ptr<SourceManager> m_source_manager;
    std::unique_ptr<SinkManager> m_sink_manager;
    std::unique_ptr<ConnectionManager> m_connection_manager;
    std::unique_ptr<ControlApiManager> m_control_api_manager;
    std::unique_ptr<DataApiManager> m_data_api_manager;
    std::unique_ptr<WebRtcManager> m_webrtc_manager;
    std::unique_ptr<ReceiverManager> m_receiver_manager;

    // --- Threading and Queues ---
    std::shared_ptr<NotificationQueue> m_notification_queue;
    std::thread m_notification_thread;

    // --- Internal Methods ---
    void process_notifications();
    };
    
    inline void bind_audio_manager(pybind11::module_ &m) {
        namespace py = pybind11;
        py::class_<AudioManager, std::shared_ptr<AudioManager>>(m, "AudioManager", "Main class for managing the C++ audio engine")
            .def(py::init<>(), "Constructor") // Bind the constructor
    
            // Lifecycle Methods
            .def("initialize", &AudioManager::initialize,
                 py::arg("rtp_listen_port") = 40000,
                 py::arg("global_timeshift_buffer_duration_sec") = 300, // Added new argument
                 "Initializes the audio manager, including TimeshiftManager. Returns true on success.")
            .def("shutdown", &AudioManager::shutdown,
                 "Stops all audio components and cleans up resources.")
    
            // Component Management Methods
           .def("add_sink", &AudioManager::add_sink, py::arg("config"), "Adds a new audio sink.")
            .def("remove_sink", &AudioManager::remove_sink,
                 py::arg("sink_id"),
                 "Stops and removes the audio sink with the given ID. Returns true on success.")
           .def("configure_source", &AudioManager::configure_source, py::arg("config"), "Configures a new source.")
            .def("remove_source", &AudioManager::remove_source, // Renamed method
                 py::arg("instance_id"), // Changed argument
                 "Removes the source processor instance with the given ID. Returns true on success.")
            .def("connect_source_sink", &AudioManager::connect_source_sink,
                 py::arg("source_instance_id"), py::arg("sink_id"), // Changed arguments
                 "Explicitly connects a source instance to a sink. Returns true on success.")
            .def("disconnect_source_sink", &AudioManager::disconnect_source_sink,
                 py::arg("source_instance_id"), py::arg("sink_id"), // Changed arguments
                 "Explicitly disconnects a source instance from a sink. Returns true on success.")
    
    
    
            // Control Methods
           .def("update_source_parameters", &AudioManager::update_source_parameters, py::arg("instance_id"), py::arg("params"), "Updates source parameters.")
    
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
                 "Allows a plugin to inject a pre-formed audio packet (as bytes) into a SourceInputProcessor instance. Returns true on success.")
           
           // WebRTC Listener Methods
           .def("add_webrtc_listener", &AudioManager::add_webrtc_listener,
                py::arg("sink_id"),
                py::arg("listener_id"),
                py::arg("offer_sdp"),
                py::arg("on_local_description_callback"),
                py::arg("on_ice_candidate_callback"),
                "Creates and attaches a WebRTC listener to a sink.")
           .def("remove_webrtc_listener", &AudioManager::remove_webrtc_listener,
                py::arg("sink_id"),
                py::arg("listener_id"),
                "Removes a WebRTC listener from a sink.")
           .def("set_webrtc_remote_description", &AudioManager::set_webrtc_remote_description,
                py::arg("sink_id"),
                py::arg("listener_id"),
                py::arg("sdp"),
                py::arg("type"),
                "Forwards a remote SDP to a specific WebRTC listener.")
           .def("add_webrtc_remote_ice_candidate", &AudioManager::add_webrtc_remote_ice_candidate,
                py::arg("sink_id"),
                py::arg("listener_id"),
                py::arg("candidate"),
                py::arg("sdpMid"),
                "Forwards a remote ICE candidate to a specific WebRTC listener.");
    }
    
    } // namespace audio
    } // namespace screamrouter

#endif // AUDIO_MANAGER_H
