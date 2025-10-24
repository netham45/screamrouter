/**
 * @file sink_synchronization_coordinator.cpp
 * @brief Implementation of per-sink coordination for synchronized multi-speaker playback.
 * @details Implements the coordination workflow for barrier synchronization, rate adjustment,
 *          and timing feedback for drift compensation.
 * 
 * Architecture Reference: MULTI_SPEAKER_SYNC_ARCHITECTURE.md lines 209-242, 554-605
 */

#include "sink_synchronization_coordinator.h"
#include "../utils/cpp_logger.h"

#include <algorithm>
#include <sstream>

namespace screamrouter {
namespace audio {

// ============================================================================
// Constructor / Destructor
// ============================================================================

SinkSynchronizationCoordinator::SinkSynchronizationCoordinator(
    const std::string& sink_id,
    SinkAudioMixer* mixer,
    GlobalSynchronizationClock* global_clock,
    int barrier_timeout_ms)
    : sink_id_(sink_id)
    , mixer_(mixer)
    , global_clock_(global_clock)
    , barrier_timeout_ms_(barrier_timeout_ms)
{
    // Validate parameters
    if (!mixer_) {
        LOG_CPP_ERROR("SinkSynchronizationCoordinator[%s]: mixer pointer is null!", sink_id_.c_str());
    }
    
    if (!global_clock_) {
        LOG_CPP_ERROR("SinkSynchronizationCoordinator[%s]: global_clock pointer is null!", sink_id_.c_str());
    }
    
    LOG_CPP_INFO("SinkSynchronizationCoordinator[%s]: Initialized with barrier_timeout=%dms",
             sink_id_.c_str(), barrier_timeout_ms);
}

SinkSynchronizationCoordinator::~SinkSynchronizationCoordinator() {
    // Unregister from global clock if enabled
    if (coordination_enabled_ && global_clock_) {
        LOG_CPP_INFO("SinkSynchronizationCoordinator[%s]: Unregistering from global clock (destructor)",
                 sink_id_.c_str());
        global_clock_->unregister_sink(sink_id_);
    }
    
    LOG_CPP_DEBUG("SinkSynchronizationCoordinator[%s]: Destroyed. Final stats - "
              "dispatches=%lu, timeouts=%lu, underruns=%lu, total_samples=%lu",
              sink_id_.c_str(),
              (unsigned long)total_dispatches_.load(),
              (unsigned long)barrier_timeouts_.load(),
              (unsigned long)underruns_.load(),
              (unsigned long)total_samples_output_);
}

// ============================================================================
// Main Coordination Method
// ============================================================================

bool SinkSynchronizationCoordinator::coordinate_dispatch() {
    // Phase 1: Pre-barrier - Check if coordination is enabled
    if (!coordination_enabled_ || !global_clock_ || !global_clock_->is_enabled()) {
        // Coordination disabled - return immediately for normal dispatch
        LOG_CPP_DEBUG("SinkSynchronizationCoordinator[%s]: Coordination disabled, skipping",
                  sink_id_.c_str());
        return true;
    }
    
    // Verify mixer is valid
    if (!mixer_) {
        LOG_CPP_ERROR("SinkSynchronizationCoordinator[%s]: mixer is null, cannot coordinate",
                  sink_id_.c_str());
        return false;
    }
    
    LOG_CPP_DEBUG("SinkSynchronizationCoordinator[%s]: Starting coordinate_dispatch (samples_output=%lu)",
              sink_id_.c_str(), (unsigned long)total_samples_output_);
    
    // Phase 2: Barrier Wait
    int timeout_ms = barrier_timeout_ms_.load();
    bool barrier_success = global_clock_->wait_for_dispatch_barrier(sink_id_, timeout_ms);
    
    if (!barrier_success) {
        // Barrier timeout - log warning and increment counter
        LOG_CPP_WARNING("SinkSynchronizationCoordinator[%s]: Barrier timeout after %dms, "
                    "proceeding with dispatch anyway",
                    sink_id_.c_str(), timeout_ms);
        barrier_timeouts_++;
    } else {
        LOG_CPP_DEBUG("SinkSynchronizationCoordinator[%s]: Passed barrier successfully",
                  sink_id_.c_str());
    }
    
    // Phase 3: Rate Adjustment
    double rate_adjustment = global_clock_->calculate_rate_adjustment(sink_id_);
    
    // Log if rate is outside normal range (±1%)
    if (rate_adjustment < 0.99 || rate_adjustment > 1.01) {
        LOG_CPP_WARNING("SinkSynchronizationCoordinator[%s]: Rate adjustment at limit: %.4f "
                    "(%+.2f%%)",
                    sink_id_.c_str(),
                    rate_adjustment,
                    (rate_adjustment - 1.0) * 100.0);
    } else {
        LOG_CPP_DEBUG("SinkSynchronizationCoordinator[%s]: Rate adjustment: %.4f (%+.2f%%)",
                  sink_id_.c_str(),
                  rate_adjustment,
                  (rate_adjustment - 1.0) * 100.0);
    }
    
    // Phase 4: Post-Dispatch Report
    // Typical chunk size is 1152 samples (24ms at 48kHz)
    const uint64_t chunk_samples = CHUNK_SIZE ;
    total_samples_output_ += chunk_samples;
    
    // Get current buffer level from mixer stats
    double buffer_fill = 0.0;
    uint64_t underrun_count = 0;
    bool had_underrun = false;
    
    try {
        auto mixer_stats = mixer_->get_stats();
        underrun_count = mixer_stats.buffer_underruns;
        
        // Calculate buffer fill percentage (approximate)
        // If we have active streams, assume reasonable fill
        if (mixer_stats.active_input_streams > 0) {
            buffer_fill = 0.75; // Nominal 75% fill
        } else {
            buffer_fill = 0.0;
            had_underrun = true;
        }
        
        // Check if underrun count increased since last check
        if (underrun_count > underruns_.load()) {
            had_underrun = true;
            underruns_.store(underrun_count);
        }
    } catch (const std::exception& e) {
        LOG_CPP_ERROR("SinkSynchronizationCoordinator[%s]: Exception getting mixer stats: %s",
                  sink_id_.c_str(), e.what());
    }
    
    // Create and send timing report to global clock
    SinkTimingReport report;
    report.samples_output = chunk_samples;
    report.rtp_timestamp_output = last_output_rtp_timestamp_;
    report.dispatch_time = std::chrono::steady_clock::now();
    report.had_underrun = had_underrun;
    report.buffer_fill_percentage = buffer_fill;
    
    global_clock_->report_sink_timing(sink_id_, report);
    
    // Update RTP timestamp for next dispatch
    last_output_rtp_timestamp_ += chunk_samples;
    
    // Increment dispatch counter
    total_dispatches_++;
    
    // Log summary at DEBUG level
    LOG_CPP_DEBUG("SinkSynchronizationCoordinator[%s]: Dispatch complete - "
              "rate=%.4f, samples=%lu, total_samples=%lu, buffer_fill=%.1f%%, underrun=%s",
              sink_id_.c_str(),
              rate_adjustment,
              (unsigned long)chunk_samples,
              (unsigned long)total_samples_output_,
              buffer_fill * 100.0,
              had_underrun ? "YES" : "NO");
    
    return !had_underrun;
}

// ============================================================================
// Enable / Disable
// ============================================================================

void SinkSynchronizationCoordinator::enable() {
    if (coordination_enabled_) {
        LOG_CPP_DEBUG("SinkSynchronizationCoordinator[%s]: Already enabled, ignoring",
                  sink_id_.c_str());
        return;
    }
    
    if (!global_clock_) {
        LOG_CPP_ERROR("SinkSynchronizationCoordinator[%s]: Cannot enable - global_clock is null",
                  sink_id_.c_str());
        return;
    }
    
    // Register with global clock
    uint64_t initial_timestamp = last_output_rtp_timestamp_;
    global_clock_->register_sink(sink_id_, initial_timestamp);
    
    // Set enabled flag
    coordination_enabled_ = true;
    
    LOG_CPP_INFO("SinkSynchronizationCoordinator[%s]: Enabled and registered with global clock "
             "(initial_timestamp=%lu)",
             sink_id_.c_str(), (unsigned long)initial_timestamp);
}

void SinkSynchronizationCoordinator::disable() {
    if (!coordination_enabled_) {
        LOG_CPP_DEBUG("SinkSynchronizationCoordinator[%s]: Already disabled, ignoring",
                  sink_id_.c_str());
        return;
    }
    
    if (!global_clock_) {
        LOG_CPP_WARNING("SinkSynchronizationCoordinator[%s]: Cannot unregister - global_clock is null",
                    sink_id_.c_str());
    } else {
        // Unregister from global clock
        global_clock_->unregister_sink(sink_id_);
    }
    
    // Clear enabled flag
    coordination_enabled_ = false;
    
    LOG_CPP_INFO("SinkSynchronizationCoordinator[%s]: Disabled and unregistered from global clock",
             sink_id_.c_str());
}

// ============================================================================
// Configuration
// ============================================================================

void SinkSynchronizationCoordinator::set_barrier_timeout(int timeout_ms) {
    int old_timeout = barrier_timeout_ms_.exchange(timeout_ms);
    
    LOG_CPP_INFO("SinkSynchronizationCoordinator[%s]: Barrier timeout changed: %dms -> %dms",
             sink_id_.c_str(), old_timeout, timeout_ms);
}

// ============================================================================
// Statistics
// ============================================================================

CoordinatorStats SinkSynchronizationCoordinator::get_statistics() const {
    CoordinatorStats stats;
    
    // Use atomic loads for thread-safe access
    stats.total_dispatches = total_dispatches_.load(std::memory_order_relaxed);
    stats.barrier_timeouts = barrier_timeouts_.load(std::memory_order_relaxed);
    stats.underruns = underruns_.load(std::memory_order_relaxed);
    stats.total_samples_output = total_samples_output_;
    stats.coordination_enabled = coordination_enabled_.load(std::memory_order_relaxed);
    
    // Get current rate adjustment from global clock
    if (global_clock_ && coordination_enabled_) {
        try {
            stats.current_rate_adjustment = global_clock_->calculate_rate_adjustment(sink_id_);
        } catch (const std::exception& e) {
            LOG_CPP_ERROR("SinkSynchronizationCoordinator[%s]: Exception getting rate adjustment: %s",
                      sink_id_.c_str(), e.what());
            stats.current_rate_adjustment = 1.0;
        }
    } else {
        stats.current_rate_adjustment = 1.0;
    }
    
    return stats;
}

// ============================================================================
// Helper Methods
// ============================================================================

void SinkSynchronizationCoordinator::report_timing_to_global_clock(
    uint64_t samples_sent,
    bool had_underrun)
{
    if (!global_clock_ || !coordination_enabled_) {
        return;
    }
    
    // Create timing report
    SinkTimingReport report;
    report.samples_output = samples_sent;
    report.rtp_timestamp_output = last_output_rtp_timestamp_;
    report.dispatch_time = std::chrono::steady_clock::now();
    report.had_underrun = had_underrun;
    
    // Get buffer fill from mixer if available
    if (mixer_) {
        try {
            auto mixer_stats = mixer_->get_stats();
            if (mixer_stats.active_input_streams > 0) {
                report.buffer_fill_percentage = 0.75; // Nominal
            } else {
                report.buffer_fill_percentage = 0.0;
            }
        } catch (const std::exception& e) {
            LOG_CPP_ERROR("SinkSynchronizationCoordinator[%s]: Exception in report_timing: %s",
                      sink_id_.c_str(), e.what());
            report.buffer_fill_percentage = 0.5; // Default
        }
    } else {
        report.buffer_fill_percentage = 0.5; // Default if no mixer
    }
    
    // Send report to global clock
    global_clock_->report_sink_timing(sink_id_, report);
    
    LOG_CPP_DEBUG("SinkSynchronizationCoordinator[%s]: Reported timing - samples=%lu, underrun=%s",
              sink_id_.c_str(), (unsigned long)samples_sent, had_underrun ? "YES" : "NO");
}

} // namespace audio
} // namespace screamrouter