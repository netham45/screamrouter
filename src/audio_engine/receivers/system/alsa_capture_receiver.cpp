#include "alsa_capture_receiver.h"

#include "../../utils/cpp_logger.h"
#include "../../input_processor/timeshift_manager.h"
#include "../../audio_processor/audio_processor.h"
#include "../../configuration/audio_engine_settings.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace screamrouter {
namespace audio {

namespace {
constexpr uint8_t kStereoLayout = 0x03;
constexpr uint8_t kMonoLayout = 0x01;

bool parse_legacy_card_device(const std::string& value, int& card, int& device) {
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
}

AlsaCaptureReceiver::AlsaCaptureReceiver(
    std::string device_tag,
    CaptureParams capture_params,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager)
    : NetworkAudioReceiver(0,
                           std::move(notification_queue),
                           timeshift_manager,
                           "[AlsaCapture]" + device_tag,
                           resolve_base_frames_per_chunk(timeshift_manager ? timeshift_manager->get_settings() : nullptr)),
      device_tag_(std::move(device_tag)),
      capture_params_(std::move(capture_params))
#if SCREAMROUTER_ALSA_CAPTURE_AVAILABLE
      , base_frames_per_chunk_mono16_(resolve_base_frames_per_chunk(timeshift_manager ? timeshift_manager->get_settings() : nullptr)),
        chunk_size_bytes_(resolve_chunk_size_bytes(timeshift_manager ? timeshift_manager->get_settings() : nullptr))
#endif
{
#if SCREAMROUTER_ALSA_CAPTURE_AVAILABLE
    chunk_size_bytes_ = compute_chunk_size_bytes_for_format(
        base_frames_per_chunk_mono16_, std::max<int>(1, capture_params_.channels ? capture_params_.channels : 2),
        capture_params_.bit_depth > 0 ? capture_params_.bit_depth : 16);
    if (chunk_size_bytes_ == 0) {
        chunk_size_bytes_ = resolve_chunk_size_bytes(timeshift_manager ? timeshift_manager->get_settings() : nullptr);
    }
    chunk_bytes_ = chunk_size_bytes_;
    chunk_buffer_.reserve(chunk_size_bytes_ * 2);
#endif
}

AlsaCaptureReceiver::~AlsaCaptureReceiver() noexcept {
    stop();
}

bool AlsaCaptureReceiver::setup_socket() {
    return true; // No network socket required for ALSA capture
}

void AlsaCaptureReceiver::close_socket() {
#if SCREAMROUTER_ALSA_CAPTURE_AVAILABLE
    std::lock_guard<std::mutex> lock(device_mutex_);
    if (pcm_handle_) {
        snd_pcm_drop(pcm_handle_);
    }
#endif
}

void AlsaCaptureReceiver::run() {
#if SCREAMROUTER_ALSA_CAPTURE_AVAILABLE
    LOG_CPP_INFO("[AlsaCapture:%s] Capture thread starting.", device_tag_.c_str());

    {
        std::lock_guard<std::mutex> lock(device_mutex_);
        if (!open_device_locked()) {
            LOG_CPP_ERROR("[AlsaCapture:%s] Failed to open ALSA device. Capture loop exiting.", device_tag_.c_str());
            return;
        }
    }

    while (!stop_flag_) {
        snd_pcm_sframes_t frames_read = snd_pcm_readi(pcm_handle_, period_buffer_.data(), period_frames_);
        if (frames_read == 0) {
            continue;
        }
        if (frames_read < 0) {
            if (!recover_from_error(static_cast<int>(frames_read))) {
                LOG_CPP_ERROR("[AlsaCapture:%s] Unrecoverable ALSA read error. Exiting loop.", device_tag_.c_str());
                break;
            }
            continue;
        }
        process_captured_frames(static_cast<size_t>(frames_read));
    }

    {
        std::lock_guard<std::mutex> lock(device_mutex_);
        close_device_locked();
    }

    LOG_CPP_INFO("[AlsaCapture:%s] Capture thread exiting.", device_tag_.c_str());
#else
    LOG_CPP_WARNING("[AlsaCapture:%s] ALSA capture requested on unsupported platform.", device_tag_.c_str());
#endif
}

bool AlsaCaptureReceiver::is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) {
    (void)buffer;
    (void)size;
    (void)client_addr;
    return true;
}

bool AlsaCaptureReceiver::process_and_validate_payload(
    const uint8_t* buffer,
    int size,
    const struct sockaddr_in& client_addr,
    std::chrono::steady_clock::time_point received_time,
    TaggedAudioPacket& out_packet,
    std::string& out_source_tag) {
    (void)buffer;
    (void)size;
    (void)client_addr;
    (void)received_time;
    (void)out_packet;
    (void)out_source_tag;
    return true;
}

size_t AlsaCaptureReceiver::get_receive_buffer_size() const {
#if SCREAMROUTER_ALSA_CAPTURE_AVAILABLE
    return chunk_size_bytes_;
#else
    return 0;
#endif
}

int AlsaCaptureReceiver::get_poll_timeout_ms() const {
    return 50;
}

#if SCREAMROUTER_ALSA_CAPTURE_AVAILABLE

std::string AlsaCaptureReceiver::resolve_hw_id() const {
    if (!capture_params_.hw_id.empty()) {
        return capture_params_.hw_id;
    }
    if (device_tag_.empty()) {
        return {};
    }

    if (device_tag_.rfind("ac:", 0) == 0) {
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

bool AlsaCaptureReceiver::open_device_locked() {
    if (pcm_handle_) {
        return true;
    }

    hw_device_name_ = resolve_hw_id();
    if (hw_device_name_.empty()) {
        LOG_CPP_ERROR("[AlsaCapture:%s] Unable to resolve ALSA hw identifier for tag '%s'.",
                      device_tag_.c_str(), device_tag_.c_str());
        return false;
    }

    active_channels_ = capture_params_.channels ? capture_params_.channels : 2;
    active_sample_rate_ = capture_params_.sample_rate ? capture_params_.sample_rate : 48000;
    active_bit_depth_ = (capture_params_.bit_depth == 32) ? 32u : 16u;
    sample_format_ = (active_bit_depth_ == 32) ? SND_PCM_FORMAT_S32_LE : SND_PCM_FORMAT_S16_LE;

    int err = snd_pcm_open(&pcm_handle_, hw_device_name_.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        LOG_CPP_ERROR("[AlsaCapture:%s] snd_pcm_open failed: %s", device_tag_.c_str(), snd_strerror(err));
        pcm_handle_ = nullptr;
        return false;
    }

    snd_pcm_hw_params_t* hw_params = nullptr;
    if (snd_pcm_hw_params_malloc(&hw_params) < 0) {
        LOG_CPP_ERROR("[AlsaCapture:%s] Failed to allocate hw params.", device_tag_.c_str());
        close_device_locked();
        return false;
    }

    if ((err = snd_pcm_hw_params_any(pcm_handle_, hw_params)) < 0) {
        LOG_CPP_ERROR("[AlsaCapture:%s] snd_pcm_hw_params_any failed: %s", device_tag_.c_str(), snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        close_device_locked();
        return false;
    }

    if ((err = snd_pcm_hw_params_set_access(pcm_handle_, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        LOG_CPP_ERROR("[AlsaCapture:%s] Failed to set interleaved access: %s", device_tag_.c_str(), snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        close_device_locked();
        return false;
    }

    if ((err = snd_pcm_hw_params_set_format(pcm_handle_, hw_params, sample_format_)) < 0) {
        if (sample_format_ == SND_PCM_FORMAT_S32_LE) {
            LOG_CPP_WARNING("[AlsaCapture:%s] S32_LE unsupported, falling back to S16_LE.", device_tag_.c_str());
            sample_format_ = SND_PCM_FORMAT_S16_LE;
            active_bit_depth_ = 16;
            err = snd_pcm_hw_params_set_format(pcm_handle_, hw_params, sample_format_);
        }
        if (err < 0) {
            LOG_CPP_ERROR("[AlsaCapture:%s] Failed to set sample format: %s", device_tag_.c_str(), snd_strerror(err));
            snd_pcm_hw_params_free(hw_params);
            close_device_locked();
            return false;
        }
    }

    if ((err = snd_pcm_hw_params_set_channels(pcm_handle_, hw_params, active_channels_)) < 0) {
        if (active_channels_ != 1) {
            LOG_CPP_WARNING("[AlsaCapture:%s] Requested %u channels unsupported (%s). Retrying as mono.",
                            device_tag_.c_str(), active_channels_, snd_strerror(err));
            unsigned int fallback_channels = 1;
            int fallback_err = snd_pcm_hw_params_set_channels(pcm_handle_, hw_params, fallback_channels);
            if (fallback_err == 0) {
                active_channels_ = fallback_channels;
            } else {
                LOG_CPP_ERROR("[AlsaCapture:%s] Failed to set fallback mono capture: %s", device_tag_.c_str(), snd_strerror(fallback_err));
                snd_pcm_hw_params_free(hw_params);
                close_device_locked();
                return false;
            }
        } else {
            LOG_CPP_ERROR("[AlsaCapture:%s] Failed to set channel count: %s", device_tag_.c_str(), snd_strerror(err));
            snd_pcm_hw_params_free(hw_params);
            close_device_locked();
            return false;
        }
    }

    unsigned int requested_rate = active_sample_rate_;
    if ((err = snd_pcm_hw_params_set_rate_near(pcm_handle_, hw_params, &requested_rate, nullptr)) < 0) {
        LOG_CPP_ERROR("[AlsaCapture:%s] Failed to set sample rate: %s", device_tag_.c_str(), snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        close_device_locked();
        return false;
    }
    active_sample_rate_ = requested_rate;

    snd_pcm_uframes_t desired_period = capture_params_.period_frames ? capture_params_.period_frames : 1024;
    if ((err = snd_pcm_hw_params_set_period_size_near(pcm_handle_, hw_params, &desired_period, nullptr)) < 0) {
        LOG_CPP_WARNING("[AlsaCapture:%s] Failed to set period size: %s", device_tag_.c_str(), snd_strerror(err));
    }

    snd_pcm_uframes_t desired_buffer = capture_params_.buffer_frames ? capture_params_.buffer_frames : desired_period * 4;
    if ((err = snd_pcm_hw_params_set_buffer_size_near(pcm_handle_, hw_params, &desired_buffer)) < 0) {
        LOG_CPP_WARNING("[AlsaCapture:%s] Failed to set buffer size: %s", device_tag_.c_str(), snd_strerror(err));
    }

    if ((err = snd_pcm_hw_params(pcm_handle_, hw_params)) < 0) {
        LOG_CPP_ERROR("[AlsaCapture:%s] Failed to apply hw params: %s", device_tag_.c_str(), snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        close_device_locked();
        return false;
    }

    snd_pcm_hw_params_get_period_size(hw_params, &period_frames_, nullptr);
    snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_frames_);
    snd_pcm_hw_params_free(hw_params);

    snd_pcm_sw_params_t* sw_params = nullptr;
    if (snd_pcm_sw_params_malloc(&sw_params) == 0) {
        if (snd_pcm_sw_params_current(pcm_handle_, sw_params) == 0) {
            snd_pcm_sw_params_set_start_threshold(pcm_handle_, sw_params, period_frames_);
            snd_pcm_sw_params_set_avail_min(pcm_handle_, sw_params, period_frames_);
            snd_pcm_sw_params(pcm_handle_, sw_params);
        }
        snd_pcm_sw_params_free(sw_params);
    }

    if ((err = snd_pcm_prepare(pcm_handle_)) < 0) {
        LOG_CPP_ERROR("[AlsaCapture:%s] Failed to prepare device: %s", device_tag_.c_str(), snd_strerror(err));
        close_device_locked();
        return false;
    }

    bytes_per_sample_ = active_bit_depth_ / 8;
    bytes_per_frame_ = bytes_per_sample_ * active_channels_;
    if (bytes_per_frame_ == 0 || (chunk_size_bytes_ % bytes_per_frame_) != 0) {
        LOG_CPP_ERROR("[AlsaCapture:%s] Incompatible format: chunk size %u not divisible by frame size %zu bytes.",
                      device_tag_.c_str(), static_cast<unsigned int>(chunk_size_bytes_), bytes_per_frame_);
        close_device_locked();
        return false;
    }
    chunk_bytes_ = chunk_size_bytes_;

    period_buffer_.assign(period_frames_ * bytes_per_frame_, 0u);
    chunk_buffer_.clear();
    chunk_buffer_.reserve(chunk_bytes_ * 2);
    running_timestamp_ = 0;

    LOG_CPP_INFO("[AlsaCapture:%s] Opened %s (rate=%u Hz, channels=%u, bit_depth=%u, period=%lu frames).",
                 device_tag_.c_str(), hw_device_name_.c_str(), active_sample_rate_, active_channels_, active_bit_depth_,
                 static_cast<unsigned long>(period_frames_));

    return true;
}

void AlsaCaptureReceiver::close_device_locked() {
    if (pcm_handle_) {
        snd_pcm_drop(pcm_handle_);
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
    }
}

bool AlsaCaptureReceiver::recover_from_error(int err) {
    if (!pcm_handle_) {
        return false;
    }
    const bool is_xrun = (err == -EPIPE);
    LOG_CPP_WARNING("[AlsaCapture:%s] Read error detected (err=%s)%s. Attempting recovery.",
                    device_tag_.c_str(),
                    snd_strerror(err),
                    is_xrun ? " [x-run]" : "");
    err = snd_pcm_recover(pcm_handle_, err, 1);
    if (err < 0) {
        LOG_CPP_ERROR("[AlsaCapture:%s] snd_pcm_recover failed: %s", device_tag_.c_str(), snd_strerror(err));
        return false;
    }
    return true;
}

void AlsaCaptureReceiver::process_captured_frames(size_t frames_captured) {
    if (frames_captured == 0) {
        return;
    }
    const size_t bytes_captured = frames_captured * bytes_per_frame_;
    const uint8_t* src = period_buffer_.data();
    chunk_buffer_.write(src, bytes_captured);

    while (chunk_buffer_.size() >= chunk_bytes_) {
        std::vector<uint8_t> chunk(chunk_bytes_);
        const std::size_t popped = chunk_buffer_.pop(chunk.data(), chunk_bytes_);
        if (popped != chunk_bytes_) {
            if (popped > 0) {
                chunk_buffer_.write(chunk.data(), popped);
            }
            break;
        }
        dispatch_chunk(std::move(chunk));
    }
}

void AlsaCaptureReceiver::dispatch_chunk(std::vector<uint8_t>&& chunk_data) {
    if (chunk_data.size() != chunk_bytes_) {
        return;
    }

    // Scream protocol uses little-endian, same as ALSA
    // No byte swapping needed - data is already in the correct format

    TaggedAudioPacket packet;
    packet.source_tag = device_tag_;
    packet.audio_data = std::move(chunk_data);
    packet.received_time = std::chrono::steady_clock::now();
    packet.channels = static_cast<int>(active_channels_);
    packet.sample_rate = static_cast<int>(active_sample_rate_);
    packet.bit_depth = static_cast<int>(active_bit_depth_);
    packet.chlayout1 = (active_channels_ == 1) ? kMonoLayout : kStereoLayout;
    packet.chlayout2 = 0x00;
    packet.playback_rate = 1.0;

    const uint32_t frames_in_chunk = static_cast<uint32_t>(chunk_bytes_ / bytes_per_frame_);
    packet.rtp_timestamp = running_timestamp_;
    running_timestamp_ += frames_in_chunk;

    bool is_new_source = false;
    {
        std::lock_guard<std::mutex> lock(known_tags_mutex_);
        auto result = known_source_tags_.insert(device_tag_);
        is_new_source = result.second;
    }

    {
        std::lock_guard<std::mutex> lock(seen_tags_mutex_);
        if (std::find(seen_tags_.begin(), seen_tags_.end(), device_tag_) == seen_tags_.end()) {
            seen_tags_.push_back(device_tag_);
        }
    }

    if (is_new_source && notification_queue_) {
        notification_queue_->push(DeviceDiscoveryNotification{device_tag_, DeviceDirection::CAPTURE, true});
    }

    if (timeshift_manager_) {
        timeshift_manager_->add_packet(std::move(packet));
    } else {
        LOG_CPP_ERROR("[AlsaCapture:%s] Timeshift manager is null, dropping chunk.", device_tag_.c_str());
    }
}

#endif // SCREAMROUTER_ALSA_CAPTURE_AVAILABLE

} // namespace audio
} // namespace screamrouter
