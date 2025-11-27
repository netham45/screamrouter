#include "alsa_playback_sender.h"

#include "../../utils/cpp_logger.h"

#include <algorithm>
#include <fstream>
#include <cerrno>
#include <cstring>
#include <limits>
#include <sstream>
#include <vector>

namespace screamrouter {
namespace audio {

namespace {

bool DetectRaspberryPi() {
    std::ifstream model_file("/proc/device-tree/model");
    if (model_file.good()) {
        std::string model;
        std::getline(model_file, model);
        if (model.find("Raspberry Pi") != std::string::npos) {
            return true;
        }
    }

    std::ifstream cpuinfo_file("/proc/cpuinfo");
    if (cpuinfo_file.good()) {
        std::string line;
        while (std::getline(cpuinfo_file, line)) {
            if (line.find("Raspberry Pi") != std::string::npos ||
                line.find("BCM27") != std::string::npos) {
                return true;
            }
        }
    }

    return false;
}

} // namespace

AlsaPlaybackSender::AlsaPlaybackSender(const SinkMixerConfig& config)
#if defined(__linux__)
    : config_(config),
      device_tag_(config.output_ip),
      is_raspberry_pi_(DetectRaspberryPi())
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

void AlsaPlaybackSender::set_playback_rate_callback(std::function<void(double)> cb) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    playback_rate_callback_ = std::move(cb);
}

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
    err = snd_pcm_hw_params_set_rate(pcm_handle_, hw_params, sample_rate_, 0);
    if (err < 0) {
        LOG_CPP_ERROR("[AlsaPlayback:%s] Failed to set exact sample rate %u Hz: %s",
                      device_tag_.c_str(), sample_rate_, snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        close_locked();
        return false;
    }

    constexpr unsigned int kTargetLatencyUs = 24000;      // 24 ms overall buffer target
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
    snd_pcm_uframes_t start_threshold = period_frames_ > 0 && buffer_frames_ > period_frames_
                                            ? buffer_frames_ - period_frames_
                                            : std::max<snd_pcm_uframes_t>(1, period_frames_);
    snd_pcm_sw_params_set_start_threshold(pcm_handle_, sw_params, start_threshold);
    // Require at least one full period available before we wake the writer; keeps a bit more headroom.
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

    frames_written_.store(0, std::memory_order_release);
    if (buffer_frames_ > 0) {
        target_delay_frames_ = static_cast<double>(buffer_frames_) / 2.0;
    } else if (period_frames_ > 0) {
        target_delay_frames_ = static_cast<double>(period_frames_) * 2.0;
    } else {
        target_delay_frames_ = 0.0;
    }
    playback_rate_integral_ = 0.0;
    last_playback_rate_command_ = 1.0;
    prefill_target_delay_locked();
    return true;
}

void AlsaPlaybackSender::maybe_update_playback_rate_locked(snd_pcm_sframes_t delay_frames) {
    if (!playback_rate_callback_ || delay_frames < 0) {
        return;
    }

    if (target_delay_frames_ <= 0.0) {
        if (buffer_frames_ > 0) {
            target_delay_frames_ = static_cast<double>(buffer_frames_) / 2.0;
        } else if (period_frames_ > 0) {
            target_delay_frames_ = static_cast<double>(period_frames_) * 2.0;
        }
    }
    const auto now = std::chrono::steady_clock::now();
    constexpr auto kUpdateInterval = std::chrono::milliseconds(10);
    if (last_rate_update_.time_since_epoch().count() != 0 &&
        now - last_rate_update_ < kUpdateInterval) {
        return;
    }
    last_rate_update_ = now;

    // Low-pass the delay to avoid reacting to single jitter spikes.
    constexpr double kAlpha = 0.1; // ~200 ms time constant with 20 ms updates
    if (filtered_delay_frames_ <= 0.0) {
        filtered_delay_frames_ = static_cast<double>(delay_frames);
    } else {
        filtered_delay_frames_ = filtered_delay_frames_ + kAlpha * (static_cast<double>(delay_frames) - filtered_delay_frames_);
    }

    // Positive error => queue above target, speed up playback to shrink it.
    const double error = filtered_delay_frames_ - target_delay_frames_;
    // More aggressive gains: ~3 ppm per frame, integral a bit faster.
    constexpr double kKp = 3e-6;
    constexpr double kKi = 5e-8;
    constexpr double kIntegralClamp = 300000.0; // ~±300 ppm contribution
    playback_rate_integral_ = std::clamp(playback_rate_integral_ + error, -kIntegralClamp, kIntegralClamp);

    double adjust = (kKp * error) + (kKi * playback_rate_integral_);
    constexpr double kMaxPpm = 800.0; // ±800 ppm
    const double max_adjust = kMaxPpm * 1e-6;
    adjust = std::clamp(adjust, -max_adjust, max_adjust);

    double desired_rate = 1.0 + adjust;
    constexpr double kMaxStepPpm = 80.0; // per update slew
    const double max_step = kMaxStepPpm * 1e-6;
    const double delta = std::clamp(desired_rate - last_playback_rate_command_, -max_step, max_step);
    desired_rate = last_playback_rate_command_ + delta;

    constexpr double kHardClamp = 0.96;
    constexpr double kHardClampMax = 1.02;
    desired_rate = std::clamp(desired_rate, kHardClamp, kHardClampMax);

    ++rate_log_counter_;
    if (rate_log_counter_ % 100 == 0) {
        LOG_CPP_INFO("[AlsaPlayback:%s] PI rate update: delay=%.1f target=%.1f err=%.1f adj=%.6f rate=%.6f int=%.1f k={%.6f,%.6f} clamp_ppm=%.0f step=%.0f",
                     device_tag_.c_str(),
                     filtered_delay_frames_,
                     target_delay_frames_,
                     error,
                     adjust,
                     desired_rate,
                     playback_rate_integral_,
                     kKp,
                     kKi,
                     kMaxPpm,
                     kMaxStepPpm);
    }

    if (std::abs(desired_rate - last_playback_rate_command_) > 1e-6) {
        last_playback_rate_command_ = desired_rate;
        auto cb = playback_rate_callback_;
        cb(desired_rate);
    }
}

void AlsaPlaybackSender::prefill_target_delay_locked() {
    if (!pcm_handle_ || bytes_per_frame_ == 0 || target_delay_frames_ <= 0.0) {
        return;
    }

    snd_pcm_sframes_t current_delay = 0;
    if (snd_pcm_delay(pcm_handle_, &current_delay) < 0) {
        return;
    }
    if (current_delay < 0) {
        current_delay = 0;
    }

    const snd_pcm_sframes_t desired_delay = static_cast<snd_pcm_sframes_t>(target_delay_frames_);
    snd_pcm_sframes_t deficit = desired_delay - current_delay;
    if (deficit <= 0) {
        return;
    }

    const snd_pcm_sframes_t max_prefill = buffer_frames_ > 0
                                              ? static_cast<snd_pcm_sframes_t>(buffer_frames_)
                                              : deficit;
    deficit = std::min(deficit, max_prefill);
    std::vector<uint8_t> zeros(static_cast<size_t>(deficit) * bytes_per_frame_, 0);
    snd_pcm_sframes_t written = snd_pcm_writei(pcm_handle_, zeros.data(), deficit);
    if (written < 0) {
        LOG_CPP_WARNING("[AlsaPlayback:%s] Prefill write failed: %s", device_tag_.c_str(), snd_strerror(static_cast<int>(written)));
    } else {
        LOG_CPP_INFO("[AlsaPlayback:%s] Prefilled %ld frames of silence toward target_delay=%.1f (initial_delay=%ld, written=%ld).",
                     device_tag_.c_str(),
                     static_cast<long>(deficit),
                     target_delay_frames_,
                     static_cast<long>(current_delay),
                     static_cast<long>(written));
    }
}

bool AlsaPlaybackSender::handle_write_error(int err) {
    if (!pcm_handle_) {
        return false;
    }

    const int original_err = err;
    const bool xrun_via_error_code = (err == -EPIPE);
    const bool xrun_via_status = detect_xrun_locked();
    if (xrun_via_error_code && !xrun_via_status) {
        LOG_CPP_DEBUG("[AlsaPlayback:%s] ALSA reported write underrun (EPIPE).", device_tag_.c_str());
    }
    const bool detected_xrun = xrun_via_status || xrun_via_error_code;

    if (detected_xrun) {
        LOG_CPP_WARNING("[AlsaPlayback:%s] x-run detected while writing audio (err=%s). Attempting recovery.",
                        device_tag_.c_str(), snd_strerror(original_err));
    } else {
        LOG_CPP_WARNING("[AlsaPlayback:%s] Write error while feeding ALSA (err=%s). Attempting recovery.",
                        device_tag_.c_str(), snd_strerror(original_err));
    }

    snd_pcm_sframes_t dbg_delay = 0;
    snd_pcm_delay(pcm_handle_, &dbg_delay);
    LOG_CPP_INFO("[AlsaPlayback:%s] Pre-recover state: pcm_state=%d delay_frames=%ld target_delay=%.1f rate_cmd=%.6f",
                 device_tag_.c_str(),
                 snd_pcm_state(pcm_handle_),
                 static_cast<long>(dbg_delay),
                 target_delay_frames_,
                 last_playback_rate_command_);

    err = snd_pcm_recover(pcm_handle_, err, 1);
    if (err < 0) {
        LOG_CPP_ERROR("[AlsaPlayback:%s] Failed to recover from write error: %s", device_tag_.c_str(), snd_strerror(err));
        close_locked();
        return false;
    }

    playback_rate_integral_ = 0.0;
    last_playback_rate_command_ = 1.0;
    if (buffer_frames_ > 0) {
        target_delay_frames_ = static_cast<double>(buffer_frames_) / 2.0;
    } else if (period_frames_ > 0) {
        target_delay_frames_ = static_cast<double>(period_frames_) * 2.0;
    } else {
        target_delay_frames_ = 0.0;
    }
    filtered_delay_frames_ = 0.0;
    prefill_target_delay_locked();

    if (detected_xrun && is_raspberry_pi_) {
        LOG_CPP_WARNING("[AlsaPlayback:%s] ALSA x-run detected on Raspberry Pi; recreating playback device.", device_tag_.c_str());
        close_locked();
        if (!configure_device()) {
            LOG_CPP_ERROR("[AlsaPlayback:%s] Failed to reopen device after x-run recovery.", device_tag_.c_str());
            return false;
        }
    }
    return true;
}

bool AlsaPlaybackSender::detect_xrun_locked() {
    if (!pcm_handle_) {
        return false;
    }

    if (snd_pcm_state(pcm_handle_) == SND_PCM_STATE_XRUN) {
        return true;
    }

    snd_pcm_status_t* status = nullptr;
    if (snd_pcm_status_malloc(&status) < 0 || !status) {
        return false;
    }

    bool xrun_detected = false;
    if (snd_pcm_status(pcm_handle_, status) == 0) {
        xrun_detected = (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN);
        if (xrun_detected) {
            LOG_CPP_DEBUG("[AlsaPlayback:%s] ALSA x-run state reported by driver.", device_tag_.c_str());
        }
    }

    snd_pcm_status_free(status);
    return xrun_detected;
}

void AlsaPlaybackSender::close_locked() {
    if (pcm_handle_) {
        snd_pcm_drop(pcm_handle_);
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
    }
    target_delay_frames_ = 0.0;
    playback_rate_integral_ = 0.0;
    last_playback_rate_command_ = 1.0;
    frames_written_.store(0, std::memory_order_release);
}

bool AlsaPlaybackSender::write_frames(const void* data, size_t frame_count, size_t bytes_per_frame) {
    if (!pcm_handle_ || frame_count == 0) {
        return false;
    }

    const uint8_t* byte_ptr = static_cast<const uint8_t*>(data);
    size_t frames_remaining = frame_count;
    // Treat a "chunk" as the ALSA period we negotiated; fall back to the buffer geometry if needed.
    constexpr snd_pcm_sframes_t kMaxBufferedPeriods = 9;
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
        // Wake frequently to avoid long sleeps that drain the hardware queue and cause underruns.
        int wait_rc = snd_pcm_wait(pcm_handle_, 10);
        if (wait_rc == 0) {
            continue; // timed out, re-check state without treating as an error
        }
        if (wait_rc < 0) {
            if (!handle_write_error(wait_rc)) {
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

        // If the hardware queue is already far beyond our target plus one buffer, drop the rest of this chunk to avoid overruns.
        if (delay_frames > 0 && buffer_frames_ > 0) {
            const snd_pcm_sframes_t hard_high = static_cast<snd_pcm_sframes_t>(target_delay_frames_ + buffer_frames_);
            if (delay_frames > hard_high) {
                LOG_CPP_WARNING("[AlsaPlayback:%s] Dropping %zu frames to cap hardware queue (delay=%ld, target=%.1f, buffer=%lu).",
                                device_tag_.c_str(),
                                frames_remaining,
                                static_cast<long>(delay_frames),
                                target_delay_frames_,
                                static_cast<unsigned long>(buffer_frames_));
                frames_remaining = 0;
                break;
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

        if (written > 0) {
            frames_written_.fetch_add(static_cast<uint64_t>(written), std::memory_order_release);
        }

        byte_ptr += static_cast<size_t>(written) * bytes_per_frame;
        frames_remaining -= static_cast<size_t>(written);

        if (snd_pcm_delay(pcm_handle_, &delay_frames) == 0) {
            double delay_ms = 1000.0 * static_cast<double>(delay_frames) / static_cast<double>(sample_rate_);
            LOG_CPP_DEBUG("[AlsaPlayback:%s] ALSA reported delay: %.2f ms (%ld frames).",
                          device_tag_.c_str(), delay_ms, static_cast<long>(delay_frames));
            maybe_update_playback_rate_locked(delay_frames);
        }
    }

    maybe_log_telemetry_locked();
    return true;
}

void AlsaPlaybackSender::maybe_log_telemetry_locked() {
    static constexpr auto kTelemetryInterval = std::chrono::seconds(30);

    if (!pcm_handle_ || sample_rate_ == 0) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (telemetry_last_log_time_.time_since_epoch().count() != 0 &&
        now - telemetry_last_log_time_ < kTelemetryInterval) {
        return;
    }

    telemetry_last_log_time_ = now;

    snd_pcm_sframes_t delay_frames = 0;
    double delay_ms = 0.0;
    if (snd_pcm_delay(pcm_handle_, &delay_frames) == 0 && delay_frames >= 0) {
        delay_ms = 1000.0 * static_cast<double>(delay_frames) / static_cast<double>(sample_rate_);
    }

    double buffer_ms = 0.0;
    if (buffer_frames_ > 0) {
        buffer_ms = 1000.0 * static_cast<double>(buffer_frames_) / static_cast<double>(sample_rate_);
    }

    double period_ms = 0.0;
    if (period_frames_ > 0) {
        period_ms = 1000.0 * static_cast<double>(period_frames_) / static_cast<double>(sample_rate_);
    }

    LOG_CPP_INFO(
        "[Telemetry][AlsaPlayback:%s] delay_frames=%ld delay_ms=%.3f buffer_frames=%lu (%.3f ms) period_frames=%lu (%.3f ms)",
        device_tag_.c_str(),
        static_cast<long>(delay_frames),
        delay_ms,
        static_cast<unsigned long>(buffer_frames_),
        buffer_ms,
        static_cast<unsigned long>(period_frames_),
        period_ms);
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
