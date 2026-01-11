/**
 * @file audio_manager.h
 * @brief Defines the AudioManager class, the central orchestrator for the audio engine.
 * @details This file contains the definition of the AudioManager class, which is responsible
 *          for managing the entire lifecycle of the audio processing pipeline. It owns and
 *          coordinates various sub-managers (for sources, sinks, connections, etc.) and
 *          provides the primary C++ API to be exposed to Python via pybind11.
 */
#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include "../audio_types.h"
#include "source_manager.h"
#include "sink_manager.h"
#include "connection_manager.h"
#include "control_api_manager.h"
#include "mp3_data_api_manager.h"
#include "webrtc_manager.h"
#include "receiver_manager.h"
#include "../input_processor/timeshift_manager.h"
#include "stats_manager.h"
#include "../configuration/audio_engine_settings.h"
#include "../system_audio/system_device_enumerator.h"
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

namespace screamrouter {
namespace audio {

class SipManager;
class GlobalSynchronizationClock;
class SinkSynchronizationCoordinator;

/**
 * @class AudioManager
 * @brief Central orchestrator for the C++ audio engine.
 * @details Manages the lifecycle of all audio components (receivers, processors, mixers),
 *          sets up communication queues, and provides the primary interface for the Python layer.
 *          This class follows the RAII principle, ensuring that all managed components are
 *          properly initialized on construction and cleaned up on destruction.
 */
class AudioManager : public std::enable_shared_from_this<AudioManager> {
public:
    /**
     * @brief Default constructor.
     */
    AudioManager();
    /**
     * @brief Destructor. Ensures a clean shutdown of all components.
     */
    ~AudioManager();

    // Prevent copying and moving to ensure singleton-like management.
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;
    AudioManager(AudioManager&&) = delete;
    AudioManager& operator=(AudioManager&&) = delete;

    std::vector<uint8_t> get_mp3_data(const std::string& sink_id);

    // --- Lifecycle Management ---
    /**
     * @brief Initializes the audio manager and all its sub-components.
     * @param rtp_listen_port The UDP port for the main RTP receiver to listen on.
     * @param global_timeshift_buffer_duration_sec The max duration for the global timeshift buffer.
     * @return true on success, false on failure.
     */
    bool initialize(int rtp_listen_port = 4010, int global_timeshift_buffer_duration_sec = 300);

    /**
     * @brief Shuts down the audio manager, stopping all components and threads gracefully.
     */
    void shutdown();

    // --- Component Management API ---
    /**
     * @brief Adds a new audio sink (output).
     * @param config Configuration for the sink.
     * @return true if the sink was added successfully, false otherwise.
     */
    bool add_sink(SinkConfig config);

    /**
     * @brief Removes an existing audio sink.
     * @param sink_id The unique identifier of the sink to remove.
     * @return true if the sink was removed successfully, false if not found.
     */
    bool remove_sink(const std::string& sink_id);

    /**
     * @brief Creates and configures a new source processing path.
     * @param config Configuration settings for the source path.
     * @return A unique instance ID for the new processor, or an empty string on failure.
     */
    std::string configure_source(SourceConfig config);

    /**
     * @brief Removes an active source processing path.
     * @param instance_id The unique identifier of the source processor instance to remove.
     * @return true if the instance was found and removed successfully, false otherwise.
     */
    bool remove_source(const std::string& instance_id);

    /**
     * @brief Connects a source processor instance to a sink.
     * @param source_instance_id The unique ID of the source processor instance.
     * @param sink_id The unique ID of the sink.
     * @return true if the connection was successful, false otherwise.
     */
    bool connect_source_sink(const std::string& source_instance_id, const std::string& sink_id);

    /**
     * @brief Disconnects a source processor instance from a sink.
     * @param source_instance_id The unique ID of the source processor instance.
     * @param sink_id The unique ID of the sink.
     * @return true if the disconnection was successful, false otherwise.
     */
    bool disconnect_source_sink(const std::string& source_instance_id, const std::string& sink_id);

    // --- Control API ---
    /**
     * @brief Atomically updates multiple parameters for a source processor.
     * @param instance_id Identifier of the source processor instance.
     * @param params A struct containing optional values for the parameters to update.
     */
    void update_source_parameters(const std::string& instance_id, SourceParameterUpdates params);

    // --- Data Retrieval API ---
    /**
     * @brief Retrieves a chunk of encoded MP3 data from a sink.
     * @param sink_id Identifier of the sink.
     * @return A vector of bytes containing MP3 data, or an empty vector if none is available.
     */

    /**
     * @brief Retrieves a chunk of encoded MP3 data from a sink by its IP address.
     * @param ip_address The output IP address of the sink.
     * @return A vector of bytes containing MP3 data, or an empty vector if none is available.
     */
    std::vector<uint8_t> get_mp3_data_by_ip(const std::string& ip_address);

    /**
     * @brief Export a raw PCM window from the timeshift buffer for a given source.
     * @param source_tag Identifier of the source stream.
     * @param lookback_seconds Length of history to include, defaults to 300 seconds.
     * @return Export payload with PCM data and metadata, or std::nullopt if unavailable.
     */
    std::optional<TimeshiftBufferExport> export_timeshift_buffer(
        const std::string& source_tag,
        double lookback_seconds = 300.0);

    // --- Receiver Info API ---
    /**
     * @brief Retrieves seen source tags from the main RTP receiver.
     * @return A vector of strings, where each string is a source tag (IP address).
     */
    std::vector<std::string> get_rtp_receiver_seen_tags();

    /**
     * @brief Retrieves SAP announcements detected by the main RTP receiver.
     * @return A Python list of dictionaries describing each SAP announcement.
     */
    pybind11::list get_rtp_sap_announcements();

    /**
     * @brief Retrieves seen source tags from a Raw Scream receiver.
     * @param listen_port The port of the Raw Scream receiver.
     * @return A vector of source tags, or an empty vector if the receiver is not found.
     */
    std::vector<std::string> get_raw_scream_receiver_seen_tags(int listen_port);

    /**
     * @brief Retrieves seen source tags from a Per-Process Scream receiver.
     * @param listen_port The port of the Per-Process Scream receiver.
     * @return A vector of composite source tags, or an empty vector if the receiver is not found.
     */
    std::vector<std::string> get_per_process_scream_receiver_seen_tags(int listen_port);
#if !defined(_WIN32)
#if !defined(_WIN32)
    std::vector<std::string> get_pulse_receiver_seen_tags();
#endif
#endif

    /**
     * @brief Adds a reference to a system capture device, creating the receiver if needed.
     * @param device_tag Platform-specific capture tag.
     * @param params Desired capture parameters.
     * @return true if the receiver is available and active.
     */
    bool add_system_capture_reference(const std::string& device_tag, CaptureParams params);

    /**
     * @brief Removes a reference to a system capture device.
     * @param device_tag Platform-specific capture tag previously registered.
     */
    void remove_system_capture_reference(const std::string& device_tag);

    /**
     * @brief Convenience wrapper for add_system_capture_reference().
     */
    bool ensure_system_capture_device(const std::string& device_tag);

    /**
     * @brief Convenience wrapper for remove_system_capture_reference().
     */
    void release_system_capture_device(const std::string& device_tag);

    /**
     * @brief Injects a plugin-generated audio packet into a specific source processor.
     * @param source_instance_tag The unique ID of the target SourceInputProcessor.
     * @param audio_payload The raw audio data.
     * @param channels Number of audio channels.
     * @param sample_rate Sample rate in Hz.
     * @param bit_depth Bit depth.
     * @param chlayout1 Scream channel layout byte 1.
     * @param chlayout2 Scream channel layout byte 2.
     * @return true if the packet was successfully injected, false otherwise.
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

    std::optional<std::string> resolve_stream_tag(const std::string& tag);
    std::vector<std::string> list_stream_tags_for_wildcard(const std::string& wildcard_tag);

    /**
     * @brief Computes the chunk size in bytes for a given audio format using current settings.
     * @param channels Number of channels.
     * @param bit_depth Bit depth (multiple of 8).
     * @return Chunk size in bytes, or 0 if the format is invalid.
     */
    std::size_t get_chunk_size_bytes_for_format(int channels, int bit_depth) const;

    // --- WebRTC Listener Management ---
    /**
     * @brief Adds a WebRTC listener to a sink.
     * @param sink_id The ID of the sink to attach the listener to.
     * @param listener_id A unique ID for the new listener.
     * @param offer_sdp The SDP offer from the remote peer.
     * @param on_local_description_callback Callback to send the local SDP answer to the peer.
     * @param on_ice_candidate_callback Callback to send local ICE candidates to the peer.
     * @return true on success, false otherwise.
     */
    bool add_webrtc_listener(
        const std::string& sink_id,
        const std::string& listener_id,
        const std::string& offer_sdp,
        std::function<void(const std::string& sdp)> on_local_description_callback,
        std::function<void(const std::string& candidate, const std::string& sdpMid)> on_ice_candidate_callback,
        const std::string& client_ip
    );
    /**
     * @brief Removes a WebRTC listener from a sink.
     * @param sink_id The ID of the sink.
     * @param listener_id The ID of the listener to remove.
     * @return true on success, false if not found.
     */
    bool remove_webrtc_listener(const std::string& sink_id, const std::string& listener_id);
    /**
     * @brief Sets the remote SDP description for a WebRTC listener.
     * @param sink_id The ID of the sink.
     * @param listener_id The ID of the listener.
     * @param sdp The SDP from the remote peer.
     * @param type The type of the SDP ("offer" or "answer").
     */
    void set_webrtc_remote_description(const std::string& sink_id, const std::string& listener_id, const std::string& sdp, const std::string& type);
    /**
     * @brief Adds a remote ICE candidate for a WebRTC listener.
     * @param sink_id The ID of the sink.
     * @param listener_id The ID of the listener.
     * @param candidate The ICE candidate string.
     * @param sdpMid The SDP media line index.
     */
    void add_webrtc_remote_ice_candidate(const std::string& sink_id, const std::string& listener_id, const std::string& candidate, const std::string& sdpMid);

    /**
     * @brief Retrieves a snapshot of all current audio engine statistics.
     * @return An object containing various performance and state metrics.
     */
    AudioEngineStats get_audio_engine_stats();

    /**
     * @brief Retrieves the current audio engine tuning settings.
     * @return A copy of the current AudioEngineSettings object.
     */
    AudioEngineSettings get_audio_settings();

    /**
     * @brief Updates the audio engine tuning settings.
     * @param new_settings The new settings to apply.
     */
    void set_audio_settings(const AudioEngineSettings& new_settings);

    /**
     * @brief Retrieves synchronization statistics for all active sync clocks.
     * @return A Python dictionary mapping sample rates to their sync statistics.
     */
    /**
     * @brief Lists cached system audio devices discovered by platform watchers.
     */
    SystemDeviceRegistry list_system_devices();

    /**
     * @brief Returns pending device discovery notifications and clears the queue.
     */
    std::vector<DeviceDiscoveryNotification> drain_device_notifications();

    void handle_stream_tag_resolved(const std::string& wildcard_tag,
                                    const std::string& concrete_tag);
    void handle_stream_tag_removed(const std::string& wildcard_tag);
    void handle_wildcard_match(const WildcardMatchEvent& event);

    void set_stream_tag_listener(std::function<void(const std::string&, const std::string&)> on_resolved,
                                 std::function<void(const std::string&)> on_removed);
    void clear_stream_tag_listener();

private:
    std::atomic<bool> m_running{false};
    mutable std::recursive_mutex m_manager_mutex;
    std::shared_ptr<AudioEngineSettings> m_settings;

    // --- Sub-Managers ---
    std::unique_ptr<TimeshiftManager> m_timeshift_manager;
    std::unique_ptr<SourceManager> m_source_manager;
    std::unique_ptr<SinkManager> m_sink_manager;
    std::unique_ptr<ConnectionManager> m_connection_manager;
    std::unique_ptr<ControlApiManager> m_control_api_manager;
    std::unique_ptr<MP3DataApiManager> m_mp3_data_api_manager;
    std::unique_ptr<WebRtcManager> m_webrtc_manager;
    std::unique_ptr<ReceiverManager> m_receiver_manager;
    std::unique_ptr<StatsManager> m_stats_manager;
    std::unique_ptr<system_audio::SystemDeviceEnumerator> m_system_device_enumerator;

    std::shared_ptr<NotificationQueue> m_notification_queue;
    std::thread m_notification_thread;

    mutable std::mutex device_registry_mutex_;
    SystemDeviceRegistry system_device_registry_;
    std::mutex pending_device_events_mutex_;
    std::vector<DeviceDiscoveryNotification> pending_device_events_;

    std::mutex stream_tag_listener_mutex_;
    std::function<void(const std::string&, const std::string&)> stream_tag_listener_on_resolved_;
    std::function<void(const std::string&)> stream_tag_listener_on_removed_;


    // --- Multi-Rate Synchronization ---
    std::map<int, std::unique_ptr<GlobalSynchronizationClock>> sync_clocks_;
    std::map<std::string, std::unique_ptr<SinkSynchronizationCoordinator>> sink_coordinators_;

    /**
     * @brief Internal method to process notifications from other components.
     */
    void process_notifications();
    
    /**
     * @brief Gets or creates a GlobalSynchronizationClock for the specified sample rate.
     * @param sample_rate The sample rate in Hz (e.g., 48000, 44100).
     * @return Pointer to the GlobalSynchronizationClock for this rate.
     */
    GlobalSynchronizationClock* get_or_create_sync_clock(int sample_rate);

    // Debug helper to dump state during shutdown hangs
    void debug_dump_state(const char* label);
};
    
/**
 * @brief Binds the AudioManager class and its methods to a Python module.
 * @param m The pybind11 module to which the class will be bound.
 */
inline void bind_audio_manager(pybind11::module_ &m) {
    namespace py = pybind11;

    py::class_<TimeshiftTuning>(m, "TimeshiftTuning")
        .def(py::init<>())
        .def_readwrite("cleanup_interval_ms", &TimeshiftTuning::cleanup_interval_ms)
        .def_readwrite("late_packet_threshold_ms", &TimeshiftTuning::late_packet_threshold_ms)
        .def_readwrite("target_buffer_level_ms", &TimeshiftTuning::target_buffer_level_ms)
        .def_readwrite("loop_max_sleep_ms", &TimeshiftTuning::loop_max_sleep_ms)
        .def_readwrite("max_catchup_lag_ms", &TimeshiftTuning::max_catchup_lag_ms)
        .def_readwrite("max_clock_pending_packets", &TimeshiftTuning::max_clock_pending_packets)
        .def_readwrite("rtp_continuity_slack_seconds", &TimeshiftTuning::rtp_continuity_slack_seconds)
        .def_readwrite("rtp_session_reset_threshold_seconds", &TimeshiftTuning::rtp_session_reset_threshold_seconds)
        .def_readwrite("playback_ratio_max_deviation_ppm", &TimeshiftTuning::playback_ratio_max_deviation_ppm)
        .def_readwrite("playback_ratio_slew_ppm_per_sec", &TimeshiftTuning::playback_ratio_slew_ppm_per_sec)
        .def_readwrite("playback_ratio_kp", &TimeshiftTuning::playback_ratio_kp)
        .def_readwrite("playback_ratio_ki", &TimeshiftTuning::playback_ratio_ki)
        .def_readwrite("playback_ratio_integral_limit_ppm", &TimeshiftTuning::playback_ratio_integral_limit_ppm)
        .def_readwrite("playback_ratio_dead_zone_ratio", &TimeshiftTuning::playback_ratio_dead_zone_ratio)
        .def_readwrite("playback_ratio_integral_decay", &TimeshiftTuning::playback_ratio_integral_decay)
        .def_readwrite("playback_ratio_smoothing", &TimeshiftTuning::playback_ratio_smoothing)
        .def_readwrite("playback_ratio_inbound_rate_smoothing", &TimeshiftTuning::playback_ratio_inbound_rate_smoothing)
        .def_readwrite("playback_catchup_ppm_per_ms", &TimeshiftTuning::playback_catchup_ppm_per_ms)
        .def_readwrite("playback_catchup_max_ppm", &TimeshiftTuning::playback_catchup_max_ppm)
        .def_readwrite("max_playout_lead_ms", &TimeshiftTuning::max_playout_lead_ms)
        .def_readwrite("playback_rate_adjustment_enabled", &TimeshiftTuning::playback_rate_adjustment_enabled);

    py::class_<ProfilerSettings>(m, "ProfilerSettings")
        .def(py::init<>())
        .def_readwrite("enabled", &ProfilerSettings::enabled)
        .def_readwrite("log_interval_ms", &ProfilerSettings::log_interval_ms);

    py::class_<MixerTuning>(m, "MixerTuning")
        .def(py::init<>())
        .def_readwrite("mp3_bitrate_kbps", &MixerTuning::mp3_bitrate_kbps)
        .def_readwrite("mp3_vbr_enabled", &MixerTuning::mp3_vbr_enabled)
        .def_readwrite("mp3_output_queue_max_size", &MixerTuning::mp3_output_queue_max_size)
        .def_readwrite("underrun_hold_timeout_ms", &MixerTuning::underrun_hold_timeout_ms)
        .def_readwrite("max_input_queue_chunks", &MixerTuning::max_input_queue_chunks)
        .def_readwrite("min_input_queue_chunks", &MixerTuning::min_input_queue_chunks)
        .def_readwrite("max_ready_chunks_per_source", &MixerTuning::max_ready_chunks_per_source)
        .def_readwrite("max_queued_chunks", &MixerTuning::max_queued_chunks)
        .def_readwrite("max_input_queue_duration_ms", &MixerTuning::max_input_queue_duration_ms)
        .def_readwrite("min_input_queue_duration_ms", &MixerTuning::min_input_queue_duration_ms)
        .def_readwrite("max_ready_queue_duration_ms", &MixerTuning::max_ready_queue_duration_ms);

    py::class_<SourceProcessorTuning>(m, "SourceProcessorTuning")
        .def(py::init<>())
        .def_readwrite("command_loop_sleep_ms", &SourceProcessorTuning::command_loop_sleep_ms)
        .def_readwrite("discontinuity_threshold_ms", &SourceProcessorTuning::discontinuity_threshold_ms);

    py::class_<ProcessorTuning>(m, "ProcessorTuning")
        .def(py::init<>())
        .def_readwrite("oversampling_factor", &ProcessorTuning::oversampling_factor)
        .def_readwrite("volume_smoothing_factor", &ProcessorTuning::volume_smoothing_factor)
        .def_readwrite("dc_filter_cutoff_hz", &ProcessorTuning::dc_filter_cutoff_hz)
        .def_readwrite("normalization_target_rms", &ProcessorTuning::normalization_target_rms)
        .def_readwrite("normalization_attack_smoothing", &ProcessorTuning::normalization_attack_smoothing)
        .def_readwrite("normalization_decay_smoothing", &ProcessorTuning::normalization_decay_smoothing)
        .def_readwrite("dither_noise_shaping_factor", &ProcessorTuning::dither_noise_shaping_factor);

    py::class_<SynchronizationSettings>(m, "SynchronizationSettings")
        .def(py::init<>())
        .def_readwrite("enable_multi_sink_sync", &SynchronizationSettings::enable_multi_sink_sync);

    py::class_<SynchronizationTuning>(m, "SynchronizationTuning")
        .def(py::init<>())
        .def_readwrite("barrier_timeout_ms", &SynchronizationTuning::barrier_timeout_ms)
        .def_readwrite("sync_proportional_gain", &SynchronizationTuning::sync_proportional_gain)
        .def_readwrite("max_rate_adjustment", &SynchronizationTuning::max_rate_adjustment)
        .def_readwrite("sync_smoothing_factor", &SynchronizationTuning::sync_smoothing_factor);

    py::class_<SystemAudioTuning>(m, "SystemAudioTuning")
        .def(py::init<>())
        .def_readwrite("alsa_target_latency_ms", &SystemAudioTuning::alsa_target_latency_ms)
        .def_readwrite("alsa_periods_per_buffer", &SystemAudioTuning::alsa_periods_per_buffer)
        .def_readwrite("alsa_dynamic_latency_enabled", &SystemAudioTuning::alsa_dynamic_latency_enabled)
        .def_readwrite("alsa_latency_min_ms", &SystemAudioTuning::alsa_latency_min_ms)
        .def_readwrite("alsa_latency_max_ms", &SystemAudioTuning::alsa_latency_max_ms)
        .def_readwrite("alsa_latency_low_water_ms", &SystemAudioTuning::alsa_latency_low_water_ms)
        .def_readwrite("alsa_latency_high_water_ms", &SystemAudioTuning::alsa_latency_high_water_ms)
        .def_readwrite("alsa_latency_integral_gain", &SystemAudioTuning::alsa_latency_integral_gain)
        .def_readwrite("alsa_latency_rate_limit_ms_per_sec", &SystemAudioTuning::alsa_latency_rate_limit_ms_per_sec)
        .def_readwrite("alsa_latency_idle_decay_ms_per_sec", &SystemAudioTuning::alsa_latency_idle_decay_ms_per_sec)
        .def_readwrite("alsa_latency_apply_hysteresis_ms", &SystemAudioTuning::alsa_latency_apply_hysteresis_ms)
        .def_readwrite("alsa_latency_reconfig_cooldown_ms", &SystemAudioTuning::alsa_latency_reconfig_cooldown_ms)
        .def_readwrite("alsa_latency_xrun_boost_ms", &SystemAudioTuning::alsa_latency_xrun_boost_ms)
        .def_readwrite("alsa_latency_low_step_ms", &SystemAudioTuning::alsa_latency_low_step_ms);

    py::class_<AudioEngineSettings>(m, "AudioEngineSettings")
        .def(py::init<>())
        .def_readwrite("chunk_size_bytes", &AudioEngineSettings::chunk_size_bytes)
        .def_readwrite("base_frames_per_chunk_mono16", &AudioEngineSettings::base_frames_per_chunk_mono16)
        .def_readwrite("timeshift_tuning", &AudioEngineSettings::timeshift_tuning)
        .def_readwrite("profiler", &AudioEngineSettings::profiler)
        .def_readwrite("mixer_tuning", &AudioEngineSettings::mixer_tuning)
        .def_readwrite("source_processor_tuning", &AudioEngineSettings::source_processor_tuning)
        .def_readwrite("processor_tuning", &AudioEngineSettings::processor_tuning)
        .def_readwrite("synchronization", &AudioEngineSettings::synchronization)
        .def_readwrite("synchronization_tuning", &AudioEngineSettings::synchronization_tuning)
        .def_readwrite("system_audio_tuning", &AudioEngineSettings::system_audio_tuning);

    py::class_<TimeshiftBufferExport>(m, "TimeshiftBufferExport")
        .def(py::init<>())
        .def_property_readonly(
            "pcm_data",
            [](const TimeshiftBufferExport& self) {
                return py::bytes(reinterpret_cast<const char*>(self.pcm_data.data()), self.pcm_data.size());
            },
            "Raw PCM payload as bytes.")
        .def_readonly("sample_rate", &TimeshiftBufferExport::sample_rate)
        .def_readonly("channels", &TimeshiftBufferExport::channels)
        .def_readonly("bit_depth", &TimeshiftBufferExport::bit_depth)
        .def_readonly("chunk_size_bytes", &TimeshiftBufferExport::chunk_size_bytes)
        .def_readonly("duration_seconds", &TimeshiftBufferExport::duration_seconds)
        .def_readonly("earliest_packet_age_seconds", &TimeshiftBufferExport::earliest_packet_age_seconds)
        .def_readonly("latest_packet_age_seconds", &TimeshiftBufferExport::latest_packet_age_seconds)
        .def_readonly("lookback_seconds_requested", &TimeshiftBufferExport::lookback_seconds_requested);

    py::class_<AudioManager, std::shared_ptr<AudioManager>>(m, "AudioManager", "Main class for managing the C++ audio engine")
        .def(py::init<>(), "Constructor")
        .def("initialize", &AudioManager::initialize,
             py::arg("rtp_listen_port") = 40000,
             py::arg("global_timeshift_buffer_duration_sec") = 300,
             "Initializes the audio manager, including TimeshiftManager. Returns true on success.")
        .def("shutdown", &AudioManager::shutdown,
             "Stops all audio components and cleans up resources.")
        .def("get_chunk_size_bytes_for_format", &AudioManager::get_chunk_size_bytes_for_format,
             py::arg("channels"), py::arg("bit_depth"),
             "Returns the chunk size in bytes for the provided channel count and bit depth.")
        .def("get_mp3_data_by_ip", [](AudioManager &self, const std::string& ip_address) -> py::bytes {
                std::vector<uint8_t> data_vec = self.get_mp3_data_by_ip(ip_address);
                return py::bytes(reinterpret_cast<const char*>(data_vec.data()), data_vec.size());
            },
            py::arg("ip_address"),
            "Retrieves a chunk of MP3 data (as bytes) from a sink identified by its output IP address.")
        .def("export_timeshift_buffer",
             &AudioManager::export_timeshift_buffer,
             py::arg("source_tag"),
             py::arg("lookback_seconds") = 300.0,
             "Exports a raw PCM window from the timeshift buffer for the specified source. "
             "Returns a TimeshiftBufferExport instance or None if data is unavailable.")
        .def("get_rtp_receiver_seen_tags", &AudioManager::get_rtp_receiver_seen_tags,
             "Retrieves the list of seen source tags from the main RTP receiver.")
        .def("get_rtp_sap_announcements", &AudioManager::get_rtp_sap_announcements,
             "Retrieves the list of SAP announcements detected by the RTP receiver.")
        .def("get_raw_scream_receiver_seen_tags", &AudioManager::get_raw_scream_receiver_seen_tags,
             py::arg("listen_port"),
             "Retrieves the list of seen source tags from a specific Raw Scream receiver.")
        .def("get_per_process_scream_receiver_seen_tags", &AudioManager::get_per_process_scream_receiver_seen_tags,
             py::arg("listen_port"),
             "Retrieves the list of seen source tags from a specific Per-Process Scream receiver.")
#if !defined(_WIN32)
        .def("get_pulse_receiver_seen_tags", &AudioManager::get_pulse_receiver_seen_tags,
             "Retrieves the list of seen source tags from the PulseAudio receiver.")
#endif
        .def("write_plugin_packet",
             [](AudioManager &self,
                const std::string& source_instance_id,
                py::bytes audio_payload_bytes,
                int channels,
                int sample_rate,
                int bit_depth,
                uint8_t chlayout1,
                uint8_t chlayout2) -> bool {
                 py::buffer_info info = py::buffer(audio_payload_bytes).request();
                 const uint8_t* ptr = static_cast<const uint8_t*>(info.ptr);
                 std::vector<uint8_t> audio_payload_vec(ptr, ptr + info.size);
                 
                 return self.write_plugin_packet(
                     source_instance_id,
                     audio_payload_vec,
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
       .def("add_webrtc_listener", &AudioManager::add_webrtc_listener,
            py::arg("sink_id"),
            py::arg("listener_id"),
            py::arg("offer_sdp"),
            py::arg("on_local_description_callback"),
            py::arg("on_ice_candidate_callback"),
            py::arg("client_ip"),
            "Creates and attaches a WebRTC listener to a sink.")
       .def("remove_webrtc_listener", &AudioManager::remove_webrtc_listener,
            py::arg("sink_id"),
            py::arg("listener_id"),
            "Removes a WebRTC listener from a sink.")
       .def("add_webrtc_remote_ice_candidate", &AudioManager::add_webrtc_remote_ice_candidate,
            py::arg("sink_id"),
            py::arg("listener_id"),
            py::arg("candidate"),
            py::arg("sdpMid"),
            "Forwards a remote ICE candidate to a specific WebRTC listener.")
       .def("get_audio_engine_stats", &AudioManager::get_audio_engine_stats,
            "Retrieves a snapshot of all current audio engine statistics.")
       .def("get_audio_settings", &AudioManager::get_audio_settings, "Retrieves the current audio engine tuning settings.")
       .def("set_audio_settings", &AudioManager::set_audio_settings, py::arg("settings"), "Updates the audio engine tuning settings.")
        .def("list_system_devices", &AudioManager::list_system_devices, "Returns the cached registry of system audio devices.")
        .def("drain_device_notifications", &AudioManager::drain_device_notifications, "Retrieves and clears pending device discovery notifications.");
}

} // namespace audio
} // namespace screamrouter

#endif // AUDIO_MANAGER_H
