/**
 * @file timeshift_manager.h
 * @brief Defines the TimeshiftManager class for handling global timeshifting and dejittering.
 * @details This class manages a global buffer of incoming audio packets from all sources.
 *          It allows multiple "processors" (consumers) to read from this buffer at different
 *          points in time, enabling synchronized playback and timeshifting capabilities.
 *          It also performs basic dejittering based on RTP timestamps.
 */
#ifndef TIMESHIFT_MANAGER_H
#define TIMESHIFT_MANAGER_H

#include "../utils/audio_component.h"
#include "../utils/thread_safe_queue.h"
#include "../audio_types.h"
#include "../configuration/audio_engine_settings.h"

#include <string>
#include <vector>
#include "../utils/object_ring_buffer.h"
#include "stream_clock.h"
#include <map>
#include <chrono>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <limits>

namespace screamrouter {
namespace audio {

using PacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>;

/**
 * @struct ProcessorTargetInfo
 * @brief Holds information about a registered consumer (processor) of the timeshift buffer.
 */
struct ProcessorTargetInfo {
    /** @brief The queue to which packets for this processor should be sent. */
    std::shared_ptr<PacketQueue> target_queue;
    /** @brief The current static delay in milliseconds for this processor. */
    int current_delay_ms;
    /** @brief The current timeshift delay in seconds for this processor. */
    float current_timeshift_backshift_sec;
    /** @brief The index into the global buffer where this processor should read its next packet. */
    size_t next_packet_read_index;
    /** @brief The configured source tag filter (may include wildcard suffix). */
    std::string source_tag_filter;
    /** @brief Indicates this filter uses a trailing '*' wildcard. */
    bool is_wildcard = false;
    /** @brief Prefix to match when wildcarding. */
    std::string wildcard_prefix;
    /** @brief Bound concrete tag once a wildcard matches a stream. */
    std::string bound_source_tag;
};

/**
 * @struct StreamTimingState
 * @brief Holds state information for dejittering a specific audio stream.
 */
struct StreamTimingState {
    bool is_first_packet = true;
    uint32_t last_rtp_timestamp = 0;
    std::chrono::steady_clock::time_point last_wallclock;
    std::unique_ptr<StreamClock> clock;

    // Jitter estimation (RFC 3550)
    double jitter_estimate = 1.0; // Start with 1ms default jitter
    double system_jitter_estimate_ms = 1.0;
    double last_system_delay_ms = 0.0;

    // Playout buffer state
    double current_buffer_level_ms = 0.0;
    double current_playback_rate = 1.0;
    uint32_t last_played_rtp_timestamp = 0;
    double last_arrival_time_error_ms = 0.0; // For stats
    double target_buffer_level_ms = 0.0;
    double buffer_target_fill_percentage = 0.0;
    std::chrono::steady_clock::time_point last_target_update_time{};

    // Stats
    std::atomic<uint64_t> total_packets{0};
    std::atomic<uint64_t> late_packets_count{0};
    std::atomic<uint64_t> tm_buffer_underruns{0};
    std::atomic<uint64_t> tm_packets_discarded{0};
    std::atomic<uint64_t> lagging_events_count{0};

    // Detailed profiling accumulators
    double arrival_error_ms_sum = 0.0;
    double arrival_error_ms_abs_sum = 0.0;
    double arrival_error_ms_max = -std::numeric_limits<double>::infinity();
    double arrival_error_ms_min = std::numeric_limits<double>::infinity();
    uint64_t arrival_error_samples = 0;

    double playout_deviation_ms_sum = 0.0;
    double playout_deviation_ms_abs_sum = 0.0;
    double playout_deviation_ms_max = -std::numeric_limits<double>::infinity();
    double playout_deviation_ms_min = std::numeric_limits<double>::infinity();
    uint64_t playout_deviation_samples = 0;

    double head_playout_lag_ms_sum = 0.0;
    double head_playout_lag_ms_max = -std::numeric_limits<double>::infinity();
    uint64_t head_playout_lag_samples = 0;
    double last_head_playout_lag_ms = 0.0;

    double last_clock_offset_ms = 0.0;
    double last_clock_drift_ppm = 0.0;
    double last_clock_innovation_ms = 0.0;
    double last_clock_measured_offset_ms = 0.0;
    double clock_innovation_abs_sum_ms = 0.0;
    uint64_t clock_innovation_samples = 0;
};

/**
 * @struct TimeshiftManagerStats
 * @brief Holds raw statistics collected from the TimeshiftManager.
 */
struct TimeshiftManagerStats {
    uint64_t total_packets_added = 0;
    size_t global_buffer_size = 0;
    std::map<std::string, double> jitter_estimates;
    std::map<std::string, uint64_t> stream_total_packets;
    std::map<std::string, size_t> processor_read_indices;
    std::map<std::string, uint64_t> stream_late_packets;
    std::map<std::string, uint64_t> stream_lagging_events;
    std::map<std::string, uint64_t> stream_tm_buffer_underruns;
    std::map<std::string, uint64_t> stream_tm_packets_discarded;
    std::map<std::string, double> stream_last_arrival_time_error_ms;
    std::map<std::string, double> stream_target_buffer_level_ms;
    std::map<std::string, double> stream_buffer_target_fill_percentage;
    std::map<std::string, double> stream_avg_arrival_error_ms;
    std::map<std::string, double> stream_avg_abs_arrival_error_ms;
    std::map<std::string, double> stream_max_arrival_error_ms;
    std::map<std::string, double> stream_min_arrival_error_ms;
    std::map<std::string, uint64_t> stream_arrival_error_sample_count;
    std::map<std::string, double> stream_avg_playout_deviation_ms;
    std::map<std::string, double> stream_avg_abs_playout_deviation_ms;
    std::map<std::string, double> stream_max_playout_deviation_ms;
    std::map<std::string, double> stream_min_playout_deviation_ms;
    std::map<std::string, uint64_t> stream_playout_deviation_sample_count;
    std::map<std::string, double> stream_avg_head_playout_lag_ms;
    std::map<std::string, double> stream_max_head_playout_lag_ms;
    std::map<std::string, uint64_t> stream_head_playout_lag_sample_count;
    std::map<std::string, double> stream_last_head_playout_lag_ms;
    std::map<std::string, double> stream_clock_offset_ms;
    std::map<std::string, double> stream_clock_drift_ppm;
    std::map<std::string, double> stream_clock_last_innovation_ms;
    std::map<std::string, double> stream_clock_avg_abs_innovation_ms;
    std::map<std::string, double> stream_system_jitter_ms;
    std::map<std::string, double> stream_clock_last_measured_offset_ms;
};

/**
 * @class TimeshiftManager
 * @brief Manages a global timeshift buffer for multiple audio streams and processors.
 * @details This component runs a thread to manage a central buffer of all incoming audio packets.
 *          It allows multiple `SourceInputProcessor` instances to register as consumers, each with its
 *          own delay and timeshift settings. The manager is responsible for dispatching packets
 *          from the buffer to the correct processors at the correct time.
 */
class TimeshiftManager : public AudioComponent {
public:
    /**
     * @brief Constructs a TimeshiftManager.
     * @param max_buffer_duration The maximum duration of audio to hold in the global buffer.
     * @param settings The shared audio engine settings.
     */
    TimeshiftManager(std::chrono::seconds max_buffer_duration, std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings);
    /**
     * @brief Destructor.
     */
    ~TimeshiftManager() override;

    /** @brief Starts the manager's processing thread. */
    void start() override;
    /** @brief Stops the manager's processing thread. */
    void stop() override;

    /**
     * @brief Adds a new audio packet to the global buffer.
     * @param packet The packet to add.
     */
    void add_packet(TaggedAudioPacket&& packet);
    /**
     * @brief Registers a new processor as a consumer of the buffer.
     * @param instance_id A unique ID for the processor instance.
     * @param source_tag The source tag the processor is interested in.
     * @param target_queue The processor's input queue.
     * @param initial_delay_ms The initial static delay for the processor.
     * @param initial_timeshift_sec The initial timeshift delay for the processor.
     */
    void register_processor(const std::string& instance_id, const std::string& source_tag, std::shared_ptr<PacketQueue> target_queue, int initial_delay_ms, float initial_timeshift_sec);
    /**
     * @brief Unregisters a processor.
     * @param instance_id The ID of the processor instance to unregister.
     * @param source_tag The source tag associated with the processor.
     */
    void unregister_processor(const std::string& instance_id, const std::string& source_tag);
    /**
     * @brief Updates the static delay for a registered processor.
     * @param instance_id The ID of the processor.
     * @param delay_ms The new delay in milliseconds.
     */
    void update_processor_delay(const std::string& instance_id, int delay_ms);
    /**
     * @brief Updates the timeshift delay for a registered processor.
     * @param instance_id The ID of the processor.
     * @param timeshift_sec The new timeshift in seconds.
     */
    void update_processor_timeshift(const std::string& instance_id, float timeshift_sec);
    /**
     * @brief Retrieves the current statistics from the manager.
     * @return A struct containing the current stats.
     */
    TimeshiftManagerStats get_stats();


    /**
     * @brief Resets timing state and pending buffer indices for a specific source tag.
     * @param source_tag The composite source tag whose timing state should be cleared.
     */
    void reset_stream_state(const std::string& source_tag);

protected:
    /** @brief The main loop for the manager's thread. */
    void run() override;

private:
    ::screamrouter::audio::utils::ObjectRingBuffer<TaggedAudioPacket> global_timeshift_buffer_;
    // Map: source_tag -> instance_id -> ProcessorTargetInfo
    std::map<std::string, std::map<std::string, ProcessorTargetInfo>> processor_targets_;
    std::mutex data_mutex_;
    std::shared_ptr<screamrouter::audio::AudioEngineSettings> m_settings;

    std::map<std::string, StreamTimingState> stream_timing_states_;
    std::mutex timing_mutex_;

    std::condition_variable run_loop_cv_;
    std::chrono::seconds max_buffer_duration_sec_;
    std::chrono::steady_clock::time_point last_cleanup_time_;

    /** @brief A single iteration of the processing loop. Assumes data_mutex_ is held. */
    void processing_loop_iteration_unlocked();
    /** @brief Periodically cleans up old packets from the global buffer. Assumes data_mutex_ is held. */
    void cleanup_global_buffer_unlocked();
    /**
     * @brief Calculates the time point for the next event to occur.
     * @return The time point of the next scheduled event.
     */
    std::chrono::steady_clock::time_point calculate_next_wakeup_time();

    std::atomic<uint64_t> m_state_version_{0};
    std::atomic<uint64_t> m_total_packets_added{0};

    // --- Profiling ---
    void reset_profiler_counters_unlocked(std::chrono::steady_clock::time_point now);
    void maybe_log_profiler_unlocked(std::chrono::steady_clock::time_point now);
    std::chrono::steady_clock::time_point profiling_last_log_time_;
    uint64_t profiling_packets_dispatched_{0};
    uint64_t profiling_packets_dropped_{0};
    uint64_t profiling_packets_late_count_{0};
    double profiling_total_lateness_ms_{0.0};

    // --- Scheduling Budget Estimation ---
    double smoothed_processing_per_packet_us_{0.0};
    bool processing_budget_initialized_{false};
    std::chrono::steady_clock::time_point last_iteration_finish_time_{};
};

} // namespace audio
} // namespace screamrouter

#endif // TIMESHIFT_MANAGER_H
