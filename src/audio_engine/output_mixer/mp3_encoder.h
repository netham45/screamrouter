/**
 * @file mp3_encoder.h
 * @brief MP3 encoding helper class for SinkAudioMixer.
 * @details Encapsulates LAME MP3 encoder initialization, encoding thread, and PCM queue management.
 */
#ifndef MP3_ENCODER_H
#define MP3_ENCODER_H

#include "../audio_types.h"
#include "../utils/thread_safe_queue.h"
#include <lame/lame.h>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <cstdint>

namespace screamrouter {
namespace audio {

class AudioEngineSettings;

using Mp3OutputQueue = utils::ThreadSafeQueue<EncodedMP3Data>;

/**
 * @class Mp3Encoder
 * @brief Handles MP3 encoding with a dedicated worker thread.
 * @details Manages LAME encoder lifecycle, PCM queue for async encoding,
 *          and thread-safe MP3 output delivery.
 */
class Mp3Encoder {
public:
    /**
     * @brief Constructs an Mp3Encoder.
     * @param sink_id Identifier for logging.
     * @param sample_rate Input sample rate for LAME.
     * @param output_queue Shared queue for encoded MP3 data.
     * @param settings Audio engine settings for bitrate/VBR config.
     */
    Mp3Encoder(const std::string& sink_id,
               int sample_rate,
               std::shared_ptr<Mp3OutputQueue> output_queue,
               std::shared_ptr<AudioEngineSettings> settings);
    
    ~Mp3Encoder();
    
    // Non-copyable
    Mp3Encoder(const Mp3Encoder&) = delete;
    Mp3Encoder& operator=(const Mp3Encoder&) = delete;
    
    /**
     * @brief Starts the MP3 encoding worker thread.
     */
    void start();
    
    /**
     * @brief Stops the MP3 encoding worker thread and flushes remaining data.
     */
    void stop();
    
    /**
     * @brief Enqueues stereo PCM samples for async encoding.
     * @param samples Pointer to interleaved stereo int32 samples.
     * @param sample_count Total number of int32 values (frames * 2).
     */
    void enqueue_pcm(const int32_t* samples, size_t sample_count);
    
    /**
     * @brief Checks if the encoder is properly initialized.
     * @return true if LAME is ready for encoding.
     */
    bool is_initialized() const { return lame_flags_ != nullptr; }
    
    /**
     * @brief Checks if the worker thread is running.
     * @return true if the encoding thread is active.
     */
    bool is_running() const { return thread_running_.load(std::memory_order_acquire); }
    
    // Statistics accessors
    uint64_t get_buffer_overflows() const { return buffer_overflows_.load(); }
    size_t get_pcm_queue_size() const;
    size_t get_pcm_high_water() const { return pcm_high_water_.load(); }
    size_t get_output_high_water() const { return output_high_water_.load(); }
    
    // Profiling accessors
    uint64_t get_encode_calls() const { return encode_calls_; }
    long double get_encode_ns_sum() const { return encode_ns_sum_; }
    uint64_t get_encode_ns_max() const { return encode_ns_max_; }
    uint64_t get_encode_ns_min() const { return encode_ns_min_; }
    
    void reset_profiling_counters();

private:
    void initialize_lame();
    void thread_loop();
    void encode_and_push(const int32_t* samples, size_t sample_count);
    
    std::string sink_id_;
    int sample_rate_;
    std::shared_ptr<Mp3OutputQueue> output_queue_;
    std::shared_ptr<AudioEngineSettings> settings_;
    
    lame_t lame_flags_ = nullptr;
    std::vector<uint8_t> encode_buffer_;
    
    // PCM queue for async encoding
    std::deque<std::vector<int32_t>> pcm_queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_thread_;
    std::atomic<bool> thread_running_{false};
    std::atomic<bool> stop_flag_{false};
    size_t max_queue_depth_{3};
    
    // Statistics
    std::atomic<uint64_t> buffer_overflows_{0};
    std::atomic<size_t> pcm_high_water_{0};
    std::atomic<size_t> output_high_water_{0};
    
    // Profiling
    uint64_t encode_calls_{0};
    long double encode_ns_sum_{0.0L};
    uint64_t encode_ns_max_{0};
    uint64_t encode_ns_min_{std::numeric_limits<uint64_t>::max()};
};

} // namespace audio
} // namespace screamrouter

#endif // MP3_ENCODER_H
