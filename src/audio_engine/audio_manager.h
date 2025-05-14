#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include "audio_component.h" // Base class
#include "rtp_receiver.h"
#include "source_input_processor.h"
#include "sink_audio_mixer.h"
#include "raw_scream_receiver.h"
#include "per_process_scream_receiver.h" // Added this line
#include "timeshift_manager.h" // Added for TimeshiftManager
#include "thread_safe_queue.h"
#include "audio_types.h"

#include <map>
#include <string>
#include <vector>
#include <memory> // For unique_ptr, shared_ptr
#include <mutex>
#include <thread>
#include <atomic>

// #include <pybind11/pybind11.h> // For pybind11 core types like py::bytes // Temporarily commented out for linter diagnosis

namespace screamrouter {
namespace audio {

// Forward declarations for queue types used internally
using NotificationQueue = utils::ThreadSafeQueue<NewSourceNotification>;
using PacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>;
using ChunkQueue = utils::ThreadSafeQueue<ProcessedAudioChunk>;
using CommandQueue = utils::ThreadSafeQueue<ControlCommand>;
using Mp3Queue = utils::ThreadSafeQueue<EncodedMP3Data>;

/**
 * @brief Central orchestrator for the C++ audio engine.
 * Manages the lifecycle of audio components (RTP Receiver, Source Processors, Sink Mixers),
 * sets up communication queues, and provides the primary interface for the Python layer.
 */
class AudioManager {
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
    bool add_sink(const SinkConfig& config);

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
    std::string configure_source(const SourceConfig& config);

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

    /**
     * @brief Adds and starts a new raw Scream receiver.
     * @param config Configuration for the raw Scream receiver (e.g., listen port).
     * @return true if the receiver was added successfully, false otherwise.
     */
    bool add_raw_scream_receiver(const RawScreamReceiverConfig& config);

    /**
     * @brief Stops and removes an existing raw Scream receiver.
     * @param listen_port The listen port of the receiver to remove.
     * @return true if the receiver was removed successfully, false if not found.
     */
    bool remove_raw_scream_receiver(int listen_port);

    /**
     * @brief Adds and starts a new per-process Scream receiver.
     * @param config Configuration for the per-process Scream receiver (e.g., listen port).
     * @return true if the receiver was added successfully, false otherwise.
     */
    bool add_per_process_scream_receiver(const PerProcessScreamReceiverConfig& config);

    /**
     * @brief Stops and removes an existing per-process Scream receiver.
     * @param listen_port The listen port of the receiver to remove.
     * @return true if the receiver was removed successfully, false if not found.
     */
    bool remove_per_process_scream_receiver(int listen_port);


    // --- Control API (for Python via pybind11) ---
    /**
     * @brief Updates the volume for a specific source processor instance.
     * @param instance_id Identifier of the source processor instance.
     * @param volume New volume level (e.g., 0.0 to 1.0+).
     * @return true if the command was sent successfully, false if the instance/command queue wasn't found.
     */
    bool update_source_volume(const std::string& instance_id, float volume);

    /**
     * @brief Updates the equalizer settings for a specific source processor instance.
     * @param instance_id Identifier of the source processor instance.
     * @param eq_values Vector of EQ gain values (size must match EQ_BANDS).
     * @return true if the command was sent successfully, false otherwise.
     */
    bool update_source_equalizer(const std::string& instance_id, const std::vector<float>& eq_values);

    /**
     * @brief Updates the delay for a specific source processor instance.
     * @param instance_id Identifier of the source processor instance.
     * @param delay_ms New delay in milliseconds.
     * @return true if the command was sent successfully, false otherwise.
     */
    bool update_source_delay(const std::string& instance_id, int delay_ms);

    /**
     * @brief Updates the timeshift (playback offset from now) for a specific source processor instance.
     * @param instance_id Identifier of the source processor instance.
     * @param timeshift_sec New timeshift offset in seconds (negative values play earlier).
     * @return true if the command was sent successfully, false otherwise.
     */
    bool update_source_timeshift(const std::string& instance_id, float timeshift_sec);

    /**
     * @brief Updates the speaker mix configuration for a specific source processor instance.
     * @param instance_id Identifier of the source processor instance.
     * @param matrix The 8x8 speaker mix matrix.
     * @param use_auto True to use automatic mixing, false to use the provided matrix.
     * @return true if the command was sent successfully, false otherwise.
     */
    // bool update_source_speaker_mix(const std::string& instance_id, const std::vector<std::vector<float>>& matrix, bool use_auto); // Old version
    
    /**
     * @brief Updates the speaker layout for a specific input channel key for a source processor instance.
     * @param instance_id Identifier of the source processor instance.
     * @param input_channel_key The input channel count key for which this layout applies.
     * @param layout The CppSpeakerLayout object.
     * @return true if the command was sent successfully, false otherwise.
     */
    bool update_source_speaker_layout_for_key(const std::string& instance_id, int input_channel_key, const screamrouter::audio::CppSpeakerLayout& layout);

    /**
     * @brief Updates the entire speaker layouts map for a specific source processor instance.
     * @param instance_id Identifier of the source processor instance.
     * @param layouts_map The map of input channel keys to CppSpeakerLayout objects.
     * @return true if all commands were sent successfully, false otherwise.
     */
    bool update_source_speaker_layouts_map(const std::string& instance_id, const std::map<int, screamrouter::audio::CppSpeakerLayout>& layouts_map);


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

private:
    // --- Internal State ---
    std::atomic<bool> running_{false};
    std::mutex manager_mutex_; // Protects access to internal maps/vectors

    // Timeshift Manager (only one)
    std::unique_ptr<TimeshiftManager> timeshift_manager_;

    // RTP Receiver (only one)
    std::unique_ptr<RtpReceiver> rtp_receiver_;
    std::shared_ptr<NotificationQueue> new_source_notification_queue_;
    std::thread notification_thread_; // Thread to process new source notifications

    // Sink Mixers (ID -> Mixer Ptr)
    std::map<std::string, std::unique_ptr<SinkAudioMixer>> sinks_;
    // Sink Configurations (ID -> Config) - Keep track for potential reconfiguration
    std::map<std::string, SinkConfig> sink_configs_;
    // Queues for MP3 output (Sink ID -> Queue Ptr)
    std::map<std::string, std::shared_ptr<Mp3Queue>> mp3_output_queues_;

    // Source Processors (Instance ID -> Processor Ptr)
    std::map<std::string, std::unique_ptr<SourceInputProcessor>> sources_;
    // Queues for RTP -> Source Processor (Instance ID -> Queue Ptr) - NOTE: RtpReceiver needs adjustment
    std::map<std::string, std::shared_ptr<PacketQueue>> rtp_to_source_queues_; // Key is Instance ID
    // Queues for Source Processor -> Sink Mixer (Instance ID -> Queue Ptr)
    std::map<std::string, std::shared_ptr<ChunkQueue>> source_to_sink_queues_; // Key is Instance ID
    // Queues for Control Commands (Instance ID -> Queue Ptr)
    std::map<std::string, std::shared_ptr<CommandQueue>> command_queues_; // Key is Instance ID

    // Raw Scream Receivers (Port -> Receiver Ptr) - Assuming one receiver per port
    std::map<int, std::unique_ptr<RawScreamReceiver>> raw_scream_receivers_;
    // Per-Process Scream Receivers (Port -> Receiver Ptr)
    std::map<int, std::unique_ptr<PerProcessScreamReceiver>> per_process_scream_receivers_;

    // Removed source_configs_ map

    // --- Internal Methods ---
    /**
     * @brief The main loop for the notification processing thread.
     * Waits for notifications from the RtpReceiver and calls handle_new_source.
     */
    void process_notifications();

    /**
     * @brief Handles a new source notification.
     * Creates the necessary queues and SourceInputProcessor instance for the new source,
     * registers the input queue with the RtpReceiver, and connects the source's
     * output queue to all existing SinkAudioMixers.
     * @param source_tag The identifier (IP address) of the newly detected source.
     */
    void handle_new_source(const std::string& source_tag); // This method's role changes significantly

    /**
     * @brief Helper function to send a control command to a specific source processor instance.
     * @param instance_id Identifier of the target source processor instance.
     * @param command The command to send.
     * @return true if the command queue was found and the command pushed, false otherwise.
     */
    bool send_command_to_source(const std::string& instance_id, const ControlCommand& command);

    /**
     * @brief Generates a unique instance ID.
     * @param base_tag Optional base tag (like IP address) to include.
     * @return A unique string identifier.
     */
    std::string generate_unique_instance_id(const std::string& base_tag = "");
};

} // namespace audio
} // namespace screamrouter

#endif // AUDIO_MANAGER_H
