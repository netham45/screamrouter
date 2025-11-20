#ifndef AUDIO_ENGINE_SETTINGS_H
#define AUDIO_ENGINE_SETTINGS_H

#include <cstddef>
#include <memory>

namespace screamrouter {
namespace audio {

inline constexpr std::size_t kDefaultChunkSizeBytes = 1152;
inline constexpr std::size_t kDefaultBaseFramesPerChunkMono16 = 576; // 576/(16/8) = 288 = (288/<sample rate>)ms 

class AudioEngineSettings;

inline std::size_t sanitize_chunk_size_bytes(std::size_t configured) {
    return configured > 0 ? configured : kDefaultChunkSizeBytes;
}

inline std::size_t compute_chunk_size_bytes_for_format(std::size_t frames_per_chunk, int channels, int bit_depth) {
    if (channels <= 0 || bit_depth <= 0 || (bit_depth % 8) != 0) {
        return 0;
    }
    const std::size_t bytes_per_frame = static_cast<std::size_t>(channels) * static_cast<std::size_t>(bit_depth / 8);
    return frames_per_chunk * bytes_per_frame;
}

struct TimeshiftTuning {
    long cleanup_interval_ms = 1000;
    double late_packet_threshold_ms = 10.0;
    double target_buffer_level_ms = 8.0;
    long loop_max_sleep_ms = 10;
    double max_catchup_lag_ms = 5000;
    double max_adaptive_delay_ms = 20.0;
    std::size_t max_clock_pending_packets = 64;
    double rtp_continuity_slack_seconds = 0.25;
    double rtp_session_reset_threshold_seconds = 0.2;
    double playback_ratio_max_deviation_ppm = 300.0;
    double playback_ratio_slew_ppm_per_sec = 100.0;
    double playback_ratio_kp = 5.0;
    double playback_ratio_ki = 1.0;
    double playback_ratio_integral_limit_ppm = 300.0;
    double playback_ratio_smoothing = 0.05;
    double playback_catchup_ppm_per_ms = 500.0;   // Extra speedup per ms of lateness (bounded)
    double playback_catchup_max_ppm = 200000.0;   // Allow up to ~20% speedup when very late
    double max_playout_lead_ms = 200.0;           // Clamp how far into the future we schedule playout

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
    std::size_t max_ready_chunks_per_source = 8;
    double max_input_queue_duration_ms = 0.0;
    double min_input_queue_duration_ms = 0.0;
    double max_ready_queue_duration_ms = 0.0;

    // Buffer drain control
    bool enable_adaptive_buffer_drain = true;      // Enable buffer draining feature
    double target_buffer_level_ms = ((kDefaultBaseFramesPerChunkMono16/2.0) / 48000.0 * 1000.0);          // Target buffer level in milliseconds
    double buffer_tolerance_ms = target_buffer_level_ms * 1.5;             // Don't adjust if within ±tolerance of target
    double max_speedup_factor = 1.02;             // Maximum playback speedup (1.02 = 2% faster)
    double drain_rate_ms_per_sec = 20.0;           // How many ms to drain per second (more aggressive)
    double drain_smoothing_factor = 0.9;           // Exponential smoothing factor for buffer measurements
    double buffer_measurement_interval_ms = 100.0;  // How often to check buffer levels (ms)
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
    std::size_t base_frames_per_chunk_mono16 = kDefaultBaseFramesPerChunkMono16;
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

inline std::size_t sanitize_base_frames_per_chunk(std::size_t configured_frames) {
    return configured_frames > 0 ? configured_frames : kDefaultBaseFramesPerChunkMono16;
}

inline std::size_t resolve_base_frames_per_chunk(const std::shared_ptr<AudioEngineSettings>& settings) {
    return sanitize_base_frames_per_chunk(settings ? settings->base_frames_per_chunk_mono16 : kDefaultBaseFramesPerChunkMono16);
}

inline std::size_t compute_processed_chunk_samples(std::size_t frames_per_chunk, int output_channels) {
    if (frames_per_chunk == 0 || output_channels <= 0) {
        return 0;
    }
    return frames_per_chunk * static_cast<std::size_t>(output_channels);
}

} // namespace audio
} // namespace screamrouter

#endif // AUDIO_ENGINE_SETTINGS_H
