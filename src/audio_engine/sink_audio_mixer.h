#ifndef SINK_AUDIO_MIXER_H
#define SINK_AUDIO_MIXER_H

// Define platform-specific socket types and macros FIRST
#include "audio_component.h"
#include "thread_safe_queue.h"
#include "audio_types.h"
#include "i_network_sender.h" // Include the new interface

#include <string>
#include <vector>
#include <map>
#include <memory> // For shared_ptr, unique_ptr
#include <mutex>
#include <condition_variable>
#include <lame/lame.h> // For LAME MP3 encoding


// Forward declare AudioProcessor
class AudioProcessor;

namespace screamrouter {
namespace audio {

// Using aliases for clarity
using InputChunkQueue = utils::ThreadSafeQueue<ProcessedAudioChunk>;
using Mp3OutputQueue = utils::ThreadSafeQueue<EncodedMP3Data>;

// Define constants based on the network protocol requirement of 1152 bytes output payload
const size_t SINK_CHUNK_SIZE_BYTES = 1152; // Network output payload size (protocol spec)
// Mixing buffer holds enough 32-bit samples to generate 1152 bytes after downscaling.
// For 16-bit stereo output (current test case): 1152 bytes / (16/8 bytes/sample) = 576 samples.
const size_t SINK_MIXING_BUFFER_SAMPLES = 576; // Samples needed for 1152 bytes @ 16-bit stereo
const size_t SINK_MP3_BUFFER_SIZE = SINK_CHUNK_SIZE_BYTES * 8; // Generous buffer for MP3 output

class SinkAudioMixer : public AudioComponent {
public:
    // Input queues map: Key is the unique source instance ID
    using InputQueueMap = std::map<std::string, std::shared_ptr<InputChunkQueue>>;

    SinkAudioMixer(
        SinkMixerConfig config,
        std::shared_ptr<Mp3OutputQueue> mp3_output_queue // Can be nullptr if MP3 not needed
    );

    ~SinkAudioMixer() override;

    // --- AudioComponent Interface ---
    void start() override;
    void stop() override;

    // --- SinkAudioMixer Specific ---
    /**
     * @brief Adds an input queue from a SourceInputProcessor instance.
     * @param instance_id The unique identifier of the source processor instance providing the queue.
     * @param queue Shared pointer to the source instance's output queue (`ProcessedAudioChunk`).
     */
    void add_input_queue(const std::string& instance_id, std::shared_ptr<InputChunkQueue> queue);

    /**
     * @brief Removes an input queue associated with a source processor instance ID.
     * @param instance_id The unique identifier of the source processor instance whose queue should be removed.
     */
    void remove_input_queue(const std::string& instance_id);

    /**
     * @brief Updates the TCP file descriptor if the connection changes.
     *        Assumes external management of the TCP connection itself.
     * @param fd The new TCP socket file descriptor, or -1 if disconnected.
     */
    // void set_tcp_fd(int fd); // Removed

protected:
    // --- AudioComponent Interface ---
    void run() override; // The main thread loop

private:
    SinkMixerConfig config_;
    std::shared_ptr<Mp3OutputQueue> mp3_output_queue_; // Null if MP3 output disabled
    std::unique_ptr<INetworkSender> network_sender_; // The sender implementation

    // Input queues from SourceInputProcessors
    InputQueueMap input_queues_;
    std::mutex queues_mutex_; // Protects input_queues_, input_active_state_, source_buffers_

    // Track active state per source instance queue
    std::map<std::string, bool> input_active_state_; // Key is instance_id

    // Buffers to hold the latest chunk popped from each source instance *this cycle*
    std::map<std::string, ProcessedAudioChunk> source_buffers_; // Key is instance_id

    // Condition variable to wait for input data (used minimally in this approach)
    std::condition_variable input_cv_; // KEEP if needed elsewhere, but wait_for logic changes
    std::mutex input_cv_mutex_;      // KEEP if needed elsewhere

    const std::chrono::milliseconds GRACE_PERIOD_TIMEOUT{45};
    const std::chrono::milliseconds GRACE_PERIOD_POLL_INTERVAL{1};

    // Mixing buffer (32-bit)
    std::vector<int32_t> mixing_buffer_;
    // Payload buffer (double buffer)
    std::vector<uint8_t> payload_buffer_; // Size = SINK_CHUNK_SIZE_BYTES * 2
    size_t payload_buffer_write_pos_ = 0; // Position where next downscaled byte goes
    
    // CSRC/SSRC tracking
    std::vector<uint32_t> current_csrcs_;
    std::mutex csrc_mutex_;

    // LAME MP3 Encoder state
    lame_t lame_global_flags_ = nullptr;
    std::unique_ptr<AudioProcessor> lame_preprocessor_; // Preprocessor for LAME input
    std::vector<uint8_t> mp3_encode_buffer_; // Temporary buffer for encoded MP3 data
    bool lame_active_ = false; // Track if MP3 stream is being consumed

    // Internal Methods
    void initialize_lame();
    void close_lame();

    // Main loop helpers
    bool wait_for_source_data(std::chrono::milliseconds timeout);
    void mix_buffers();
    void downscale_buffer();
    void encode_and_push_mp3();
};

} // namespace audio
} // namespace screamrouter

#endif // SINK_AUDIO_MIXER_H
