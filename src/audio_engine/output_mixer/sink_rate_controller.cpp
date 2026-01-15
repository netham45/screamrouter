/**
 * @file sink_rate_controller.cpp
 * @brief Implementation of rate control for SinkAudioMixer.
 */
#include "sink_rate_controller.h"
#include "../utils/cpp_logger.h"
#include "../utils/profiler.h"
#include <algorithm>
#include <cmath>

namespace screamrouter {
namespace audio {

SinkRateController::SinkRateController(const std::string& sink_id,
                                       std::shared_ptr<AudioEngineSettings> settings)
    : sink_id_(sink_id),
      settings_(settings),
      last_drain_check_(std::chrono::steady_clock::now())
{
}

void SinkRateController::set_rate_command_callback(RateCommandCallback callback) {
    rate_command_callback_ = std::move(callback);
}

void SinkRateController::update_drain_ratio(int sample_rate, std::size_t frames_per_chunk,
                                            std::function<InputBufferMetrics()> get_metrics) {
    PROFILE_FUNCTION();
    auto now = std::chrono::steady_clock::now();
    
    if (!settings_) {
        return;
    }
    
    // Only update periodically
    auto elapsed = std::chrono::duration<double, std::milli>(now - last_drain_check_).count();
    
    if (elapsed < settings_->mixer_tuning.buffer_measurement_interval_ms) {
        return;
    }
    
    last_drain_check_ = now;
    
    InputBufferMetrics metrics = get_metrics();
    if (!metrics.valid) {
        LOG_CPP_WARNING("[RateControl:%s] Unable to evaluate input buffer backlog (invalid timing parameters).",
                        sink_id_.c_str());
        return;
    }
    
    double buffer_ms = metrics.total_ms;
    
    LOG_CPP_DEBUG("[RateControl:%s] Input backlog: total=%.2fms avg=%.2fms max=%.2fms blocks=%zu sources=%zu",
                  sink_id_.c_str(), buffer_ms, metrics.avg_per_source_ms, metrics.max_per_source_ms,
                  metrics.queued_blocks, metrics.active_sources);
    
    double alpha = 1.0 - settings_->mixer_tuning.drain_smoothing_factor;
    double prev_smoothed = smoothed_buffer_level_ms_.load();
    double smoothed = prev_smoothed * (1.0 - alpha) + buffer_ms * alpha;
    smoothed_buffer_level_ms_.store(smoothed);
    
    LOG_CPP_DEBUG("[RateControl:%s] Smoothing: prev=%.2fms, raw=%.2fms, alpha=%.3f -> new=%.2fms",
                  sink_id_.c_str(), prev_smoothed, buffer_ms, alpha, smoothed);
    
    dispatch_drain_adjustments(metrics, alpha);
}

void SinkRateController::remove_source(const std::string& instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    per_source_smoothed_buffer_ms_.erase(instance_id);
    source_last_rate_command_.erase(instance_id);
}

double SinkRateController::calculate_drain_ratio_for_level(double buffer_ms, double block_duration_ms) const {
    PROFILE_FUNCTION();
    if (!settings_ || block_duration_ms <= 0.0) {
        return 1.0;
    }
    
    const auto& tuning = settings_->mixer_tuning;
    if (!tuning.enable_adaptive_buffer_drain) {
        return 1.0;
    }
    
    const double blocks = buffer_ms / block_duration_ms;
    // Derive targets in blocks so we're tolerant to bursty arrivals.
    const double target_blocks = std::max(2.0, tuning.target_buffer_level_ms / block_duration_ms);
    const double tolerance_blocks = std::max(1.0, tuning.buffer_tolerance_ms / block_duration_ms);
    const double upper_band = target_blocks + tolerance_blocks;
    
    if (blocks <= upper_band) {
        return 1.0;
    }
    
    // Bump ~1% per block over the upper band, capped.
    const double excess_blocks = blocks - upper_band;
    double ratio = 1.0 + (0.01 * excess_blocks);
    return std::min(ratio, tuning.max_speedup_factor);
}

void SinkRateController::dispatch_drain_adjustments(const InputBufferMetrics& metrics, double alpha) {
    PROFILE_FUNCTION();
    if (!settings_ || !rate_command_callback_) {
        return;
    }
    
    const auto& tuning = settings_->mixer_tuning;
    if (!tuning.enable_adaptive_buffer_drain) {
        return;
    }
    
    struct PendingCommand {
        std::string instance_id;
        double ratio;
        double smoothed_ms;
    };
    std::vector<PendingCommand> pending;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Clean up stale entries
        for (auto it = per_source_smoothed_buffer_ms_.begin(); it != per_source_smoothed_buffer_ms_.end();) {
            if (!metrics.per_source_ms.count(it->first)) {
                it = per_source_smoothed_buffer_ms_.erase(it);
            } else {
                ++it;
            }
        }
        
        for (const auto& [instance_id, backlog_ms] : metrics.per_source_ms) {
            double prev_smoothed = backlog_ms;
            auto it = per_source_smoothed_buffer_ms_.find(instance_id);
            if (it != per_source_smoothed_buffer_ms_.end()) {
                prev_smoothed = it->second;
            }
            double smoothed = prev_smoothed * (1.0 - alpha) + backlog_ms * alpha;
            per_source_smoothed_buffer_ms_[instance_id] = smoothed;
            
            double new_ratio = calculate_drain_ratio_for_level(smoothed, metrics.block_duration_ms);
            double prev_ratio = 1.0;
            auto ratio_it = source_last_rate_command_.find(instance_id);
            if (ratio_it != source_last_rate_command_.end()) {
                prev_ratio = ratio_it->second;
            }
            
            LOG_CPP_DEBUG("[RateControl:%s] Source %s backlog_raw=%.2fms smoothed=%.2fms prev=%.6f new=%.6f",
                          sink_id_.c_str(), instance_id.c_str(), backlog_ms, smoothed, prev_ratio, new_ratio);
            
            if (std::abs(new_ratio - prev_ratio) <= 0.0001) {
                continue;
            }
            
            source_last_rate_command_[instance_id] = new_ratio;
            pending.push_back(PendingCommand{instance_id, new_ratio, smoothed});
        }
    }
    
    for (const auto& cmd : pending) {
        rate_command_callback_(cmd.instance_id, cmd.ratio);
        if (cmd.ratio > 1.0) {
            LOG_CPP_INFO("[RateControl:%s] Source %s backlog=%.2fms -> rate scale=%.6f",
                         sink_id_.c_str(), cmd.instance_id.c_str(), cmd.smoothed_ms, cmd.ratio);
        } else {
            LOG_CPP_INFO("[RateControl:%s] Source %s backlog settled (%.2fms), resetting to 1.0",
                         sink_id_.c_str(), cmd.instance_id.c_str(), cmd.smoothed_ms);
        }
    }
}

} // namespace audio
} // namespace screamrouter
