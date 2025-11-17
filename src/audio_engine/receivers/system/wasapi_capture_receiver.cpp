#ifdef _WIN32


#include "wasapi_capture_receiver.h"

#include "../../utils/cpp_logger.h"
#include "../../input_processor/timeshift_manager.h"
#include "../../system_audio/system_audio_tags.h"
#include "../../system_audio/windows_utils.h"

#include <mmreg.h>

#include <algorithm>
#include <cstring>

namespace screamrouter {
namespace audio {
namespace system_audio {

namespace {
constexpr uint8_t kStereoLayout = 0x03;
constexpr uint8_t kMonoLayout = 0x01;

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
                           resolve_base_frames_per_chunk(timeshift_manager ? timeshift_manager->get_settings() : nullptr)),
      device_tag_(std::move(device_tag)),
      capture_params_(std::move(capture_params)),
      base_frames_per_chunk_(resolve_base_frames_per_chunk(timeshift_manager ? timeshift_manager->get_settings() : nullptr)),
      chunk_size_bytes_(resolve_chunk_size_bytes(timeshift_manager ? timeshift_manager->get_settings() : nullptr))
{
    loopback_mode_ = system_audio::tag_has_prefix(device_tag_, system_audio::kWasapiLoopbackPrefix) || capture_params_.loopback;
    exclusive_mode_ = capture_params_.exclusive_mode;
    const int configured_channels = capture_params_.channels > 0
        ? static_cast<int>(capture_params_.channels)
        : 2;
    const int configured_bit_depth = capture_params_.bit_depth == 32 ? 32 : 16;
    const auto computed_bytes = compute_chunk_size_bytes_for_format(
        base_frames_per_chunk_, configured_channels, configured_bit_depth);
    if (computed_bytes > 0) {
        chunk_size_bytes_ = computed_bytes;
    }
    if (chunk_size_bytes_ == 0) {
        chunk_size_bytes_ = resolve_chunk_size_bytes(timeshift_manager ? timeshift_manager->get_settings() : nullptr);
    }
    chunk_bytes_ = chunk_size_bytes_;
    chunk_accumulator_.reserve(chunk_size_bytes_ * 2);
}

WasapiCaptureReceiver::~WasapiCaptureReceiver() noexcept {
    stop();
}

bool WasapiCaptureReceiver::setup_socket() {
    return true;
}

void WasapiCaptureReceiver::close_socket() {
    std::lock_guard<std::mutex> lock(device_mutex_);
    stop_stream();
    close_device();
    if (com_initialized_) {
        CoUninitialize();
        com_initialized_ = false;
    }
}

size_t WasapiCaptureReceiver::get_receive_buffer_size() const {
    return chunk_size_bytes_;
}

int WasapiCaptureReceiver::get_poll_timeout_ms() const {
    return 50;
}

void WasapiCaptureReceiver::run() {
    LOG_CPP_INFO("[WasapiCapture:%s] Thread starting.", device_tag_.c_str());

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

    capture_loop();

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

    REFERENCE_TIME buffer_duration = 0;
    if (capture_params_.buffer_duration_ms > 0) {
        buffer_duration = static_cast<REFERENCE_TIME>(capture_params_.buffer_duration_ms) * 10000;
    }

    hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                   stream_flags,
                                   buffer_duration,
                                   0,
                                   format_ptr_,
                                   nullptr);
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WasapiCapture:%s] IAudioClient::Initialize failed: 0x%lx", device_tag_.c_str(), hr);
        return false;
    }

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

    const size_t format_size = sizeof(WAVEFORMATEX) + mix_format->cbSize;
    format_buffer_.resize(format_size);
    std::memcpy(format_buffer_.data(), mix_format, format_size);
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

    target_bit_depth_ = capture_params_.bit_depth == 32 ? 32 : 16;
    target_bytes_per_frame_ = (target_bit_depth_ / 8) * active_channels_;
    source_bytes_per_frame_ = format_ptr_->nBlockAlign;

    if (target_bytes_per_frame_ == 0 || source_bytes_per_frame_ == 0) {
        LOG_CPP_ERROR("[WasapiCapture:%s] Invalid frame sizing (target=%zu, source=%zu).",
                      device_tag_.c_str(), target_bytes_per_frame_, source_bytes_per_frame_);
        return false;
    }

    chunk_size_bytes_ = compute_chunk_size_bytes_for_format(
        base_frames_per_chunk_, active_channels_,
        capture_params_.bit_depth == 32 ? 32 : 16);
    if (chunk_size_bytes_ == 0) {
        chunk_size_bytes_ = target_bytes_per_frame_;
    }
    chunk_bytes_ = chunk_size_bytes_;
    if (chunk_bytes_ % target_bytes_per_frame_ != 0) {
        const size_t frames = std::max<size_t>(1, chunk_bytes_ / target_bytes_per_frame_);
        chunk_bytes_ = frames * target_bytes_per_frame_;
    }

    if (chunk_bytes_ == 0) {
        chunk_bytes_ = target_bytes_per_frame_;
    }

    chunk_accumulator_.reserve(chunk_bytes_ * 2);

    reset_chunk_state();

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
    HANDLE wait_handles[1] = {capture_event_};

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
                return;
            }

            process_packet(data, frames, flags, device_position, qpc_position);

            hr = capture_client_->ReleaseBuffer(frames);
            if (FAILED(hr)) {
                LOG_CPP_ERROR("[WasapiCapture:%s] ReleaseBuffer failed: 0x%lx", device_tag_.c_str(), hr);
                return;
            }
        }
    }
}

void WasapiCaptureReceiver::process_packet(BYTE* data, UINT32 frames, DWORD flags, UINT64 device_position, UINT64 /*qpc_position*/) {
    if (frames == 0) {
        return;
    }

    if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
        LOG_CPP_WARNING("[WasapiCapture:%s] Data discontinuity signaled by WASAPI. Resetting capture state.", device_tag_.c_str());
        reset_chunk_state();
    }

    if (!stream_time_initialized_) {
        stream_start_time_ = std::chrono::steady_clock::now();
        stream_start_frame_position_ = static_cast<uint64_t>(device_position);
        stream_time_initialized_ = true;
    }

    const size_t total_target_bytes = static_cast<size_t>(frames) * target_bytes_per_frame_;

    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        conversion_buffer_.assign(total_target_bytes, 0);
    } else if (source_format_ == SampleFormat::Float32) {
        conversion_buffer_.resize(total_target_bytes);
        const float* src = reinterpret_cast<const float*>(data);
        if (target_bit_depth_ == 16) {
            int16_t* dst = reinterpret_cast<int16_t*>(conversion_buffer_.data());
            for (size_t i = 0; i < static_cast<size_t>(frames) * active_channels_; ++i) {
                float sample = src[i];
                if (sample > 1.0f) sample = 1.0f;
                if (sample < -1.0f) sample = -1.0f;
                dst[i] = static_cast<int16_t>(sample * 32767.0f);
            }
        } else {
            int32_t* dst = reinterpret_cast<int32_t*>(conversion_buffer_.data());
            for (size_t i = 0; i < static_cast<size_t>(frames) * active_channels_; ++i) {
                float sample = src[i];
                if (sample > 1.0f) sample = 1.0f;
                if (sample < -1.0f) sample = -1.0f;
                dst[i] = static_cast<int32_t>(sample * 2147483647.0f);
            }
        }
    } else if (source_format_ == SampleFormat::Int16 && target_bit_depth_ == 32) {
        conversion_buffer_.resize(total_target_bytes);
        const int16_t* src = reinterpret_cast<const int16_t*>(data);
        int32_t* dst = reinterpret_cast<int32_t*>(conversion_buffer_.data());
        for (size_t i = 0; i < static_cast<size_t>(frames) * active_channels_; ++i) {
            dst[i] = static_cast<int32_t>(src[i]) << 16;
        }
    } else if (source_format_ == SampleFormat::Int32 && target_bit_depth_ == 16) {
        conversion_buffer_.resize(total_target_bytes);
        const int32_t* src = reinterpret_cast<const int32_t*>(data);
        int16_t* dst = reinterpret_cast<int16_t*>(conversion_buffer_.data());
        for (size_t i = 0; i < static_cast<size_t>(frames) * active_channels_; ++i) {
            dst[i] = static_cast<int16_t>(src[i] >> 16);
        }
    } else if (source_format_ == SampleFormat::Int24) {
        conversion_buffer_.resize(total_target_bytes);
        const uint8_t* src = reinterpret_cast<const uint8_t*>(data);
        if (target_bit_depth_ == 16) {
            int16_t* dst = reinterpret_cast<int16_t*>(conversion_buffer_.data());
            for (size_t i = 0; i < static_cast<size_t>(frames) * active_channels_; ++i) {
                int32_t value = (static_cast<int32_t>(src[3 * i + 2]) << 24) |
                                (static_cast<int32_t>(src[3 * i + 1]) << 16) |
                                (static_cast<int32_t>(src[3 * i]) << 8);
                dst[i] = static_cast<int16_t>(value >> 16);
            }
        } else {
            int32_t* dst = reinterpret_cast<int32_t*>(conversion_buffer_.data());
            for (size_t i = 0; i < static_cast<size_t>(frames) * active_channels_; ++i) {
                int32_t value = (static_cast<int32_t>(src[3 * i + 2]) << 24) |
                                (static_cast<int32_t>(src[3 * i + 1]) << 16) |
                                (static_cast<int32_t>(src[3 * i]) << 8);
                dst[i] = value;
            }
        }
    } else {
    const size_t copy_bytes = static_cast<size_t>(frames) * (std::min)(target_bytes_per_frame_, source_bytes_per_frame_);
        conversion_buffer_.assign(reinterpret_cast<uint8_t*>(data), reinterpret_cast<uint8_t*>(data) + copy_bytes);
        if (copy_bytes < total_target_bytes) {
            conversion_buffer_.resize(total_target_bytes, 0);
        }
    }

    const uint8_t* src_ptr = conversion_buffer_.empty() ? reinterpret_cast<uint8_t*>(data) : conversion_buffer_.data();
    const size_t bytes_from_packet = conversion_buffer_.empty()
        ? static_cast<size_t>(frames) * target_bytes_per_frame_
        : conversion_buffer_.size();

    const uint64_t device_frame_position = static_cast<uint64_t>(device_position);

    if (!accumulator_position_initialized_) {
        accumulator_frame_position_ = device_frame_position;
        accumulator_position_initialized_ = true;
    } else {
        size_t frames_in_accumulator = chunk_accumulator_.size() / target_bytes_per_frame_;
        uint64_t expected_position = accumulator_frame_position_ + frames_in_accumulator;
        if (device_frame_position != expected_position) {
            LOG_CPP_WARNING("[WasapiCapture:%s] Device position jump detected. expected=%llu actual=%llu. Resetting accumulator.",
                            device_tag_.c_str(),
                            static_cast<unsigned long long>(expected_position),
                            static_cast<unsigned long long>(device_frame_position));
            chunk_accumulator_.clear();
            accumulator_frame_position_ = device_frame_position;
        }
    }

    chunk_accumulator_.insert(chunk_accumulator_.end(), src_ptr, src_ptr + bytes_from_packet);

    while (chunk_accumulator_.size() >= chunk_bytes_) {
        std::vector<uint8_t> chunk(chunk_bytes_);
        std::copy_n(chunk_accumulator_.begin(), chunk_bytes_, chunk.begin());
        chunk_accumulator_.erase(chunk_accumulator_.begin(), chunk_accumulator_.begin() + chunk_bytes_);
        dispatch_chunk(std::move(chunk), accumulator_frame_position_);
        accumulator_frame_position_ += chunk_bytes_ / target_bytes_per_frame_;
    }
}

void WasapiCaptureReceiver::dispatch_chunk(std::vector<uint8_t>&& chunk_data, uint64_t frame_position) {
    if (chunk_data.empty()) {
        return;
    }

    TaggedAudioPacket packet;
    packet.source_tag = device_tag_;
    packet.audio_data = std::move(chunk_data);
    packet.received_time = std::chrono::steady_clock::now();
    packet.channels = static_cast<int>(active_channels_);
    packet.sample_rate = static_cast<int>(active_sample_rate_);
    packet.bit_depth = static_cast<int>(target_bit_depth_);
    packet.playback_rate = 1.0;
    packet.chlayout1 = (active_channels_ == 1) ? kMonoLayout : kStereoLayout;
    packet.chlayout2 = 0x00;

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
    chunk_accumulator_.clear();
    running_timestamp_ = 0;
    accumulator_position_initialized_ = false;
    accumulator_frame_position_ = 0;
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
