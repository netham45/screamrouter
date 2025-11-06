#pragma once

#include "../i_network_sender.h"
#include "../../audio_types.h"

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <atomic>
#include <cstdint>

#if defined(__linux__)
#include <alsa/asoundlib.h>
#endif

namespace screamrouter {
namespace audio {

class AlsaPlaybackSender : public INetworkSender {
public:
    explicit AlsaPlaybackSender(const SinkMixerConfig& config);
    ~AlsaPlaybackSender() override;

    bool setup() override;
    void close() override;
    void send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) override;

#if defined(__linux__)
    unsigned int get_effective_sample_rate() const;
    unsigned int get_effective_channels() const;
    unsigned int get_effective_bit_depth() const;
    bool is_actively_playing() const;
#endif

private:
#if defined(__linux__)
    bool parse_legacy_card_device(const std::string& value, int& card, int& device) const;
    std::string resolve_alsa_device_name() const;
    bool configure_device();
    bool handle_write_error(int err);
    bool write_frames(const void* data, size_t frame_count, size_t bytes_per_frame);
    bool detect_xrun_locked();
    void close_locked();
    void maybe_log_telemetry_locked();

    SinkMixerConfig config_;
    std::string device_tag_;
    std::string hw_device_name_;

    snd_pcm_t* pcm_handle_ = nullptr;
    unsigned int sample_rate_ = 0;
    unsigned int channels_ = 0;
    int bit_depth_ = 0;
    unsigned int hardware_bit_depth_ = 0;
    snd_pcm_format_t sample_format_ = SND_PCM_FORMAT_UNKNOWN;
    snd_pcm_uframes_t period_frames_ = 0;
    snd_pcm_uframes_t buffer_frames_ = 0;
    size_t bytes_per_frame_ = 0;
    bool is_raspberry_pi_ = false;

    mutable std::mutex state_mutex_;
    std::chrono::steady_clock::time_point telemetry_last_log_time_{};
    std::atomic<uint64_t> frames_written_{0};
#else
    SinkMixerConfig config_;
#endif
};

} // namespace audio
} // namespace screamrouter
