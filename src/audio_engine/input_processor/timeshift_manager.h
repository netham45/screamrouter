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
#include "../audio_types.h"
#include "../configuration/audio_engine_settings.h"
#include "../utils/packet_ring.h"

#include <string>
#include <vector>
#include <deque>
#include "stream_clock.h"
#include <map>
#include <chrono>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace screamrouter {
namespace audio {

using PacketRing = utils::PacketRing<TaggedAudioPacket>;

/**
 * @struct TimeshiftBufferExport
 * @brief Represents a contiguous PCM dump taken from the timeshift buffer.
 */
struct TimeshiftBufferExport {
    std::vector<uint8_t> pcm_data;               ///< Raw PCM payload concatenated from packets.
    int sample_rate = 0;                         ///< Sample rate in Hz.
    int channels = 0;                            ///< Channel count.
    int bit_depth = 0;                           ///< Bits per sample per channel.
    std::size_t chunk_size_bytes = 0;            ///< Size of the originating packet chunks.
    double duration_seconds = 0.0;               ///< Approximate duration of the exported audio.
    double earliest_packet_age_seconds = 0.0;    ///< Age of the oldest packet relative to export time.
    double latest_packet_age_seconds = 0.0;      ///< Age of the newest packet relative to export time.
    double lookback_seconds_requested = 0.0;     ///< Lookback window requested by caller.
};

/**
 * @struct ProcessorTargetInfo
 * @brief Holds information about a registered consumer (processor) of the timeshift buffer.
 */
struct ProcessorTargetInfo {
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
    /** @brief Per-sink ready rings for dispatch. */
    std::map<std::string, std::weak_ptr<PacketRing>> sink_rings;
    uint64_t dropped_packets = 0;
    /** @brief Owning processor instance identifier. */
    std::string instance_id;
    /** @brief Tracks concrete tags already observed for this wildcard. */
    std::unordered_set<std::string> matched_concrete_tags;
    /** @brief Last actual tag logged for mismatch diagnostics. */
    std::string last_logged_mismatch_tag;
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
    bool jitter_initialized = false;
    double rfc3550_jitter_sec = 0.0;
    double jitter_estimate = 1.0; // Start with 1ms default jitter
    double system_jitter_estimate_ms = 1.0;
    double last_system_delay_ms = 0.0;
    double last_arrival_time_sec = 0.0;
    double last_transit_sec = 0.0;

    // Playout state
    double current_playback_rate = 1.0;
    double target_buffer_level_ms = 0.0;
    std::chrono::steady_clock::time_point last_target_update_time{};
    double current_buffer_level_ms = 0.0;
    double buffer_target_fill_percentage = 0.0;
    uint32_t last_played_rtp_timestamp = 0;
    std::chrono::steady_clock::time_point last_controller_update_time{};
    double playback_ratio_integral_ppm = 0.0;
    double last_arrival_time_error_ms = 0.0; // For stats
    int sample_rate = 0;
    int channels = 0;
    int bit_depth = 0;
    uint32_t samples_per_chunk = 0;
    double buffer_fill_error_ratio = 0.0;

    // Stats
    std::atomic<uint64_t> total_packets{0};
    std::atomic<uint64_t> late_packets_count{0};
    std::atomic<uint64_t> tm_buffer_underruns{0};
    std::atomic<uint64_t> tm_packets_discarded{0};
    std::atomic<uint64_t> lagging_events_count{0};
    std::chrono::steady_clock::time_point last_late_log_time{};
    std::chrono::steady_clock::time_point last_discard_log_time{};
    std::chrono::steady_clock::time_point last_reanchor_log_time{};

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

    // Reanchoring state
    std::chrono::steady_clock::time_point last_reanchor_time{};
    std::atomic<uint64_t> reanchor_count{0};
    std::atomic<uint64_t> consecutive_late_packets{0};
    double cumulative_lateness_ms = 0.0;
    std::atomic<uint64_t> packets_skipped_on_reanchor{0};
    bool is_reanchored = false;  // True immediately after reanchor, cleared when clock re-initializes
    uint64_t packets_since_reanchor = 0;  // Counter reset on reanchor, incremented each packet
};

/**
 * @struct TimeshiftManagerStats
 * @brief Holds raw statistics collected from the TimeshiftManager.
 */
struct TimeshiftManagerStats {
    uint64_t total_packets_added = 0;
    uint64_t total_inbound_received = 0;
    uint64_t total_inbound_dropped = 0;
    size_t inbound_queue_size = 0;
    size_t inbound_queue_high_water = 0;
    size_t global_buffer_size = 0;
    std::map<std::string, double> jitter_estimates;
    std::map<std::string, uint64_t> stream_total_packets;
    std::map<std::string, size_t> stream_buffered_packets;
    std::map<std::string, double> stream_buffered_duration_ms;
    std::map<std::string, size_t> processor_read_indices;
    std::map<std::string, uint64_t> stream_late_packets;
    std::map<std::string, uint64_t> stream_lagging_events;
    std::map<std::string, uint64_t> stream_tm_buffer_underruns;
    std::map<std::string, uint64_t> stream_tm_packets_discarded;
    std::map<std::string, double> stream_last_arrival_time_error_ms;
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
    std::map<std::string, double> stream_target_buffer_level_ms;
    std::map<std::string, double> stream_buffer_target_fill_percentage;
    std::map<std::string, double> stream_system_jitter_ms;
    std::map<std::string, double> stream_clock_last_measured_offset_ms;
    std::map<std::string, double> stream_last_system_delay_ms;
    std::map<std::string, double> stream_playback_rate;
    // Reanchoring stats
    std::map<std::string, uint64_t> stream_reanchor_count;
    std::map<std::string, double> stream_time_since_last_reanchor_ms;
    std::map<std::string, uint64_t> stream_packets_skipped_on_reanchor;
    struct ProcessorStats {
        std::string instance_id;
        std::string source_tag;
        size_t pending_packets = 0;
        double pending_ms = 0.0;
        size_t target_queue_depth = 0;
        size_t target_queue_high_water = 0;
        uint64_t dispatched_packets = 0;
        uint64_t dropped_packets = 0;
    };
    std::map<std::string, ProcessorStats> processor_stats;
};

struct WildcardMatchEvent {
    std::string processor_instance_id;
    std::string filter_tag;
    std::string concrete_tag;
    bool is_primary_binding = false;
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
     * @brief Export the most recent PCM window for a specific source.
     * @param source_tag The source identifier to filter packets by.
     * @param lookback_duration Duration of history to export.
     * @return Populated export data on success, std::nullopt if no data found.
     */
    std::optional<TimeshiftBufferExport> export_recent_buffer(
        const std::string& source_tag,
        std::chrono::milliseconds lookback_duration);

    /**
     * @brief Registers a new processor as a consumer of the buffer.
     * @param instance_id A unique ID for the processor instance.
     * @param source_tag The source tag the processor is interested in.
     * @param initial_delay_ms The initial static delay for the processor.
     * @param initial_timeshift_sec The initial timeshift delay for the processor.
     */
    void register_processor(const std::string& instance_id, const std::string& source_tag, int initial_delay_ms, float initial_timeshift_sec);
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
     * @brief Registers a sink-ready ring for a processor instance.
     */
    void attach_sink_ring(const std::string& instance_id, const std::string& source_tag, const std::string& sink_id, std::shared_ptr<PacketRing> ring);
    /**
     * @brief Detaches a sink-ready ring.
     */
    void detach_sink_ring(const std::string& instance_id, const std::string& source_tag, const std::string& sink_id);

    /**
     * @brief Registers a listener for wildcard source matches.
     */
    void set_wildcard_match_callback(std::function<void(const WildcardMatchEvent&)> cb);
    /**
     * @brief Retrieves the current statistics from the manager.
     * @return A struct containing the current stats.
     */
    TimeshiftManagerStats get_stats();

    std::shared_ptr<screamrouter::audio::AudioEngineSettings> get_settings() const { return m_settings; }


    /**
     * @brief Resets timing state and pending buffer indices for a specific source tag.
     * @param source_tag The composite source tag whose timing state should be cleared.
     */
    void reset_stream_state(const std::string& source_tag);

protected:
    /** @brief The main loop for the manager's thread. */
    void run() override;

private:
    struct TimingStateAccess {
        std::unique_lock<std::mutex> lock;
        StreamTimingState* state = nullptr;

        TimingStateAccess() = default;
        TimingStateAccess(std::unique_lock<std::mutex>&& l, StreamTimingState* s)
            : lock(std::move(l)), state(s) {}
    };

    std::deque<TaggedAudioPacket> global_timeshift_buffer_;
    // Map: source_tag -> instance_id -> ProcessorTargetInfo
    std::map<std::string, std::map<std::string, ProcessorTargetInfo>> processor_targets_;
    std::mutex data_mutex_;
    std::shared_ptr<screamrouter::audio::AudioEngineSettings> m_settings;

    std::map<std::string, StreamTimingState> stream_timing_states_;
    std::mutex timing_mutex_;
    std::mutex timing_map_mutex_;
    std::unordered_map<std::string, std::shared_ptr<std::mutex>> timing_locks_;

    std::condition_variable run_loop_cv_;
    std::chrono::seconds max_buffer_duration_sec_;
    std::chrono::steady_clock::time_point last_cleanup_time_;

    // Inbound queue metrics
    std::atomic<uint64_t> m_inbound_received{0};
    std::atomic<uint64_t> m_inbound_dropped{0};
    std::atomic<size_t> m_inbound_high_water{0};

    // Inbound decoupling to avoid blocking capture threads on the main data mutex.
    utils::ThreadSafeQueue<TaggedAudioPacket> inbound_queue_;
    static constexpr std::size_t kInboundQueueMaxSize = 1024;
    std::chrono::steady_clock::time_point last_inbound_drop_log_{};

    // Per-processor dispatch/drop accounting
    std::mutex processor_stats_mutex_;
    std::map<std::string, uint64_t> processor_dispatched_totals_;
    std::map<std::string, uint64_t> processor_dropped_totals_;
    std::map<std::string, size_t> processor_queue_high_water_;
    std::function<void(const WildcardMatchEvent&)> wildcard_match_callback_;
    mutable std::mutex wildcard_callback_mutex_;

    /** @brief A single iteration of the processing loop. Collects ready packets while data_mutex_ is held. */
    void processing_loop_iteration_unlocked(std::vector<WildcardMatchEvent>& wildcard_matches);
    /** @brief Periodically cleans up old packets from the global buffer. Assumes data_mutex_ is held. */
    void cleanup_global_buffer_unlocked();

    /** @brief Process one inbound packet while holding data_mutex_. */
    void process_incoming_packet_unlocked(TaggedAudioPacket&& packet);
    void ingest_packet_locked(TaggedAudioPacket&& packet, std::unique_lock<std::mutex>& data_lock);
    /**
     * @brief Calculates the time point for the next event to occur.
     * @return The time point of the next scheduled event.
     */
    std::chrono::steady_clock::time_point calculate_next_wakeup_time();

    /**
     * @brief Reanchors a stream's timing state to recover from excessive latency.
     * @param source_tag The source tag of the stream to reanchor.
     * @param timing_state The timing state of the stream (must be under timing_mutex_).
     * @param reason A string describing why the reanchor was triggered.
     */
    void reanchor_stream_unlocked(const std::string& source_tag,
                                  StreamTimingState& timing_state,
                                  const std::string& reason);

    std::atomic<uint64_t> m_state_version_{0};
    std::atomic<uint64_t> m_total_packets_added{0};

    // --- Profiling ---
    void reset_profiler_counters_unlocked(std::chrono::steady_clock::time_point now);
    void maybe_log_profiler_unlocked(std::chrono::steady_clock::time_point now);
    std::chrono::steady_clock::time_point profiling_last_log_time_;
    std::chrono::steady_clock::time_point telemetry_last_log_time_{};
    uint64_t profiling_packets_dispatched_{0};
    uint64_t profiling_packets_dropped_{0};
    uint64_t profiling_packets_late_count_{0};
    double profiling_total_lateness_ms_{0.0};

    // --- Scheduling Budget Estimation ---
    double smoothed_processing_per_packet_us_{0.0};
    bool processing_budget_initialized_{false};
    std::chrono::steady_clock::time_point last_iteration_finish_time_{};

    // --- Timing helpers ---
    std::shared_ptr<std::mutex> acquire_timing_lock(const std::string& source_tag);
    TimingStateAccess get_timing_state(const std::string& source_tag);
    TimingStateAccess get_or_create_timing_state(const std::string& source_tag);
};

} // namespace audio
} // namespace screamrouter

#endif // TIMESHIFT_MANAGER_H
