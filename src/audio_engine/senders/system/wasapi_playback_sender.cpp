#ifdef _WIN32



#include "wasapi_playback_sender.h"

#include "../../utils/cpp_logger.h"
#include "../../system_audio/windows_utils.h"

#include <mmreg.h>
#include <functiondiscoverykeys_devpkey.h>

#include <cstring>
#include <algorithm>
#include <cmath>

namespace screamrouter {
namespace audio {
namespace system_audio {

namespace {

WasapiPlaybackSender::SampleFormat IdentifyFormat(const WAVEFORMATEX* format) {
    if (!format) {
        return WasapiPlaybackSender::SampleFormat::Unknown;
    }
    WORD tag = format->wFormatTag;
    WORD bits = format->wBitsPerSample;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            return WasapiPlaybackSender::SampleFormat::Float32;
        }
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            bits = ext->Samples.wValidBitsPerSample ? ext->Samples.wValidBitsPerSample : format->wBitsPerSample;
        }
    }

    switch (bits) {
        case 16: return WasapiPlaybackSender::SampleFormat::Int16;
        case 24: return WasapiPlaybackSender::SampleFormat::Int24;
        case 32:
            if (tag == WAVE_FORMAT_IEEE_FLOAT || tag == WAVE_FORMAT_EXTENSIBLE) {
                return WasapiPlaybackSender::SampleFormat::Float32;
            }
            return WasapiPlaybackSender::SampleFormat::Int32;
        default: return WasapiPlaybackSender::SampleFormat::Unknown;
    }
}

unsigned int BitsPerSample(const WAVEFORMATEX* format) {
    if (!format) {
        return 0;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        if (ext->Samples.wValidBitsPerSample) {
            return ext->Samples.wValidBitsPerSample;
        }
    }
    return format->wBitsPerSample;
}

DWORD ChannelMaskFor(unsigned int channels) {
    switch (channels) {
        case 1: return SPEAKER_FRONT_CENTER;
        case 2: return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        case 4: return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
        case 6: return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
                        SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
        case 8: return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
                        SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT |
                        SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;
        default: return 0;
    }
}

} // namespace

WasapiPlaybackSender::WasapiPlaybackSender(const SinkMixerConfig& config)
    : config_(config) {}

WasapiPlaybackSender::~WasapiPlaybackSender() {
    close();
}

bool WasapiPlaybackSender::setup() {
    if (running_) {
        return true;
    }

    if (!initialize_com()) {
        return false;
    }

    if (!open_device()) {
        LOG_CPP_ERROR("[WasapiPlayback:%s] Failed to open device.", config_.sink_id.c_str());
        close();
        return false;
    }

    if (!configure_audio_client()) {
        LOG_CPP_ERROR("[WasapiPlayback:%s] Failed to configure audio client.", config_.sink_id.c_str());
        close();
        return false;
    }

    HRESULT hr = audio_client_->Start();
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WasapiPlayback:%s] Failed to start audio client: 0x%lx", config_.sink_id.c_str(), hr);
        close();
        return false;
    }

    running_ = true;
    return true;
}

void WasapiPlaybackSender::close() {
    if (!running_) {
        uninitialize_com();
        return;
    }

    if (audio_client_) {
        audio_client_->Stop();
    }
    if (render_event_) {
        CloseHandle(render_event_);
        render_event_ = nullptr;
    }
    render_client_.Reset();
    audio_client_.Reset();
    device_.Reset();
    device_enumerator_.Reset();
    format_buffer_.clear();
    device_format_ = nullptr;
    running_ = false;
    frames_written_.store(0, std::memory_order_release);
    playback_rate_integral_ = 0.0;
    target_delay_frames_ = 0.0;
    last_playback_rate_command_ = 1.0;

    uninitialize_com();
}

void WasapiPlaybackSender::set_playback_rate_callback(std::function<void(double)> cb) {
    playback_rate_callback_ = std::move(cb);
}

bool WasapiPlaybackSender::initialize_com() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        LOG_CPP_WARNING("[WasapiPlayback:%s] COM already initialized on different mode.", config_.sink_id.c_str());
        return true;
    }
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WasapiPlayback:%s] CoInitializeEx failed: 0x%lx", config_.sink_id.c_str(), hr);
        return false;
    }
    com_initialized_ = true;
    return true;
}

void WasapiPlaybackSender::uninitialize_com() {
    if (com_initialized_) {
        CoUninitialize();
        com_initialized_ = false;
    }
}

bool WasapiPlaybackSender::open_device() {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator_));
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WasapiPlayback:%s] Failed to create MMDeviceEnumerator: 0x%lx", config_.sink_id.c_str(), hr);
        return false;
    }

    std::wstring endpoint_id;
    if (!resolve_endpoint_id(endpoint_id)) {
        LOG_CPP_ERROR("[WasapiPlayback:%s] Unable to resolve endpoint id for tag %s.", config_.sink_id.c_str(), config_.output_ip.c_str());
        return false;
    }

    hr = device_enumerator_->GetDevice(endpoint_id.c_str(), &device_);
    if (FAILED(hr) || !device_) {
        LOG_CPP_ERROR("[WasapiPlayback:%s] GetDevice failed: 0x%lx", config_.sink_id.c_str(), hr);
        return false;
    }

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audio_client_);
    if (FAILED(hr) || !audio_client_) {
        LOG_CPP_ERROR("[WasapiPlayback:%s] Activate IAudioClient failed: 0x%lx", config_.sink_id.c_str(), hr);
        return false;
    }
    return true;
}

bool WasapiPlaybackSender::resolve_endpoint_id(std::wstring& endpoint_id) {
    const std::string& tag = config_.output_ip;
    if (tag == kWasapiDefaultPlaybackTag) {
        Microsoft::WRL::ComPtr<IMMDevice> default_device;
        HRESULT hr = device_enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &default_device);
        if (FAILED(hr) || !default_device) {
            return false;
        }
        LPWSTR id = nullptr;
        hr = default_device->GetId(&id);
        if (FAILED(hr) || !id) {
            return false;
        }
        endpoint_id.assign(id);
        CoTaskMemFree(id);
        return true;
    }
    if (tag == kWasapiDefaultLoopbackTag) {
        Microsoft::WRL::ComPtr<IMMDevice> default_device;
        HRESULT hr = device_enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &default_device);
        if (FAILED(hr) || !default_device) {
            return false;
        }
        LPWSTR id = nullptr;
        hr = default_device->GetId(&id);
        if (FAILED(hr) || !id) {
            return false;
        }
        endpoint_id.assign(id);
        CoTaskMemFree(id);
        return true;
    }
    if (tag == kWasapiDefaultCaptureTag) {
        Microsoft::WRL::ComPtr<IMMDevice> default_device;
        HRESULT hr = device_enumerator_->GetDefaultAudioEndpoint(eCapture, eConsole, &default_device);
        if (FAILED(hr) || !default_device) {
            return false;
        }
        LPWSTR id = nullptr;
        hr = default_device->GetId(&id);
        if (FAILED(hr) || !id) {
            return false;
        }
        endpoint_id.assign(id);
        CoTaskMemFree(id);
        return true;
    }
    if (tag.rfind(kWasapiPlaybackPrefix, 0) == 0) {
        endpoint_id = Utf8ToWide(tag.substr(3));
        return !endpoint_id.empty();
    }
    if (tag.rfind(kWasapiLoopbackPrefix, 0) == 0) {
        endpoint_id = Utf8ToWide(tag.substr(3));
        return !endpoint_id.empty();
    }
    if (tag.rfind(kWasapiDefaultPlaybackTag, 0) == 0 || tag == kWasapiDefaultPlaybackTag || tag == kWasapiDefaultLoopbackTag) {
        EDataFlow flow = eRender;
        Microsoft::WRL::ComPtr<IMMDevice> default_device;
        HRESULT hr = device_enumerator_->GetDefaultAudioEndpoint(flow, eConsole, &default_device);
        if (FAILED(hr) || !default_device) {
            return false;
        }
        LPWSTR id = nullptr;
        hr = default_device->GetId(&id);
        if (FAILED(hr) || !id) {
            return false;
        }
        endpoint_id.assign(id);
        CoTaskMemFree(id);
        return true;
    }
    if (tag.rfind(kWasapiDefaultCaptureTag, 0) == 0 || tag == kWasapiDefaultCaptureTag) {
        // Allow sinks to target default capture as fall-through (unlikely).
        Microsoft::WRL::ComPtr<IMMDevice> default_device;
        HRESULT hr = device_enumerator_->GetDefaultAudioEndpoint(eCapture, eConsole, &default_device);
        if (FAILED(hr) || !default_device) {
            return false;
        }
        LPWSTR id = nullptr;
        hr = default_device->GetId(&id);
        if (FAILED(hr) || !id) {
            return false;
        }
        endpoint_id.assign(id);
        CoTaskMemFree(id);
        return true;
    }
    // Fallback: assume whole tag is endpoint id.
    endpoint_id = Utf8ToWide(tag);
    return !endpoint_id.empty();
}

bool WasapiPlaybackSender::configure_audio_client() {
    WAVEFORMATEXTENSIBLE desired = {};
    bool desired_valid = build_desired_format(desired);

    WAVEFORMATEX* closest = nullptr;
    HRESULT hr = E_FAIL;
    bool use_desired = false;

    if (desired_valid) {
        hr = audio_client_->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                              reinterpret_cast<WAVEFORMATEX*>(&desired),
                                              &closest);
        if (hr == S_OK) {
            use_desired = true;
        } else if (hr != S_FALSE) {
            use_desired = false;
        }
    }

    if (use_desired) {
        choose_device_format(reinterpret_cast<WAVEFORMATEX*>(&desired), true);
    } else if (hr == S_FALSE && closest) {
        choose_device_format(closest, true);
    } else {
        LOG_CPP_WARNING("[WasapiPlayback:%s] Desired format unsupported, falling back to mix format.", config_.sink_id.c_str());
        WAVEFORMATEX* mix_format = nullptr;
        hr = audio_client_->GetMixFormat(&mix_format);
        if (FAILED(hr) || !mix_format) {
            LOG_CPP_ERROR("[WasapiPlayback:%s] GetMixFormat failed: 0x%lx", config_.sink_id.c_str(), hr);
            if (closest) {
                CoTaskMemFree(closest);
            }
            return false;
        }
        choose_device_format(mix_format, false);
        CoTaskMemFree(mix_format);
    }

    if (closest) {
        CoTaskMemFree(closest);
    }

    DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    REFERENCE_TIME buffer_duration = 0;
    HRESULT init_hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                                stream_flags,
                                                buffer_duration,
                                                0,
                                                device_format_,
                                                nullptr);
    if (FAILED(init_hr)) {
        LOG_CPP_ERROR("[WasapiPlayback:%s] Initialize failed: 0x%lx", config_.sink_id.c_str(), init_hr);
        return false;
    }

    hr = audio_client_->GetBufferSize(&buffer_frames_);
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WasapiPlayback:%s] GetBufferSize failed: 0x%lx", config_.sink_id.c_str(), hr);
        return false;
    }
    target_delay_frames_ = buffer_frames_ / 2;

    hr = audio_client_->GetService(IID_PPV_ARGS(&render_client_));
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WasapiPlayback:%s] GetService(IAudioRenderClient) failed: 0x%lx", config_.sink_id.c_str(), hr);
        return false;
    }

    if (!render_event_) {
        render_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!render_event_) {
            LOG_CPP_ERROR("[WasapiPlayback:%s] Failed to create render event handle.", config_.sink_id.c_str());
            return false;
        }
    }

    hr = audio_client_->SetEventHandle(render_event_);
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WasapiPlayback:%s] SetEventHandle failed: 0x%lx", config_.sink_id.c_str(), hr);
        return false;
    }

    update_conversion_state();
    reset_playback_counters();

    return true;
}

bool WasapiPlaybackSender::build_desired_format(WAVEFORMATEXTENSIBLE& desired) const {
    if (config_.output_channels <= 0 || config_.output_samplerate <= 0 || (config_.output_bitdepth != 16 && config_.output_bitdepth != 24 && config_.output_bitdepth != 32)) {
        return false;
    }

    desired = {};
    desired.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    desired.Format.nChannels = static_cast<WORD>(config_.output_channels);
    desired.Format.nSamplesPerSec = static_cast<DWORD>(config_.output_samplerate);
    desired.Format.wBitsPerSample = static_cast<WORD>(config_.output_bitdepth);
    desired.Format.nBlockAlign = static_cast<WORD>((desired.Format.wBitsPerSample / 8) * desired.Format.nChannels);
    desired.Format.nAvgBytesPerSec = desired.Format.nBlockAlign * desired.Format.nSamplesPerSec;
    desired.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    desired.Samples.wValidBitsPerSample = desired.Format.wBitsPerSample;
    desired.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    desired.dwChannelMask = ChannelMaskFor(desired.Format.nChannels);
    return true;
}

void WasapiPlaybackSender::choose_device_format(WAVEFORMATEX* format, bool format_supported) {
    const size_t format_size = sizeof(WAVEFORMATEX) + format->cbSize;
    format_buffer_.resize(format_size);
    std::memcpy(format_buffer_.data(), format, format_size);
    device_format_ = reinterpret_cast<WAVEFORMATEX*>(format_buffer_.data());

    device_sample_format_ = IdentifyFormat(device_format_);
    device_bits_per_sample_ = BitsPerSample(device_format_);
    channels_ = device_format_->nChannels;
    sample_rate_ = device_format_->nSamplesPerSec;
    device_bytes_per_frame_ = device_format_->nBlockAlign;

    source_bits_per_sample_ = static_cast<unsigned int>(config_.output_bitdepth);
    source_bytes_per_frame_ = static_cast<size_t>(config_.output_bitdepth / 8) * static_cast<size_t>(config_.output_channels);

    if (!format_supported) {
        // We'll need conversion to device format.
        requires_conversion_ = true;
    } else {
        requires_conversion_ = (device_bits_per_sample_ != source_bits_per_sample_) || (config_.output_channels != static_cast<int>(channels_));
    }
}

void WasapiPlaybackSender::update_conversion_state() {
    if (requires_conversion_) {
        conversion_buffer_.reserve(device_bytes_per_frame_ * 512);
    } else {
        conversion_buffer_.clear();
    }
}

void WasapiPlaybackSender::convert_frames(const uint8_t* src, UINT32 frames, BYTE* dst) {
    if (!requires_conversion_) {
        std::memcpy(dst, src, static_cast<size_t>(frames) * source_bytes_per_frame_);
        return;
    }

    const size_t samples = static_cast<size_t>(frames) * channels_;
    if (device_sample_format_ == SampleFormat::Float32) {
        float* out = reinterpret_cast<float*>(dst);
        if (source_bits_per_sample_ == 16) {
            const int16_t* in = reinterpret_cast<const int16_t*>(src);
            for (size_t i = 0; i < samples; ++i) {
                out[i] = static_cast<float>(in[i]) / 32767.0f;
            }
        } else if (source_bits_per_sample_ == 32) {
            const int32_t* in = reinterpret_cast<const int32_t*>(src);
            for (size_t i = 0; i < samples; ++i) {
                out[i] = static_cast<float>(in[i]) / 2147483647.0f;
            }
        } else {
            std::memset(dst, 0, static_cast<size_t>(frames) * device_bytes_per_frame_);
        }
    } else if (device_sample_format_ == SampleFormat::Int16 && source_bits_per_sample_ == 32) {
        const int32_t* in = reinterpret_cast<const int32_t*>(src);
        int16_t* out = reinterpret_cast<int16_t*>(dst);
        for (size_t i = 0; i < samples; ++i) {
            out[i] = static_cast<int16_t>(in[i] >> 16);
        }
    } else if (device_sample_format_ == SampleFormat::Int32 && source_bits_per_sample_ == 16) {
        const int16_t* in = reinterpret_cast<const int16_t*>(src);
        int32_t* out = reinterpret_cast<int32_t*>(dst);
        for (size_t i = 0; i < samples; ++i) {
            out[i] = static_cast<int32_t>(in[i]) << 16;
        }
    } else {
        std::memset(dst, 0, static_cast<size_t>(frames) * device_bytes_per_frame_);
    }
}

void WasapiPlaybackSender::send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>&) {
    if (!running_ || !audio_client_ || !render_client_) {
        return;
    }
    if (payload_size == 0 || source_bytes_per_frame_ == 0) {
        return;
    }

    size_t total_frames = payload_size / source_bytes_per_frame_;
    size_t frames_written = 0;

    while (frames_written < total_frames) {
        UINT32 padding = 0;
        HRESULT hr = audio_client_->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            LOG_CPP_ERROR("[WasapiPlayback:%s] GetCurrentPadding failed: 0x%lx", config_.sink_id.c_str(), hr);
            return;
        }
        UINT32 available = buffer_frames_ > padding ? buffer_frames_ - padding : 0;
        maybe_update_playback_rate(padding);
        if (available == 0) {
            if (render_event_) {
                WaitForSingleObject(render_event_, 5);
            } else {
                Sleep(2);
            }
            continue;
        }

        const size_t frames_available = static_cast<size_t>(available);
        const size_t frames_remaining = total_frames - frames_written;
        const size_t frames_to_write_sz = (std::min)(frames_available, frames_remaining);
        UINT32 frames_to_write = static_cast<UINT32>(frames_to_write_sz);
        BYTE* buffer = nullptr;
        hr = render_client_->GetBuffer(frames_to_write, &buffer);
        if (FAILED(hr)) {
            LOG_CPP_ERROR("[WasapiPlayback:%s] GetBuffer failed: 0x%lx", config_.sink_id.c_str(), hr);
            return;
        }

        const uint8_t* src_ptr = payload_data + frames_written * source_bytes_per_frame_;
        convert_frames(src_ptr, frames_to_write, buffer);

        hr = render_client_->ReleaseBuffer(frames_to_write, 0);
        if (FAILED(hr)) {
            LOG_CPP_ERROR("[WasapiPlayback:%s] ReleaseBuffer failed: 0x%lx", config_.sink_id.c_str(), hr);
            return;
        }

        if (frames_to_write > 0) {
            frames_written_.fetch_add(static_cast<std::uint64_t>(frames_to_write), std::memory_order_release);
        }

        frames_written += frames_to_write;
    }
}

void WasapiPlaybackSender::reset_playback_counters() {
    frames_written_.store(0, std::memory_order_release);
}

void WasapiPlaybackSender::maybe_update_playback_rate(UINT32 padding_frames) {
    if (!playback_rate_callback_) {
        return;
    }

    if (target_delay_frames_ <= 0.0 && buffer_frames_ > 0) {
        target_delay_frames_ = static_cast<double>(buffer_frames_) / 2.0;
    }

    const auto now = std::chrono::steady_clock::now();
    constexpr auto kUpdateInterval = std::chrono::milliseconds(20);
    if (last_rate_update_.time_since_epoch().count() != 0 &&
        now - last_rate_update_ < kUpdateInterval) {
        return;
    }
    last_rate_update_ = now;

    const double queued_frames = static_cast<double>(padding_frames);
    // Positive error => queue above target, speed up playback.
    const double error = queued_frames - target_delay_frames_;
    constexpr double kKp = 0.0005;
    constexpr double kKi = 0.000005;
    constexpr double kIntegralClamp = 12000.0;
    playback_rate_integral_ = std::clamp(playback_rate_integral_ + error, -kIntegralClamp, kIntegralClamp);

    double adjust = (kKp * error) + (kKi * playback_rate_integral_);
    constexpr double kMaxPpm = 0.0012; // ±1200 ppm
    adjust = std::clamp(adjust, -kMaxPpm, kMaxPpm);

    double desired_rate = 1.0 + adjust;
    constexpr double kMaxStep = 0.00015; // ±150 ppm per update
    const double delta = std::clamp(desired_rate - last_playback_rate_command_, -kMaxStep, kMaxStep);
    desired_rate = last_playback_rate_command_ + delta;

    constexpr double kHardClamp = 0.98;
    constexpr double kHardClampMax = 1.02;
    desired_rate = std::clamp(desired_rate, kHardClamp, kHardClampMax);

    ++rate_log_counter_;
    if (rate_log_counter_ % 100 == 0) {
        LOG_CPP_INFO("[WasapiPlayback:%s] PI rate update: padding=%u target=%.1f err=%.1f adj=%.6f rate=%.6f int=%.1f k={%.6f,%.6f} clamp_ppm=%.0f step=%.0f",
                     config_.sink_id.c_str(),
                     padding_frames,
                     target_delay_frames_,
                     error,
                     adjust,
                     desired_rate,
                     playback_rate_integral_,
                     kKp,
                     kKi,
                     kMaxPpm * 1e6,
                     kMaxStep * 1e6);
    }

    if (std::abs(desired_rate - last_playback_rate_command_) > 1e-6) {
        last_playback_rate_command_ = desired_rate;
        playback_rate_callback_(desired_rate);
    }
}

} // namespace system_audio
} // namespace audio
} // namespace screamrouter

#endif // _WIN32
