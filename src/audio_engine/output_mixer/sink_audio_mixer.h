/**
 * @file sink_audio_mixer.h
 * @brief Defines the SinkAudioMixer class for mixing and outputting audio.
 * @details This class is responsible for collecting processed audio chunks from multiple
 *          source processors, mixing them together, and sending the final output to a
 *          network destination. It can also encode the output to MP3 and handle
 *          multiple network listeners (e.g., for WebRTC).
 */
#ifndef SINK_AUDIO_MIXER_H
#define SINK_AUDIO_MIXER_H
#if defined(_WIN32) && !defined(NOMINMAX)

#endif

#include "../utils/audio_component.h"
#include "../audio_types.h"
#include "../senders/i_network_sender.h"
#include "../configuration/audio_engine_settings.h"
#include "../receivers/clock_manager.h"
#include "../utils/packet_ring.h"
#include "mp3_encoder.h"
#include "listener_dispatcher.h"
#include "sink_rate_controller.h"

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <lame/lame.h>
#include <atomic>
#include <chrono>
#include <limits>
#include <thread>
#include <cstdint>
#include <deque>
#include <unordered_map>

#if defined(_WIN32)


#endif

class AudioProcessor;

namespace screamrouter {
namespace audio {

class SinkSynchronizationCoordinator;
class SourceInputProcessor;
/**
 * @struct SinkAudioMixerStats
 * @brief Holds raw statistics collected from the SinkAudioMixer.
 */
struct SinkAudioMixerStats {
    uint64_t total_chunks_mixed = 0;
    size_t active_input_streams = 0;
    size_t total_input_streams = 0;
    std::vector<std::string> listener_ids;
    uint64_t buffer_underruns = 0;
    uint64_t buffer_overflows = 0;
    uint64_t mp3_buffer_overflows = 0;
    BufferMetrics payload_buffer;
    BufferMetrics mp3_output_buffer;
    BufferMetrics mp3_pcm_buffer;
    double last_chunk_dwell_ms = 0.0;
    double avg_chunk_dwell_ms = 0.0;
    double last_send_gap_ms = 0.0;
    double avg_send_gap_ms = 0.0;
    std::vector<SinkInputLaneStats> input_lanes;
};

/**
 * @struct PipelineState
 * @brief Aggregated buffer state from all stages for unified rate control.
 */
struct PipelineState {
    double hw_fill_ms = 0.0;      ///< Hardware buffer fill level (ALSA/WASAPI)
    double hw_target_ms = 0.0;    ///< Hardware buffer target level
    double mixer_queue_ms = 0.0;  ///< Mixer input queue backlog
    double mixer_target_ms = 0.0; ///< Mixer target queue level
};

using Mp3OutputQueue = utils::ThreadSafeQueue<EncodedMP3Data>;
using ReadyPacketRing = utils::PacketRing<TaggedAudioPacket>;

/**
 * @class SinkAudioMixer
 * @brief Mixes audio from multiple sources and sends it to a network sink.
 * @details This component runs its own thread to pull processed audio chunks from one or
 *          more input queues, mix them, and then dispatch the result. It can send the
 *          output via a primary network sender (e.g., UDP) and also to multiple
 *          additional listeners (e.g., WebRTC peers). It can optionally encode the
 *          mixed audio to MP3 and place it in an output queue.
 */
class SinkAudioMixer : public AudioComponent {
public:
    /** @brief A map of ready packet rings, keyed by source instance ID. */
    using ReadyRingMap = std::map<std::string, std::shared_ptr<ReadyPacketRing>>;

    /**
     * @brief Constructs a SinkAudioMixer.
     * @param config The configuration for this sink mixer.
     * @param mp3_output_queue A shared pointer to a queue for MP3 data. Can be nullptr if MP3 output is disabled.
     */
    SinkAudioMixer(
        SinkMixerConfig config,
        std::shared_ptr<Mp3OutputQueue> mp3_output_queue,
        std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings
    );

    /**
     * @brief Destructor. Stops the mixer thread and cleans up resources.
     */
    ~SinkAudioMixer() override;

    /** @brief Starts the mixer's processing thread. */
    void start() override;
    /** @brief Stops the mixer's processing thread. */
    void stop() override;

    /**
     * @brief Adds an input queue from a source processor.
     * @param instance_id The unique ID of the source processor instance.
     * @param queue A shared pointer to the source's output queue.
     */
    void add_input_queue(const std::string& instance_id,
                         std::shared_ptr<ReadyPacketRing> ready_ring,
                         SourceInputProcessor* sip);

    /**
     * @brief Removes an input queue.
     * @param instance_id The unique ID of the source processor instance whose queue should be removed.
     */
    void remove_input_queue(const std::string& instance_id);

    /** @brief Gets the MP3 output queue. */
    std::shared_ptr<Mp3OutputQueue> get_mp3_queue() const { return mp3_output_queue_; }

    /**
     * @brief Adds a network listener to this sink.
     * @param listener_id A unique ID for the listener.
     * @param sender A unique pointer to the listener's network sender.
     */
    void add_listener(const std::string& listener_id, std::unique_ptr<INetworkSender> sender);
    /**
     * @brief Removes a network listener from this sink.
     * @param listener_id The ID of the listener to remove.
     */
    void remove_listener(const std::string& listener_id);
    /**
     * @brief Gets a raw pointer to a listener's network sender.
     * @param listener_id The ID of the listener.
     * @return A pointer to the `INetworkSender`, or `nullptr` if not found.
     */
    INetworkSender* get_listener(const std::string& listener_id);

    /**
     * @brief Retrieves the current statistics from the mixer.
     * @return A struct containing the current stats.
     */
    SinkAudioMixerStats get_stats();

    /**
     * @brief Gets the configuration of the sink mixer.
     * @return The sink mixer's configuration.
     */
    const SinkMixerConfig& get_config() const;

    /**
     * @brief Enables or disables coordination mode for synchronized multi-sink playback.
     * @param enable True to enable coordination, false to disable.
     */
    void set_coordination_mode(bool enable);

    /**
     * @brief Sets the synchronization coordinator for this mixer.
     * @param coord Pointer to the coordinator (not owned by mixer, must outlive mixer).
     */
    void set_coordinator(SinkSynchronizationCoordinator* coord);

    /**
     * @brief Gets the current aggregated pipeline buffer state for upstream rate control.
     * @return PipelineState containing hardware and mixer queue levels.
     */
    PipelineState get_pipeline_state() const;
 
 protected:
     /** @brief The main processing loop for the mixer thread. */
    void run() override;

private:
    SinkMixerConfig config_;
    std::shared_ptr<screamrouter::audio::AudioEngineSettings> m_settings;
    const std::size_t frames_per_chunk_;
    std::size_t chunk_size_bytes_;
    std::size_t mixing_buffer_samples_;
    std::size_t mp3_buffer_size_;
    std::shared_ptr<Mp3OutputQueue> mp3_output_queue_;
    std::unique_ptr<INetworkSender> network_sender_;
    
    // Helper classes for modular functionality
    std::unique_ptr<Mp3Encoder> mp3_encoder_;
    std::unique_ptr<ListenerDispatcher> listener_dispatcher_;
    std::unique_ptr<SinkRateController> rate_controller_;

    ReadyRingMap ready_rings_;
    std::mutex queues_mutex_;
    std::map<std::string, SourceInputProcessor*> source_processors_;

    std::map<std::string, bool> input_active_state_;
    std::map<std::string, ProcessedAudioChunk> source_buffers_;
    std::map<std::string, std::deque<ProcessedAudioChunk>> processed_ready_;
    struct ReadyQueueDropState {
        std::chrono::steady_clock::time_point last_update{};
        double drop_credit = 0.0;
    };
    std::unordered_map<std::string, ReadyQueueDropState> ready_queue_drop_state_;
    std::unordered_map<std::string, size_t> ready_queue_high_water_;
    std::unordered_map<std::string, uint64_t> ready_total_received_;
    std::unordered_map<std::string, uint64_t> ready_total_popped_;
    std::unordered_map<std::string, uint64_t> ready_total_dropped_;

    std::unique_ptr<ClockManager> clock_manager_;
    std::atomic<bool> clock_manager_enabled_{false};
    ClockManager::ConditionHandle clock_condition_handle_{};
    uint64_t clock_last_sequence_{0};
    uint64_t clock_pending_ticks_{0};
    int timer_sample_rate_{0};
    int timer_channels_{0};
    int timer_bit_depth_{0};

    int playback_sample_rate_{0};
    int playback_channels_{0};
    int playback_bit_depth_{0};

    std::chrono::microseconds mix_period_{std::chrono::microseconds(12000)};

    std::vector<int32_t> mixing_buffer_;
    std::unique_ptr<AudioProcessor> output_post_processor_;
    std::vector<int32_t> output_post_buffer_;
    std::mutex output_processor_mutex_;
    std::atomic<double> output_playback_rate_{1.0};
    
    // Hardware buffer state from ALSA/WASAPI for unified rate control
    std::atomic<double> hw_fill_ms_{0.0};
    std::atomic<double> hw_target_ms_{0.0};
    
    uint64_t output_post_log_counter_{0};
    std::vector<int32_t> stereo_buffer_;
    std::vector<uint8_t> payload_buffer_;
    size_t payload_buffer_read_pos_ = 0;
    size_t payload_buffer_fill_bytes_ = 0;
    std::vector<uint8_t> payload_chunk_temp_;
    std::vector<int32_t> last_sample_frame_;
    bool last_sample_valid_ = false;
    
    std::vector<uint32_t> current_csrcs_;
    std::mutex csrc_mutex_;

    lame_t lame_global_flags_ = nullptr;  // TODO: Remove after full Mp3Encoder integration
    std::unique_ptr<AudioProcessor> stereo_preprocessor_;
    
    // Legacy members - keep until legacy code removed from sink_audio_mixer.cpp
    std::vector<uint8_t> mp3_encode_buffer_;
    std::deque<std::vector<int32_t>> mp3_pcm_queue_;
    std::mutex mp3_mutex_;
    std::condition_variable mp3_cv_;
    std::thread mp3_thread_;
    std::atomic<bool> mp3_thread_running_{false};
    std::atomic<bool> mp3_stop_flag_{false};
    size_t mp3_pcm_queue_max_depth_{0};
    std::atomic<size_t> mp3_output_high_water_{0};
    std::atomic<size_t> mp3_pcm_high_water_{0};

    std::atomic<uint64_t> m_total_chunks_mixed{0};
    std::atomic<uint64_t> m_buffer_underruns{0};
    std::atomic<uint64_t> m_buffer_overflows{0};
    std::atomic<uint64_t> m_mp3_buffer_overflows{0};

    bool underrun_silence_active_ = false;
    std::chrono::steady_clock::time_point underrun_silence_deadline_{};
    std::chrono::steady_clock::time_point last_silence_log_time_{};

    // --- Synchronization Coordination ---
    /** @brief Whether coordination mode is enabled for synchronized dispatch. */
    bool coordination_mode_ = false;
    
    /** @brief Pointer to the synchronization coordinator (not owned). */
    SinkSynchronizationCoordinator* coordinator_ = nullptr;

    std::thread startup_thread_;
    std::atomic<bool> startup_in_progress_{false};

    void initialize_lame();
    void close_lame();
    void start_mp3_thread();
    void stop_mp3_thread();
    void mp3_thread_loop();

    void refresh_format_dependent_buffers(int sample_rate, int channels, int bit_depth);
    void handle_system_audio_format_change(unsigned int device_rate,
                                           unsigned int device_channels,
                                           unsigned int device_bit_depth);
    bool wait_for_source_data();
    void mix_buffers();
    void downscale_buffer();
    size_t preprocess_for_listeners_and_mp3();
    void dispatch_to_listeners(size_t samples_to_dispatch);
    void enqueue_mp3_pcm(const int32_t* samples, size_t sample_count);
    void encode_and_push_mp3(const int32_t* samples, size_t sample_count);
    void cleanup_closed_listeners();
    void clear_pending_audio();
    void start_async();
    bool start_internal();
    void join_startup_thread();
    // --- Profiling ---
    void reset_profiler_counters();
    void maybe_log_profiler();
    void maybe_log_telemetry(std::chrono::steady_clock::time_point now);
    std::chrono::steady_clock::time_point profiling_last_log_time_;
    std::chrono::steady_clock::time_point telemetry_last_log_time_{};
    uint64_t profiling_cycles_{0};
    uint64_t profiling_data_ready_cycles_{0};
    uint64_t profiling_chunks_sent_{0};
    uint64_t profiling_payload_bytes_sent_{0};
    size_t profiling_ready_sources_sum_{0};
    size_t profiling_lagging_sources_sum_{0};
    size_t profiling_samples_count_{0};
    size_t profiling_max_payload_buffer_bytes_{0};
    double profiling_chunk_dwell_sum_ms_{0.0};
    double profiling_chunk_dwell_max_ms_{0.0};
    double profiling_chunk_dwell_min_ms_{std::numeric_limits<double>::infinity()};
    double profiling_last_chunk_dwell_ms_{0.0};
    uint64_t profiling_chunk_dwell_samples_{0};
    std::chrono::steady_clock::time_point profiling_underrun_active_since_{};
    double profiling_underrun_hold_time_ms_{0.0};
    double profiling_last_underrun_hold_ms_{0.0};
    uint64_t profiling_underrun_events_{0};
    std::chrono::steady_clock::time_point profiling_last_chunk_send_time_{};
    double profiling_send_gap_sum_ms_{0.0};
    double profiling_send_gap_max_ms_{0.0};
    double profiling_send_gap_min_ms_{std::numeric_limits<double>::infinity()};
    double profiling_last_send_gap_ms_{0.0};
    uint64_t profiling_send_gap_samples_{0};

    // Detailed operation timings
    long double profiling_mix_ns_sum_{0.0L};
    uint64_t profiling_mix_calls_{0};
    uint64_t profiling_mix_ns_max_{0};
    uint64_t profiling_mix_ns_min_{std::numeric_limits<uint64_t>::max()};

    long double profiling_downscale_ns_sum_{0.0L};
    uint64_t profiling_downscale_calls_{0};
    uint64_t profiling_downscale_ns_max_{0};
    uint64_t profiling_downscale_ns_min_{std::numeric_limits<uint64_t>::max()};

    long double profiling_preprocess_ns_sum_{0.0L};
    uint64_t profiling_preprocess_calls_{0};
    uint64_t profiling_preprocess_ns_max_{0};
    uint64_t profiling_preprocess_ns_min_{std::numeric_limits<uint64_t>::max()};

    long double profiling_dispatch_ns_sum_{0.0L};
    uint64_t profiling_dispatch_calls_{0};
    uint64_t profiling_dispatch_ns_max_{0};
    uint64_t profiling_dispatch_ns_min_{std::numeric_limits<uint64_t>::max()};

    long double profiling_mp3_ns_sum_{0.0L};
    uint64_t profiling_mp3_calls_{0};
    uint64_t profiling_mp3_ns_max_{0};
    uint64_t profiling_mp3_ns_min_{std::numeric_limits<uint64_t>::max()};

    // Per-source underrun counters
    std::map<std::string, uint64_t> profiling_source_underruns_;
    std::map<std::string, size_t> input_queue_high_water_;

    // Buffer drain control members
    std::atomic<double> smoothed_buffer_level_ms_{0.0};
    std::chrono::steady_clock::time_point last_drain_check_;
    std::mutex drain_control_mutex_;
    std::unordered_map<std::string, double> per_source_smoothed_buffer_ms_;
    std::unordered_map<std::string, double> source_last_rate_command_;

    struct InputBufferMetrics {
        double total_ms = 0.0;
        double avg_per_source_ms = 0.0;
        double max_per_source_ms = 0.0;
        std::size_t queued_blocks = 0;
        std::size_t active_sources = 0;
        double block_duration_ms = 0.0;
        bool valid = false;
        std::map<std::string, std::size_t> per_source_blocks;
        std::map<std::string, double> per_source_ms;
    };

    void set_playback_format(int sample_rate, int channels, int bit_depth);
    void update_playback_format_from_sender();
    void setup_output_post_processor();
    void set_output_playback_rate(double rate);
    std::chrono::microseconds calculate_mix_period(int sample_rate, int channels, int bit_depth) const;
    void register_mix_timer();
    void unregister_mix_timer();
    bool wait_for_mix_tick();

    // Buffer drain control methods
    void update_drain_ratio();
    InputBufferMetrics compute_input_buffer_metrics();
    void dispatch_drain_adjustments(const InputBufferMetrics& metrics, double alpha);
    double calculate_drain_ratio_for_level(double buffer_ms, double block_duration_ms) const;
    void send_playback_rate_command(const std::string& instance_id, double ratio);
};

} // namespace audio
} // namespace screamrouter

#endif // SINK_AUDIO_MIXER_H
