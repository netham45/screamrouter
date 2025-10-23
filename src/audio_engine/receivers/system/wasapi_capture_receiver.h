#pragma once

#ifdef _WIN32

#define SCREAMROUTER_WASAPI_CAPTURE_AVAILABLE 1

#include "../network_audio_receiver.h"

#include <windows.h>
#include <wrl/client.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <string>
#include <vector>
#include <mutex>
#include <chrono>

namespace screamrouter {
namespace audio {
namespace system_audio {

class WasapiCaptureReceiver : public NetworkAudioReceiver {
public:
    enum class SampleFormat {
        Int16,
        Int24,
        Int32,
        Float32,
        Unknown
    };

    static constexpr size_t kChunkSize = 1152;

    WasapiCaptureReceiver(std::string device_tag,
                          CaptureParams capture_params,
                          std::shared_ptr<NotificationQueue> notification_queue,
                          TimeshiftManager* timeshift_manager);
    ~WasapiCaptureReceiver() noexcept override;

protected:
    bool setup_socket() override;
    void close_socket() override;
    void run() override;

    bool is_valid_packet_structure(const uint8_t*, int, const struct sockaddr_in&) override { return true; }
    bool process_and_validate_payload(const uint8_t*,
                                      int,
                                      const struct sockaddr_in&,
                                      std::chrono::steady_clock::time_point,
                                      TaggedAudioPacket&,
                                      std::string&) override { return true; }
    size_t get_receive_buffer_size() const override;
    int get_poll_timeout_ms() const override;

private:

    bool open_device();
    void close_device();
    bool configure_audio_client();
    bool initialize_capture_format(WAVEFORMATEX* mix_format);
    bool start_stream();
    void stop_stream();
    void capture_loop();
    bool resolve_endpoint_id(std::wstring& endpoint_id_w);
    void process_packet(BYTE* data, UINT32 frames, DWORD flags);
    void dispatch_chunk(std::vector<uint8_t>&& chunk_data);
    void reset_chunk_state();

    std::string device_tag_;
    CaptureParams capture_params_;
    bool loopback_mode_ = false;
    bool exclusive_mode_ = false;

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> device_enumerator_;
    Microsoft::WRL::ComPtr<IMMDevice> device_;
    Microsoft::WRL::ComPtr<IAudioClient> audio_client_;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> capture_client_;

    HANDLE capture_event_ = nullptr;
    bool com_initialized_ = false;

    std::vector<uint8_t> format_buffer_;
    WAVEFORMATEX* format_ptr_ = nullptr;

    SampleFormat source_format_ = SampleFormat::Unknown;
    unsigned int source_bits_per_sample_ = 0;
    unsigned int target_bit_depth_ = 16;
    unsigned int active_channels_ = 0;
    unsigned int active_sample_rate_ = 48000;
    size_t source_bytes_per_frame_ = 0;
    size_t target_bytes_per_frame_ = 0;
    size_t chunk_bytes_ = kChunkSize;

    std::vector<uint8_t> chunk_accumulator_;
    std::vector<uint8_t> conversion_buffer_;

    uint32_t running_timestamp_ = 0;
    bool next_chunk_time_initialized_ = false;
    std::chrono::steady_clock::time_point next_chunk_time_{};

    std::mutex device_mutex_;
};

} // namespace system_audio
} // namespace audio
} // namespace screamrouter

#endif // _WIN32

#ifndef SCREAMROUTER_WASAPI_CAPTURE_AVAILABLE
#define SCREAMROUTER_WASAPI_CAPTURE_AVAILABLE 0
#endif
