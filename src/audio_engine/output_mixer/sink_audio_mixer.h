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
#include "../utils/thread_safe_queue.h"
#include "../audio_types.h"
#include "../senders/i_network_sender.h"
#include "../configuration/audio_engine_settings.h"
#include "../receivers/clock_manager.h"

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

#if defined(_WIN32)


#endif

class AudioProcessor;

namespace screamrouter {
namespace audio {

class SinkSynchronizationCoordinator;
class MixScheduler;
class IHardwareClockConsumer;

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
};

using InputChunkQueue = utils::ThreadSafeQueue<ProcessedAudioChunk>;
using Mp3OutputQueue = utils::ThreadSafeQueue<EncodedMP3Data>;

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
    /** @brief A map of input queues, keyed by the unique source instance ID. */
    using InputQueueMap = std::map<std::string, std::shared_ptr<InputChunkQueue>>;

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
    void add_input_queue(const std::string& instance_id, std::shared_ptr<InputChunkQueue> queue);

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
     * @brief Checks if coordination mode is currently enabled.
     * @return True if coordination is enabled, false otherwise.
     */
    bool is_coordination_enabled() const;
 
 protected:
     /** @brief The main processing loop for the mixer thread. */
    void run() override;

private:
    SinkMixerConfig config_;
    std::shared_ptr<screamrouter::audio::AudioEngineSettings> m_settings;
    const std::size_t chunk_size_bytes_;
    const std::size_t mixing_buffer_samples_;
    const std::size_t mp3_buffer_size_;
    std::shared_ptr<Mp3OutputQueue> mp3_output_queue_;
    std::unique_ptr<INetworkSender> network_sender_;
    std::unique_ptr<MixScheduler> mix_scheduler_;
    
    std::map<std::string, std::unique_ptr<INetworkSender>> listener_senders_;
    std::mutex listener_senders_mutex_;

    InputQueueMap input_queues_;
    std::mutex queues_mutex_;

    std::map<std::string, bool> input_active_state_;
    std::map<std::string, ProcessedAudioChunk> source_buffers_;

    std::unique_ptr<ClockManager> clock_manager_;
    std::atomic<bool> clock_manager_enabled_{false};
    ClockManager::ConditionHandle clock_condition_handle_{};
    uint64_t clock_last_sequence_{0};
    uint64_t clock_pending_ticks_{0};
    int timer_sample_rate_{0};
    int timer_channels_{0};
    int timer_bit_depth_{0};
    IHardwareClockConsumer* hardware_clock_consumer_{nullptr};
    bool hardware_clock_active_{false};
    bool can_use_hardware_clock_{false};
    ClockManager::ConditionHandle hardware_clock_handle_{};
    std::uint32_t hardware_frames_per_tick_{0};
    std::uint32_t hardware_clock_check_counter_{0};

    int playback_sample_rate_{0};
    int playback_channels_{0};
    int playback_bit_depth_{0};

    std::chrono::microseconds mix_period_{std::chrono::microseconds(12000)};

    std::vector<int32_t> mixing_buffer_;
    std::vector<int32_t> stereo_buffer_;
    std::vector<uint8_t> payload_buffer_;
    size_t payload_buffer_write_pos_ = 0;
    
    std::vector<uint32_t> current_csrcs_;
    std::mutex csrc_mutex_;

    lame_t lame_global_flags_ = nullptr;
    std::unique_ptr<AudioProcessor> stereo_preprocessor_;
    std::vector<uint8_t> mp3_encode_buffer_;

    std::atomic<uint64_t> m_total_chunks_mixed{0};
    std::atomic<uint64_t> m_buffer_underruns{0};
    std::atomic<uint64_t> m_buffer_overflows{0};
    std::atomic<uint64_t> m_mp3_buffer_overflows{0};

    bool underrun_silence_active_ = false;
    std::chrono::steady_clock::time_point underrun_silence_deadline_{};

    // --- Synchronization Coordination ---
    /** @brief Whether coordination mode is enabled for synchronized dispatch. */
    bool coordination_mode_ = false;
    
    /** @brief Pointer to the synchronization coordinator (not owned). */
    SinkSynchronizationCoordinator* coordinator_ = nullptr;

    std::thread startup_thread_;
    std::atomic<bool> startup_in_progress_{false};

    void initialize_lame();
    void close_lame();

    bool wait_for_source_data();
    void mix_buffers();
    void downscale_buffer();
    size_t preprocess_for_listeners_and_mp3();
    void dispatch_to_listeners(size_t samples_to_dispatch);
    void encode_and_push_mp3(size_t samples_to_encode);
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

    void set_playback_format(int sample_rate, int channels, int bit_depth);
    void update_playback_format_from_sender();
    std::chrono::microseconds calculate_mix_period(int sample_rate, int channels, int bit_depth) const;
    void register_mix_timer();
    void unregister_mix_timer();
    bool wait_for_mix_tick();
    void try_switch_to_hardware_clock();
};

} // namespace audio
} // namespace screamrouter

#endif // SINK_AUDIO_MIXER_H
