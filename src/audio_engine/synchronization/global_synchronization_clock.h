/**
 * @file global_synchronization_clock.h
 * @brief Centralized time authority for multi-speaker synchronization.
 * @details This component maintains a master playback position and coordinates multiple audio sinks
 *          to achieve synchronized playback within microseconds. Each sample rate has its own
 *          GlobalSynchronizationClock instance to handle independent clock domains (e.g., 44.1kHz
 *          sinks sync separately from 48kHz sinks).
 * 
 * Key Features:
 * - Master RTP timestamp progression based on wall clock time
 * - Per-sink drift tracking and rate adjustment calculation
 * - Barrier synchronization for coordinated audio dispatch
 * - Thread-safe design for concurrent access from multiple sink threads
 * 
 * Architecture Reference: MULTI_SPEAKER_SYNC_ARCHITECTURE.md lines 127-207
 */

#ifndef GLOBAL_SYNCHRONIZATION_CLOCK_H
#define GLOBAL_SYNCHRONIZATION_CLOCK_H

#include <chrono>
#include <string>
#include <map>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>

namespace screamrouter {
namespace audio {

/**
 * @struct SinkTimingInfo
 * @brief Tracks timing and drift information for a single registered sink.
 * @details Used internally by GlobalSynchronizationClock to monitor each sink's
 *          playback position and calculate necessary rate adjustments.
 */
struct SinkTimingInfo {
    /** @brief Unique identifier for this sink. */
    std::string sink_id;
    
    /** @brief Total number of samples output by this sink since registration. */
    uint64_t total_samples_output = 0;
    
    /** @brief The last RTP timestamp reported by this sink. */
    uint64_t last_reported_rtp_timestamp = 0;
    
    /** @brief Wall clock time of the last timing report. */
    std::chrono::steady_clock::time_point last_report_time;
    
    /** @brief Smoothed error in samples (exponential moving average). */
    double accumulated_error_samples = 0.0;
    
    /** @brief Current playback rate adjustment factor (e.g., 1.001 = 0.1% faster). */
    double current_rate_adjustment = 1.0;
    
    /** @brief Whether this sink is currently active and participating in synchronization. */
    bool is_active = true;
    
    /** @brief Count of buffer underruns reported by this sink. */
    uint64_t underrun_count = 0;
};

/**
 * @struct SinkTimingReport
 * @brief Report structure sent from sink coordinators to the global clock.
 * @details Contains timing information about a completed audio dispatch operation.
 */
struct SinkTimingReport {
    /** @brief Number of samples output in this dispatch. */
    uint64_t samples_output;
    
    /** @brief RTP timestamp of the audio that was output. */
    uint64_t rtp_timestamp_output;
    
    /** @brief Wall clock time when the dispatch occurred. */
    std::chrono::steady_clock::time_point dispatch_time;
    
    /** @brief True if this dispatch experienced a buffer underrun. */
    bool had_underrun;
    
    /** @brief Current buffer fill level as a percentage (0.0 to 1.0). */
    double buffer_fill_percentage;
};

/**
 * @struct SyncStats
 * @brief Aggregated statistics about the synchronization system.
 * @details Used for monitoring and diagnostics.
 */
struct SyncStats {
    /** @brief Number of currently active sinks in this rate group. */
    int active_sinks = 0;
    
    /** @brief Current target playback timestamp (RTP domain). */
    uint64_t current_playback_timestamp = 0;
    
    /** @brief Maximum drift among all sinks in parts per million (ppm). */
    double max_drift_ppm = 0.0;
    
    /** @brief Average time spent waiting at the barrier (milliseconds). */
    double avg_barrier_wait_ms = 0.0;
    
    /** @brief Total number of barrier timeout events since initialization. */
    uint64_t total_barrier_timeouts = 0;
};

/**
 * @class GlobalSynchronizationClock
 * @brief Master time authority for synchronized multi-speaker playback.
 * 
 * @details This class manages a single sample rate clock domain (e.g., 48000 Hz) and coordinates
 *          all audio sinks operating at that rate. It provides:
 *          
 *          1. **Master Timestamp Progression**: Calculates the current target RTP timestamp
 *             based on elapsed wall clock time: 
 *             `current_ts = reference_ts + (elapsed_seconds * sample_rate)`
 *          
 *          2. **Drift Compensation**: Tracks each sink's actual sample output vs. expected
 *             output, calculating rate adjustment factors (typically ±0.1% to ±2%) to
 *             correct for hardware clock drift.
 *          
 *          3. **Barrier Synchronization**: Implements a reusable barrier where all sinks
 *             wait until the entire group is ready, ensuring simultaneous audio dispatch
 *             within microseconds.
 * 
 * Thread Safety:
 * - All public methods are thread-safe and can be called from multiple sink threads
 * - Internal state is protected by mutexes and condition variables
 * - Barrier uses generation counter to support multiple cycles
 * 
 * Usage Pattern:
 * ```cpp
 * // Create one clock per sample rate (e.g., 48000 Hz)
 * auto clock = std::make_unique<GlobalSynchronizationClock>(48000);
 * clock->initialize_reference(initial_rtp_ts, std::chrono::steady_clock::now());
 * clock->set_enabled(true);
 * 
 * // Register each sink
 * clock->register_sink("sink1", initial_ts);
 * clock->register_sink("sink2", initial_ts);
 * 
 * // In each sink's processing loop:
 * uint64_t target_ts = clock->get_current_playback_timestamp();
 * double rate_adj = clock->calculate_rate_adjustment(sink_id);
 * bool barrier_ok = clock->wait_for_dispatch_barrier(sink_id, 50);
 * // ... dispatch audio ...
 * clock->report_sink_timing(sink_id, timing_report);
 * ```
 */
class GlobalSynchronizationClock {
public:
    /**
     * @brief Constructs a global synchronization clock for a specific sample rate.
     * @param master_sample_rate The sample rate this clock manages (e.g., 48000, 44100, 96000).
     * 
     * @details Creates a clock instance that will coordinate all sinks operating at the
     *          specified sample rate. Each sample rate in the system requires its own
     *          independent GlobalSynchronizationClock.
     */
    explicit GlobalSynchronizationClock(int master_sample_rate);
    
    /**
     * @brief Destructor - cleans up resources.
     */
    ~GlobalSynchronizationClock();
    
    /**
     * @brief Initializes the reference point for timestamp progression.
     * @param initial_rtp_timestamp The RTP timestamp to use as the reference point.
     * @param initial_time The wall clock time corresponding to the reference timestamp.
     * 
     * @details This establishes the time=0 baseline for the clock. All future timestamp
     *          calculations are relative to this reference point. Typically called when
     *          the first audio packet arrives.
     * 
     * Thread Safety: Safe to call from any thread.
     */
    void initialize_reference(
        uint64_t initial_rtp_timestamp, 
        std::chrono::steady_clock::time_point initial_time);
    
    /**
     * @brief Gets the current target RTP timestamp for playback.
     * @return The RTP timestamp that should be playing RIGHT NOW.
     * 
     * @details Calculates the current playback position based on elapsed wall clock time:
     *          `current_ts = reference_ts + (elapsed_seconds * master_sample_rate)`
     * 
     *          This timestamp represents the "now" that all sinks in this rate group
     *          should be synchronized to.
     * 
     * Thread Safety: Safe to call from any thread.
     */
    uint64_t get_current_playback_timestamp() const;
    
    /**
     * @brief Registers a new sink with the synchronization system.
     * @param sink_id Unique identifier for the sink (e.g., "living_room_left").
     * @param initial_timestamp The starting RTP timestamp for this sink.
     * 
     * @details Adds a new sink to the synchronization group. The sink will participate
     *          in barrier synchronization and receive rate adjustment calculations.
     * 
     * Thread Safety: Safe to call from any thread.
     */
    void register_sink(const std::string& sink_id, uint64_t initial_timestamp);
    
    /**
     * @brief Unregisters a sink from the synchronization system.
     * @param sink_id Unique identifier of the sink to remove.
     * 
     * @details Removes a sink from the synchronization group. The sink will no longer
     *          participate in barrier synchronization, and any threads waiting at the
     *          barrier will be notified to proceed without this sink.
     * 
     * Thread Safety: Safe to call from any thread.
     */
    void unregister_sink(const std::string& sink_id);
    
    /**
     * @brief Reports timing information from a sink after audio dispatch.
     * @param sink_id Unique identifier of the reporting sink.
     * @param report Timing information about the completed dispatch operation.
     * 
     * @details Sinks call this after dispatching audio to update the global clock's
     *          tracking of their playback position. This information is used to calculate
     *          drift and determine rate adjustments.
     * 
     * Thread Safety: Safe to call from any thread.
     */
    void report_sink_timing(const std::string& sink_id, 
                           const SinkTimingReport& report);
    
    /**
     * @brief Calculates the recommended playback rate adjustment for a sink.
     * @param sink_id Unique identifier of the sink.
     * @return Playback rate multiplier (e.g., 1.001 = 0.1% faster, 0.999 = 0.1% slower).
     * 
     * @details Implements a proportional controller that compares the sink's actual
     *          sample output with the expected output based on elapsed time:
     * 
     *          ```
     *          expected_samples = reference_ts + (elapsed_sec * master_sample_rate)
     *          error_samples = expected_samples - actual_samples
     *          accumulated_error = 0.9 * old_error + 0.1 * error  // EMA smoothing
     *          adjustment = 1.0 + (accumulated_error / samples) * gain
     *          ```
     * 
     *          The result is clamped to the configured min/max range (typically ±2%).
     * 
     * Thread Safety: Safe to call from any thread.
     */
    double calculate_rate_adjustment(const std::string& sink_id);
    
    /**
     * @brief Waits at a barrier until all active sinks are ready to dispatch.
     * @param sink_id Unique identifier of the waiting sink.
     * @param timeout_ms Maximum time to wait in milliseconds.
     * @return True if all sinks arrived at the barrier, false if timeout occurred.
     * 
     * @details Implements a reusable barrier synchronization mechanism:
     *          - Each sink calls this before dispatching audio
     *          - The call blocks until all sinks in the group have arrived
     *          - When the last sink arrives, all threads are released simultaneously
     *          - Uses generation counter to handle multiple barrier cycles
     * 
     *          If timeout occurs, the sink is allowed to proceed anyway to prevent
     *          stalling the entire system due to one slow sink.
     * 
     * Thread Safety: Safe to call from any thread.
     */
    bool wait_for_dispatch_barrier(const std::string& sink_id, 
                                   int timeout_ms);
    
    /**
     * @brief Retrieves current synchronization statistics.
     * @return Aggregated statistics about the synchronization system.
     * 
     * @details Provides monitoring data including active sink count, current timestamp,
     *          drift measurements, and barrier performance metrics.
     * 
     * Thread Safety: Safe to call from any thread.
     */
    SyncStats get_stats() const;
    
    /**
     * @brief Enables or disables the synchronization system.
     * @param enabled True to enable synchronization, false to disable.
     * 
     * @details When disabled, sinks will bypass synchronization and operate independently
     *          (legacy mode). When enabled, sinks participate in coordinated dispatch.
     * 
     * Thread Safety: Safe to call from any thread (atomic operation).
     */
    void set_enabled(bool enabled) { enabled_ = enabled; }
    
    /**
     * @brief Checks if synchronization is currently enabled.
     * @return True if synchronization is active, false otherwise.
     * 
     * Thread Safety: Safe to call from any thread (atomic read).
     */
    bool is_enabled() const { return enabled_; }
    
    /**
     * @brief Gets the sample rate this clock operates at.
     * @return Sample rate in Hz (e.g., 48000, 44100, 96000).
     * 
     * Thread Safety: Safe to call from any thread (const member).
     */
    int get_sample_rate() const { return master_sample_rate_; }

private:
    // --- Reference Time State ---
    
    /** @brief Whether the reference time has been initialized. */
    bool reference_initialized_ = false;
    
    /** @brief Wall clock time point of the reference timestamp. */
    std::chrono::steady_clock::time_point reference_time_;
    
    /** @brief RTP timestamp corresponding to the reference time. */
    uint64_t reference_rtp_timestamp_ = 0;
    
    /** @brief Sample rate this clock manages (e.g., 48000 Hz). */
    int master_sample_rate_;
    
    // --- Sink Tracking State ---
    
    /** @brief Map of registered sinks and their timing information. */
    std::map<std::string, SinkTimingInfo> sinks_;
    
    /** @brief Mutex protecting the sinks_ map. */
    mutable std::mutex sinks_mutex_;
    
    // --- Barrier Synchronization State ---
    
    /** @brief Condition variable for barrier coordination. */
    std::condition_variable barrier_cv_;
    
    /** @brief Mutex for barrier synchronization. */
    std::mutex barrier_mutex_;
    
    /** @brief Number of sinks currently waiting at the barrier. */
    std::atomic<int> sinks_ready_count_{0};
    
    /** @brief Barrier generation counter (increments each barrier cycle). */
    int barrier_generation_ = 0;
    
    /** @brief Total barrier timeout events (for statistics). */
    mutable std::atomic<uint64_t> total_barrier_timeouts_{0};
    
    // --- Configuration Parameters ---
    
    /** @brief Whether synchronization is enabled (can be toggled at runtime). */
    std::atomic<bool> enabled_{false};
    
    /** @brief Proportional gain for drift compensation (default: 0.01). */
    double sync_proportional_gain_ = 0.01;
    
    /** @brief Maximum allowed rate adjustment (default: ±5% = 0.05). */
    double max_rate_adjustment_ = 0.5;
    
    /** @brief Smoothing factor for error accumulation (default: 0.9 for EMA). */
    double sync_smoothing_factor_ = 0.9;
    
    // --- Helper Methods ---
    
    /**
     * @brief Counts the number of currently active sinks.
     * @return Number of active sinks.
     * 
     * @note Must be called with sinks_mutex_ held.
     */
    int count_active_sinks_locked() const;
};

} // namespace audio
} // namespace screamrouter

#endif // GLOBAL_SYNCHRONIZATION_CLOCK_H