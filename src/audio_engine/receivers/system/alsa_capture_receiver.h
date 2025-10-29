#pragma once

#include "../network_audio_receiver.h"

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>

#include "../../utils/byte_ring_buffer.h"

#if defined(__linux__) && defined(__has_include)
#  if __has_include(<alsa/asoundlib.h>)
#    define SCREAMROUTER_ALSA_CAPTURE_AVAILABLE 1
#  else
#    define SCREAMROUTER_ALSA_CAPTURE_AVAILABLE 0
#  endif
#elif defined(__linux__)
#  define SCREAMROUTER_ALSA_CAPTURE_AVAILABLE 1
#else
#  define SCREAMROUTER_ALSA_CAPTURE_AVAILABLE 0
#endif

#if SCREAMROUTER_ALSA_CAPTURE_AVAILABLE
#include <alsa/asoundlib.h>
#endif

namespace screamrouter {
namespace audio {

class AlsaCaptureReceiver : public NetworkAudioReceiver {
public:
    AlsaCaptureReceiver(
        std::string device_tag,
        CaptureParams capture_params,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager);

    ~AlsaCaptureReceiver() noexcept override;

protected:
    bool setup_socket() override;
    void close_socket() override;
    void run() override;

    bool is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) override;
    bool process_and_validate_payload(
        const uint8_t* buffer,
        int size,
        const struct sockaddr_in& client_addr,
        std::chrono::steady_clock::time_point received_time,
        TaggedAudioPacket& out_packet,
        std::string& out_source_tag) override;
    size_t get_receive_buffer_size() const override;
    int get_poll_timeout_ms() const override;

private:
#if SCREAMROUTER_ALSA_CAPTURE_AVAILABLE
    bool open_device_locked();
    void close_device_locked();
    bool recover_from_error(int err);
    void process_captured_frames(size_t frames_captured);
    void dispatch_chunk(std::vector<uint8_t>&& chunk_data);
    std::string resolve_hw_id() const;

    std::string device_tag_;
    CaptureParams capture_params_;
    std::string hw_device_name_;
    const std::size_t chunk_size_bytes_;

    snd_pcm_t* pcm_handle_ = nullptr;
    snd_pcm_format_t sample_format_ = SND_PCM_FORMAT_UNKNOWN;
    unsigned int active_sample_rate_ = 48000;
    unsigned int active_channels_ = 2;
    unsigned int active_bit_depth_ = 16;
    snd_pcm_uframes_t period_frames_ = 0;
    snd_pcm_uframes_t buffer_frames_ = 0;
    size_t bytes_per_sample_ = 0;
    size_t bytes_per_frame_ = 0;
    size_t chunk_bytes_ = 0;
    uint32_t running_timestamp_ = 0;

    std::vector<uint8_t> period_buffer_;
    ::screamrouter::audio::utils::ByteRingBuffer chunk_buffer_;

    std::mutex device_mutex_;
#else
    std::string device_tag_;
    CaptureParams capture_params_;
#endif
};

} // namespace audio
} // namespace screamrouter
