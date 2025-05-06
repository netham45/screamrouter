#ifndef SINK_AUDIO_MIXER_H
#define SINK_AUDIO_MIXER_H

#include "audio_component.h"
#include "thread_safe_queue.h"
#include "audio_types.h"

#include <string>
#include <vector>
#include <map>
#include <memory> // For shared_ptr, unique_ptr
#include <mutex>
#include <condition_variable>
#include <lame/lame.h> // For LAME MP3 encoding

// Socket related includes
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h> // For sockaddr_in
#include <arpa/inet.h>  // For inet_pton, inet_ntoa
#include <unistd.h>     // For close
#include <poll.h>       // For poll

// Forward declare AudioProcessor
class AudioProcessor;

namespace screamrouter {
namespace audio {

// Using aliases for clarity
using InputChunkQueue = utils::ThreadSafeQueue<ProcessedAudioChunk>;
using Mp3OutputQueue = utils::ThreadSafeQueue<EncodedMP3Data>;

// Define constants based on the network protocol requirement of 1152 bytes output payload
const size_t SINK_CHUNK_SIZE_BYTES = 1152; // Network output payload size (protocol spec)
const size_t SINK_HEADER_SIZE = 5;
const size_t SINK_PACKET_SIZE_BYTES = SINK_CHUNK_SIZE_BYTES + SINK_HEADER_SIZE; // 1152 + 5 = 1157 bytes
// Mixing buffer holds enough 32-bit samples to generate 1152 bytes after downscaling.
// For 16-bit stereo output (current test case): 1152 bytes / (16/8 bytes/sample) = 576 samples.
// For 32-bit stereo output: 1152 bytes / (32/8 bytes/sample) = 288 samples.
// Setting based on the 16-bit stereo requirement for now.
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
    void set_tcp_fd(int fd);

protected:
    // --- AudioComponent Interface ---
    void run() override; // The main thread loop

private:
    SinkMixerConfig config_;
    std::shared_ptr<Mp3OutputQueue> mp3_output_queue_; // Null if MP3 output disabled

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

    // Configuration for the grace period timeout (replicating old 15ms)
    const std::chrono::milliseconds GRACE_PERIOD_TIMEOUT{15};
    const std::chrono::milliseconds GRACE_PERIOD_POLL_INTERVAL{1};

    // Network state
    int udp_socket_fd_ = -1;
    int tcp_socket_fd_ = -1; // Managed externally via set_tcp_fd()
    struct sockaddr_in udp_dest_addr_; // Store the actual struct, not pointer

    // Mixing buffer (32-bit)
    std::vector<int32_t> mixing_buffer_;
    // Network output buffer (double buffer)
    std::vector<uint8_t> output_network_buffer_; // Size = SINK_PACKET_SIZE_BYTES * 2
    size_t output_buffer_write_pos_ = 0; // Position where next downscaled byte goes
    uint8_t scream_header_[SINK_HEADER_SIZE]; // Store precomputed Scream header

    // LAME MP3 Encoder state
    lame_t lame_global_flags_ = nullptr;
    std::unique_ptr<AudioProcessor> lame_preprocessor_; // Preprocessor for LAME input
    std::vector<uint8_t> mp3_encode_buffer_; // Temporary buffer for encoded MP3 data
    bool lame_active_ = false; // Track if MP3 stream is being consumed

    // Internal Methods
    bool setup_networking(); // Create UDP socket, prepare UDP dest addr
    void close_networking(); // Close sockets
    void build_scream_header();
    void initialize_lame();
    void close_lame();

    // Main loop helpers
    bool wait_for_source_data(std::chrono::milliseconds timeout); // Waits for data on input queues, updates source_buffers_ and input_active_state_
    void mix_buffers(); // Mixes data from active sources in source_buffers_ into mixing_buffer_
    void downscale_buffer(); // Converts mixing_buffer_ to target bit depth into output_network_buffer_
    void send_network_buffer(size_t length); // Sends data via UDP or TCP
    void encode_and_push_mp3(); // Encodes mixing_buffer_ using LAME and pushes to mp3_output_queue_
};

} // namespace audio
} // namespace screamrouter

#endif // SINK_AUDIO_MIXER_H
