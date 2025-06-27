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
#include "../utils/thread_safe_queue.h"
#include "../audio_types.h"
#include "../audio_processor/audio_processor.h"

#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <map>
#include <atomic>

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
};

// Using aliases for clarity
using InputPacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>;
using OutputChunkQueue = utils::ThreadSafeQueue<ProcessedAudioChunk>;
using CommandQueue = utils::ThreadSafeQueue<ControlCommand>;

/** @brief The size of the raw Scream protocol header in bytes. */
const size_t SCREAM_HEADER_SIZE = 5;
/** @brief The expected size of the audio data payload in a TaggedAudioPacket, in bytes. */
const size_t INPUT_CHUNK_BYTES = 1152;
/** @brief The default bit depth assumed for input audio if not specified. */
const int DEFAULT_INPUT_BITDEPTH = 16;
/** @brief The default number of channels assumed for input audio if not specified. */
const int DEFAULT_INPUT_CHANNELS = 2;
/** @brief The default sample rate assumed for input audio if not specified. */
const int DEFAULT_INPUT_SAMPLERATE = 48000;
/** @brief The number of interleaved 32-bit samples expected in a ProcessedAudioChunk. */
const size_t OUTPUT_CHUNK_SAMPLES = 576;

/**
 * @class SourceInputProcessor
 * @brief An audio component that processes a single audio source stream.
 * @details This class runs its own thread to pull packets from an input queue,
 *          process them, and push them to an output queue. It is a stateful
 *          component that can be configured via a command queue.
 */
class SourceInputProcessor : public AudioComponent {
public:
    /**
     * @brief Constructs a SourceInputProcessor.
     * @param config The initial configuration for this processor instance.
     * @param input_queue The queue from which to receive raw audio packets.
     * @param output_queue The queue to which processed audio chunks will be sent.
     * @param command_queue The queue for receiving control commands.
     */
    SourceInputProcessor(
        SourceProcessorConfig config,
        std::shared_ptr<InputPacketQueue> input_queue,
        std::shared_ptr<OutputChunkQueue> output_queue,
        std::shared_ptr<CommandQueue> command_queue
    );

    /**
     * @brief Destructor. Stops the processing thread.
     */
    ~SourceInputProcessor() noexcept override;

    // --- AudioComponent Interface ---
    /**
     * @brief Starts the processor's internal thread.
     */
    void start() override;
    /**
     * @brief Stops the processor's internal thread.
     */
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
    /** @brief Gets the full configuration struct of this processor. */
    const SourceProcessorConfig& get_config() const { return config_; }
    /** @brief Gets the input queue used by this processor. */
    std::shared_ptr<InputPacketQueue> get_input_queue() const { return input_queue_; }

    /**
     * @brief Retrieves the current statistics from the processor.
     * @return A struct containing the current stats.
     */
    SourceInputProcessorStats get_stats();

protected:
    /**
     * @brief The main processing loop, executed in a separate thread.
     */
    void run() override;

    /**
     * @brief The loop for handling input packets and processing.
     */
    void input_loop();
    /**
     * @brief The loop for handling output chunks. (Currently unused, logic merged into input_loop).
     */
    void output_loop();

private:
    int m_current_ap_input_channels = 0;
    int m_current_ap_input_samplerate = 0;
    int m_current_ap_input_bitdepth = 0;

    SourceProcessorConfig config_;
    std::shared_ptr<InputPacketQueue> input_queue_;
    std::shared_ptr<OutputChunkQueue> output_queue_;
    std::shared_ptr<CommandQueue> command_queue_;

    std::unique_ptr<AudioProcessor> audio_processor_;
    std::mutex processor_config_mutex_;

    std::vector<int32_t> process_buffer_;
    std::vector<uint32_t> current_packet_ssrcs_;

    float current_volume_;
    std::vector<float> current_eq_;
    int current_delay_ms_;
    float current_timeshift_backshift_sec_config_;

    std::map<int, screamrouter::audio::CppSpeakerLayout> current_speaker_layouts_map_;

    std::thread input_thread_;

    std::atomic<uint64_t> m_total_packets_processed{0};

    /**
     * @brief Processes any pending commands from the command queue.
     */
    void process_commands();

    /**
     * @brief Processes a single chunk of raw audio data using the internal AudioProcessor.
     * @param input_chunk_data The raw audio data to process.
     */
    void process_audio_chunk(const std::vector<uint8_t>& input_chunk_data);
    
    /**
     * @brief Pushes a completed ProcessedAudioChunk to the output queue if the buffer is full.
     */
    void push_output_chunk_if_ready();

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
};

} // namespace audio
} // namespace screamrouter

#endif // SOURCE_INPUT_PROCESSOR_H
