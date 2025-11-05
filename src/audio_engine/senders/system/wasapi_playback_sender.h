#pragma once

#ifdef _WIN32



#include "../i_network_sender.h"
#include "hardware_clock_consumer.h"
#include "../../audio_types.h"
#include "../../system_audio/system_audio_tags.h"

#include <windows.h>
#include <wrl/client.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <cstdint>

namespace screamrouter {
namespace audio {
namespace system_audio {

class WasapiPlaybackSender : public INetworkSender, public IHardwareClockConsumer {
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
    bool start_hardware_clock(ClockManager* clock_manager,
                              const ClockManager::ConditionHandle& handle,
                              std::uint32_t frames_per_tick) override;
    void stop_hardware_clock() override;

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
    void hardware_clock_loop();
    void reset_hardware_clock_state();

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

    std::mutex clock_mutex_;
    ClockManager* clock_manager_ = nullptr;
    ClockManager::ConditionHandle clock_handle_{};
    std::uint32_t frames_per_tick_ = 0;
    std::uint64_t last_clock_position_ = 0;
    std::uint64_t residual_frames_ = 0;
    std::atomic<bool> clock_thread_running_{false};
    std::atomic<bool> clock_thread_stop_{false};
    std::thread clock_thread_;
    std::atomic<std::uint64_t> frames_written_{0};
};

} // namespace system_audio
} // namespace audio
} // namespace screamrouter

#endif // _WIN32
