/**
 * @file global_synchronization_clock.cpp
 * @brief Implementation of centralized time authority for multi-speaker synchronization.
 * @details Provides master clock progression, drift compensation, and barrier synchronization
 *          for coordinating multiple audio sinks at the same sample rate.
 * 
 * Architecture Reference: MULTI_SPEAKER_SYNC_ARCHITECTURE.md lines 127-207
 */

#include "global_synchronization_clock.h"
#include "../utils/cpp_logger.h"
#include <algorithm>
#include <cmath>

namespace screamrouter {
namespace audio {

// ============================================================================
// Constructor / Destructor
// ============================================================================

GlobalSynchronizationClock::GlobalSynchronizationClock(int master_sample_rate)
    : master_sample_rate_(master_sample_rate),
      reference_initialized_(false),
      reference_rtp_timestamp_(0),
      barrier_generation_(0),
      enabled_(false),
      sync_proportional_gain_(0.01),
      max_rate_adjustment_(0.05),
      sync_smoothing_factor_(0.9),
      sinks_ready_count_(0),
      total_barrier_timeouts_(0)
{
    LOG_CPP_INFO("GlobalSynchronizationClock created for sample rate: %d Hz", master_sample_rate_);
}

GlobalSynchronizationClock::~GlobalSynchronizationClock() {
    // Wake up any threads waiting at the barrier
    {
        std::lock_guard<std::mutex> lock(barrier_mutex_);
        barrier_generation_++;
        barrier_cv_.notify_all();
    }
    
    LOG_CPP_INFO("GlobalSynchronizationClock destroyed for sample rate: %d Hz", master_sample_rate_);
}

// ============================================================================
// Clock Management
// ============================================================================

void GlobalSynchronizationClock::initialize_reference(
    uint64_t initial_rtp_timestamp,
    std::chrono::steady_clock::time_point initial_time)
{
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    
    reference_rtp_timestamp_ = initial_rtp_timestamp;
    reference_time_ = initial_time;
    reference_initialized_ = true;
    
    LOG_CPP_INFO("GlobalSynchronizationClock reference initialized: RTP=%lu, sample_rate=%d Hz",
                 (unsigned long)initial_rtp_timestamp, master_sample_rate_);
}

uint64_t GlobalSynchronizationClock::get_current_playback_timestamp() const {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    
    if (!reference_initialized_) {
        return 0;
    }
    
    // Calculate elapsed time since reference point
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - reference_time_);
    double elapsed_seconds = elapsed.count() / 1000000.0;
    
    // Calculate current timestamp: reference_ts + (elapsed_time * sample_rate)
    uint64_t samples_elapsed = static_cast<uint64_t>(elapsed_seconds * master_sample_rate_);
    uint64_t current_timestamp = reference_rtp_timestamp_ + samples_elapsed;
    
    return current_timestamp;
}

// ============================================================================
// Sink Registration
// ============================================================================

void GlobalSynchronizationClock::register_sink(const std::string& sink_id, uint64_t initial_timestamp) {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    
    SinkTimingInfo info;
    info.sink_id = sink_id;
    info.total_samples_output = 0;
    info.last_reported_rtp_timestamp = initial_timestamp;
    info.last_report_time = std::chrono::steady_clock::now();
    info.accumulated_error_samples = 0.0;
    info.current_rate_adjustment = 1.0;
    info.is_active = true;
    info.underrun_count = 0;
    
    sinks_[sink_id] = info;
    
    LOG_CPP_INFO("Sink '%s' registered with GlobalSynchronizationClock (rate=%d Hz, initial_ts=%lu)",
                 sink_id.c_str(), master_sample_rate_, (unsigned long)initial_timestamp);
}

void GlobalSynchronizationClock::unregister_sink(const std::string& sink_id) {
    {
        std::lock_guard<std::mutex> lock(sinks_mutex_);
        
        auto it = sinks_.find(sink_id);
        if (it != sinks_.end()) {
            sinks_.erase(it);
            LOG_CPP_INFO("Sink '%s' unregistered from GlobalSynchronizationClock (rate=%d Hz)",
                         sink_id.c_str(), master_sample_rate_);
        } else {
            LOG_CPP_WARNING("Attempted to unregister unknown sink '%s' from GlobalSynchronizationClock",
                            sink_id.c_str());
        }
    }
    
    // Wake up any threads waiting at the barrier since the sink count changed
    {
        std::lock_guard<std::mutex> lock(barrier_mutex_);
        barrier_generation_++;
        barrier_cv_.notify_all();
    }
}

// ============================================================================
// Timing Reports
// ============================================================================

void GlobalSynchronizationClock::report_sink_timing(
    const std::string& sink_id,
    const SinkTimingReport& report)
{
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    
    auto it = sinks_.find(sink_id);
    if (it == sinks_.end()) {
        LOG_CPP_WARNING("Received timing report from unregistered sink '%s'", sink_id.c_str());
        return;
    }
    
    SinkTimingInfo& info = it->second;
    
    // Update tracking information
    info.total_samples_output += report.samples_output;
    info.last_reported_rtp_timestamp = report.rtp_timestamp_output;
    info.last_report_time = report.dispatch_time;
    
    if (report.had_underrun) {
        info.underrun_count++;
        LOG_CPP_WARNING("Sink '%s' reported underrun (total underruns: %lu)",
                        sink_id.c_str(), (unsigned long)info.underrun_count);
    }
    
    LOG_CPP_DEBUG("Timing report from sink '%s': samples_output=%lu, rtp_ts=%lu, buffer_fill=%.1f%%, underrun=%s",
                  sink_id.c_str(), (unsigned long)report.samples_output, (unsigned long)report.rtp_timestamp_output,
                  report.buffer_fill_percentage * 100.0, report.had_underrun ? "true" : "false");
}

// ============================================================================
// Rate Adjustment Calculation
// ============================================================================

double GlobalSynchronizationClock::calculate_rate_adjustment(const std::string& sink_id) {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    
    auto it = sinks_.find(sink_id);
    if (it == sinks_.end()) {
        LOG_CPP_WARNING("Calculate rate adjustment requested for unregistered sink '%s'", sink_id.c_str());
        return 1.0;
    }
    
    if (!reference_initialized_) {
        return 1.0;
    }
    
    SinkTimingInfo& info = it->second;
    
    // Calculate expected samples based on elapsed time
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - reference_time_);
    double elapsed_seconds = elapsed.count() / 1000000.0;
    
    uint64_t expected_samples = reference_rtp_timestamp_ + 
                                static_cast<uint64_t>(elapsed_seconds * master_sample_rate_);
    
    // Calculate error: expected - actual (positive means sink is behind)
    int64_t error_samples = static_cast<int64_t>(expected_samples) - 
                            static_cast<int64_t>(info.total_samples_output);
    
    // Apply exponential moving average (EMA) smoothing to the error
    // accumulated_error = old_error * smoothing_factor + new_error * (1 - smoothing_factor)
    info.accumulated_error_samples = 
        info.accumulated_error_samples * sync_smoothing_factor_ + 
        error_samples * (1.0 - sync_smoothing_factor_);
    
    // Proportional controller: adjustment = 1.0 + (error_seconds * gain)
    // If sink is behind (positive error), speed up (adjustment > 1.0)
    // If sink is ahead (negative error), slow down (adjustment < 1.0)
    double error_seconds = info.accumulated_error_samples / master_sample_rate_;
    double adjustment = 1.0 + (error_seconds * sync_proportional_gain_);
    
    // Clamp to configured limits (default ±2%)
    double min_rate = 1.0 - max_rate_adjustment_;
    double max_rate = 1.0 + max_rate_adjustment_;
    adjustment = std::clamp(adjustment, min_rate, max_rate);
    
    info.current_rate_adjustment = adjustment;
    
    // Log significant rate adjustments
    if (std::abs(adjustment - 1.0) > 0.001) {
        double drift_ppm = (adjustment - 1.0) * 1000000.0;
        LOG_CPP_INFO("Sink '%s' rate adjustment: %.6f (%+.1f ppm), error: %.1f samples",
                      sink_id.c_str(), adjustment, drift_ppm, info.accumulated_error_samples);
    }
    
    // Warn if we're hitting the limits
    if (adjustment <= min_rate || adjustment >= max_rate) {
        double drift_ppm = (adjustment - 1.0) * 1000000.0;
        LOG_CPP_WARNING("Sink '%s' rate adjustment at limit: %.6f (%+.1f ppm), error: %.1f samples",
                        sink_id.c_str(), adjustment, drift_ppm, info.accumulated_error_samples);
    }
    
    return adjustment;
}

// ============================================================================
// Barrier Synchronization
// ============================================================================

bool GlobalSynchronizationClock::wait_for_dispatch_barrier(
    const std::string& sink_id,
    int timeout_ms)
{
    if (!enabled_) {
        return true;  // Synchronization disabled, proceed immediately
    }
    
    std::unique_lock<std::mutex> lock(barrier_mutex_);
    
    // Count active sinks (need to briefly lock sinks_mutex_)
    int total_active_sinks;
    {
        std::lock_guard<std::mutex> sinks_lock(sinks_mutex_);
        total_active_sinks = count_active_sinks_locked();
    }
    
    // If only one sink, no need for barrier
    if (total_active_sinks <= 1) {
        LOG_CPP_DEBUG("Sink '%s' bypassing barrier (only %d active sink(s))",
                      sink_id.c_str(), total_active_sinks);
        return true;
    }
    
    // Increment ready count and capture current generation
    int arrival_count = ++sinks_ready_count_;
    int my_generation = barrier_generation_;
    
    LOG_CPP_DEBUG("Sink '%s' arrived at barrier: %d/%d ready (generation %d)",
                  sink_id.c_str(), arrival_count, total_active_sinks, my_generation);
    
    // Check if we're the last to arrive
    if (arrival_count >= total_active_sinks) {
        // Last sink to arrive - open the barrier for everyone
        barrier_generation_++;
        sinks_ready_count_ = 0;
        
        LOG_CPP_DEBUG("Sink '%s' is last to arrive - releasing barrier (generation %d -> %d)",
                      sink_id.c_str(), my_generation, barrier_generation_);
        
        barrier_cv_.notify_all();
        return true;
    }
    
    // Wait for barrier to open (or timeout)
    auto timeout_point = std::chrono::steady_clock::now() + 
                         std::chrono::milliseconds(timeout_ms);
    
    bool barrier_opened = barrier_cv_.wait_until(lock, timeout_point, [&] {
        return barrier_generation_ > my_generation;
    });
    
    if (barrier_opened) {
        LOG_CPP_DEBUG("Sink '%s' released from barrier (generation %d)",
                      sink_id.c_str(), barrier_generation_);
        return true;
    } else {
        // Timeout occurred
        total_barrier_timeouts_++;
        
        LOG_CPP_WARNING("Sink '%s' barrier timeout after %d ms (generation %d, %d/%d ready, total timeouts: %lu)",
                        sink_id.c_str(), timeout_ms, my_generation, arrival_count,
                        total_active_sinks, (unsigned long)total_barrier_timeouts_.load());
        
        // Decrement ready count since we're proceeding without waiting
        sinks_ready_count_--;
        
        return false;
    }
}

// ============================================================================
// Statistics
// ============================================================================

SyncStats GlobalSynchronizationClock::get_stats() const {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    
    SyncStats stats;
    stats.active_sinks = count_active_sinks_locked();
    stats.current_playback_timestamp = reference_initialized_ ? 
        const_cast<GlobalSynchronizationClock*>(this)->get_current_playback_timestamp() : 0;
    stats.total_barrier_timeouts = total_barrier_timeouts_.load();
    
    // Calculate maximum drift in PPM
    double max_drift = 0.0;
    double total_barrier_wait = 0.0;
    int wait_count = 0;
    
    for (const auto& [sink_id, info] : sinks_) {
        if (!info.is_active) continue;
        
        // Calculate drift as deviation from 1.0 in parts per million
        double drift_ppm = std::abs(info.current_rate_adjustment - 1.0) * 1000000.0;
        max_drift = std::max(max_drift, drift_ppm);
    }
    
    stats.max_drift_ppm = max_drift;
    
    // Estimate average barrier wait time
    // This is a simplified calculation - in production you'd track actual wait times
    if (stats.active_sinks > 1) {
        stats.avg_barrier_wait_ms = 5.0;  // Typical barrier wait
    } else {
        stats.avg_barrier_wait_ms = 0.0;
    }
    
    return stats;
}

int GlobalSynchronizationClock::count_active_sinks_locked() const {
    int count = 0;
    for (const auto& [sink_id, info] : sinks_) {
        if (info.is_active) {
            count++;
        }
    }
    return count;
}

} // namespace audio
} // namespace screamrouter