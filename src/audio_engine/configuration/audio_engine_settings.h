#ifndef AUDIO_ENGINE_SETTINGS_H
#define AUDIO_ENGINE_SETTINGS_H

#include <cstddef>

namespace screamrouter {
namespace audio {

struct TimeshiftTuning {
    long cleanup_interval_ms = 1000;
    long reanchor_interval_sec = 5;
    double jitter_smoothing_factor = 16.0;
    double jitter_safety_margin_multiplier = 2.0;
    double system_jitter_safety_multiplier = 1.0;
    double late_packet_threshold_ms = 10.0;
    double target_buffer_level_ms = 8.0;
    double target_recovery_rate_ms_per_sec = 15.0;
    double proportional_gain_kp = 0.05;
    double catchup_boost_gain = 2.0;
    double min_playback_rate = 0.90;
    double max_playback_rate = 1.10;
    double absolute_max_playback_rate = 1.25;
    double system_jitter_gain = 0.05;
    double system_delay_ratio_cap = 3.0;
    long loop_max_sleep_ms = 10;
    double max_catchup_lag_ms = 20.0;
    double max_jitter_ms = 150.0;
    double jitter_decay_factor = 0.25;
    double jitter_decay_stable_threshold_ms = 2.0;
    int jitter_decay_stable_packet_window = 50;
    long jitter_idle_decay_interval_ms = 500;
    double jitter_idle_decay_factor = 0.1;
    double max_adaptive_delay_ms = 200.0;
    std::size_t max_processor_queue_packets = 128;

    // --- Temporal Store / DVR defaults ---
    // Target playout delay (D) relative to now_ref; mixer follows head at D behind.
    long target_playout_delay_ms = 200;  // auto-tune in future
    // Commit guard: distance behind (now + D) before we consider items immutable/committed.
    long commit_guard_ms = 24;           // ~2 × 12ms chunk by default
    // DVR retention window in seconds (ring buffer duration).
    long dvr_retention_sec = 300;        // 5 minutes
    // Segment duration for durable window (ms); used if/when segmenting to disk.
    long dvr_segment_ms = 250;           // audio-only default
};

struct ProfilerSettings {
    bool enabled = true;
    long log_interval_ms = 1000;
};

struct TelemetrySettings {
    bool enabled = true;
    long log_interval_ms = 30000;
};

struct MixerTuning {
    long grace_period_timeout_ms = 12;
    long grace_period_poll_interval_ms = 1;
    int mp3_bitrate_kbps = 192;
    bool mp3_vbr_enabled = false;
    int mp3_output_queue_max_size = 10;
    long underrun_hold_timeout_ms = 250;
    double host_jitter_skip_threshold_ms = 1.5;
    double host_jitter_skip_grace_ms = 24.0;
    std::size_t max_input_queue_chunks = 16;
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
    TelemetrySettings telemetry;
    MixerTuning mixer_tuning;
    SourceProcessorTuning source_processor_tuning;
    ProcessorTuning processor_tuning;
    SynchronizationSettings synchronization;
    SynchronizationTuning synchronization_tuning;
};

} // namespace audio
} // namespace screamrouter

#endif // AUDIO_ENGINE_SETTINGS_H
