/**
 * @file sink_rate_controller.h
 * @brief Rate control helper class for SinkAudioMixer.
 * @details Manages adaptive buffer drain and playback rate adjustments.
 */
#ifndef SINK_RATE_CONTROLLER_H
#define SINK_RATE_CONTROLLER_H

#include "../configuration/audio_engine_settings.h"
#include <string>
#include <map>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>

namespace screamrouter {
namespace audio {

class SourceInputProcessor;

/**
 * @struct InputBufferMetrics
 * @brief Metrics for input buffer backlog analysis.
 */
struct InputBufferMetrics {
    double total_ms = 0.0;
    double avg_per_source_ms = 0.0;
    double max_per_source_ms = 0.0;
    std::size_t queued_blocks = 0;
    std::size_t active_sources = 0;
    double block_duration_ms = 0.0;
    bool valid = false;
    std::map<std::string, std::size_t> per_source_blocks;
    std::map<std::string, double> per_source_ms;
};

/**
 * @class SinkRateController
 * @brief Manages adaptive playback rate adjustments based on buffer levels.
 * @details Implements buffer drain control to prevent overflow/underflow.
 */
class SinkRateController {
public:
    using RateCommandCallback = std::function<void(const std::string& instance_id, double ratio)>;
    
    /**
     * @brief Constructs a SinkRateController.
     * @param sink_id Identifier for logging.
     * @param settings Audio engine settings for tuning parameters.
     */
    SinkRateController(const std::string& sink_id,
                       std::shared_ptr<AudioEngineSettings> settings);
    
    ~SinkRateController() = default;
    
    // Non-copyable
    SinkRateController(const SinkRateController&) = delete;
    SinkRateController& operator=(const SinkRateController&) = delete;
    
    /**
     * @brief Sets the callback for sending rate commands to source processors.
     * @param callback Function to call when rate needs adjustment.
     */
    void set_rate_command_callback(RateCommandCallback callback);
    
    /**
     * @brief Updates drain ratio based on current buffer metrics.
     * @param sample_rate Current playback sample rate.
     * @param frames_per_chunk Frames per audio chunk.
     * @param get_metrics Callback to retrieve current buffer metrics.
     */
    void update_drain_ratio(int sample_rate, std::size_t frames_per_chunk,
                           std::function<InputBufferMetrics()> get_metrics);
    
    /**
     * @brief Removes tracking for a source that was removed.
     * @param instance_id The source instance ID.
     */
    void remove_source(const std::string& instance_id);
    
    /**
     * @brief Gets the smoothed buffer level.
     * @return Smoothed buffer level in milliseconds.
     */
    double get_smoothed_buffer_level_ms() const {
        return smoothed_buffer_level_ms_.load();
    }

private:
    double calculate_drain_ratio_for_level(double buffer_ms, double block_duration_ms) const;
    void dispatch_drain_adjustments(const InputBufferMetrics& metrics, double alpha);
    
    std::string sink_id_;
    std::shared_ptr<AudioEngineSettings> settings_;
    
    std::atomic<double> smoothed_buffer_level_ms_{0.0};
    std::chrono::steady_clock::time_point last_drain_check_;
    
    std::mutex mutex_;
    std::unordered_map<std::string, double> per_source_smoothed_buffer_ms_;
    std::unordered_map<std::string, double> source_last_rate_command_;
    
    RateCommandCallback rate_command_callback_;
};

} // namespace audio
} // namespace screamrouter

#endif // SINK_RATE_CONTROLLER_H
