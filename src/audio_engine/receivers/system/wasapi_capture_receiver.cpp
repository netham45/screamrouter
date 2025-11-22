#ifdef _WIN32


#include "wasapi_capture_receiver.h"

#include "../../utils/cpp_logger.h"
#include "../../input_processor/timeshift_manager.h"
#include "../../system_audio/system_audio_tags.h"
#include "../../system_audio/windows_utils.h"

#include <mmreg.h>
#include <avrt.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace screamrouter {
namespace audio {
namespace system_audio {

namespace {
constexpr uint8_t kStereoLayout = 0x03;
constexpr uint8_t kMonoLayout = 0x01;
constexpr size_t kMaxCaptureQueueDepth = 8; // Prevent unbounded growth if processing stalls.

uint32_t FramesFromBytes(size_t bytes, size_t bytes_per_frame) {
    if (bytes_per_frame == 0) {
        return 0;
    }
    return static_cast<uint32_t>(bytes / bytes_per_frame);
}

WasapiCaptureReceiver::SampleFormat IdentifyFormat(const WAVEFORMATEX* format) {
    if (!format) {
        return WasapiCaptureReceiver::SampleFormat::Unknown;
    }
    WORD tag = format->wFormatTag;
    WORD bits = format->wBitsPerSample;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            return WasapiCaptureReceiver::SampleFormat::Float32;
        }
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            bits = ext->Samples.wValidBitsPerSample ? ext->Samples.wValidBitsPerSample : format->wBitsPerSample;
        }
    }

    switch (bits) {
        case 16:
            return WasapiCaptureReceiver::SampleFormat::Int16;
        case 24:
            return WasapiCaptureReceiver::SampleFormat::Int24;
        case 32:
            return (tag == WAVE_FORMAT_IEEE_FLOAT || tag == WAVE_FORMAT_EXTENSIBLE)
                       ? WasapiCaptureReceiver::SampleFormat::Float32
                       : WasapiCaptureReceiver::SampleFormat::Int32;
        default:
            return WasapiCaptureReceiver::SampleFormat::Unknown;
    }
}

unsigned int BitsPerSample(const WAVEFORMATEX* format) {
    if (!format) {
        return 0;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        if (ext->Samples.wValidBitsPerSample != 0) {
            return ext->Samples.wValidBitsPerSample;
        }
    }
    return format->wBitsPerSample;
}

} // namespace

WasapiCaptureReceiver::WasapiCaptureReceiver(std::string device_tag,
                                             CaptureParams capture_params,
                                             std::shared_ptr<NotificationQueue> notification_queue,
                                             TimeshiftManager* timeshift_manager)
    : NetworkAudioReceiver(0,
                           std::move(notification_queue),
                           timeshift_manager,
                           "[WasapiCapture]" + device_tag,
                           1024),  // Use a reasonable default, doesn't matter since we're not accumulating
      device_tag_(std::move(device_tag)),
      capture_params_(std::move(capture_params))
{
    loopback_mode_ = system_audio::tag_has_prefix(device_tag_, system_audio::kWasapiLoopbackPrefix) || capture_params_.loopback;
    exclusive_mode_ = capture_params_.exclusive_mode;
}

WasapiCaptureReceiver::~WasapiCaptureReceiver() noexcept {
    stop();
}

bool WasapiCaptureReceiver::setup_socket() {
    return true;
}

void WasapiCaptureReceiver::close_socket() {
    request_capture_stop();
    join_capture_thread();

    std::lock_guard<std::mutex> lock(device_mutex_);
    if (cleanup_started_) {
        return;
    }
    cleanup_started_ = true;
    stop_stream();
    close_device();
    if (com_initialized_) {
        CoUninitialize();
        com_initialized_ = false;
    }
}

size_t WasapiCaptureReceiver::get_receive_buffer_size() const {
    // ~20ms at 48kHz stereo 16-bit.
    return 3840;
}

int WasapiCaptureReceiver::get_poll_timeout_ms() const {
    return 50;
}

void WasapiCaptureReceiver::run() {
    LOG_CPP_INFO("[WasapiCapture:%s] Thread starting.", device_tag_.c_str());
    {
        std::lock_guard<std::mutex> lock(capture_thread_mutex_);
        capture_thread_started_ = false;
        capture_thread_joined_ = false;
    }
    cleanup_started_ = false;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        LOG_CPP_WARNING("[WasapiCapture:%s] COM already initialized on different mode.", device_tag_.c_str());
    } else if (FAILED(hr)) {
        LOG_CPP_ERROR("[WasapiCapture:%s] CoInitializeEx failed: 0x%lx", device_tag_.c_str(), hr);
        return;
    } else {
        com_initialized_ = true;
    }

    if (!open_device()) {
        LOG_CPP_ERROR("[WasapiCapture:%s] Failed to open device.", device_tag_.c_str());
        close_socket();
        return;
    }

    if (!configure_audio_client()) {
        LOG_CPP_ERROR("[WasapiCapture:%s] Failed to configure audio client.", device_tag_.c_str());
        close_socket();
        return;
    }

    if (!start_stream()) {
        LOG_CPP_ERROR("[WasapiCapture:%s] Failed to start stream.", device_tag_.c_str());
        close_socket();
        return;
    }

    try {
        capture_thread_ = std::thread([this]() { capture_loop(); });
        std::lock_guard<std::mutex> lock(capture_thread_mutex_);
        capture_thread_started_ = true;
        capture_thread_joined_ = false;
    } catch (const std::system_error& e) {
        LOG_CPP_ERROR("[WasapiCapture:%s] Failed to start capture thread: %s", device_tag_.c_str(), e.what());
        close_socket();
        return;
    }

    processing_loop();
    join_capture_thread();

    close_socket();

    LOG_CPP_INFO("[WasapiCapture:%s] Thread exiting.", device_tag_.c_str());
}

bool WasapiCaptureReceiver::open_device() {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator_));
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WasapiCapture:%s] Failed to create MMDeviceEnumerator: 0x%lx", device_tag_.c_str(), hr);
        return false;
    }

    std::wstring endpoint_id_w;
    if (!resolve_endpoint_id(endpoint_id_w)) {
        LOG_CPP_ERROR("[WasapiCapture:%s] Failed to resolve endpoint id.", device_tag_.c_str());
        return false;
    }

    hr = device_enumerator_->GetDevice(endpoint_id_w.c_str(), &device_);
    if (FAILED(hr) || !device_) {
        LOG_CPP_ERROR("[WasapiCapture:%s] GetDevice failed: 0x%lx", device_tag_.c_str(), hr);
        return false;
    }

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audio_client_);
    if (FAILED(hr) || !audio_client_) {
        LOG_CPP_ERROR("[WasapiCapture:%s] Activate IAudioClient failed: 0x%lx", device_tag_.c_str(), hr);
        return false;
    }

    return true;
}

void WasapiCaptureReceiver::close_device() {
    if (capture_event_) {
        CloseHandle(capture_event_);
        capture_event_ = nullptr;
    }

    if (mmcss_handle_) {
        AvRevertMmThreadCharacteristics(mmcss_handle_);
        mmcss_handle_ = nullptr;
    }

    capture_client_.Reset();
    audio_client_.Reset();
    device_.Reset();
    device_enumerator_.Reset();
    format_buffer_.clear();
    format_ptr_ = nullptr;
}

bool WasapiCaptureReceiver::configure_audio_client() {
    WAVEFORMATEX* mix_format = nullptr;
    HRESULT hr = audio_client_->GetMixFormat(&mix_format);
    if (FAILED(hr) || !mix_format) {
        LOG_CPP_ERROR("[WasapiCapture:%s] GetMixFormat failed: 0x%lx", device_tag_.c_str(), hr);
        return false;
    }

    bool format_ok = initialize_capture_format(mix_format);
    CoTaskMemFree(mix_format);
    if (!format_ok) {
        return false;
    }

    DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (loopback_mode_) {
        stream_flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    }

    AUDCLNT_SHAREMODE share_mode = exclusive_mode_ ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;
    if (loopback_mode_) {
        // Loopback is only valid in shared mode.
        share_mode = AUDCLNT_SHAREMODE_SHARED;
    }

    // Derive a buffer request based on config and device period to avoid underruns.
    REFERENCE_TIME default_period = 0;
    REFERENCE_TIME min_period = 0;
    hr = audio_client_->GetDevicePeriod(&default_period, &min_period);
    if (FAILED(hr)) {
        default_period = 0;
        min_period = 0;
    }

    UINT32 shared_default_frames = 0;
    UINT32 shared_fundamental_frames = 0;
    UINT32 shared_min_frames = 0;
    UINT32 shared_max_frames = 0;
    {
        Microsoft::WRL::ComPtr<IAudioClient3> audio_client3;
        if (SUCCEEDED(audio_client_.As(&audio_client3)) && audio_client3) {
            HRESULT hr_period = audio_client3->GetSharedModeEnginePeriod(
                format_ptr_,
                &shared_default_frames,
                &shared_fundamental_frames,
                &shared_min_frames,
                &shared_max_frames);
            if (SUCCEEDED(hr_period) && active_sample_rate_ > 0) {
                auto FramesToHns = [this](UINT32 frames) -> REFERENCE_TIME {
                    return static_cast<REFERENCE_TIME>(
                        (static_cast<uint64_t>(frames) * 10000000ULL) / active_sample_rate_);
                };
                if (shared_default_frames > 0) {
                    default_period = FramesToHns(shared_default_frames);
                }
                if (shared_fundamental_frames > 0) {
                    min_period = FramesToHns(shared_fundamental_frames);
                }
                if (shared_min_frames > 0) {
                    min_period = FramesToHns(shared_min_frames);
                }
            }
        }
    }

    REFERENCE_TIME requested_buffer = 0;
    bool requested_from_config = false;
    if (capture_params_.buffer_duration_ms > 0) {
        requested_buffer = static_cast<REFERENCE_TIME>(capture_params_.buffer_duration_ms) * 10000;
        requested_from_config = true;
    } else if (capture_params_.buffer_frames > 0 && active_sample_rate_ > 0) {
        requested_buffer = static_cast<REFERENCE_TIME>(
            (static_cast<uint64_t>(capture_params_.buffer_frames) * 10000000ULL) / active_sample_rate_);
        requested_from_config = true;
    } else if (capture_params_.period_frames > 0 && active_sample_rate_ > 0) {
        requested_buffer = static_cast<REFERENCE_TIME>(
            (static_cast<uint64_t>(capture_params_.period_frames) * 10000000ULL) / active_sample_rate_);
        requested_from_config = true;
    } else if (default_period > 0) {
        // Use 4x the default engine period to get headroom without being too large.
        requested_buffer = default_period * 4;
    } else {
        // Fallback to 20ms if device period is unavailable (keep latency modest).
        requested_buffer = 200000; // 20ms in 100ns units
    }

    // Round up to a multiple of the engine period to avoid rounding down by WASAPI.
    REFERENCE_TIME quantum = default_period > 0 ? default_period : (min_period > 0 ? min_period : 0);
    if (quantum > 0) {
        if (requested_buffer < quantum) {
            requested_buffer = quantum;
        }
        const REFERENCE_TIME periods = (requested_buffer + quantum - 1) / quantum;
        const REFERENCE_TIME min_periods = 4; // at least 4 periods worth of buffering
        const REFERENCE_TIME final_periods = (periods < min_periods) ? min_periods : periods;
        requested_buffer = final_periods * quantum;
    }

    // For exclusive mode, periodicity must be set; for shared pass 0.
    REFERENCE_TIME periodicity = (share_mode == AUDCLNT_SHAREMODE_EXCLUSIVE) ? requested_buffer : 0;

    hr = audio_client_->Initialize(share_mode,
                                   stream_flags,
                                   requested_buffer,
                                   periodicity,
                                   format_ptr_,
                                   nullptr);
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WasapiCapture:%s] IAudioClient::Initialize failed: 0x%lx", device_tag_.c_str(), hr);
        return false;
    }

    UINT32 buffer_frames = 0;
    hr = audio_client_->GetBufferSize(&buffer_frames);
    if (SUCCEEDED(hr) && active_sample_rate_ > 0) {
        const double buffer_ms = (static_cast<double>(buffer_frames) / static_cast<double>(active_sample_rate_)) * 1000.0;
        LOG_CPP_INFO("[WasapiCapture:%s] Buffer configured: %u frames (~%.2f ms), share_mode=%s, requested_buffer_ms=%.2f (from_config=%s), device_period_ms=[default:%.2f,min:%.2f]",
                     device_tag_.c_str(),
                     buffer_frames,
                     buffer_ms,
                     share_mode == AUDCLNT_SHAREMODE_EXCLUSIVE ? "exclusive" : "shared",
                     requested_buffer / 10000.0,
                     requested_from_config ? "true" : "false",
                     default_period / 10000.0,
                     min_period / 10000.0);
    }
    configured_buffer_frames_ = buffer_frames;
    configured_buffer_ms_ = (active_sample_rate_ > 0)
                                ? (static_cast<double>(configured_buffer_frames_) / static_cast<double>(active_sample_rate_)) * 1000.0
                                : 0.0;
    max_packet_bytes_ = static_cast<size_t>(buffer_frames) * target_bytes_per_frame_;
    if (max_packet_bytes_ == 0) {
        max_packet_bytes_ = static_cast<size_t>(active_sample_rate_ / 50) * target_bytes_per_frame_; // ~20ms fallback
    }
    packet_buffer_.reserve(max_packet_bytes_);
    spare_buffer_.reserve(max_packet_bytes_);

    hr = audio_client_->GetService(IID_PPV_ARGS(&capture_client_));
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WasapiCapture:%s] GetService(IAudioCaptureClient) failed: 0x%lx", device_tag_.c_str(), hr);
        return false;
    }

    if (!capture_event_) {
        capture_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!capture_event_) {
            LOG_CPP_ERROR("[WasapiCapture:%s] Failed to create capture event.", device_tag_.c_str());
            return false;
        }
    }

    hr = audio_client_->SetEventHandle(capture_event_);
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WasapiCapture:%s] SetEventHandle failed: 0x%lx", device_tag_.c_str(), hr);
        return false;
    }

    return true;
}

bool WasapiCaptureReceiver::initialize_capture_format(WAVEFORMATEX* mix_format) {
    if (!mix_format) {
        return false;
    }

    AUDCLNT_SHAREMODE share_mode = (loopback_mode_) ? AUDCLNT_SHAREMODE_SHARED
                                                    : (exclusive_mode_ ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED);

    // For loopback in shared mode, WASAPI only guarantees the mix format; avoid conversion by
    // using it verbatim instead of requesting an alternate PCM format.
    if (loopback_mode_) {
        format_buffer_.resize(sizeof(WAVEFORMATEX) + mix_format->cbSize);
        std::memcpy(format_buffer_.data(), mix_format, format_buffer_.size());
        format_ptr_ = reinterpret_cast<WAVEFORMATEX*>(format_buffer_.data());

        source_format_ = IdentifyFormat(format_ptr_);
        source_bits_per_sample_ = BitsPerSample(format_ptr_);

        active_channels_ = format_ptr_->nChannels;
        active_sample_rate_ = format_ptr_->nSamplesPerSec;
        seconds_per_frame_ = active_sample_rate_ > 0 ? (1.0 / static_cast<double>(active_sample_rate_)) : 0.0;

        // Choose output bit depth: convert float to 32-bit PCM; for PCM keep container width
        // so frame sizing matches the bytes WASAPI delivers (important for 24-bit in 32-bit containers).
        source_bytes_per_frame_ = format_ptr_->nBlockAlign;
        if (source_format_ == SampleFormat::Float32) {
            target_bit_depth_ = 32;
            target_bytes_per_frame_ = (target_bit_depth_ / 8) * active_channels_;
        } else {
            const unsigned int container_bits = (active_channels_ > 0 && source_bytes_per_frame_ > 0)
                                                    ? static_cast<unsigned int>((source_bytes_per_frame_ / active_channels_) * 8)
                                                    : source_bits_per_sample_;
            target_bit_depth_ = container_bits;
            target_bytes_per_frame_ = source_bytes_per_frame_;
        }

        LOG_CPP_INFO("[WasapiCapture:%s] Using mix format for loopback: %u Hz, %u ch, %u-bit (mask=0x%08x)",
                     device_tag_.c_str(),
                     format_ptr_->nSamplesPerSec,
                     format_ptr_->nChannels,
                     source_bits_per_sample_,
                     (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
                         ? reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix_format)->dwChannelMask
                         : 0u);

        return true;
    }

    // Attempt to honor user-requested format (primarily for exclusive mode).
    WAVEFORMATEXTENSIBLE requested = {};
    requested.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    requested.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    requested.Format.nChannels = capture_params_.channels > 0 ? capture_params_.channels : mix_format->nChannels;
    requested.Format.nSamplesPerSec = capture_params_.sample_rate > 0 ? capture_params_.sample_rate : mix_format->nSamplesPerSec;
    requested.Format.wBitsPerSample = capture_params_.bit_depth == 32 ? 32 : 16;
    requested.Format.nBlockAlign = (requested.Format.nChannels * requested.Format.wBitsPerSample) / 8;
    requested.Format.nAvgBytesPerSec = requested.Format.nSamplesPerSec * requested.Format.nBlockAlign;
    requested.Samples.wValidBitsPerSample = requested.Format.wBitsPerSample;
    requested.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    if (requested.Format.nChannels == 1) {
        requested.dwChannelMask = SPEAKER_FRONT_CENTER;
    } else if (requested.Format.nChannels == 2) {
        requested.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    } else {
        requested.dwChannelMask = 0;
    }

    WAVEFORMATEX* chosen_format = mix_format;
    size_t chosen_size = sizeof(WAVEFORMATEX) + mix_format->cbSize;
    WAVEFORMATEX* allocated_format = nullptr;

    if (share_mode == AUDCLNT_SHAREMODE_EXCLUSIVE ||
        requested.Format.nSamplesPerSec != mix_format->nSamplesPerSec ||
        requested.Format.nChannels != mix_format->nChannels ||
        requested.Format.wBitsPerSample != mix_format->wBitsPerSample) {
        WAVEFORMATEX* closest = nullptr;
        HRESULT support_hr = audio_client_->IsFormatSupported(share_mode,
                                                              reinterpret_cast<WAVEFORMATEX*>(&requested),
                                                              &closest);
        if (closest) {
            allocated_format = closest;
        }
        if (support_hr == S_OK) {
            chosen_format = reinterpret_cast<WAVEFORMATEX*>(&requested);
            chosen_size = sizeof(WAVEFORMATEXTENSIBLE);
            LOG_CPP_INFO("[WasapiCapture:%s] Using requested format %u Hz, %u ch, %u-bit (%s mode).",
                         device_tag_.c_str(),
                         requested.Format.nSamplesPerSec,
                         requested.Format.nChannels,
                         requested.Format.wBitsPerSample,
                         share_mode == AUDCLNT_SHAREMODE_EXCLUSIVE ? "exclusive" : "shared");
        } else {
            if (support_hr == S_FALSE && closest) {
                chosen_format = closest;
                chosen_size = sizeof(WAVEFORMATEX) + closest->cbSize;
                allocated_format = closest;
                LOG_CPP_WARNING("[WasapiCapture:%s] Requested format not supported, using closest format %u Hz, %u ch, %u-bit.",
                                device_tag_.c_str(),
                                closest->nSamplesPerSec,
                                closest->nChannels,
                                closest->wBitsPerSample);
            } else {
                LOG_CPP_WARNING("[WasapiCapture:%s] Requested format not supported (hr=0x%lx). Falling back to mix format %u Hz, %u ch, %u-bit.",
                                device_tag_.c_str(),
                                support_hr,
                                mix_format->nSamplesPerSec,
                                mix_format->nChannels,
                                mix_format->wBitsPerSample);
            }
            // Do not free 'closest' yet if we chose it; free after copying.
        }
    }

    format_buffer_.resize(chosen_size);
    std::memcpy(format_buffer_.data(), chosen_format, chosen_size);
    if (allocated_format) {
        CoTaskMemFree(allocated_format);
    }
    format_ptr_ = reinterpret_cast<WAVEFORMATEX*>(format_buffer_.data());

    source_format_ = IdentifyFormat(format_ptr_);
    source_bits_per_sample_ = BitsPerSample(format_ptr_);

    active_channels_ = format_ptr_->nChannels;
    active_sample_rate_ = format_ptr_->nSamplesPerSec;

    if (active_sample_rate_ > 0) {
        seconds_per_frame_ = 1.0 / static_cast<double>(active_sample_rate_);
    } else {
        seconds_per_frame_ = 0.0;
    }

    // Choose output bit depth: convert float to 32-bit PCM; for PCM keep container width
    // so frame sizing matches the bytes WASAPI delivers (important for 24-bit in 32-bit containers).
    source_bytes_per_frame_ = format_ptr_->nBlockAlign;
    if (source_format_ == SampleFormat::Float32) {
        target_bit_depth_ = 32;
        target_bytes_per_frame_ = (target_bit_depth_ / 8) * active_channels_;
    } else {
        const unsigned int container_bits = (active_channels_ > 0 && source_bytes_per_frame_ > 0)
                                                ? static_cast<unsigned int>((source_bytes_per_frame_ / active_channels_) * 8)
                                                : source_bits_per_sample_;
        target_bit_depth_ = container_bits;
        target_bytes_per_frame_ = source_bytes_per_frame_;
    }

    if (target_bytes_per_frame_ == 0 || source_bytes_per_frame_ == 0) {
        LOG_CPP_ERROR("[WasapiCapture:%s] Invalid frame sizing (target=%zu, source=%zu).",
                      device_tag_.c_str(), target_bytes_per_frame_, source_bytes_per_frame_);
        return false;
    }

    // No chunk accumulation needed anymore
    reset_chunk_state();
    packets_seen_ = 0;
    bytes_seen_ = 0;
    frames_seen_ = 0;
    min_frames_seen_ = std::numeric_limits<uint32_t>::max();
    max_frames_seen_ = 0;
    last_stats_log_time_ = std::chrono::steady_clock::now();

    LOG_CPP_INFO("[WasapiCapture:%s] Active format: %u Hz, %u channels, source %u-bit (%zu bytes/frame), target %u-bit.",
                 device_tag_.c_str(),
                 active_sample_rate_,
                 active_channels_,
                 source_bits_per_sample_,
                 source_bytes_per_frame_,
                 target_bit_depth_);

    return true;
}

bool WasapiCaptureReceiver::start_stream() {
    HRESULT hr = audio_client_->Start();
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WasapiCapture:%s] Failed to start IAudioClient: 0x%lx", device_tag_.c_str(), hr);
        return false;
    }
    return true;
}

void WasapiCaptureReceiver::stop_stream() {
    if (audio_client_) {
        audio_client_->Stop();
    }
}

void WasapiCaptureReceiver::capture_loop() {
    HANDLE hThread = GetCurrentThread();
    if (!SetThreadPriority(hThread, THREAD_PRIORITY_TIME_CRITICAL)) {
        LOG_CPP_WARNING("[WasapiCapture:%s] Failed to set capture thread priority (last_error=%lu).", device_tag_.c_str(), GetLastError());
    }
    if (!mmcss_handle_) {
        mmcss_handle_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcss_task_index_);
        if (!mmcss_handle_) {
            LOG_CPP_WARNING("[WasapiCapture:%s] Failed to enter MMCSS Pro Audio on capture thread (last_error=%lu).", device_tag_.c_str(), GetLastError());
        }
    }

    HANDLE wait_handles[1] = {capture_event_};
    auto cleanup = [this]() {
        capture_queue_.stop();
        if (mmcss_handle_) {
            AvRevertMmThreadCharacteristics(mmcss_handle_);
            mmcss_handle_ = nullptr;
        }
    };

    if (!capture_event_) {
        LOG_CPP_ERROR("[WasapiCapture:%s] Capture event handle is null.", device_tag_.c_str());
        cleanup();
        return;
    }

    using PushResult = ::screamrouter::audio::utils::ThreadSafeQueue<CapturedBuffer>::PushResult;

    while (!stop_flag_) {
        DWORD wait_result = WaitForMultipleObjects(1, wait_handles, FALSE, 2000);
        if (wait_result == WAIT_TIMEOUT) {
            continue;
        }
        if (wait_result == WAIT_FAILED) {
            LOG_CPP_ERROR("[WasapiCapture:%s] WaitForMultipleObjects failed: %lu", device_tag_.c_str(), GetLastError());
            break;
        }

        while (!stop_flag_) {
            UINT32 packet_length = 0;
            HRESULT hr = capture_client_->GetNextPacketSize(&packet_length);
            if (FAILED(hr)) {
                LOG_CPP_ERROR("[WasapiCapture:%s] GetNextPacketSize failed: 0x%lx", device_tag_.c_str(), hr);
                cleanup();
                return;
            }
            if (packet_length == 0) {
                break;
            }

            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 device_position = 0;
            UINT64 qpc_position = 0;
            hr = capture_client_->GetBuffer(&data, &frames, &flags, &device_position, &qpc_position);
            if (FAILED(hr)) {
                LOG_CPP_ERROR("[WasapiCapture:%s] GetBuffer failed: 0x%lx", device_tag_.c_str(), hr);
                cleanup();
                return;
            }

            CapturedBuffer captured;
            captured.frames = frames;
            captured.flags = flags;
            captured.device_position = device_position;
            captured.qpc_position = qpc_position;

            const size_t copy_bytes = static_cast<size_t>(frames) * source_bytes_per_frame_;
            if (copy_bytes > 0) {
                captured.data.resize(copy_bytes);
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    std::memset(captured.data.data(), 0, copy_bytes);
                } else {
                    std::memcpy(captured.data.data(), reinterpret_cast<uint8_t*>(data), copy_bytes);
                }
            }

            hr = capture_client_->ReleaseBuffer(frames);
            if (FAILED(hr)) {
                LOG_CPP_ERROR("[WasapiCapture:%s] ReleaseBuffer failed: 0x%lx", device_tag_.c_str(), hr);
                cleanup();
                return;
            }

            auto push_result = capture_queue_.push_bounded(std::move(captured), kMaxCaptureQueueDepth, true);
            if (push_result == PushResult::QueueStopped) {
                cleanup();
                return;
            }
            if (push_result == PushResult::DroppedOldest) {
                LOG_CPP_WARNING("[WasapiCapture:%s] Capture queue full; dropping oldest packet to keep capture thread responsive.", device_tag_.c_str());
            }
        }
    }

    cleanup();
}

void WasapiCaptureReceiver::processing_loop() {
    CapturedBuffer captured;
    while (capture_queue_.pop(captured)) {
        process_packet(captured);
    }
}

void WasapiCaptureReceiver::request_capture_stop() {
    stop_flag_ = true;
    capture_queue_.stop();
    if (capture_event_) {
        SetEvent(capture_event_);
    }
}

void WasapiCaptureReceiver::join_capture_thread() {
    std::lock_guard<std::mutex> lock(capture_thread_mutex_);
    if (capture_thread_joined_ || !capture_thread_started_) {
        return;
    }
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    capture_thread_joined_ = true;
}

void WasapiCaptureReceiver::process_packet(const CapturedBuffer& captured) {
    const UINT32 frames = captured.frames;
    const DWORD flags = captured.flags;
    const BYTE* data = captured.data.empty() ? nullptr : captured.data.data();
    if (frames == 0) {
        return;
    }

    if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
        discontinuity_count_++;

        // Only log every second to avoid spam, but always reset the state
        auto now = std::chrono::steady_clock::now();
        auto time_since_last_log = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_discontinuity_log_time_).count();

        if (time_since_last_log >= 1) {
            LOG_CPP_WARNING("[WasapiCapture:%s] Data discontinuity signaled by WASAPI (%zu times in last second). Resetting capture state.",
                           device_tag_.c_str(), discontinuity_count_);
            last_discontinuity_log_time_ = now;
            discontinuity_count_ = 0;
        }

        reset_chunk_state();
    }

    if (!stream_time_initialized_) {
        stream_start_time_ = std::chrono::steady_clock::now();
        stream_start_frame_position_ = static_cast<uint64_t>(captured.device_position);
        stream_time_initialized_ = true;
    }

    if (source_format_ == SampleFormat::Float32) {
        // Convert float to 32-bit PCM.
        const size_t total_target_bytes = static_cast<size_t>(frames) * target_bytes_per_frame_;
        if (packet_buffer_.capacity() < total_target_bytes) {
            const size_t target_reserve = (max_packet_bytes_ > 0 && max_packet_bytes_ > total_target_bytes)
                                              ? max_packet_bytes_
                                              : total_target_bytes;
            packet_buffer_.reserve(target_reserve);
        }
        packet_buffer_.resize(total_target_bytes);
        uint8_t* dst_bytes = packet_buffer_.data();

        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data) {
            std::memset(dst_bytes, 0, total_target_bytes);
        } else {
            const float* src = reinterpret_cast<const float*>(data);
            int32_t* dst = reinterpret_cast<int32_t*>(dst_bytes);
            for (size_t i = 0; i < static_cast<size_t>(frames) * active_channels_; ++i) {
                float sample = src[i];
                if (sample > 1.0f) sample = 1.0f;
                if (sample < -1.0f) sample = -1.0f;
                dst[i] = static_cast<int32_t>(sample * 2147483647.0f);
            }
        }
    } else {
        // PCM input: keep native bit depth and copy as-is.
        const size_t copy_bytes = static_cast<size_t>(frames) * source_bytes_per_frame_;
        if (packet_buffer_.capacity() < copy_bytes) {
            const size_t target_reserve = (max_packet_bytes_ > 0 && max_packet_bytes_ > copy_bytes)
                                              ? max_packet_bytes_
                                              : copy_bytes;
            packet_buffer_.reserve(target_reserve);
        }
        packet_buffer_.resize(copy_bytes);
        uint8_t* dst_bytes = packet_buffer_.data();

        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data) {
            std::memset(dst_bytes, 0, copy_bytes);
        } else {
            std::memcpy(dst_bytes, reinterpret_cast<uint8_t*>(data), copy_bytes);
        }
    }

    const uint64_t device_frame_position = static_cast<uint64_t>(captured.device_position);

    // Telemetry accumulation
    packets_seen_++;
    bytes_seen_ += packet_buffer_.size();
    frames_seen_ += frames;
    min_frames_seen_ = std::min<uint32_t>(min_frames_seen_, frames);
    max_frames_seen_ = std::max<uint32_t>(max_frames_seen_, frames);

    auto now = std::chrono::steady_clock::now();
    const auto elapsed_since_log = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats_log_time_).count();
    if (elapsed_since_log >= 2000 && packets_seen_ > 0 && frames_seen_ > 0) {
        const double avg_frames = static_cast<double>(frames_seen_) / static_cast<double>(packets_seen_);
        const double avg_ms = (active_sample_rate_ > 0)
                                  ? (avg_frames / static_cast<double>(active_sample_rate_)) * 1000.0
                                  : 0.0;
        const double avg_bytes = static_cast<double>(bytes_seen_) / static_cast<double>(packets_seen_);
        LOG_CPP_INFO("[WasapiCapture:%s][telemetry] packets=%llu avg_frames=%.2f min_frames=%u max_frames=%u avg_ms=%.2f avg_bytes=%.2f buf_frames=%u buf_ms=%.2f rate=%uHz ch=%u bit_depth=%u",
                     device_tag_.c_str(),
                     static_cast<unsigned long long>(packets_seen_),
                     avg_frames,
                     min_frames_seen_ == std::numeric_limits<uint32_t>::max() ? 0u : min_frames_seen_,
                     max_frames_seen_,
                     avg_ms,
                     avg_bytes,
                     configured_buffer_frames_,
                     configured_buffer_ms_,
                     active_sample_rate_,
                     active_channels_,
                     target_bit_depth_);
        packets_seen_ = 0;
        bytes_seen_ = 0;
        frames_seen_ = 0;
        min_frames_seen_ = std::numeric_limits<uint32_t>::max();
        max_frames_seen_ = 0;
        last_stats_log_time_ = now;
    }

    // Swap out the filled buffer and keep an empty spare for the next packet.
    std::vector<uint8_t> packet_data;
    packet_data.swap(packet_buffer_);
    if (spare_buffer_.capacity() < max_packet_bytes_) {
        spare_buffer_.reserve(max_packet_bytes_);
    }
    packet_buffer_.swap(spare_buffer_);

    // Dispatch the packet directly with whatever size Windows gave us
    dispatch_chunk(std::move(packet_data), device_frame_position);
}

void WasapiCaptureReceiver::dispatch_chunk(std::vector<uint8_t>&& chunk_data, uint64_t frame_position) {
    if (chunk_data.empty()) {
        return;
    }

    TaggedAudioPacket packet;
    packet.source_tag = device_tag_;
    packet.audio_data = std::move(chunk_data); // Move to avoid per-packet copy.
    packet.received_time = std::chrono::steady_clock::now();
    // Stamp packet format from the active WASAPI settings we actually negotiated.
    int packet_sample_rate = 0;
    if (format_ptr_) {
        packet_sample_rate = static_cast<int>(format_ptr_->nSamplesPerSec);
    }
    if (packet_sample_rate <= 0) {
        packet_sample_rate = static_cast<int>(active_sample_rate_);
    }

    int packet_bit_depth = 0;
    if (active_channels_ > 0 && target_bytes_per_frame_ > 0) {
        packet_bit_depth = static_cast<int>((target_bytes_per_frame_ / active_channels_) * 8);
    }
    if (packet_bit_depth <= 0 && format_ptr_) {
        packet_bit_depth = static_cast<int>(BitsPerSample(format_ptr_));
    }
    if (packet_bit_depth <= 0) {
        packet_bit_depth = 32; // fallback to our conversion target
    }

    packet.channels = static_cast<int>(active_channels_);
    packet.sample_rate = packet_sample_rate;
    packet.bit_depth = packet_bit_depth;
    packet.playback_rate = 1.0;
    // Preserve the actual channel layout from the active format instead of forcing stereo.
    uint8_t layout1 = 0;
    uint8_t layout2 = 0;
    if (format_ptr_ && format_ptr_->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format_ptr_);
        const uint32_t mask = ext->dwChannelMask;
        layout1 = static_cast<uint8_t>(mask & 0xFFu);
        layout2 = static_cast<uint8_t>((mask >> 8) & 0xFFu);
    } else {
        layout1 = (active_channels_ == 1) ? kMonoLayout : kStereoLayout;
        layout2 = 0x00;
    }
    packet.chlayout1 = layout1;
    packet.chlayout2 = layout2;

    const uint32_t frames = FramesFromBytes(packet.audio_data.size(), target_bytes_per_frame_);
    if (stream_time_initialized_ && seconds_per_frame_ > 0.0) {
        const double frames_since_start = static_cast<double>(frame_position - stream_start_frame_position_);
        const double seconds_since_start = frames_since_start * seconds_per_frame_;
        auto offset_duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds_since_start));
        packet.received_time = stream_start_time_ + offset_duration;
    } else {
        packet.received_time = std::chrono::steady_clock::now();
    }

    packet.rtp_timestamp = static_cast<uint32_t>(frame_position & 0xFFFFFFFFu);
    running_timestamp_ = static_cast<uint32_t>((frame_position + frames) & 0xFFFFFFFFu);

    bool is_new_source = false;
    {
        std::lock_guard<std::mutex> lock(known_tags_mutex_);
        auto result = known_source_tags_.insert(device_tag_);
        is_new_source = result.second;
    }

    if (is_new_source && notification_queue_) {
        notification_queue_->push(DeviceDiscoveryNotification{device_tag_, DeviceDirection::CAPTURE, true});
    }

    {
        std::lock_guard<std::mutex> lock(seen_tags_mutex_);
        if (std::find(seen_tags_.begin(), seen_tags_.end(), device_tag_) == seen_tags_.end()) {
            seen_tags_.push_back(device_tag_);
        }
    }

    if (timeshift_manager_) {
        timeshift_manager_->add_packet(std::move(packet));
    }
}

void WasapiCaptureReceiver::reset_chunk_state() {
    // No more accumulator needed - just reset timing state
    running_timestamp_ = 0;
    stream_time_initialized_ = false;
    stream_start_frame_position_ = 0;
}

bool WasapiCaptureReceiver::resolve_endpoint_id(std::wstring& endpoint_id_w) {
    if (!capture_params_.endpoint_id.empty()) {
        endpoint_id_w = Utf8ToWide(capture_params_.endpoint_id);
        return !endpoint_id_w.empty();
    }

    if (system_audio::tag_has_prefix(device_tag_, system_audio::kWasapiCapturePrefix) ||
        system_audio::tag_has_prefix(device_tag_, system_audio::kWasapiLoopbackPrefix) ||
        system_audio::tag_has_prefix(device_tag_, system_audio::kWasapiPlaybackPrefix)) {
        std::string body = device_tag_.substr(3);
        endpoint_id_w = Utf8ToWide(body);
        return !endpoint_id_w.empty();
    }

    if (!device_enumerator_) {
        return false;
    }

    EDataFlow flow = loopback_mode_ ? eRender : eCapture;
    Microsoft::WRL::ComPtr<IMMDevice> default_device;
    HRESULT hr = device_enumerator_->GetDefaultAudioEndpoint(flow, eConsole, &default_device);
    if (FAILED(hr) || !default_device) {
        return false;
    }

    LPWSTR default_id = nullptr;
    hr = default_device->GetId(&default_id);
    if (FAILED(hr) || !default_id) {
        return false;
    }
    endpoint_id_w.assign(default_id);
    CoTaskMemFree(default_id);
    return !endpoint_id_w.empty();
}

} // namespace system_audio
} // namespace audio
} // namespace screamrouter

#endif // _WIN32
