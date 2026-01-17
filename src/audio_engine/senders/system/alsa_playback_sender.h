#pragma once

#include "../i_network_sender.h"
#include "../../audio_types.h"
#include "../../configuration/audio_engine_settings.h"

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#if defined(__linux__)
#include <alsa/asoundlib.h>
#endif

namespace screamrouter {
namespace audio {

class AlsaPlaybackSender : public INetworkSender {
public:
    AlsaPlaybackSender(const SinkMixerConfig& config,
                       std::shared_ptr<AudioEngineSettings> settings);
    ~AlsaPlaybackSender() override;

    bool setup() override;
    void close() override;
    void send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) override;
    void set_playback_rate_callback(std::function<void(double)> cb);
    
    /**
     * @brief Callback to report hardware buffer state upstream for unified rate control.
     * @param hw_fill_ms Current hardware buffer fill level in milliseconds.
     * @param hw_target_ms Target hardware buffer level in milliseconds.
     */
    using BufferStateCallback = std::function<void(double hw_fill_ms, double hw_target_ms)>;
    void set_buffer_state_callback(BufferStateCallback cb);
    
    void update_pipeline_backlog(double upstream_frames, double upstream_target_frames);
    using FormatChangeCallback = std::function<void(unsigned int device_rate,
                                                   unsigned int device_channels,
                                                   unsigned int device_bit_depth)>;
    void set_format_change_callback(FormatChangeCallback cb);
    void update_source_format(unsigned int sample_rate, unsigned int channels, unsigned int bit_depth);

#if defined(__linux__)
    unsigned int get_effective_sample_rate() const;
    unsigned int get_effective_channels() const;
    unsigned int get_effective_bit_depth() const;
#endif

private:
    SinkMixerConfig config_;
    std::shared_ptr<AudioEngineSettings> settings_;

#if defined(__linux__)
    bool parse_legacy_card_device(const std::string& value, int& card, int& device) const;
    std::string resolve_alsa_device_name() const;
    struct FormatNotification {
        bool valid = false;
        unsigned int sample_rate = 0;
        unsigned int channels = 0;
        unsigned int bit_depth = 0;
    };
    bool configure_device();
    bool handle_write_error(int err);
    bool write_frames(const void* data, size_t frame_count, size_t bytes_per_frame);
    bool detect_xrun_locked();
    void close_locked();
    void maybe_log_telemetry_locked();
    void maybe_update_playback_rate_locked(snd_pcm_sframes_t delay_frames);
    void prefill_target_delay_locked();
    void maybe_adjust_dynamic_latency_locked(double filtered_delay_frames,
                                             double current_delay_frames,
                                             double dt_sec);
    void schedule_dynamic_latency_reconfigure_locked(double desired_latency_ms,
                                                     bool force_reconfigure);
    void apply_pending_dynamic_latency_locked();
    void handle_dynamic_latency_xrun_locked();
    bool send_payload_locked(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs);
    void dispatch_format_notification(const FormatNotification& note);
    FormatNotification consume_pending_format_notification();
    void update_source_format_locked(unsigned int sample_rate, unsigned int channels, unsigned int bit_depth);

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
    unsigned int source_sample_rate_ = 0;
    unsigned int source_channels_ = 0;
    unsigned int source_bit_depth_ = 0;
    size_t source_bytes_per_frame_ = 0;
    bool is_raspberry_pi_ = false;
    std::function<void(double)> playback_rate_callback_;
    BufferStateCallback buffer_state_callback_;
    FormatChangeCallback format_change_callback_;
    FormatNotification pending_format_notification_{};
    bool format_notification_pending_ = false;
    double playback_rate_integral_ = 0.0;
    double target_delay_frames_ = 0.0;
    double upstream_buffer_frames_ = 0.0;
    double upstream_target_frames_ = 0.0;
    double last_playback_rate_command_ = 1.0;
    std::chrono::steady_clock::time_point last_rate_update_;
    uint64_t rate_log_counter_ = 0;
    double filtered_delay_frames_ = 0.0;
    double dynamic_latency_target_ms_ = 0.0;
    double dynamic_latency_applied_ms_ = 0.0;
    std::chrono::steady_clock::time_point dynamic_latency_last_reconfig_{};
    std::chrono::steady_clock::time_point dynamic_latency_last_log_{};
    bool dynamic_latency_reconfigure_pending_ = false;
    double dynamic_latency_pending_latency_ms_ = 0.0;

    mutable std::mutex state_mutex_;
    std::chrono::steady_clock::time_point telemetry_last_log_time_{};
    std::atomic<uint64_t> frames_written_{0};
#endif
};

} // namespace audio
} // namespace screamrouter
