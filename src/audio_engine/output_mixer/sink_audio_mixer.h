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

#include "../utils/audio_component.h"
#include "../utils/thread_safe_queue.h"
#include "../audio_types.h"
#include "../senders/i_network_sender.h"
#include "../configuration/audio_engine_settings.h"

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <lame/lame.h>
#include <atomic>
#include <chrono>

class AudioProcessor;

namespace screamrouter {
namespace audio {

class SinkSynchronizationCoordinator;

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

/** @brief The size of the network output payload in bytes. */
const size_t SINK_CHUNK_SIZE_BYTES = 1152;
/** @brief The number of 32-bit samples required in the mixing buffer to produce a full output chunk. */
const size_t SINK_MIXING_BUFFER_SAMPLES = 576;
/** @brief A generous buffer size for MP3 encoding output. */
const size_t SINK_MP3_BUFFER_SIZE = SINK_CHUNK_SIZE_BYTES * 8;

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
    std::shared_ptr<Mp3OutputQueue> mp3_output_queue_;
    std::unique_ptr<INetworkSender> network_sender_;
    
    std::map<std::string, std::unique_ptr<INetworkSender>> listener_senders_;
    std::mutex listener_senders_mutex_;

    InputQueueMap input_queues_;
    std::mutex queues_mutex_;

    std::map<std::string, bool> input_active_state_;
    std::map<std::string, ProcessedAudioChunk> source_buffers_;

    std::condition_variable input_cv_;
    std::mutex input_cv_mutex_;

    const std::chrono::milliseconds GRACE_PERIOD_TIMEOUT{12};
    const std::chrono::milliseconds GRACE_PERIOD_POLL_INTERVAL{1};

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

    void initialize_lame();
    void close_lame();

    bool wait_for_source_data();
    void mix_buffers();
    void downscale_buffer();
    size_t preprocess_for_listeners_and_mp3();
    void dispatch_to_listeners(size_t samples_to_dispatch);
    void encode_and_push_mp3(size_t samples_to_encode);
    void cleanup_closed_listeners();

    // --- Profiling ---
    void reset_profiler_counters();
    void maybe_log_profiler();
    std::chrono::steady_clock::time_point profiling_last_log_time_;
    uint64_t profiling_cycles_{0};
    uint64_t profiling_data_ready_cycles_{0};
    uint64_t profiling_chunks_sent_{0};
    uint64_t profiling_payload_bytes_sent_{0};
    size_t profiling_ready_sources_sum_{0};
    size_t profiling_lagging_sources_sum_{0};
    size_t profiling_samples_count_{0};
    size_t profiling_max_payload_buffer_bytes_{0};
};

} // namespace audio
} // namespace screamrouter

#endif // SINK_AUDIO_MIXER_H
