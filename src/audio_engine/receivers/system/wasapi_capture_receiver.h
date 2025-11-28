#pragma once

#ifdef _WIN32

#define SCREAMROUTER_WASAPI_CAPTURE_AVAILABLE 1

#include "../network_audio_receiver.h"
#include "../../configuration/audio_engine_settings.h"

#include <windows.h>
#include <wrl/client.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <chrono>
#include <thread>

#include "../../utils/thread_safe_queue.h"

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
    void processing_loop();
    void request_capture_stop();
    void join_capture_thread();
    bool resolve_endpoint_id(std::wstring& endpoint_id_w);
    struct CapturedBuffer {
        std::vector<uint8_t> data;
        UINT32 frames = 0;
        DWORD flags = 0;
        uint64_t device_position = 0;
        uint64_t qpc_position = 0;
    };
    void process_packet(const CapturedBuffer& captured);
    void dispatch_chunk(std::vector<uint8_t>&& chunk_data, uint64_t frame_position);
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
    size_t max_packet_bytes_ = 0;
    // Reusable buffers to avoid per-packet reallocations.
    std::vector<uint8_t> packet_buffer_;
    std::vector<uint8_t> spare_buffer_;
    UINT32 configured_buffer_frames_ = 0;
    double configured_buffer_ms_ = 0.0;

    // Telemetry
    uint64_t packets_seen_ = 0;
    uint64_t bytes_seen_ = 0;
    uint64_t frames_seen_ = 0;
    uint32_t min_frames_seen_ = 0;
    uint32_t max_frames_seen_ = 0;
    std::chrono::steady_clock::time_point last_stats_log_time_{};

    uint32_t running_timestamp_ = 0;
    bool stream_time_initialized_ = false;
    std::chrono::steady_clock::time_point stream_start_time_{};
    uint64_t stream_start_frame_position_ = 0;
    double seconds_per_frame_ = 0.0;

    // Discontinuity tracking for throttled logging
    std::chrono::steady_clock::time_point last_discontinuity_log_time_{};
    size_t discontinuity_count_ = 0;

    ::screamrouter::audio::utils::ThreadSafeQueue<CapturedBuffer> capture_queue_;
    std::thread capture_thread_;
    std::mutex capture_thread_mutex_;
    bool capture_thread_started_ = false;
    bool capture_thread_joined_ = false;
    bool cleanup_started_ = false;

    std::mutex device_mutex_;

    HANDLE mmcss_handle_ = nullptr;
    DWORD mmcss_task_index_ = 0;
};

} // namespace system_audio
} // namespace audio
} // namespace screamrouter

#endif // _WIN32

#ifndef SCREAMROUTER_WASAPI_CAPTURE_AVAILABLE
#define SCREAMROUTER_WASAPI_CAPTURE_AVAILABLE 0
#endif
