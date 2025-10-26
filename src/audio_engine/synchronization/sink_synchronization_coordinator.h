/**
 * @file sink_synchronization_coordinator.h
 * @brief Per-sink coordination layer for synchronized multi-speaker playback.
 * @details This class wraps a SinkAudioMixer to enable coordinated dispatch across multiple
 *          audio sinks. It interfaces with GlobalSynchronizationClock to implement barrier
 *          synchronization, rate adjustment, and timing feedback for drift compensation.
 * 
 * Architecture Reference: MULTI_SPEAKER_SYNC_ARCHITECTURE.md lines 209-242, 554-605
 */

#ifndef SINK_SYNCHRONIZATION_COORDINATOR_H
#define SINK_SYNCHRONIZATION_COORDINATOR_H

#include "global_synchronization_clock.h"
#include "../output_mixer/sink_audio_mixer.h"

#include <string>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace screamrouter {
namespace audio {

/**
 * @struct CoordinatorStats
 * @brief Statistics tracking for a single sink coordinator.
 * @details Provides monitoring data about dispatch operations, synchronization
 *          performance, and timing adjustments for diagnostics and tuning.
 */
struct CoordinatorStats {
    /** @brief Total number of successful audio dispatches since start. */
    uint64_t total_dispatches = 0;
    
    /** @brief Number of times the barrier wait timed out. */
    uint64_t barrier_timeouts = 0;
    
    /** @brief Number of buffer underruns (mixer had no data ready). */
    uint64_t underruns = 0;
    
    /** @brief Current playback rate adjustment factor (e.g., 1.001 = 0.1% faster). */
    double current_rate_adjustment = 1.0;
    
    /** @brief Total samples output by this sink since registration. */
    uint64_t total_samples_output = 0;
    
    /** @brief Whether coordination is currently enabled for this sink. */
    bool coordination_enabled = false;
};

/**
 * @class SinkSynchronizationCoordinator
 * @brief Wraps a SinkAudioMixer to provide synchronized dispatch coordination.
 * 
 * @details This class acts as a coordination layer between a single sink's audio mixer
 *          and the global synchronization clock. It implements the following workflow:
 * 
 *          1. Query mixer for mixed audio (mixer buffers instead of immediate dispatch)
 *          2. Wait at barrier until all sinks in the rate group are ready
 *          3. Get and apply rate adjustment from global clock
 *          4. Dispatch audio simultaneously with other sinks
 *          5. Report timing information back to global clock for drift tracking
 * 
 * **Key Responsibilities:**
 * 
 * - **Lifecycle Management**: Registers/unregisters with global clock
 * - **Dispatch Coordination**: Implements barrier-synchronized dispatch workflow
 * - **Rate Control**: Applies playback rate adjustments for drift compensation
 * - **State Tracking**: Monitors samples output, buffer levels, underruns
 * - **Statistics Export**: Provides performance metrics for monitoring
 * 
 * **Thread Safety:**
 * - Designed to be called from the mixer's run thread
 * - Interacts with thread-safe GlobalSynchronizationClock methods
 * - Statistics use atomic counters for lock-free access
 * 
 * **Usage Pattern:**
 * ```cpp
 * // In AudioManager, when creating a sink:
 * auto mixer = std::make_unique<SinkAudioMixer>(config, ...);
 * auto* global_clock = get_or_create_sync_clock(config.samplerate);
 * 
 * auto coordinator = std::make_unique<SinkSynchronizationCoordinator>(
 *     config.id,
 *     mixer.get(),
 *     global_clock,
 *     50  // barrier timeout ms
 * );
 * 
 * coordinator->enable();
 * 
 * // In mixer's run loop:
 * if (should_coordinate()) {
 *     if (coordinator->begin_dispatch()) {
 *         // ... perform mix & send, capture timing ...
 *         SinkSynchronizationCoordinator::DispatchTimingInfo timing{start_time, end_time};
 *         coordinator->complete_dispatch(frames_output, timing);
 *     }
 * }
 * ```
 * 
 * Architecture Reference: MULTI_SPEAKER_SYNC_ARCHITECTURE.md lines 554-605
 */
class SinkSynchronizationCoordinator {
public:
    /**
     * @brief Timing metrics captured by the mixer for a single dispatch cycle.
     */
    struct DispatchTimingInfo {
        std::chrono::steady_clock::time_point dispatch_start{}; ///< Timestamp taken immediately after the barrier is released.
        std::chrono::steady_clock::time_point dispatch_end{};   ///< Timestamp captured right after payload emission completes.

        /**
         * @brief Calculates the duration spent performing local work for the dispatch.
         */
        std::chrono::steady_clock::duration processing_duration() const {
            return dispatch_end - dispatch_start;
        }
    };

    /**
     * @brief Constructs a coordinator for a specific sink.
     * @param sink_id Unique identifier for this sink (e.g., "living_room_left").
     * @param mixer Pointer to the SinkAudioMixer this coordinator wraps (must outlive coordinator).
     * @param global_clock Pointer to the GlobalSynchronizationClock for this rate group (must outlive coordinator).
     * @param barrier_timeout_ms Maximum time to wait at barrier before proceeding anyway (default: 50ms).
     * 
     * @details Constructs the coordinator but does NOT register with the global clock yet.
     *          Call enable() to activate coordination and register with the clock.
     * 
     * Thread Safety: Safe to call from any thread.
     */
    SinkSynchronizationCoordinator(
        const std::string& sink_id,
        SinkAudioMixer* mixer,
        GlobalSynchronizationClock* global_clock,
        int barrier_timeout_ms = 50);
    
    /**
     * @brief Destructor - unregisters from global clock if enabled.
     * 
     * @details Automatically unregisters this sink from the global synchronization clock
     *          to prevent other sinks from waiting at the barrier for a destroyed sink.
     * 
     * Thread Safety: Should be called from the same thread that owns the mixer.
     */
    ~SinkSynchronizationCoordinator();
    
    /**
     * @brief Main coordination method - implements synchronized dispatch workflow.
     * @return True if dispatch succeeded, false if underrun or coordination failed.
     * 
     * @details This method should be called from the mixer's run loop when coordination
     *          is enabled. It implements the complete synchronized dispatch workflow:
     * 
     *          1. Get target playback timestamp from global clock
     *          2. Check if mixer has buffered audio ready
     *          3. Calculate and apply rate adjustment if needed
     *          4. Wait at barrier for all sinks in this rate group
     *          5. Dispatch audio to network (synchronized with other sinks)
     *          6. Update internal sample counters
     *          7. Report timing back to global clock
     * 
     *          If the mixer has no data ready (underrun), this method:
     *          - Increments underrun counter
     *          - Reports underrun to global clock
     *          - Returns false (no audio dispatched)
     * 
     *          If barrier timeout occurs, this method:
     *          - Proceeds with dispatch anyway (graceful degradation)
     *          - Increments timeout counter
     *          - Logs warning (if diagnostics enabled)
     * 
     * Thread Safety: Must be called from mixer's run thread only.
     * 
     * Architecture Reference: MULTI_SPEAKER_SYNC_ARCHITECTURE.md lines 569-603
     */
    /**
     * @brief Waits on the shared barrier and prepares for the next dispatch cycle.
     * @return True if the mixer should proceed with mixing/output, false otherwise.
     */
    bool begin_dispatch();

    /**
     * @brief Finalizes a dispatch using measured execution timings from the mixer.
     * @param samples_output Number of PCM frames emitted during the dispatch.
     * @param timing Detailed timing information captured by the mixer.
     */
    double complete_dispatch(uint64_t samples_output, const DispatchTimingInfo& timing);
    
    /**
     * @brief Checks if coordination is currently enabled.
     * @return True if coordination is active, false if in legacy mode.
     * 
     * @details The mixer should check this before calling begin_dispatch().
     *          If false, mixer should use legacy immediate dispatch mode.
     * 
     * Thread Safety: Safe to call from any thread (atomic read).
     */
    bool should_coordinate() const { 
        return coordination_enabled_ && global_clock_->is_enabled(); 
    }
    
    /**
     * @brief Enables coordination and registers with global clock.
     * 
     * @details Activates synchronized dispatch mode for this sink. The sink will:
     *          - Register with the global synchronization clock
     *          - Participate in barrier synchronization
     *          - Receive rate adjustments for drift compensation
     *          - Report timing information after each dispatch
     * 
     * Thread Safety: Safe to call from any thread.
     */
    void enable();
    
    /**
     * @brief Disables coordination and unregisters from global clock.
     * 
     * @details Deactivates synchronized dispatch mode. The sink will:
     *          - Unregister from the global synchronization clock
     *          - Revert to legacy immediate dispatch mode
     *          - Stop participating in barrier synchronization
     * 
     * Thread Safety: Safe to call from any thread.
     */
    void disable();
    
    /**
     * @brief Retrieves current statistics from this coordinator.
     * @return Struct containing performance and synchronization metrics.
     * 
     * @details Provides monitoring data including:
     *          - Total dispatches and samples output
     *          - Underrun and timeout counts
     *          - Current rate adjustment factor
     *          - Coordination enabled state
     * 
     * Thread Safety: Safe to call from any thread (uses atomic reads).
     */
    CoordinatorStats get_statistics() const;
    
    /**
     * @brief Sets the barrier timeout duration.
     * @param timeout_ms Maximum time to wait at barrier in milliseconds.
     * 
     * @details Adjusts how long this sink will wait for other sinks at the barrier
     *          before proceeding anyway. Typical values:
     *          - 20ms: Low latency mode
     *          - 50ms: Default balanced mode
     *          - 100ms: High reliability mode
     * 
     * Thread Safety: Safe to call from any thread.
     */
    void set_barrier_timeout(int timeout_ms);
    
    /**
     * @brief Gets the current barrier timeout setting.
     * @return Barrier timeout in milliseconds.
     * 
     * Thread Safety: Safe to call from any thread (atomic read).
     */
    int get_barrier_timeout() const { return barrier_timeout_ms_; }
    
    /**
     * @brief Gets the sink ID this coordinator manages.
     * @return Unique sink identifier string.
     * 
     * Thread Safety: Safe to call from any thread (immutable after construction).
     */
    const std::string& get_sink_id() const { return sink_id_; }

private:
    // --- Configuration ---
    
    /** @brief Unique identifier for this sink. */
    std::string sink_id_;
    
    /** @brief Pointer to the wrapped mixer (not owned). */
    SinkAudioMixer* mixer_;
    
    /** @brief Pointer to the global clock for this rate group (not owned). */
    GlobalSynchronizationClock* global_clock_;
    
    /** @brief Maximum time to wait at barrier in milliseconds. */
    std::atomic<int> barrier_timeout_ms_;
    
    // --- State Tracking ---
    
    /** @brief Total samples output by this sink since registration. */
    uint64_t total_samples_output_ = 0;
    
    /** @brief RTP timestamp of the last dispatched audio chunk. */
    uint64_t last_output_rtp_timestamp_ = 0;
    
    /** @brief Whether coordination is currently enabled. */
    std::atomic<bool> coordination_enabled_{false};
    
    // --- Statistics (atomic for lock-free access) ---
    
    /** @brief Total number of successful dispatches. */
    std::atomic<uint64_t> total_dispatches_{0};
    
    /** @brief Number of barrier timeout events. */
    std::atomic<uint64_t> barrier_timeouts_{0};
    
    /** @brief Number of buffer underrun events. */
    std::atomic<uint64_t> underruns_{0};

    /** @brief Last rate adjustment communicated by the global clock. */
    std::atomic<double> last_rate_adjustment_{1.0};
    
    // --- Helper Methods ---
    
    /**
     * @brief Reports timing information to the global clock after dispatch.
     * @param samples_sent Number of samples dispatched in this operation.
     * @param had_underrun True if this dispatch experienced an underrun.
     * 
     * @details Constructs a SinkTimingReport and sends it to the global clock
     *          for drift tracking and rate adjustment calculation.
     * 
     * @note Must be called after each dispatch operation (success or underrun).
     */
    void report_timing_to_global_clock(uint64_t samples_sent,
                                      bool had_underrun,
                                      double buffer_fill,
                                      const DispatchTimingInfo& timing,
                                      uint64_t rtp_start_timestamp);
};

} // namespace audio
} // namespace screamrouter

#endif // SINK_SYNCHRONIZATION_COORDINATOR_H
