#pragma once

#ifdef _WIN32



#include "../i_network_sender.h"
#include "../../audio_types.h"
#include "../../system_audio/system_audio_tags.h"

#include <windows.h>
#include <wrl/client.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>
#include <functional>
#include <chrono>

namespace screamrouter {
namespace audio {
namespace system_audio {

class WasapiPlaybackSender : public INetworkSender {
public:
    enum class SampleFormat {
        Int16,
        Int24,
        Int32,
        Float32,
        Unknown
    };

    explicit WasapiPlaybackSender(const SinkMixerConfig& config);
    ~WasapiPlaybackSender() override;

    bool setup() override;
    void close() override;
    void send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) override;
    void set_playback_rate_callback(std::function<void(double)> cb);
    void update_pipeline_backlog(double upstream_frames, double upstream_target_frames);

private:

    bool initialize_com();
    void uninitialize_com();
    bool open_device();
    bool resolve_endpoint_id(std::wstring& endpoint_id);
    bool configure_audio_client();
    bool build_desired_format(WAVEFORMATEXTENSIBLE& desired) const;
    void choose_device_format(WAVEFORMATEX* mix_format, bool format_supported);
    void update_conversion_state();
    void convert_frames(const uint8_t* src, UINT32 frames, BYTE* dst);
    void reset_playback_counters();
    void maybe_update_playback_rate(UINT32 padding_frames);

    SinkMixerConfig config_;

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> device_enumerator_;
    Microsoft::WRL::ComPtr<IMMDevice> device_;
    Microsoft::WRL::ComPtr<IAudioClient> audio_client_;
    Microsoft::WRL::ComPtr<IAudioRenderClient> render_client_;

    HANDLE render_event_ = nullptr;
    bool com_initialized_ = false;
    bool running_ = false;

    std::vector<uint8_t> format_buffer_;
    WAVEFORMATEX* device_format_ = nullptr;

    SampleFormat device_sample_format_ = SampleFormat::Unknown;
    SampleFormat source_sample_format_ = SampleFormat::Unknown;
    unsigned int device_bits_per_sample_ = 0;
    unsigned int source_bits_per_sample_ = 0;
    unsigned int channels_ = 0;
    unsigned int sample_rate_ = 0;
    size_t source_bytes_per_frame_ = 0;
    size_t device_bytes_per_frame_ = 0;
    bool requires_conversion_ = false;

    UINT32 buffer_frames_ = 0;
    std::vector<uint8_t> conversion_buffer_;

    std::atomic<std::uint64_t> frames_written_{0};
    std::function<void(double)> playback_rate_callback_;
    double playback_rate_integral_ = 0.0;
    double target_delay_frames_ = 0.0;
    double upstream_buffer_frames_ = 0.0;
    double upstream_target_frames_ = 0.0;
    double last_playback_rate_command_ = 1.0;
    std::chrono::steady_clock::time_point last_rate_update_;
    uint64_t rate_log_counter_ = 0;
    double filtered_padding_frames_ = 0.0;
};

} // namespace system_audio
} // namespace audio
} // namespace screamrouter

#endif // _WIN32
