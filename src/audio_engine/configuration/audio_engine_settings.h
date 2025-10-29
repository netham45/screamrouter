#ifndef AUDIO_ENGINE_SETTINGS_H
#define AUDIO_ENGINE_SETTINGS_H

#include <cstddef>
#include <memory>

namespace screamrouter {
namespace audio {

inline constexpr std::size_t kDefaultChunkSizeBytes = 1152;

class AudioEngineSettings;

inline std::size_t sanitize_chunk_size_bytes(std::size_t configured) {
    return configured > 0 ? configured : kDefaultChunkSizeBytes;
}

struct TimeshiftTuning {
    long cleanup_interval_ms = 1000;
    double late_packet_threshold_ms = 10.0;
    double target_buffer_level_ms = 8.0;
    long loop_max_sleep_ms = 10;
    double max_catchup_lag_ms = 20.0;
    double max_adaptive_delay_ms = 200.0;
    std::size_t max_clock_pending_packets = 64;
    double rtp_continuity_slack_seconds = 0.25;
    double rtp_session_reset_threshold_seconds = 0.2;

    // --- Temporal Store / DVR defaults ---
    // Target playout delay (D) relative to now_ref; mixer follows head at D behind.
    // Future DVR tuning fields are intentionally omitted until implemented.
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
    int mp3_bitrate_kbps = 384;
    bool mp3_vbr_enabled = false;
    int mp3_output_queue_max_size = 10;
    long underrun_hold_timeout_ms = 250;
    std::size_t max_input_queue_chunks = 32;
    std::size_t min_input_queue_chunks = 4;
    std::size_t max_ready_chunks_per_source = 12;
};

struct SourceProcessorTuning {
    long command_loop_sleep_ms = 20;
    long discontinuity_threshold_ms = 100;
};

struct ProcessorTuning {
    int oversampling_factor = 1;
    float volume_smoothing_factor = 0.005f;
    float dc_filter_cutoff_hz = 20.0f;
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
    std::size_t chunk_size_bytes = kDefaultChunkSizeBytes;
    TimeshiftTuning timeshift_tuning;
    ProfilerSettings profiler;
    TelemetrySettings telemetry;
    MixerTuning mixer_tuning;
    SourceProcessorTuning source_processor_tuning;
    ProcessorTuning processor_tuning;
    SynchronizationSettings synchronization;
    SynchronizationTuning synchronization_tuning;
};

inline std::size_t resolve_chunk_size_bytes(const std::shared_ptr<AudioEngineSettings>& settings) {
    return sanitize_chunk_size_bytes(settings ? settings->chunk_size_bytes : kDefaultChunkSizeBytes);
}

inline std::size_t compute_processed_chunk_samples(std::size_t chunk_size_bytes) {
    const auto sanitized = sanitize_chunk_size_bytes(chunk_size_bytes);
    // Default assumption: 16-bit PCM converted to 32-bit samples => bytes / 2.
    return sanitized / 2;
}

} // namespace audio
} // namespace screamrouter

#endif // AUDIO_ENGINE_SETTINGS_H
