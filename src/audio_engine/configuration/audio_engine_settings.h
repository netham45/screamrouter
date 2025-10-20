#ifndef AUDIO_ENGINE_SETTINGS_H
#define AUDIO_ENGINE_SETTINGS_H

namespace screamrouter {
namespace audio {

struct TimeshiftTuning {
    long cleanup_interval_ms = 1000;
    long reanchor_interval_sec = 5;
    double jitter_smoothing_factor = 16.0;
    double jitter_safety_margin_multiplier = 2.0;
    double late_packet_threshold_ms = 10.0;
    double target_buffer_level_ms = 15.0;
    double proportional_gain_kp = 0.0005;
    double min_playback_rate = 0.98;
    double max_playback_rate = 1.02;
    long loop_max_sleep_ms = 10;
    double max_catchup_lag_ms = 20.0;
};

struct ProfilerSettings {
    bool enabled = true;
    long log_interval_ms = 1000;
};

struct MixerTuning {
    long grace_period_timeout_ms = 12;
    long grace_period_poll_interval_ms = 1;
    int mp3_bitrate_kbps = 192;
    bool mp3_vbr_enabled = false;
    int mp3_output_queue_max_size = 10;
};

struct SourceProcessorTuning {
    long command_loop_sleep_ms = 20;
};

struct ProcessorTuning {
    int oversampling_factor = 1;
    float volume_smoothing_factor = 0.005f;
    float dc_filter_cutoff_hz = 20.0f;
    // Soft Clipper
    float soft_clip_threshold = 0.8f;
    float soft_clip_knee = 0.2f;
    // Volume Normalization
    float normalization_target_rms = 0.1f;
    float normalization_attack_smoothing = 0.2f;
    float normalization_decay_smoothing = 0.05f;
    // Dithering
    float dither_noise_shaping_factor = 0.25f;
};

struct SynchronizationSettings {
    bool enable_multi_sink_sync = false;
};

struct SynchronizationTuning {
    int barrier_timeout_ms = 50;
    double sync_proportional_gain = 0.01;
    double max_rate_adjustment = 0.02;
    double sync_smoothing_factor = 0.9;
};

class AudioEngineSettings {
public:
    TimeshiftTuning timeshift_tuning;
    ProfilerSettings profiler;
    MixerTuning mixer_tuning;
    SourceProcessorTuning source_processor_tuning;
    ProcessorTuning processor_tuning;
    SynchronizationSettings synchronization;
    SynchronizationTuning synchronization_tuning;
};

} // namespace audio
} // namespace screamrouter

#endif // AUDIO_ENGINE_SETTINGS_H
