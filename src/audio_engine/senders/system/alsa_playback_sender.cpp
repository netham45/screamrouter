#include "alsa_playback_sender.h"

#include "../../utils/cpp_logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>

namespace screamrouter {
namespace audio {

AlsaPlaybackSender::AlsaPlaybackSender(const SinkMixerConfig& config)
#if defined(__linux__)
    : config_(config),
      device_tag_(config.output_ip)
#else
    : config_(config)
#endif
{
#if defined(__linux__)
    sample_rate_ = static_cast<unsigned int>(config.output_samplerate);
    channels_ = static_cast<unsigned int>(config.output_channels);
    bit_depth_ = config.output_bitdepth;
    adaptive_settings_ = config.adaptive_playback;
#endif
}

AlsaPlaybackSender::~AlsaPlaybackSender() {
    close();
}

bool AlsaPlaybackSender::setup() {
#if defined(__linux__)
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (pcm_handle_) {
        return true;
    }
    return configure_device();
#else
    LOG_CPP_WARNING("AlsaPlaybackSender setup called on unsupported platform.");
    return false;
#endif
}

void AlsaPlaybackSender::close() {
#if defined(__linux__)
    std::lock_guard<std::mutex> lock(state_mutex_);
    close_locked();
#else
    LOG_CPP_WARNING("AlsaPlaybackSender close called on unsupported platform.");
#endif
}

void AlsaPlaybackSender::send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) {
    (void)csrcs;
#if defined(__linux__)
    if (!payload_data || payload_size == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!pcm_handle_) {
        if (!configure_device()) {
            LOG_CPP_ERROR("[AlsaPlayback:%s] Unable to configure device before playback.", device_tag_.c_str());
            return;
        }
    }

    // The sink audio mixer already outputs data in the correct bit depth and little-endian format
    // We just need to write it directly to ALSA
    const unsigned int source_bit_depth = static_cast<unsigned int>(config_.output_bitdepth);
    const size_t bytes_per_source_sample = source_bit_depth / 8;
    if (bytes_per_source_sample == 0 || channels_ == 0) {
        LOG_CPP_ERROR("[AlsaPlayback:%s] Invalid source format: bitdepth=%u channels=%u",
                      device_tag_.c_str(), source_bit_depth, channels_);
        return;
    }

    const size_t source_frame_bytes = bytes_per_source_sample * channels_;
    if (payload_size % source_frame_bytes != 0) {
        LOG_CPP_ERROR("[AlsaPlayback:%s] Payload size %zu not aligned with frame size %zu.",
                      device_tag_.c_str(), payload_size, source_frame_bytes);
        return;
    }

    const size_t frames_in_payload = payload_size / source_frame_bytes;
    if (frames_in_payload == 0) {
        return;
    }

    const uint8_t* write_ptr = payload_data;
    size_t frames_to_write = frames_in_payload;
    size_t pad_frames = 0;

    if (adaptive_control_enabled()) {
        AdaptiveFrameAction action = evaluate_adaptive_action_locked(frames_to_write);
        if (action.type == AdaptiveActionType::Drop && action.frames > 0) {
            const size_t frames_to_drop = std::min(frames_to_write, action.frames);
            write_ptr += frames_to_drop * source_frame_bytes;
            frames_to_write -= frames_to_drop;
        } else if (action.type == AdaptiveActionType::Pad && action.frames > 0) {
            pad_frames = action.frames;
        }
    }

    bool write_result = true;
    if (frames_to_write > 0) {
        write_result = write_frames(write_ptr, frames_to_write, source_frame_bytes);
    }

    if (write_result && pad_frames > 0) {
        ensure_padding_capacity(pad_frames * source_frame_bytes);
        write_result = write_frames(padding_buffer_.data(), pad_frames, source_frame_bytes);
    }

    if (!write_result) {
        LOG_CPP_WARNING("[AlsaPlayback:%s] Dropped audio chunk due to write failure.", device_tag_.c_str());
    }
#else
    (void)payload_data;
    (void)payload_size;
    LOG_CPP_WARNING("AlsaPlaybackSender send_payload called on unsupported platform.");
#endif
}

#if defined(__linux__)

bool AlsaPlaybackSender::parse_legacy_card_device(const std::string& value, int& card, int& device) const {
    const auto dot_pos = value.find('.');
    if (dot_pos == std::string::npos) {
        return false;
    }
    try {
        card = std::stoi(value.substr(0, dot_pos));
        device = std::stoi(value.substr(dot_pos + 1));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

std::string AlsaPlaybackSender::resolve_alsa_device_name() const {
    if (device_tag_.empty()) {
        return {};
    }

    if (device_tag_.rfind("ap:", 0) == 0) {
        const std::string body = device_tag_.substr(3);
        int card = 0;
        int device = 0;
        if (parse_legacy_card_device(body, card, device)) {
            std::ostringstream oss;
            oss << "hw:" << card << "," << device;
            return oss.str();
        }
        return body;
    }

    return device_tag_;
}

bool AlsaPlaybackSender::configure_device() {
    if (pcm_handle_) {
        return true;
    }

    hw_device_name_ = resolve_alsa_device_name();
    if (hw_device_name_.empty()) {
        LOG_CPP_ERROR("[AlsaPlayback:%s] Invalid device tag. Expected ap:<alsa_device> (e.g. ap:hw:0,0) or any ALSA device string.", device_tag_.c_str());
        return false;
    }

    int err = snd_pcm_open(&pcm_handle_, hw_device_name_.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        LOG_CPP_ERROR("[AlsaPlayback:%s] snd_pcm_open failed: %s", device_tag_.c_str(), snd_strerror(err));
        pcm_handle_ = nullptr;
        return false;
    }

    // Use the bit depth from the sink mixer configuration
    switch (bit_depth_) {
        case 16:
            sample_format_ = SND_PCM_FORMAT_S16_LE;
            hardware_bit_depth_ = 16;
            bytes_per_frame_ = channels_ * sizeof(int16_t);
            break;
        case 24:
            sample_format_ = SND_PCM_FORMAT_S24_LE;
            hardware_bit_depth_ = 24;
            bytes_per_frame_ = channels_ * 3;
            break;
        case 32:
            sample_format_ = SND_PCM_FORMAT_S32_LE;
            hardware_bit_depth_ = 32;
            bytes_per_frame_ = channels_ * sizeof(int32_t);
            break;
        default:
            LOG_CPP_ERROR("[AlsaPlayback:%s] Unsupported bit depth %d, defaulting to 16-bit.",
                          device_tag_.c_str(), bit_depth_);
            sample_format_ = SND_PCM_FORMAT_S16_LE;
            hardware_bit_depth_ = 16;
            bytes_per_frame_ = channels_ * sizeof(int16_t);
            bit_depth_ = 16;
            break;
    }

    snd_pcm_hw_params_t* hw_params = nullptr;
    snd_pcm_hw_params_malloc(&hw_params);
    snd_pcm_hw_params_any(pcm_handle_, hw_params);
    snd_pcm_hw_params_set_access(pcm_handle_, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    err = snd_pcm_hw_params_set_format(pcm_handle_, hw_params, sample_format_);
    if (err < 0) {
        LOG_CPP_WARNING("[AlsaPlayback:%s] Failed to set %d-bit format (%s); continuing without applying format override.",
                        device_tag_.c_str(), bit_depth_, snd_strerror(err));
    }
    snd_pcm_hw_params_set_channels(pcm_handle_, hw_params, channels_);
    unsigned int rate = sample_rate_;
    snd_pcm_hw_params_set_rate_near(pcm_handle_, hw_params, &rate, nullptr);
    sample_rate_ = rate;

    constexpr unsigned int kTargetLatencyUs = 20000;      // 20 ms overall buffer target
    constexpr unsigned int kPeriodsPerBuffer = 4;          // keep a few smaller periods for smoothness
    unsigned int buffer_time = kTargetLatencyUs;
    unsigned int period_time = std::max(1000u, buffer_time / kPeriodsPerBuffer);
    snd_pcm_hw_params_set_period_time_near(pcm_handle_, hw_params, &period_time, nullptr);
    snd_pcm_hw_params_set_buffer_time_near(pcm_handle_, hw_params, &buffer_time, nullptr);

    err = snd_pcm_hw_params(pcm_handle_, hw_params);
    if (err < 0) {
        LOG_CPP_ERROR("[AlsaPlayback:%s] Failed to apply hw params: %s", device_tag_.c_str(), snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        close_locked();
        return false;
    }

    snd_pcm_hw_params_get_period_size(hw_params, &period_frames_, nullptr);
    snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_frames_);

    snd_pcm_hw_params_free(hw_params);

    snd_pcm_sw_params_t* sw_params = nullptr;
    snd_pcm_sw_params_malloc(&sw_params);
    snd_pcm_sw_params_current(pcm_handle_, sw_params);
    snd_pcm_sw_params_set_start_threshold(pcm_handle_, sw_params, buffer_frames_ / 2);
    snd_pcm_sw_params_set_avail_min(pcm_handle_, sw_params, period_frames_);
    snd_pcm_sw_params(pcm_handle_, sw_params);
    snd_pcm_sw_params_free(sw_params);

    err = snd_pcm_prepare(pcm_handle_);
    if (err < 0) {
        LOG_CPP_ERROR("[AlsaPlayback:%s] Failed to prepare PCM device: %s", device_tag_.c_str(), snd_strerror(err));
        close_locked();
        return false;
    }

    LOG_CPP_INFO("[AlsaPlayback:%s] Opened device %s (rate=%u Hz, channels=%u, bit_depth=%d, period=%lu frames).",
                 device_tag_.c_str(), hw_device_name_.c_str(), sample_rate_, channels_, bit_depth_, period_frames_);

    return true;
}

void AlsaPlaybackSender::close_locked() {
    if (pcm_handle_) {
        snd_pcm_drop(pcm_handle_);
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
    }
}

bool AlsaPlaybackSender::handle_write_error(int err) {
    if (!pcm_handle_) {
        return false;
    }
    err = snd_pcm_recover(pcm_handle_, err, 1);
    if (err < 0) {
        LOG_CPP_ERROR("[AlsaPlayback:%s] Failed to recover from write error: %s", device_tag_.c_str(), snd_strerror(err));
        close_locked();
        return false;
    }
    return true;
}

bool AlsaPlaybackSender::write_frames(const void* data, size_t frame_count, size_t bytes_per_frame) {
    if (!pcm_handle_ || frame_count == 0) {
        return false;
    }

    const uint8_t* byte_ptr = static_cast<const uint8_t*>(data);
    size_t frames_remaining = frame_count;

    while (frames_remaining > 0) {
        snd_pcm_sframes_t written = snd_pcm_writei(pcm_handle_, byte_ptr, frames_remaining);
        if (written < 0) {
            if (!handle_write_error(static_cast<int>(written))) {
                return false;
            }
            continue;
        }
        byte_ptr += static_cast<size_t>(written) * bytes_per_frame;
        frames_remaining -= static_cast<size_t>(written);
    }

    return true;
}

double AlsaPlaybackSender::query_playback_delay_locked() {
    if (!pcm_handle_) {
        return -1.0;
    }

    snd_pcm_status_t* status = nullptr;
    if (snd_pcm_status_malloc(&status) < 0 || !status) {
        return -1.0;
    }

    double delay_ms = -1.0;
    if (snd_pcm_status(pcm_handle_, status) == 0) {
        const snd_pcm_sframes_t delay_frames = snd_pcm_status_get_delay(status);
        if (delay_frames >= 0 && sample_rate_ > 0) {
            delay_ms = (static_cast<double>(delay_frames) / static_cast<double>(sample_rate_)) * 1000.0;
        }
    }

    snd_pcm_status_free(status);
    return delay_ms;
}

bool AlsaPlaybackSender::adaptive_control_enabled() const {
    return pcm_handle_ && bytes_per_frame_ > 0 && sample_rate_ > 0;
}

void AlsaPlaybackSender::ensure_padding_capacity(size_t bytes_needed) {
    if (bytes_needed == 0) {
        return;
    }

    if (padding_buffer_.size() < bytes_needed) {
        padding_buffer_.assign(bytes_needed, 0);
    } else {
        std::fill_n(padding_buffer_.begin(), bytes_needed, 0);
    }
}

size_t AlsaPlaybackSender::delay_ms_to_frames(double delay_ms) const {
    if (delay_ms <= 0.0 || sample_rate_ == 0) {
        return 0;
    }

    const double frames = (delay_ms * static_cast<double>(sample_rate_)) / 1000.0;
    if (frames <= 0.0) {
        return 0;
    }

    const auto rounded = static_cast<long long>(std::llround(frames));
    return rounded > 0 ? static_cast<size_t>(rounded) : 0;
}

AlsaPlaybackSender::AdaptiveFrameAction AlsaPlaybackSender::evaluate_adaptive_action_locked(size_t frames_available) {
    AdaptiveFrameAction action;
    if (!adaptive_control_enabled()) {
        return action;
    }

    const double measured_delay = query_playback_delay_locked();
    if (measured_delay >= 0.0) {
        const double alpha = std::clamp(adaptive_settings_.smoothing_alpha, 0.0, 1.0);
        if (!adaptive_delay_initialized_) {
            smoothed_delay_ms_ = measured_delay;
            adaptive_delay_initialized_ = true;
        } else {
            smoothed_delay_ms_ = (alpha * measured_delay) + ((1.0 - alpha) * smoothed_delay_ms_);
        }
        last_delay_ms_ = measured_delay;
    } else if (!adaptive_delay_initialized_) {
        return action;
    }

    action.measured_delay_ms = last_delay_ms_;
    action.smoothed_delay_ms = smoothed_delay_ms_;

    if (adaptive_settings_.max_compensation_frames == 0) {
        return action;
    }

    const double target = std::max(0.0, adaptive_settings_.target_delay_ms);
    const double tolerance = std::max(0.0, adaptive_settings_.tolerance_ms);

    if (smoothed_delay_ms_ > target + tolerance && frames_available > 0) {
        size_t frames_to_drop = delay_ms_to_frames(smoothed_delay_ms_ - target);
        frames_to_drop = std::min(frames_to_drop, adaptive_settings_.max_compensation_frames);
        frames_to_drop = std::min(frames_to_drop, frames_available);
        if (frames_to_drop > 0) {
            action.type = AdaptiveActionType::Drop;
            action.frames = frames_to_drop;
            maybe_log_adaptive_event(action);
        }
    } else if (smoothed_delay_ms_ + tolerance < target) {
        size_t frames_to_add = delay_ms_to_frames(target - smoothed_delay_ms_);
        frames_to_add = std::min(frames_to_add, adaptive_settings_.max_compensation_frames);
        if (frames_to_add > 0) {
            action.type = AdaptiveActionType::Pad;
            action.frames = frames_to_add;
            maybe_log_adaptive_event(action);
        }
    }

    return action;
}

void AlsaPlaybackSender::maybe_log_adaptive_event(const AdaptiveFrameAction& action) {
    if (action.type == AdaptiveActionType::None) {
        return;
    }

    const int interval_ms = adaptive_settings_.log_interval_ms;
    const auto now = std::chrono::steady_clock::now();
    if (interval_ms > 0 && last_adaptive_log_ts_ != std::chrono::steady_clock::time_point{}) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_log_ts_);
        if (elapsed.count() < interval_ms) {
            return;
        }
    }

    last_adaptive_log_ts_ = now;
    const char* label = action.type == AdaptiveActionType::Drop ? "drop" : "pad";

    LOG_CPP_INFO("[AlsaPlayback:%s] Adaptive playback action=%s frames=%zu measured=%.2fms smoothed=%.2fms target=%.2f±%.2fms",
                 device_tag_.c_str(), label, action.frames, action.measured_delay_ms,
                 action.smoothed_delay_ms, adaptive_settings_.target_delay_ms, adaptive_settings_.tolerance_ms);
}

unsigned int AlsaPlaybackSender::get_effective_sample_rate() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return sample_rate_;
}

unsigned int AlsaPlaybackSender::get_effective_channels() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return channels_;
}

unsigned int AlsaPlaybackSender::get_effective_bit_depth() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return static_cast<unsigned int>(bit_depth_);
}

#endif // __linux__

} // namespace audio
} // namespace screamrouter
