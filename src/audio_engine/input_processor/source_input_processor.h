/**
 * @file source_input_processor.h
 * @brief Defines the SourceInputProcessor class for handling individual audio source streams.
 * @details This class is responsible for processing an incoming audio stream from a single
 *          source. It pulls raw audio packets from an input queue, uses an AudioProcessor
 *          instance to perform DSP tasks (volume, EQ, resampling), and pushes the processed
 *          audio chunks to an output queue. It also handles dynamic reconfiguration based on
 *          packet format and control commands.
 */
#ifndef SOURCE_INPUT_PROCESSOR_H
#define SOURCE_INPUT_PROCESSOR_H

#include "../utils/audio_component.h"
#include "../audio_types.h"
#include "../audio_processor/audio_processor.h"
#include "../configuration/audio_engine_settings.h"
#include "../utils/byte_ring_buffer.h"

#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <map>
#include <atomic>
#include <optional>

namespace screamrouter {
namespace audio {

/**
 * @struct SourceInputProcessorStats
 * @brief Holds raw statistics collected from the SourceInputProcessor.
 */
struct SourceInputProcessorStats {
    uint64_t total_packets_processed = 0;
    size_t input_queue_size = 0;
    size_t output_queue_size = 0;
    uint64_t reconfigurations = 0;
    double input_queue_ms = 0.0;
    double output_queue_ms = 0.0;
    double process_buffer_ms = 0.0;
    size_t process_buffer_samples = 0;
    size_t peak_process_buffer_samples = 0;
    uint64_t total_chunks_pushed = 0;
    uint64_t total_discarded_packets = 0;
    size_t output_queue_high_water = 0;
    size_t input_queue_high_water = 0;
    double avg_loop_ms = 0.0;
    double last_packet_age_ms = 0.0;
    double last_origin_age_ms = 0.0;
    double playback_rate = 1.0;
    double input_samplerate = 0.0;
    double output_samplerate = 0.0;
    double resample_ratio = 0.0;
};

/** @brief The size of the raw Scream protocol header in bytes. */
const size_t SCREAM_HEADER_SIZE = 5;
/** @brief The default bit depth assumed for input audio if not specified. */
const int DEFAULT_INPUT_BITDEPTH = 16;
/** @brief The default number of channels assumed for input audio if not specified. */
const int DEFAULT_INPUT_CHANNELS = 2;
/** @brief The default sample rate assumed for input audio if not specified. */
const int DEFAULT_INPUT_SAMPLERATE = 48000;

/**
 * @class SourceInputProcessor
 * @brief An audio component that processes a single audio source stream.
 * @details This class provides synchronous ingest via ingest_packet(); callers
 *          provide one packet and receive zero or more processed chunks.
 */
class SourceInputProcessor : public AudioComponent {
public:
    /**
     * @brief Constructs a SourceInputProcessor.
     * @param config The initial configuration for this processor instance.
     */
    SourceInputProcessor(
        SourceProcessorConfig config,
        std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings
    );

    /**
     * @brief Destructor. Stops the processing thread.
     */
    ~SourceInputProcessor() override;

    // --- AudioComponent Interface ---
    void start() override;
    void stop() override;

    /**
     * @brief Updates the speaker layout configuration for the internal AudioProcessor.
     * @param layouts_map A map of input channel counts to speaker layout configurations.
     */
    void set_speaker_layouts_config(const std::map<int, screamrouter::audio::CppSpeakerLayout>& layouts_map);

    // --- Getters for Configuration Info ---
    /** @brief Gets the unique instance ID of this processor. */
    const std::string& get_instance_id() const { return config_.instance_id; }
    /** @brief Gets the tag of the source this processor is handling. */
    const std::string& get_source_tag() const;
    /** @brief Checks if an incoming tag matches this processor (ignoring optional '#ip.port' suffix). */
    bool matches_source_tag(const std::string& actual_tag) const;
    /** @brief Gets the full configuration struct of this processor. */
    const SourceProcessorConfig& get_config() const { return config_; }
    /**
     * @brief Retrieves the current statistics from the processor.
     * @return A struct containing the current stats.
     */
    SourceInputProcessorStats get_stats();

    /**
     * @brief Applies a control command (used by managers for rate/volume updates).
     */
    void apply_control_command(const ControlCommand& cmd);

    /**
     * @brief Synchronously ingest a packet and emit zero or more processed chunks.
     * @param packet Packet to process.
     * @param out_chunks Vector to append produced chunks into.
     */
    void ingest_packet(const TaggedAudioPacket& packet, std::vector<ProcessedAudioChunk>& out_chunks);

    // --- setters (formerly command queue driven) ---
    void set_volume(float vol);
    void set_eq(const std::vector<float>& eq_values);
    void set_delay(int delay_ms);
    void set_timeshift(float timeshift_sec);
    void set_eq_normalization(bool enabled);
    void set_volume_normalization(bool enabled);
    void set_speaker_mix(int input_channel_key, const CppSpeakerLayout& layout);

    // --- runtime state helpers for cloning/cascading ---
    float get_current_volume() const;
    std::vector<float> get_current_eq() const;
    int get_current_delay_ms() const;
    float get_current_timeshift_sec() const;
    bool is_eq_normalization_enabled() const;
    bool is_volume_normalization_enabled() const;
    std::map<int, screamrouter::audio::CppSpeakerLayout> get_current_speaker_layouts() const;

protected:
    void run() override;

private:
    const std::size_t base_frames_per_chunk_;
    std::size_t current_input_chunk_bytes_;
    int m_current_ap_input_channels = 0;
    int m_current_ap_input_samplerate = 0;
    int m_current_ap_input_bitdepth = 0;

    SourceProcessorConfig config_;
    std::shared_ptr<screamrouter::audio::AudioEngineSettings> m_settings;

    std::unique_ptr<AudioProcessor> audio_processor_;
    mutable std::mutex processor_config_mutex_;

    std::vector<int32_t> process_buffer_;
    std::vector<uint32_t> current_packet_ssrcs_;
    double m_current_input_chunk_ms = 0.0;
    double m_current_output_chunk_ms = 0.0;

    float current_volume_;
    std::vector<float> current_eq_;
    int current_delay_ms_;
    float current_timeshift_backshift_sec_config_;

    std::map<int, screamrouter::audio::CppSpeakerLayout> current_speaker_layouts_map_;
    double current_playback_rate_ = 1.0;
    bool eq_normalization_enabled_ = false;
    bool volume_normalization_enabled_ = false;

    std::atomic<uint64_t> m_total_packets_processed{0};
    std::atomic<uint64_t> m_reconfigurations{0};
    std::atomic<uint64_t> m_total_chunks_pushed{0};
    std::atomic<uint64_t> m_total_discarded_packets{0};
    std::atomic<size_t> m_process_buffer_high_water{0};
    std::chrono::steady_clock::time_point m_last_packet_time;
    std::chrono::steady_clock::time_point m_last_packet_origin_time;
    bool m_is_first_packet_after_discontinuity = true;
    std::size_t pending_sentinel_samples_ = 0;
    
    // Tracks cumulative time dilation from playback_rate != 1.0
    // When rate > 1.0, we consume audio faster than real-time, so output timestamps
    // should be shifted earlier. This accumulates the difference.
    double cumulative_time_dilation_ms_ = 0.0;

    /**
     * @brief Processes a single chunk of raw audio data using the internal AudioProcessor.
     * @param input_chunk_data The raw audio data to process.
     */
    void process_audio_chunk(const std::vector<uint8_t>& input_chunk_data, bool is_sentinel_chunk);
    
    void push_output_chunk_if_ready(std::vector<ProcessedAudioChunk>& out_chunks);

    /**
     * @brief Checks packet format, reconfigures AudioProcessor if needed, and returns audio payload.
     * @param packet The incoming tagged audio packet.
     * @param out_audio_payload_ptr Pointer to store the start of the audio data within the packet.
     * @param out_audio_payload_size Pointer to store the size of the audio data.
     * @return true if successful and audio payload is valid, false otherwise.
     */
    bool check_format_and_reconfigure(
        const TaggedAudioPacket& packet,
        const uint8_t** out_audio_payload_ptr,
        size_t* out_audio_payload_size
    );
    void reset_input_accumulator();
    void append_to_input_accumulator(const TaggedAudioPacket& packet);
    bool try_dequeue_input_chunk(std::vector<uint8_t>& chunk_data,
                                 std::chrono::steady_clock::time_point& chunk_time,
                                 std::optional<uint32_t>& chunk_timestamp,
                                 std::vector<uint32_t>& chunk_ssrcs,
                                 bool& chunk_is_sentinel);

    // --- Profiling ---
    void reset_profiler_counters();
    void maybe_log_profiler();
    void maybe_log_telemetry(std::chrono::steady_clock::time_point now);
    std::chrono::steady_clock::time_point profiling_last_log_time_;
    std::chrono::steady_clock::time_point telemetry_last_log_time_{};
    uint64_t profiling_packets_received_{0};
    uint64_t profiling_chunks_pushed_{0};
    uint64_t profiling_discarded_packets_{0};
    uint64_t profiling_processing_ns_{0};
    uint64_t profiling_processing_samples_{0};
    size_t profiling_peak_process_buffer_samples_{0};
    uint64_t profiling_input_queue_sum_{0};
    uint64_t profiling_output_queue_sum_{0};
    uint64_t profiling_queue_samples_{0};
    std::chrono::steady_clock::time_point last_empty_packet_log_{};

    struct InputFragmentMetadata {
        std::size_t bytes = 0;
        std::size_t consumed_bytes = 0;
        std::chrono::steady_clock::time_point received_time{};
        std::optional<uint32_t> rtp_timestamp;
        std::vector<uint32_t> ssrcs;
        bool is_sentinel = false;
    };

    utils::ByteRingBuffer input_ring_buffer_;
    std::deque<InputFragmentMetadata> input_fragments_;
    uint64_t input_ring_base_offset_ = 0;
    bool input_chunk_active_ = false;
    std::chrono::steady_clock::time_point first_fragment_time_{};
    std::optional<uint32_t> first_fragment_rtp_timestamp_;
    size_t input_bytes_per_frame_ = 0;
};

} // namespace audio
} // namespace screamrouter

#endif // SOURCE_INPUT_PROCESSOR_H
