/**
 * @file mp3_encoder.cpp
 * @brief Implementation of MP3 encoding helper for SinkAudioMixer.
 */
#include "mp3_encoder.h"
#include "../utils/cpp_logger.h"
#include "../utils/profiler.h"
#include "../configuration/audio_engine_settings.h"
#include <cstring>
#include <chrono>
#include <system_error>

namespace screamrouter {
namespace audio {

Mp3Encoder::Mp3Encoder(const std::string& sink_id,
                       int sample_rate,
                       std::shared_ptr<Mp3OutputQueue> output_queue,
                       std::shared_ptr<AudioEngineSettings> settings)
    : sink_id_(sink_id),
      sample_rate_(sample_rate),
      output_queue_(output_queue),
      settings_(settings)
{
    if (settings_) {
        max_queue_depth_ = static_cast<size_t>(
            std::max(1, settings_->mixer_tuning.mp3_output_queue_max_size));
    }
    
    if (output_queue_) {
        initialize_lame();
    }
}

Mp3Encoder::~Mp3Encoder() {
    stop();
    if (lame_flags_) {
        lame_close(lame_flags_);
        lame_flags_ = nullptr;
    }
}

void Mp3Encoder::initialize_lame() {
    if (!output_queue_) return;
    
    LOG_CPP_INFO("[Mp3Encoder:%s] Initializing LAME MP3 encoder...", sink_id_.c_str());
    lame_flags_ = lame_init();
    if (!lame_flags_) {
        LOG_CPP_ERROR("[Mp3Encoder:%s] lame_init() failed.", sink_id_.c_str());
        return;
    }
    
    lame_set_in_samplerate(lame_flags_, sample_rate_);
    if (settings_) {
        lame_set_brate(lame_flags_, settings_->mixer_tuning.mp3_bitrate_kbps);
        lame_set_VBR(lame_flags_, settings_->mixer_tuning.mp3_vbr_enabled ? vbr_default : vbr_off);
    } else {
        lame_set_brate(lame_flags_, 128);
        lame_set_VBR(lame_flags_, vbr_off);
    }
    
    int ret = lame_init_params(lame_flags_);
    if (ret < 0) {
        LOG_CPP_ERROR("[Mp3Encoder:%s] lame_init_params() failed with code: %d", sink_id_.c_str(), ret);
        lame_close(lame_flags_);
        lame_flags_ = nullptr;
        return;
    }
    
    // Allocate encode buffer (max MP3 frame size)
    encode_buffer_.resize(8192);
    
    LOG_CPP_INFO("[Mp3Encoder:%s] LAME initialized successfully.", sink_id_.c_str());
}

void Mp3Encoder::start() {
    if (!output_queue_ || !lame_flags_) {
        return;
    }
    if (thread_running_.load(std::memory_order_acquire)) {
        return;
    }
    
    stop_flag_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pcm_queue_.clear();
    }
    
    try {
        worker_thread_ = std::thread(&Mp3Encoder::thread_loop, this);
        thread_running_.store(true, std::memory_order_release);
        LOG_CPP_INFO("[Mp3Encoder:%s] Worker thread started.", sink_id_.c_str());
    } catch (const std::system_error& e) {
        LOG_CPP_ERROR("[Mp3Encoder:%s] Failed to start thread: %s", sink_id_.c_str(), e.what());
        thread_running_.store(false, std::memory_order_release);
    }
}

void Mp3Encoder::stop() {
    stop_flag_.store(true, std::memory_order_release);
    cv_.notify_all();
    
    if (worker_thread_.joinable()) {
        try {
            worker_thread_.join();
            LOG_CPP_INFO("[Mp3Encoder:%s] Worker thread stopped.", sink_id_.c_str());
        } catch (const std::system_error& e) {
            LOG_CPP_ERROR("[Mp3Encoder:%s] Error joining thread: %s", sink_id_.c_str(), e.what());
        }
    }
    thread_running_.store(false, std::memory_order_release);
    
    // Flush remaining LAME buffer
    if (output_queue_ && lame_flags_) {
        LOG_CPP_INFO("[Mp3Encoder:%s] Flushing LAME buffer...", sink_id_.c_str());
        int flush_bytes = lame_encode_flush(lame_flags_, encode_buffer_.data(), encode_buffer_.size());
        if (flush_bytes > 0) {
            EncodedMP3Data mp3_data;
            mp3_data.mp3_data.assign(encode_buffer_.begin(), encode_buffer_.begin() + flush_bytes);
            output_queue_->push(std::move(mp3_data));
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pcm_queue_.clear();
    }
}

void Mp3Encoder::enqueue_pcm(const int32_t* samples, size_t sample_count) {
    PROFILE_FUNCTION();
    if (!output_queue_ || !samples || sample_count == 0) {
        return;
    }
    if (!thread_running_.load(std::memory_order_acquire)) {
        return;
    }
    
    std::unique_lock<std::mutex> lock(mutex_);
    if (pcm_queue_.size() >= max_queue_depth_) {
        pcm_queue_.pop_front();  // Drop oldest to keep freshest audio
        buffer_overflows_++;
        LOG_CPP_DEBUG("[Mp3Encoder:%s] PCM queue full (depth=%zu), dropping oldest chunk.",
                      sink_id_.c_str(), pcm_queue_.size());
    }
    
    std::vector<int32_t> buffer(sample_count);
    std::memcpy(buffer.data(), samples, sample_count * sizeof(int32_t));
    pcm_queue_.push_back(std::move(buffer));
    
    size_t depth = pcm_queue_.size();
    size_t hw = pcm_high_water_.load();
    while (depth > hw && !pcm_high_water_.compare_exchange_weak(hw, depth)) {
        // retry
    }
    
    lock.unlock();
    cv_.notify_one();
}

size_t Mp3Encoder::get_pcm_queue_size() const {
    // Note: This is approximate without locking
    return pcm_queue_.size();
}

void Mp3Encoder::reset_profiling_counters() {
    encode_calls_ = 0;
    encode_ns_sum_ = 0.0L;
    encode_ns_max_ = 0;
    encode_ns_min_ = std::numeric_limits<uint64_t>::max();
}

void Mp3Encoder::thread_loop() {
    PROFILE_FUNCTION();
    while (true) {
        std::vector<int32_t> work;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return stop_flag_.load(std::memory_order_acquire) || !pcm_queue_.empty();
            });
            
            if (stop_flag_.load(std::memory_order_acquire) && pcm_queue_.empty()) {
                break;
            }
            
            if (!pcm_queue_.empty()) {
                work = std::move(pcm_queue_.front());
                pcm_queue_.pop_front();
            }
        }
        
        if (!work.empty()) {
            encode_and_push(work.data(), work.size());
        }
    }
}

void Mp3Encoder::encode_and_push(const int32_t* samples, size_t sample_count) {
    PROFILE_FUNCTION();
    auto t0 = std::chrono::steady_clock::now();
    
    if (!output_queue_ || !lame_flags_ || sample_count == 0 || !samples) {
        return;
    }
    
    if (settings_ && output_queue_->size() > static_cast<size_t>(settings_->mixer_tuning.mp3_output_queue_max_size)) {
        LOG_CPP_DEBUG("[Mp3Encoder:%s] Output queue full, skipping encoding.", sink_id_.c_str());
        buffer_overflows_++;
        return;
    }
    
    int frames_per_channel = sample_count / 2;
    if (frames_per_channel <= 0) {
        return;
    }
    
    int mp3_bytes_encoded = lame_encode_buffer_interleaved_int(
        lame_flags_,
        samples,
        frames_per_channel,
        encode_buffer_.data(),
        static_cast<int>(encode_buffer_.size())
    );
    
    if (mp3_bytes_encoded < 0) {
        LOG_CPP_ERROR("[Mp3Encoder:%s] LAME encoding failed with code: %d", sink_id_.c_str(), mp3_bytes_encoded);
    } else if (mp3_bytes_encoded > 0) {
        EncodedMP3Data mp3_data;
        mp3_data.mp3_data.assign(encode_buffer_.begin(), encode_buffer_.begin() + mp3_bytes_encoded);
        output_queue_->push(std::move(mp3_data));
        
        size_t depth = output_queue_->size();
        size_t hw = output_high_water_.load();
        while (depth > hw && !output_high_water_.compare_exchange_weak(hw, depth)) {
            // retry
        }
    }
    
    auto t1 = std::chrono::steady_clock::now();
    uint64_t dt = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    encode_calls_++;
    encode_ns_sum_ += static_cast<long double>(dt);
    if (dt > encode_ns_max_) encode_ns_max_ = dt;
    if (dt < encode_ns_min_) encode_ns_min_ = dt;
}

} // namespace audio
} // namespace screamrouter
