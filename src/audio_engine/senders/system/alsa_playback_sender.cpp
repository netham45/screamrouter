#include "alsa_playback_sender.h"

#include "../../utils/cpp_logger.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
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

    // Write directly to ALSA - no conversion needed
    bool write_result = write_frames(payload_data, frames_in_payload, source_frame_bytes);

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

    int err = snd_pcm_open(&pcm_handle_, hw_device_name_.c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
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

    // Disable hidden conversions so latency stays predictable.
    snd_pcm_hw_params_set_rate_resample(pcm_handle_, hw_params, 0);

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

    constexpr unsigned int kTargetLatencyUs = 12000;      // 12 ms overall buffer target
    constexpr unsigned int kPeriodsPerBuffer = 3;        // keep a few smaller periods for smoothness
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

    unsigned int got_period_us = 0;
    unsigned int got_buffer_us = 0;
    snd_pcm_hw_params_get_period_time(hw_params, &got_period_us, nullptr);
    snd_pcm_hw_params_get_buffer_time(hw_params, &got_buffer_us, nullptr);

    snd_pcm_hw_params_free(hw_params);

    snd_pcm_sw_params_t* sw_params = nullptr;
    snd_pcm_sw_params_malloc(&sw_params);
    snd_pcm_sw_params_current(pcm_handle_, sw_params);
    snd_pcm_uframes_t start_threshold = std::max<snd_pcm_uframes_t>(1, period_frames_);
    snd_pcm_sw_params_set_start_threshold(pcm_handle_, sw_params, start_threshold);
    snd_pcm_sw_params_set_avail_min(pcm_handle_, sw_params, period_frames_);
    snd_pcm_sw_params_set_stop_threshold(pcm_handle_, sw_params, buffer_frames_);
    snd_pcm_sw_params(pcm_handle_, sw_params);
    snd_pcm_sw_params_free(sw_params);

    err = snd_pcm_prepare(pcm_handle_);
    if (err < 0) {
        LOG_CPP_ERROR("[AlsaPlayback:%s] Failed to prepare PCM device: %s", device_tag_.c_str(), snd_strerror(err));
        close_locked();
        return false;
    }

    LOG_CPP_INFO("[AlsaPlayback:%s] Opened %s rate=%u Hz channels=%u bit_depth=%d period=%lu frames (%u us) buffer=%lu frames (%u us).",
                 device_tag_.c_str(), hw_device_name_.c_str(), sample_rate_, channels_, bit_depth_, period_frames_,
                 got_period_us, buffer_frames_, got_buffer_us);

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
    // Treat a "chunk" as the ALSA period we negotiated; fall back to the buffer geometry if needed.
    constexpr snd_pcm_sframes_t kMaxBufferedPeriods = 6;
    snd_pcm_sframes_t period_frames = 0;
    if (period_frames_ > 0) {
        period_frames = static_cast<snd_pcm_sframes_t>(period_frames_);
    } else if (buffer_frames_ >= static_cast<snd_pcm_uframes_t>(kMaxBufferedPeriods)) {
        period_frames = static_cast<snd_pcm_sframes_t>(buffer_frames_ / kMaxBufferedPeriods);
    }
    const snd_pcm_sframes_t period_target = period_frames > 0 ? period_frames : static_cast<snd_pcm_sframes_t>(1);
    // Bound the ALSA hardware queue to a few periods so resumes don't replay large backlogs.
    const snd_pcm_sframes_t max_buffered_frames = period_frames > 0
                                                      ? std::min(period_frames * kMaxBufferedPeriods,
                                                                 std::numeric_limits<snd_pcm_sframes_t>::max())
                                                      : static_cast<snd_pcm_sframes_t>(0);

    while (frames_remaining > 0) {
        int wait_rc = snd_pcm_wait(pcm_handle_, 50);
        if (wait_rc <= 0) {
            int wait_err = (wait_rc == 0) ? -EPIPE : wait_rc;
            if (!handle_write_error(wait_err)) {
                return false;
            }
            continue;
        }

        snd_pcm_sframes_t avail = snd_pcm_avail_update(pcm_handle_);
        if (avail < 0) {
            if (!handle_write_error(static_cast<int>(avail))) {
                return false;
            }
            continue;
        }

        snd_pcm_sframes_t delay_frames = 0;
        snd_pcm_sframes_t allowed_extra = std::numeric_limits<snd_pcm_sframes_t>::max();
        if (max_buffered_frames > 0) {
            int delay_rc = snd_pcm_delay(pcm_handle_, &delay_frames);
            if (delay_rc < 0) {
                if (!handle_write_error(delay_rc)) {
                    return false;
                }
                continue;
            }
            if (delay_frames < 0) {
                delay_frames = 0;
            }

            allowed_extra = max_buffered_frames - delay_frames;
            if (allowed_extra <= 0) {
                LOG_CPP_WARNING("[AlsaPlayback:%s] Dropping %zu frames to cap ALSA queue (queued=%ld frames, limit=%ld frames).",
                                device_tag_.c_str(), frames_remaining, static_cast<long>(delay_frames),
                                static_cast<long>(max_buffered_frames));
                return true;
            }
        }

        snd_pcm_sframes_t frames_desired = static_cast<snd_pcm_sframes_t>(std::min(frames_remaining, static_cast<size_t>(period_target)));
        if (max_buffered_frames > 0) {
            frames_desired = std::min(frames_desired, allowed_extra);
        }
        if (frames_desired <= 0) {
            break;
        }

        if (avail < frames_desired) {
            continue;
        }

        snd_pcm_sframes_t written = snd_pcm_writei(pcm_handle_, byte_ptr, frames_desired);
        if (written == -EAGAIN) {
            continue;
        }
        if (written < 0) {
            if (!handle_write_error(static_cast<int>(written))) {
                return false;
            }
            continue;
        }

        byte_ptr += static_cast<size_t>(written) * bytes_per_frame;
        frames_remaining -= static_cast<size_t>(written);

        if (snd_pcm_delay(pcm_handle_, &delay_frames) == 0) {
            double delay_ms = 1000.0 * static_cast<double>(delay_frames) / static_cast<double>(sample_rate_);
            LOG_CPP_DEBUG("[AlsaPlayback:%s] ALSA reported delay: %.2f ms (%ld frames).",
                          device_tag_.c_str(), delay_ms, static_cast<long>(delay_frames));
        }
    }

    return true;
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
