/**
 * @file timeshift_manager.cpp
 * @brief Implements the TimeshiftManager class for handling global timeshifting.
 * @details This file contains the implementation of the TimeshiftManager, which manages
 *          a global buffer of audio packets to enable timeshifting and synchronized playback
 *          across multiple consumers.
 */
#include "timeshift_manager.h"
#include "stream_clock.h"
#include "../utils/cpp_logger.h"
#include "../audio_types.h"

#include <iostream>
#include <algorithm>
#include <thread>
#include <utility>
#include <vector>
#include <cmath>

namespace screamrouter {
namespace audio {

namespace {
constexpr double kProcessingBudgetAlpha = 0.2;
constexpr double kPlaybackDriftGain = 1.0 / 1'000'000.0; // Convert ppm to ratio
constexpr double kFallbackSmoothing = 0.1;

bool has_prefix(const std::string& value, const std::string& prefix) {
    if (prefix.empty()) {
        return true;
    }
    if (value.size() < prefix.size()) {
        return false;
    }
    return value.compare(0, prefix.size(), prefix) == 0;
}

bool match_and_bind_source(ProcessorTargetInfo& info, const std::string& actual_tag) {
    if (!info.is_wildcard) {
        return actual_tag == info.source_tag_filter;
    }
    if (!info.bound_source_tag.empty()) {
        return info.bound_source_tag == actual_tag;
    }
    if (has_prefix(actual_tag, info.wildcard_prefix)) {
        info.bound_source_tag = actual_tag;
        LOG_CPP_INFO("[TimeshiftManager] Bound wildcard '%s*' -> '%s'",
                     info.wildcard_prefix.c_str(), actual_tag.c_str());
        return true;
    }
    return false;
}

const std::string& active_tag(const ProcessorTargetInfo& info) {
    return info.is_wildcard ? info.bound_source_tag : info.source_tag_filter;
}

uint32_t rtp_timestamp_diff(uint32_t current, uint32_t previous) {
    return current - previous;
}

double smooth_playback_rate(double previous_rate,
                            double target_rate,
                            double smoothing_factor,
                            double max_deviation_ppm) {
    const double max_deviation_ratio =
        std::max(max_deviation_ppm, 0.0) * kPlaybackDriftGain;
    const double clamped_target =
        std::clamp(target_rate, 1.0 - max_deviation_ratio, 1.0 + max_deviation_ratio);
    const double clamped_smoothing = std::clamp(smoothing_factor, 0.0, 1.0);
    const double blended =
        previous_rate * (1.0 - clamped_smoothing) + clamped_target * clamped_smoothing;
    return std::clamp(blended, 1.0 - max_deviation_ratio, 1.0 + max_deviation_ratio);
}
} // namespace

/**
 * @brief Constructs a TimeshiftManager.
 * @param max_buffer_duration The maximum duration of audio to hold in the global buffer.
 */
TimeshiftManager::TimeshiftManager(std::chrono::seconds max_buffer_duration, std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings)
    : max_buffer_duration_sec_(max_buffer_duration),
      m_settings(settings),
      last_cleanup_time_(std::chrono::steady_clock::now()),
      profiling_last_log_time_(std::chrono::steady_clock::now()) {
    LOG_CPP_INFO("[TimeshiftManager] Initializing with max buffer duration: %llds", (long long)max_buffer_duration_sec_.count());
}

/**
 * @brief Destructor for the TimeshiftManager.
 */
TimeshiftManager::~TimeshiftManager() {
    LOG_CPP_INFO("[TimeshiftManager] Destroying...");
    if (!stop_flag_) {
        stop();
    }
    LOG_CPP_INFO("[TimeshiftManager] Destruction complete.");
}

std::shared_ptr<std::mutex> TimeshiftManager::acquire_timing_lock(const std::string& source_tag) {
    std::lock_guard<std::mutex> map_lock(timing_map_mutex_);
    auto [it, inserted] = timing_locks_.try_emplace(source_tag);
    if (inserted || !it->second) {
        it->second = std::make_shared<std::mutex>();
    }
    return it->second;
}

TimeshiftManager::TimingStateAccess TimeshiftManager::get_timing_state(const std::string& source_tag) {
    std::unique_lock<std::mutex> map_lock(timing_map_mutex_);
    auto lock_it = timing_locks_.find(source_tag);
    if (lock_it == timing_locks_.end() || !lock_it->second) {
        map_lock.unlock();
        return {};
    }

    std::unique_lock<std::mutex> per_stream_lock(*lock_it->second);
    auto state_it = stream_timing_states_.find(source_tag);
    StreamTimingState* state_ptr = (state_it != stream_timing_states_.end()) ? &state_it->second : nullptr;
    map_lock.unlock();
    return TimingStateAccess(std::move(per_stream_lock), state_ptr);
}

TimeshiftManager::TimingStateAccess TimeshiftManager::get_or_create_timing_state(const std::string& source_tag) {
    std::unique_lock<std::mutex> map_lock(timing_map_mutex_);
    auto [lock_it, inserted_lock] = timing_locks_.try_emplace(source_tag);
    if (inserted_lock || !lock_it->second) {
        lock_it->second = std::make_shared<std::mutex>();
    }

    std::unique_lock<std::mutex> per_stream_lock(*lock_it->second);
    auto [state_it, inserted_state] = stream_timing_states_.try_emplace(source_tag);
    (void)inserted_state;
    StreamTimingState* state_ptr = &state_it->second;
    map_lock.unlock();
    return TimingStateAccess(std::move(per_stream_lock), state_ptr);
}

/**
 * @brief Starts the manager's processing thread.
 */
void TimeshiftManager::start() {
    if (is_running()) {
        LOG_CPP_WARNING("[TimeshiftManager] Already running.");
        return;
    }
    LOG_CPP_INFO("[TimeshiftManager] Starting...");
    stop_flag_ = false;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        reset_profiler_counters_unlocked(std::chrono::steady_clock::now());
    }
    try {
        component_thread_ = std::thread(&TimeshiftManager::run, this);
        LOG_CPP_INFO("[TimeshiftManager] Component thread launched.");
    } catch (const std::system_error& e) {
        LOG_CPP_ERROR("[TimeshiftManager] Failed to start component thread: %s", e.what());
        stop_flag_ = true;
        throw;
    }
}

/**
 * @brief Stops the manager's processing thread.
 */
void TimeshiftManager::stop() {
    if (stop_flag_) {
        LOG_CPP_WARNING("[TimeshiftManager] Already stopped or stopping.");
        return;
    }
    size_t buf_size = 0;
    size_t processor_count = 0;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        buf_size = global_timeshift_buffer_.size();
        for (auto const& kv : processor_targets_) {
            processor_count += kv.second.size();
        }
    }
    LOG_CPP_INFO("[TimeshiftManager] Stopping... buffer=%zu processors=%zu", buf_size, processor_count);
    stop_flag_ = true;
    m_state_version_++;
    run_loop_cv_.notify_all();

    if (component_thread_.joinable()) {
        try {
            component_thread_.join();
            LOG_CPP_INFO("[TimeshiftManager] Component thread joined.");
        } catch (const std::system_error& e) {
            LOG_CPP_ERROR("[TimeshiftManager] Error joining component thread: %s", e.what());
        }
    } else {
        LOG_CPP_WARNING("[TimeshiftManager] Component thread was not joinable in stop().");
    }
    LOG_CPP_INFO("[TimeshiftManager] Stopped.");
}

/**
 * @brief Adds a new audio packet to the global buffer and performs jitter calculation.
 * @param packet The packet to add, moved into the buffer.
 */
void TimeshiftManager::add_packet(TaggedAudioPacket&& packet) {
     if (stop_flag_ || !packet.rtp_timestamp.has_value() || packet.sample_rate <= 0) {
         return;
     }

    const uint32_t frames_per_second = static_cast<uint32_t>(packet.sample_rate);
    const double configured_reset_threshold_sec =
        (m_settings) ? m_settings->timeshift_tuning.rtp_session_reset_threshold_seconds : 0.2;
    const double bounded_reset_threshold_sec = std::max(configured_reset_threshold_sec, 0.0);
    const uint32_t reset_threshold_frames = static_cast<uint32_t>(
        static_cast<double>(frames_per_second) * bounded_reset_threshold_sec);

    std::unique_lock<std::mutex> data_lock(data_mutex_);
    auto timing_access = get_or_create_timing_state(packet.source_tag);
    StreamTimingState* state_ptr = timing_access.state;
    if (!state_ptr) {
        data_lock.unlock();
        return;
    }

    bool should_reset = false;
    const uint32_t current_ts = packet.rtp_timestamp.value();
    const uint32_t last_ts = state_ptr->last_rtp_timestamp;

    if (!state_ptr->is_first_packet && state_ptr->clock && reset_threshold_frames > 0) {
        const uint32_t delta = rtp_timestamp_diff(current_ts, last_ts);
        should_reset = delta > reset_threshold_frames;

        if (should_reset) {
            const auto last_wallclock = state_ptr->last_wallclock;
            if (last_wallclock.time_since_epoch().count() != 0) {
                const auto wallclock_gap = packet.received_time - last_wallclock;
                const double wallclock_gap_sec = std::chrono::duration<double>(wallclock_gap).count();
                if (wallclock_gap_sec > 0.0) {
                    const auto delta_frames = static_cast<uint64_t>(delta);
                    const double configured_slack_seconds =
                        (m_settings) ? m_settings->timeshift_tuning.rtp_continuity_slack_seconds : 0.25;
                    const double bounded_slack_seconds = std::max(configured_slack_seconds, 0.0);
                    const auto expected_frames = static_cast<uint64_t>(
                        std::llround(wallclock_gap_sec * static_cast<double>(frames_per_second)));
                    const auto continuity_slack_frames = static_cast<uint64_t>(
                        std::llround(static_cast<double>(frames_per_second) * bounded_slack_seconds));

                    const auto lower_bound = (expected_frames > continuity_slack_frames)
                                                 ? (expected_frames - continuity_slack_frames)
                                                 : 0ULL;
                    const auto upper_bound = expected_frames + continuity_slack_frames;

                    if (delta_frames >= lower_bound && delta_frames <= upper_bound) {
                        should_reset = false;
                        LOG_CPP_DEBUG("[TimeshiftManager] RTP jump matches wall-clock advance for '%s' (delta=%u frames, expected=%llu, slack=%llu). Keeping timing state.",
                                      packet.source_tag.c_str(), delta,
                                      static_cast<unsigned long long>(expected_frames),
                                      static_cast<unsigned long long>(continuity_slack_frames));
                    }
                }
            }
        }

        if (should_reset) {
            LOG_CPP_INFO("[TimeshiftManager] Detected RTP jump for '%s' (delta=%u frames). Resetting timing state.",
                         packet.source_tag.c_str(), delta);

            size_t reset_position = global_timeshift_buffer_.size();
            auto targets_it = processor_targets_.find(packet.source_tag);
            if (targets_it != processor_targets_.end()) {
                for (auto& [instance_id, info] : targets_it->second) {
                    (void)instance_id;
                    info.next_packet_read_index = reset_position;
                }
            }

            timing_access.lock.unlock();
            {
                std::unique_lock<std::mutex> map_lock(timing_map_mutex_);
                auto lock_it = timing_locks_.find(packet.source_tag);
                if (lock_it == timing_locks_.end() || !lock_it->second) {
                    lock_it = timing_locks_.emplace(packet.source_tag, std::make_shared<std::mutex>()).first;
                }
                std::unique_lock<std::mutex> per_stream_lock(*lock_it->second);
                stream_timing_states_.erase(packet.source_tag);
                auto [new_state_it, _] = stream_timing_states_.try_emplace(packet.source_tag);
                (void)_;
                state_ptr = &new_state_it->second;
                map_lock.unlock();
                timing_access.lock = std::move(per_stream_lock);
                timing_access.state = state_ptr;
            }
            m_state_version_++;
            run_loop_cv_.notify_one();
        }
    }

    StreamTimingState& state = *state_ptr;
    if (state.is_first_packet) {
        state.target_buffer_level_ms = m_settings->timeshift_tuning.target_buffer_level_ms;
        state.last_target_update_time = packet.received_time;
    }
    state.total_packets++;

    if (!state.clock) {
        state.clock = std::make_unique<StreamClock>(packet.sample_rate);
    }

    state.clock->update(packet.rtp_timestamp.value(), packet.received_time);

    if (state.clock->is_initialized()) {
        state.last_clock_offset_ms = state.clock->get_offset_seconds() * 1000.0;
        state.last_clock_drift_ppm = state.clock->get_drift_ppm();
        state.last_clock_innovation_ms = state.clock->get_last_innovation_seconds() * 1000.0;
        state.last_clock_measured_offset_ms = state.clock->get_last_measured_offset_seconds() * 1000.0;
        state.clock_innovation_abs_sum_ms += std::abs(state.last_clock_innovation_ms);
        state.clock_innovation_samples++;
    }

    global_timeshift_buffer_.push_back(packet); // copy to keep packet available for timing updates
    m_total_packets_added++;
    data_lock.unlock();

    const double arrival_time_sec =
        std::chrono::duration<double>(packet.received_time.time_since_epoch()).count();

    if (!state.is_first_packet && packet.sample_rate > 0 &&
        state.last_wallclock.time_since_epoch().count() > 0) {
        const double arrival_delta_sec =
            std::chrono::duration<double>(packet.received_time - state.last_wallclock).count();
        const uint32_t timestamp_diff =
            rtp_timestamp_diff(packet.rtp_timestamp.value(), state.last_rtp_timestamp);
        const double rtp_delta_sec =
            static_cast<double>(timestamp_diff) / static_cast<double>(packet.sample_rate);
        const double transit_delta_sec = arrival_delta_sec - rtp_delta_sec;
        const double abs_transit_delta_sec = std::abs(transit_delta_sec);

        if (!state.jitter_initialized) {
            state.rfc3550_jitter_sec = abs_transit_delta_sec;
            state.jitter_initialized = true;
        } else {
            state.rfc3550_jitter_sec += (abs_transit_delta_sec - state.rfc3550_jitter_sec) / 16.0;
        }

        state.jitter_estimate = state.rfc3550_jitter_sec * 1000.0;
        state.system_jitter_estimate_ms = state.jitter_estimate;
        state.last_system_delay_ms = transit_delta_sec * 1000.0;
        state.last_transit_sec = transit_delta_sec;
    } else {
        state.jitter_estimate = std::max(state.jitter_estimate, 0.0);
        state.system_jitter_estimate_ms = state.jitter_estimate;
        state.last_system_delay_ms = 0.0;
    }

    state.last_arrival_time_sec = arrival_time_sec;

    state.is_first_packet = false;
    state.last_rtp_timestamp = packet.rtp_timestamp.value();
    state.last_wallclock = packet.received_time;
    state.sample_rate = packet.sample_rate;
    state.channels = packet.channels;
    state.bit_depth = packet.bit_depth;
    state.samples_per_chunk = 0;
    if (packet.sample_rate > 0 && packet.channels > 0 && packet.bit_depth > 0 && (packet.bit_depth % 8) == 0) {
        const std::size_t bytes_per_frame =
            static_cast<std::size_t>(packet.channels) * static_cast<std::size_t>(packet.bit_depth / 8);
        if (bytes_per_frame > 0) {
            state.samples_per_chunk = static_cast<uint32_t>(packet.audio_data.size() / bytes_per_frame);
        }
    }
}

std::optional<TimeshiftBufferExport> TimeshiftManager::export_recent_buffer(
    const std::string& source_tag,
    std::chrono::milliseconds lookback_duration) {
    if (source_tag.empty()) {
        return std::nullopt;
    }

    if (lookback_duration.count() <= 0) {
        lookback_duration = std::chrono::milliseconds(1);
    }

    TimeshiftBufferExport export_data;
    export_data.lookback_seconds_requested = std::chrono::duration<double>(lookback_duration).count();

    const auto now = std::chrono::steady_clock::now();
    const auto cutoff_time = now - lookback_duration;

    std::vector<const TaggedAudioPacket*> selected_packets;
    std::chrono::steady_clock::time_point first_packet_time{};
    std::chrono::steady_clock::time_point last_packet_time{};
    std::size_t total_bytes = 0;
    bool metadata_initialized = false;

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        selected_packets.reserve(global_timeshift_buffer_.size());

        for (const auto& packet : global_timeshift_buffer_) {
            if (packet.source_tag != source_tag) {
                continue;
            }
            if (packet.received_time < cutoff_time) {
                continue;
            }
            if (packet.audio_data.empty()) {
                continue;
            }
            if (packet.sample_rate <= 0 || packet.channels <= 0 || packet.bit_depth <= 0) {
                LOG_CPP_WARNING("[TimeshiftManager] Skipping packet with invalid audio parameters for export: sample_rate=%d channels=%d bit_depth=%d",
                                packet.sample_rate, packet.channels, packet.bit_depth);
                continue;
            }

            if (!metadata_initialized) {
                metadata_initialized = true;
                export_data.sample_rate = packet.sample_rate;
                export_data.channels = packet.channels;
                export_data.bit_depth = packet.bit_depth;
                export_data.chunk_size_bytes = packet.audio_data.size();
                first_packet_time = packet.received_time;
            } else {
                if (packet.sample_rate != export_data.sample_rate ||
                    packet.channels != export_data.channels ||
                    packet.bit_depth != export_data.bit_depth) {
                    LOG_CPP_WARNING("[TimeshiftManager] Dropping packet with mismatched format during export (expected sr=%d ch=%d bit_depth=%d, got sr=%d ch=%d bit_depth=%d)",
                                    export_data.sample_rate,
                                    export_data.channels,
                                    export_data.bit_depth,
                                    packet.sample_rate,
                                    packet.channels,
                                    packet.bit_depth);
                    continue;
                }
            }

            selected_packets.push_back(&packet);
            total_bytes += packet.audio_data.size();
            last_packet_time = packet.received_time;
        }

        if (!metadata_initialized || selected_packets.empty()) {
            return std::nullopt;
        }

        export_data.pcm_data.reserve(total_bytes);
        for (const auto* packet_ptr : selected_packets) {
            const auto& packet = *packet_ptr;
            export_data.pcm_data.insert(export_data.pcm_data.end(),
                                        packet.audio_data.begin(),
                                        packet.audio_data.end());
        }
    }

    // Calculate timing metadata outside the lock.
    export_data.earliest_packet_age_seconds =
        std::chrono::duration<double>(now - first_packet_time).count();
    export_data.latest_packet_age_seconds =
        std::chrono::duration<double>(now - last_packet_time).count();

    if (export_data.sample_rate > 0 && export_data.channels > 0 && export_data.bit_depth > 0) {
        const double bytes_per_sample = static_cast<double>(export_data.bit_depth) / 8.0;
        const double bytes_per_frame = bytes_per_sample * static_cast<double>(export_data.channels);
        if (bytes_per_frame > 0.0) {
            const double total_frames = static_cast<double>(export_data.pcm_data.size()) / bytes_per_frame;
            export_data.duration_seconds = total_frames / static_cast<double>(export_data.sample_rate);
        }
    }

    return export_data;
}

TimeshiftManagerStats TimeshiftManager::get_stats() {
    TimeshiftManagerStats stats;
    stats.total_packets_added = m_total_packets_added.load();

    struct ProcessorSnapshot {
        std::string instance_id;
        ProcessorTargetInfo info;
    };
    std::vector<ProcessorSnapshot> processor_snapshots;

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        stats.global_buffer_size = global_timeshift_buffer_.size();
        for (const auto& [source_tag, source_map] : processor_targets_) {
            for (const auto& [instance_id, target_info] : source_map) {
                stats.processor_read_indices[instance_id] = target_info.next_packet_read_index;
                processor_snapshots.push_back({instance_id, target_info});
            }
        }
    }

    std::vector<std::string> timing_tags;
    {
        std::lock_guard<std::mutex> map_lock(timing_map_mutex_);
        timing_tags.reserve(stream_timing_states_.size());
        for (const auto& [source_tag, _] : stream_timing_states_) {
            timing_tags.push_back(source_tag);
        }
    }

    for (const auto& source_tag : timing_tags) {
        auto access = get_timing_state(source_tag);
        if (!access.state) {
            continue;
        }

        const auto& timing_state = *access.state;
        stats.jitter_estimates[source_tag] = timing_state.jitter_estimate;
        stats.stream_system_jitter_ms[source_tag] = timing_state.system_jitter_estimate_ms;
        stats.stream_total_packets[source_tag] = timing_state.total_packets.load();
        stats.stream_late_packets[source_tag] = timing_state.late_packets_count.load();
        stats.stream_lagging_events[source_tag] = timing_state.lagging_events_count.load();
        stats.stream_tm_buffer_underruns[source_tag] = timing_state.tm_buffer_underruns.load();
        stats.stream_tm_packets_discarded[source_tag] = timing_state.tm_packets_discarded.load();
        stats.stream_last_arrival_time_error_ms[source_tag] = timing_state.last_arrival_time_error_ms;
        stats.stream_target_buffer_level_ms[source_tag] = timing_state.target_buffer_level_ms;
        stats.stream_buffer_target_fill_percentage[source_tag] = timing_state.buffer_target_fill_percentage;

        if (timing_state.arrival_error_samples > 0) {
            stats.stream_avg_arrival_error_ms[source_tag] = timing_state.arrival_error_ms_sum / static_cast<double>(timing_state.arrival_error_samples);
            stats.stream_avg_abs_arrival_error_ms[source_tag] = timing_state.arrival_error_ms_abs_sum / static_cast<double>(timing_state.arrival_error_samples);
            stats.stream_max_arrival_error_ms[source_tag] = timing_state.arrival_error_ms_max;
            stats.stream_min_arrival_error_ms[source_tag] = timing_state.arrival_error_ms_min;
        } else {
            stats.stream_avg_arrival_error_ms[source_tag] = 0.0;
            stats.stream_avg_abs_arrival_error_ms[source_tag] = 0.0;
            stats.stream_max_arrival_error_ms[source_tag] = 0.0;
            stats.stream_min_arrival_error_ms[source_tag] = 0.0;
        }
        stats.stream_arrival_error_sample_count[source_tag] = timing_state.arrival_error_samples;

        if (timing_state.playout_deviation_samples > 0) {
            stats.stream_avg_playout_deviation_ms[source_tag] = timing_state.playout_deviation_ms_sum / static_cast<double>(timing_state.playout_deviation_samples);
            stats.stream_avg_abs_playout_deviation_ms[source_tag] = timing_state.playout_deviation_ms_abs_sum / static_cast<double>(timing_state.playout_deviation_samples);
            stats.stream_max_playout_deviation_ms[source_tag] = timing_state.playout_deviation_ms_max;
            stats.stream_min_playout_deviation_ms[source_tag] = timing_state.playout_deviation_ms_min;
        } else {
            stats.stream_avg_playout_deviation_ms[source_tag] = 0.0;
            stats.stream_avg_abs_playout_deviation_ms[source_tag] = 0.0;
            stats.stream_max_playout_deviation_ms[source_tag] = 0.0;
            stats.stream_min_playout_deviation_ms[source_tag] = 0.0;
        }
        stats.stream_playout_deviation_sample_count[source_tag] = timing_state.playout_deviation_samples;

        if (timing_state.head_playout_lag_samples > 0) {
            stats.stream_avg_head_playout_lag_ms[source_tag] = timing_state.head_playout_lag_ms_sum / static_cast<double>(timing_state.head_playout_lag_samples);
            stats.stream_max_head_playout_lag_ms[source_tag] = timing_state.head_playout_lag_ms_max;
        } else {
            stats.stream_avg_head_playout_lag_ms[source_tag] = 0.0;
            stats.stream_max_head_playout_lag_ms[source_tag] = 0.0;
        }
        stats.stream_head_playout_lag_sample_count[source_tag] = timing_state.head_playout_lag_samples;
        stats.stream_last_head_playout_lag_ms[source_tag] = timing_state.last_head_playout_lag_ms;

        stats.stream_clock_offset_ms[source_tag] = timing_state.last_clock_offset_ms;
        stats.stream_clock_drift_ppm[source_tag] = timing_state.last_clock_drift_ppm;
        stats.stream_clock_last_innovation_ms[source_tag] = timing_state.last_clock_innovation_ms;
        stats.stream_clock_last_measured_offset_ms[source_tag] = timing_state.last_clock_measured_offset_ms;
        if (timing_state.clock_innovation_samples > 0) {
            stats.stream_clock_avg_abs_innovation_ms[source_tag] = timing_state.clock_innovation_abs_sum_ms / static_cast<double>(timing_state.clock_innovation_samples);
        } else {
            stats.stream_clock_avg_abs_innovation_ms[source_tag] = 0.0;
        }
    }

    for (const auto& snapshot : processor_snapshots) {
        TimeshiftManagerStats::ProcessorStats p_stats;
        p_stats.instance_id = snapshot.instance_id;
        p_stats.source_tag = active_tag(snapshot.info);

        size_t pending_packets = 0;
        size_t max_ring_depth = 0;
        for (const auto& [sink_id, weak_ring] : snapshot.info.sink_rings) {
            (void)sink_id;
            if (auto ring = weak_ring.lock()) {
                const size_t sz = ring->size();
                pending_packets += sz;
                max_ring_depth = std::max(max_ring_depth, sz);
            }
        }
        p_stats.pending_packets = pending_packets;
        p_stats.target_queue_depth = max_ring_depth;

        double chunk_ms = 0.0;
        const std::string bound_tag = p_stats.source_tag;
        if (!bound_tag.empty()) {
            auto access = get_timing_state(bound_tag);
            if (access.state && access.state->sample_rate > 0 && access.state->samples_per_chunk > 0) {
                chunk_ms = (static_cast<double>(access.state->samples_per_chunk) * 1000.0) /
                           static_cast<double>(access.state->sample_rate);
            }
        }
        p_stats.pending_ms = chunk_ms * static_cast<double>(pending_packets);

        {
            std::lock_guard<std::mutex> stats_lock(processor_stats_mutex_);
            p_stats.target_queue_high_water = processor_queue_high_water_[snapshot.instance_id];
            p_stats.dispatched_packets = processor_dispatched_totals_[snapshot.instance_id];
            p_stats.dropped_packets = processor_dropped_totals_[snapshot.instance_id] + snapshot.info.dropped_packets;
        }

        stats.processor_stats[p_stats.instance_id] = p_stats;
    }

    return stats;
}

/**
 * @brief Registers a new processor as a consumer of the buffer.
 * @param instance_id A unique ID for the processor instance.
 * @param source_tag The source tag the processor is interested in.
 * @param initial_delay_ms The initial static delay for the processor.
 * @param initial_timeshift_sec The initial timeshift delay for the processor.
 */
void TimeshiftManager::register_processor(
    const std::string& instance_id,
    const std::string& source_tag,
    int initial_delay_ms,
    float initial_timeshift_sec) {
    LOG_CPP_INFO("[TimeshiftManager] Registering processor: instance_id=%s, source_tag=%s, delay=%dms, timeshift=%.2fs",
                 instance_id.c_str(), source_tag.c_str(), initial_delay_ms, initial_timeshift_sec);

    ProcessorTargetInfo info;
    info.current_delay_ms = initial_delay_ms;
    info.current_timeshift_backshift_sec = initial_timeshift_sec;
    info.source_tag_filter = source_tag;
    info.is_wildcard = !source_tag.empty() && source_tag.back() == '*';
    if (info.is_wildcard) {
        info.wildcard_prefix = source_tag.substr(0, source_tag.size() - 1);
        LOG_CPP_INFO("[TimeshiftManager] Processor %s registered with wildcard prefix '%s'",
                     instance_id.c_str(), info.wildcard_prefix.c_str());
    } else {
        info.bound_source_tag = source_tag;
    }

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (initial_timeshift_sec > 0.0f && !global_timeshift_buffer_.empty()) {
            auto now = std::chrono::steady_clock::now();
            auto target_past_time = now - std::chrono::milliseconds(initial_delay_ms) - std::chrono::duration<double>(initial_timeshift_sec);
            
            size_t found_idx = global_timeshift_buffer_.size();
            for (size_t i = 0; i < global_timeshift_buffer_.size(); ++i) {
                if (global_timeshift_buffer_[i].received_time >= target_past_time) {
                    found_idx = i;
                    break;
                }
            }
            info.next_packet_read_index = found_idx;
            LOG_CPP_INFO("[TimeshiftManager] Initial timeshift > 0. Set next_packet_read_index to %zu based on %.2fs backshift.",
                          found_idx, initial_timeshift_sec);
        } else {
            info.next_packet_read_index = global_timeshift_buffer_.size();
            LOG_CPP_INFO("[TimeshiftManager] Initial timeshift is 0 or buffer empty. Set next_packet_read_index to end of buffer: %zu", info.next_packet_read_index);
        }
        processor_targets_[source_tag][instance_id] = info;
        LOG_CPP_DEBUG("[TimeshiftManager] Processor %s stored under filter '%s' (wildcard=%d)",
                      instance_id.c_str(), source_tag.c_str(), info.is_wildcard ? 1 : 0);
    }
    LOG_CPP_INFO("[TimeshiftManager] Processor %s registered for source_tag %s with read_idx %zu",
                 instance_id.c_str(), source_tag.c_str(), info.next_packet_read_index);
    m_state_version_++;
    run_loop_cv_.notify_one();
}

/**
 * @brief Unregisters a processor.
 * @param instance_id The ID of the processor instance to unregister.
 * @param source_tag The source tag associated with the processor.
 */
void TimeshiftManager::unregister_processor(const std::string& instance_id, const std::string& source_tag) {
    LOG_CPP_INFO("[TimeshiftManager] Unregistering processor: instance_id=%s, source_tag=%s", instance_id.c_str(), source_tag.c_str());
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto& source_map = processor_targets_[source_tag];
    source_map.erase(instance_id);
    if (source_map.empty()) {
        processor_targets_.erase(source_tag);
        LOG_CPP_INFO("[TimeshiftManager] Source tag %s removed as no processors are listening to it.", source_tag.c_str());
    }
    LOG_CPP_INFO("[TimeshiftManager] Processor %s unregistered.", instance_id.c_str());
    m_state_version_++;
    run_loop_cv_.notify_one();
}

/**
 * @brief Updates the static delay for a registered processor.
 * @param instance_id The ID of the processor.
 * @param delay_ms The new delay in milliseconds.
 */
void TimeshiftManager::update_processor_delay(const std::string& instance_id, int delay_ms) {
    LOG_CPP_INFO("[TimeshiftManager] Updating delay for processor %s to %dms", instance_id.c_str(), delay_ms);
    std::lock_guard<std::mutex> lock(data_mutex_);
    bool found = false;
    for (auto& [tag, source_map] : processor_targets_) {
        auto it = source_map.find(instance_id);
        if (it != source_map.end()) {
            it->second.current_delay_ms = delay_ms;
            found = true;
            break;
        }
    }
    if (!found) {
        LOG_CPP_WARNING("[TimeshiftManager] Attempted to update delay for unknown processor instance_id: %s", instance_id.c_str());
    }
    m_state_version_++;
    run_loop_cv_.notify_one();
}

/**
 * @brief Updates the timeshift delay for a registered processor and recalculates its read position.
 * @param instance_id The ID of the processor.
 * @param timeshift_sec The new timeshift in seconds.
 */
void TimeshiftManager::update_processor_timeshift(const std::string& instance_id, float timeshift_sec) {
    LOG_CPP_INFO("[TimeshiftManager] Updating timeshift for processor %s to %.2fs", instance_id.c_str(), timeshift_sec);
    std::lock_guard<std::mutex> lock(data_mutex_);
    bool found_processor = false;
    for (auto& [tag, source_map] : processor_targets_) {
        auto proc_it = source_map.find(instance_id);
        if (proc_it != source_map.end()) {
            found_processor = true;
            proc_it->second.current_timeshift_backshift_sec = timeshift_sec;

            if (global_timeshift_buffer_.empty()) {
                proc_it->second.next_packet_read_index = 0;
                 LOG_CPP_INFO("[TimeshiftManager] Timeshift updated for %s, buffer empty. Read index set to 0.", instance_id.c_str());
            } else {
                auto now = std::chrono::steady_clock::now();
                auto target_past_time = now - std::chrono::milliseconds(proc_it->second.current_delay_ms) - std::chrono::duration<double>(timeshift_sec);
                
                size_t new_read_idx = global_timeshift_buffer_.size();
                for (size_t i = 0; i < global_timeshift_buffer_.size(); ++i) {
                    if (global_timeshift_buffer_[i].received_time >= target_past_time) {
                        new_read_idx = i;
                        break;
                    }
                }
                proc_it->second.next_packet_read_index = new_read_idx;
                LOG_CPP_INFO("[TimeshiftManager] Timeshift updated for %s. New read_idx: %zu based on %.2fs backshift.",
                             instance_id.c_str(), new_read_idx, timeshift_sec);
            }
            break;
        }
    }
    if (!found_processor) {
        LOG_CPP_WARNING("[TimeshiftManager] Attempted to update timeshift for unknown processor instance_id: %s", instance_id.c_str());
    }
    m_state_version_++;
    run_loop_cv_.notify_one();
}

void TimeshiftManager::attach_sink_ring(const std::string& instance_id,
                                        const std::string& source_tag,
                                        const std::string& sink_id,
                                        std::shared_ptr<PacketRing> ring) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto& source_map = processor_targets_[source_tag];
    auto it = source_map.find(instance_id);
    if (it == source_map.end()) {
        LOG_CPP_WARNING("[TimeshiftManager] attach_sink_ring: unknown processor %s for source %s", instance_id.c_str(), source_tag.c_str());
        return;
    }
    it->second.sink_rings[sink_id] = ring;
    m_state_version_++;
    run_loop_cv_.notify_one();
}

void TimeshiftManager::detach_sink_ring(const std::string& instance_id,
                                        const std::string& source_tag,
                                        const std::string& sink_id) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto source_it = processor_targets_.find(source_tag);
    if (source_it == processor_targets_.end()) {
        return;
    }
    auto proc_it = source_it->second.find(instance_id);
    if (proc_it == source_it->second.end()) {
        return;
    }
    proc_it->second.sink_rings.erase(sink_id);
    m_state_version_++;
    run_loop_cv_.notify_one();
}

void TimeshiftManager::reset_stream_state(const std::string& source_tag) {
    LOG_CPP_INFO("[TimeshiftManager] Resetting stream state for tag %s", source_tag.c_str());

    size_t reset_position = 0;
    {
        std::lock_guard<std::mutex> data_lock(data_mutex_);
        reset_position = global_timeshift_buffer_.size();

        for (auto& [filter_tag, source_map] : processor_targets_) {
            for (auto& [instance_id, info] : source_map) {
                (void)instance_id;
                const bool direct_match = (!info.is_wildcard && filter_tag == source_tag);
                const bool bound_match = info.is_wildcard && !info.bound_source_tag.empty() && info.bound_source_tag == source_tag;
                if (!direct_match && !bound_match) {
                    continue;
                }

                info.next_packet_read_index = reset_position;
                if (info.is_wildcard) {
                    info.bound_source_tag.clear();
                }
            }
        }
    }

    {
        std::unique_lock<std::mutex> map_lock(timing_map_mutex_);
        auto lock_it = timing_locks_.find(source_tag);
        if (lock_it == timing_locks_.end() || !lock_it->second) {
            lock_it = timing_locks_.emplace(source_tag, std::make_shared<std::mutex>()).first;
        }
        std::unique_lock<std::mutex> per_stream_lock(*lock_it->second);
        stream_timing_states_.erase(source_tag);
        timing_locks_.erase(lock_it);
        map_lock.unlock();
    }

    m_state_version_++;
    run_loop_cv_.notify_one();
}

/**
 * @brief The main processing loop for the manager's thread.
 */
void TimeshiftManager::run() {
    LOG_CPP_INFO("[TimeshiftManager] Run loop started.");
    uint64_t last_processed_version = m_state_version_.load();

    while (!stop_flag_) {
        std::unique_lock<std::mutex> lock(data_mutex_);

        // Process any packets that are already due.
        processing_loop_iteration_unlocked();

        // Perform cleanup if needed.
        auto now = std::chrono::steady_clock::now();
        if (now - last_cleanup_time_ > std::chrono::milliseconds(m_settings->timeshift_tuning.cleanup_interval_ms)) {
            cleanup_global_buffer_unlocked();
            last_cleanup_time_ = now;
        }

        // Calculate the next time we need to wake up.
        auto next_wakeup_time = calculate_next_wakeup_time();
        
        // Wait until the next event or until notified.
        run_loop_cv_.wait_until(lock, next_wakeup_time, [this, &last_processed_version] {
            return stop_flag_.load() || (m_state_version_.load() != last_processed_version);
        });

        // Update the version so we don't spin on the same notification.
        last_processed_version = m_state_version_.load();
    }
    LOG_CPP_INFO("[TimeshiftManager] Run loop exiting.");
}

/**
 * @brief A single iteration of the processing loop to dispatch ready packets. Assumes data_mutex_ is held.
 */
void TimeshiftManager::processing_loop_iteration_unlocked() {
    if (global_timeshift_buffer_.empty()) {
        return;
    }

    const auto iteration_start = std::chrono::steady_clock::now();
    auto now = iteration_start;
    const double max_catchup_lag_ms = m_settings->timeshift_tuning.max_catchup_lag_ms;
    size_t packets_processed = 0;

    for (auto& [source_tag, source_map] : processor_targets_) {
        (void)source_tag;
        for (auto& [instance_id, target_info] : source_map) {
            while (target_info.next_packet_read_index < global_timeshift_buffer_.size()) {
                const auto& candidate_packet = global_timeshift_buffer_[target_info.next_packet_read_index];

                if (!match_and_bind_source(target_info, candidate_packet.source_tag)) {
                    if (target_info.is_wildcard && (target_info.next_packet_read_index % 256 == 0)) {
                        LOG_CPP_DEBUG("[TimeshiftManager] Instance %s skipping packet tag '%s' (filter '%s')",
                                      instance_id.c_str(), candidate_packet.source_tag.c_str(), target_info.source_tag_filter.c_str());
                    }
                    target_info.next_packet_read_index++;
                    continue;
                }

                if (!candidate_packet.rtp_timestamp.has_value() || candidate_packet.sample_rate == 0) {
                    target_info.next_packet_read_index++;
                    continue;
                }

                auto timing_access = get_timing_state(candidate_packet.source_tag);
                if (!timing_access.state || !timing_access.state->clock) {
                    target_info.next_packet_read_index++;
                    continue;
                }

                StreamTimingState& ts = *timing_access.state;

                now = std::chrono::steady_clock::now();

                // --- Playout Time Calculation ---
                auto expected_arrival_time =
                    ts.clock->get_expected_arrival_time(candidate_packet.rtp_timestamp.value());

                // 2. Add the adaptive playout delay
                const double timeshift_backshift_ms = std::max(0.0f, target_info.current_timeshift_backshift_sec) * 1000.0;
                double base_latency_ms = std::max<double>(
                    target_info.current_delay_ms,
                    m_settings->timeshift_tuning.target_buffer_level_ms);
                const double max_adaptive_delay_ms = m_settings->timeshift_tuning.max_adaptive_delay_ms;
                if (max_adaptive_delay_ms > 0.0) {
                    base_latency_ms = std::min(base_latency_ms, max_adaptive_delay_ms);
                }
                const double desired_latency_ms = base_latency_ms + timeshift_backshift_ms;

                ts.target_buffer_level_ms = desired_latency_ms;
                ts.last_target_update_time = now;

                auto ideal_playout_time = expected_arrival_time + std::chrono::duration<double, std::milli>(desired_latency_ms);
                auto time_until_playout_ms = std::chrono::duration<double, std::milli>(ideal_playout_time - now).count();

                const double buffer_level_ms = std::max(time_until_playout_ms, 0.0);
                ts.current_buffer_level_ms = buffer_level_ms;
                if (desired_latency_ms > 1e-6) {
                    ts.buffer_target_fill_percentage =
                        std::clamp((buffer_level_ms / desired_latency_ms) * 100.0, 0.0, 100.0);
                } else {
                    ts.buffer_target_fill_percentage = 0.0;
                }

                double head_lag_ms = std::max(-time_until_playout_ms, 0.0);
                ts.last_head_playout_lag_ms = head_lag_ms;
                ts.head_playout_lag_ms_sum += head_lag_ms;
                ts.head_playout_lag_ms_max = std::max(ts.head_playout_lag_ms_max, head_lag_ms);
                ts.head_playout_lag_samples++;

                // Check if the packet is ready to be played
                if (ideal_playout_time <= now) {
                    const double lateness_ms = -time_until_playout_ms;
                    if (lateness_ms > m_settings->timeshift_tuning.late_packet_threshold_ms) {
                        ts.late_packets_count++;
                    }
                    if (lateness_ms > 0.0) {
                        profiling_total_lateness_ms_ += lateness_ms;
                        profiling_packets_late_count_++;
                    }

                    ts.playout_deviation_ms_sum += lateness_ms;
                    ts.playout_deviation_ms_abs_sum += std::abs(lateness_ms);
                    ts.playout_deviation_ms_max = std::max(ts.playout_deviation_ms_max, lateness_ms);
                    ts.playout_deviation_ms_min = std::min(ts.playout_deviation_ms_min, lateness_ms);
                    ts.playout_deviation_samples++;

                    if (max_catchup_lag_ms > 0.0 && lateness_ms > max_catchup_lag_ms) {
                        ts.tm_packets_discarded++;
                        profiling_packets_dropped_++;
                        const std::string& log_tag = target_info.source_tag_filter.empty() ? candidate_packet.source_tag : target_info.source_tag_filter;
                        LOG_CPP_DEBUG(
                            "[TimeshiftManager] Dropping late packet for source '%s'. Lateness=%.2f ms exceeds catchup limit=%.2f ms.",
                            log_tag.c_str(),
                            lateness_ms,
                            max_catchup_lag_ms);

                        target_info.next_packet_read_index++;
                        continue;
                    }

                    TaggedAudioPacket packet_to_send = candidate_packet;
                    const auto& tuning = m_settings->timeshift_tuning;
                    double controller_dt_sec = 0.0;
                    if (ts.last_controller_update_time.time_since_epoch().count() != 0) {
                        controller_dt_sec =
                            std::chrono::duration<double>(now - ts.last_controller_update_time).count();
                    }
                    if (controller_dt_sec <= 0.0) {
                        controller_dt_sec =
                            std::max(static_cast<double>(tuning.loop_max_sleep_ms) / 1000.0, 0.001);
                    }

                    const double buffer_error_ms = desired_latency_ms - buffer_level_ms;
                    ts.last_controller_update_time = now;

                    const double proportional_ppm = tuning.playback_ratio_kp * buffer_error_ms;
                    ts.playback_ratio_integral_ppm +=
                        tuning.playback_ratio_ki * buffer_error_ms * controller_dt_sec;
                    const double integral_cap_ppm =
                        std::max(tuning.playback_ratio_integral_limit_ppm,
                                 tuning.playback_ratio_max_deviation_ppm);
                    ts.playback_ratio_integral_ppm =
                        std::clamp(ts.playback_ratio_integral_ppm,
                                   -integral_cap_ppm,
                                   integral_cap_ppm);

                    double controller_ppm = proportional_ppm + ts.playback_ratio_integral_ppm;
                    const double max_slew_ppm =
                        std::max(tuning.playback_ratio_slew_ppm_per_sec, 0.0) * controller_dt_sec;
                    if (max_slew_ppm > 0.0) {
                        controller_ppm = std::clamp(controller_ppm,
                                                    ts.playback_ratio_controller_ppm - max_slew_ppm,
                                                    ts.playback_ratio_controller_ppm + max_slew_ppm);
                    }

                    const double max_deviation_ppm = std::max(tuning.playback_ratio_max_deviation_ppm, 0.0);
                    controller_ppm = std::clamp(controller_ppm, -max_deviation_ppm, max_deviation_ppm);
                    ts.playback_ratio_controller_ppm = controller_ppm;

                    double combined_ppm = ts.last_clock_drift_ppm + controller_ppm;
                    combined_ppm = std::clamp(combined_ppm, -max_deviation_ppm, max_deviation_ppm);

                    double target_rate = 1.0 + combined_ppm * kPlaybackDriftGain;
                    if (!std::isfinite(target_rate)) {
                        target_rate = 1.0;
                    }

                    const double smoothing_factor =
                        m_settings ? tuning.playback_ratio_smoothing : kFallbackSmoothing;
                    const double smoothed_rate =
                        smooth_playback_rate(ts.current_playback_rate,
                                             target_rate,
                                             smoothing_factor,
                                             max_deviation_ppm);

                    if (std::abs(smoothed_rate - ts.current_playback_rate) > 5e-4) {
                        LOG_CPP_DEBUG(
                            "[TimeshiftManager] Adjusted playback rate for '%s': drift_ppm=%.3f error_ms=%.3f controller_ppm=%.3f combined_ppm=%.3f target=%.6f smoothed=%.6f",
                            candidate_packet.source_tag.c_str(),
                            ts.last_clock_drift_ppm,
                            buffer_error_ms,
                            controller_ppm,
                            combined_ppm,
                            target_rate,
                            smoothed_rate);
                    }

                    ts.current_playback_rate = smoothed_rate;
                    ts.last_system_delay_ms = lateness_ms;

                    packet_to_send.playback_rate = smoothed_rate;

                    size_t sinks_dispatched = 0;
                    for (auto it = target_info.sink_rings.begin(); it != target_info.sink_rings.end();) {
                        auto ring_ptr = it->second.lock();
                        if (!ring_ptr) {
                            it = target_info.sink_rings.erase(it);
                            continue;
                        }

                        const std::size_t before_drop = ring_ptr->drop_count();
                        ring_ptr->push(packet_to_send);
                        const std::size_t after_drop = ring_ptr->drop_count();
                        const std::size_t ring_size = ring_ptr->size();

                        {
                            std::lock_guard<std::mutex> stats_lock(processor_stats_mutex_);
                            processor_dispatched_totals_[instance_id]++;
                            processor_queue_high_water_[instance_id] =
                                std::max(processor_queue_high_water_[instance_id], ring_size);
                            if (after_drop > before_drop) {
                                processor_dropped_totals_[instance_id] += (after_drop - before_drop);
                            }
                        }

                        if (after_drop > before_drop) {
                            target_info.dropped_packets += (after_drop - before_drop);
                        }

                        ++sinks_dispatched;
                        ++it;
                    }

                    // Ensure progress even if no sinks are attached.
                    profiling_packets_dispatched_ += sinks_dispatched > 0 ? sinks_dispatched : 1;
                    packets_processed += sinks_dispatched > 0 ? sinks_dispatched : 1;

                    ts.last_played_rtp_timestamp = candidate_packet.rtp_timestamp.value();

                    target_info.next_packet_read_index++;

                    now = std::chrono::steady_clock::now();
                    if (ideal_playout_time > now) {
                        continue;
                    }

                    // If we consumed the available slack, let the outer loop reschedule.
                    break;
                } else {
                    // The loop is breaking because the next packet's playout time is in the future.
                    break;
                }
            }
        }
    }

    const auto iteration_end = std::chrono::steady_clock::now();
    last_iteration_finish_time_ = iteration_end;

    if (packets_processed > 0) {
        const double iteration_us = std::chrono::duration<double, std::micro>(iteration_end - iteration_start).count();
        const double per_packet_us = iteration_us / static_cast<double>(packets_processed);

        if (processing_budget_initialized_) {
            smoothed_processing_per_packet_us_ =
                smoothed_processing_per_packet_us_ * (1.0 - kProcessingBudgetAlpha) +
                per_packet_us * kProcessingBudgetAlpha;
        } else {
            smoothed_processing_per_packet_us_ = per_packet_us;
            processing_budget_initialized_ = true;
        }
    }

    if (m_settings->profiler.enabled) {
        maybe_log_profiler_unlocked(iteration_end);
    }
}

void TimeshiftManager::reset_profiler_counters_unlocked(std::chrono::steady_clock::time_point now) {
    profiling_last_log_time_ = now;
    profiling_packets_dispatched_ = 0;
    profiling_packets_dropped_ = 0;
    profiling_packets_late_count_ = 0;
    profiling_total_lateness_ms_ = 0.0;
}

void TimeshiftManager::maybe_log_profiler_unlocked(std::chrono::steady_clock::time_point now) {
    if (!m_settings || !m_settings->profiler.enabled) {
        return;
    }

    long interval_ms = m_settings->profiler.log_interval_ms;
    if (interval_ms <= 0) {
        interval_ms = 1000;
    }
    auto interval = std::chrono::milliseconds(interval_ms);
    if (now - profiling_last_log_time_ < interval) {
        return;
    }

    const size_t buffer_size = global_timeshift_buffer_.size();
    size_t total_targets = 0;
    size_t total_backlog = 0;
    size_t max_backlog = 0;

    for (const auto& [source_tag, source_map] : processor_targets_) {
        (void)source_tag;
        for (const auto& [instance_id, target_info] : source_map) {
            (void)instance_id;
            total_targets++;
            size_t backlog = 0;
            if (target_info.next_packet_read_index < global_timeshift_buffer_.size()) {
                backlog = global_timeshift_buffer_.size() - target_info.next_packet_read_index;
            }
            total_backlog += backlog;
            if (backlog > max_backlog) {
                max_backlog = backlog;
            }
        }
    }

    const double avg_backlog = total_targets > 0 ? static_cast<double>(total_backlog) / static_cast<double>(total_targets) : 0.0;
    const double avg_late_ms = profiling_packets_late_count_ > 0
                                    ? (profiling_total_lateness_ms_ / static_cast<double>(profiling_packets_late_count_))
                                    : 0.0;

    LOG_CPP_INFO(
        "[Profiler][Timeshift][Global] buffer=%zu targets=%zu avg_backlog=%.2f max_backlog=%zu dispatched=%llu dropped=%llu late_count=%llu avg_late_ms=%.2f late_ms_sum=%.2f proc_budget_us=%.2f",
        buffer_size,
        total_targets,
        avg_backlog,
        max_backlog,
        static_cast<unsigned long long>(profiling_packets_dispatched_),
        static_cast<unsigned long long>(profiling_packets_dropped_),
        static_cast<unsigned long long>(profiling_packets_late_count_),
        avg_late_ms,
        profiling_total_lateness_ms_,
        processing_budget_initialized_ ? smoothed_processing_per_packet_us_ : 0.0);

    std::vector<std::string> timing_tags;
    {
        std::lock_guard<std::mutex> map_lock(timing_map_mutex_);
        timing_tags.reserve(stream_timing_states_.size());
        for (const auto& [source_tag, _] : stream_timing_states_) {
            timing_tags.push_back(source_tag);
        }
    }

    for (const auto& source_tag : timing_tags) {
        auto access = get_timing_state(source_tag);
        if (!access.state) {
            continue;
        }

        const auto& timing_state = *access.state;
        const double arrival_avg = timing_state.arrival_error_samples > 0
                                        ? (timing_state.arrival_error_ms_sum / static_cast<double>(timing_state.arrival_error_samples))
                                        : 0.0;
        const double arrival_abs_avg = timing_state.arrival_error_samples > 0
                                            ? (timing_state.arrival_error_ms_abs_sum / static_cast<double>(timing_state.arrival_error_samples))
                                            : 0.0;
        const double playout_avg = timing_state.playout_deviation_samples > 0
                                        ? (timing_state.playout_deviation_ms_sum / static_cast<double>(timing_state.playout_deviation_samples))
                                        : 0.0;
        const double playout_abs_avg = timing_state.playout_deviation_samples > 0
                                             ? (timing_state.playout_deviation_ms_abs_sum / static_cast<double>(timing_state.playout_deviation_samples))
                                             : 0.0;
        const double head_avg = timing_state.head_playout_lag_samples > 0
                                       ? (timing_state.head_playout_lag_ms_sum / static_cast<double>(timing_state.head_playout_lag_samples))
                                       : 0.0;
        double clock_update_age_ms = 0.0;
        if (timing_state.clock && timing_state.clock->is_initialized()) {
            auto last_update = timing_state.clock->get_last_update_time();
            if (last_update != std::chrono::steady_clock::time_point{}) {
                clock_update_age_ms = std::chrono::duration<double, std::milli>(now - last_update).count();
            }
        }

        LOG_CPP_INFO(
            "[Profiler][Timeshift][Stream %s] jitter=%.2fms sys_jitter=%.2fms sys_delay=%.2fms clk_offset=%.3fms drift=%.3fppm clk_innov_last=%.3fms clk_innov_avg_abs=%.3fms clk_update_age=%.2fms clk_meas_offset=%.3fms arrival(avg=%.3fms abs_avg=%.3fms max=%.3fms min=%.3fms samples=%llu) playout_dev(avg=%.3fms abs_avg=%.3fms max=%.3fms min=%.3fms samples=%llu) head_lag(last=%.3fms avg=%.3fms max=%.3fms samples=%llu) buffer(cur=%.3fms target=%.3fms fill=%.1f%% playback_rate=%.6f)",
            source_tag.c_str(),
            timing_state.jitter_estimate,
            timing_state.system_jitter_estimate_ms,
            timing_state.last_system_delay_ms,
            timing_state.last_clock_offset_ms,
            timing_state.last_clock_drift_ppm,
            timing_state.last_clock_innovation_ms,
            timing_state.clock_innovation_samples > 0 ? (timing_state.clock_innovation_abs_sum_ms / static_cast<double>(timing_state.clock_innovation_samples)) : 0.0,
            clock_update_age_ms,
            timing_state.last_clock_measured_offset_ms,
            arrival_avg,
            arrival_abs_avg,
            timing_state.arrival_error_samples > 0 ? timing_state.arrival_error_ms_max : 0.0,
            timing_state.arrival_error_samples > 0 ? timing_state.arrival_error_ms_min : 0.0,
            static_cast<unsigned long long>(timing_state.arrival_error_samples),
            playout_avg,
            playout_abs_avg,
            timing_state.playout_deviation_samples > 0 ? timing_state.playout_deviation_ms_max : 0.0,
            timing_state.playout_deviation_samples > 0 ? timing_state.playout_deviation_ms_min : 0.0,
            static_cast<unsigned long long>(timing_state.playout_deviation_samples),
            timing_state.last_head_playout_lag_ms,
            head_avg,
            timing_state.head_playout_lag_samples > 0 ? timing_state.head_playout_lag_ms_max : 0.0,
            static_cast<unsigned long long>(timing_state.head_playout_lag_samples),
            timing_state.current_buffer_level_ms,
            timing_state.target_buffer_level_ms,
            timing_state.buffer_target_fill_percentage,
            timing_state.current_playback_rate);
    }

    reset_profiler_counters_unlocked(now);
}

/**
 * @brief Periodically cleans up old packets from the global buffer. Assumes data_mutex_ is held.
 */
void TimeshiftManager::cleanup_global_buffer_unlocked() {
    if (global_timeshift_buffer_.empty()) {
        return; // Nothing to do
    }

    auto oldest_allowed_time_by_duration = std::chrono::steady_clock::now() - max_buffer_duration_sec_;
    
    size_t remove_count = 0;
    for (const auto& packet : global_timeshift_buffer_) {
        if (packet.received_time < oldest_allowed_time_by_duration) {
            remove_count++;
        } else {
            // Packets are ordered by time, so we can stop here
            break;
        }
    }

    if (remove_count > 0) {
        LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: Removing %zu packets older than max duration.", remove_count);

        // Now, adjust all processor indices.
        for (auto& [tag, source_map] : processor_targets_) {
            for (auto& [id, proc_info] : source_map) {
                if (proc_info.next_packet_read_index < remove_count) {
                    // The processor's read index is within the block to be removed.
                    // We must determine if it's truly lagging or just idle.
                    bool is_truly_lagging = false;
                    const std::string& bound_tag = active_tag(proc_info);
                    for (size_t i = proc_info.next_packet_read_index; i < remove_count; ++i) {
                        if (!bound_tag.empty() && global_timeshift_buffer_[i].source_tag == bound_tag) {
                            // It missed a packet it was supposed to play. It's lagging.
                            is_truly_lagging = true;
                            break;
                        }
                    }

                    if (is_truly_lagging) {
                        LOG_CPP_WARNING("[TimeshiftManager] Cleanup: Processor %s was lagging. Its read index %zu was inside the removed block of size %zu. Forcing catch-up to index 0.",
                                        id.c_str(), proc_info.next_packet_read_index, remove_count);
                        
                        // Safely increment the lagging event counter for this stream
                        if (!bound_tag.empty()) {
                            auto timing_access = get_timing_state(bound_tag);
                            if (timing_access.state) {
                                timing_access.state->lagging_events_count++;
                            }
                        }

                        proc_info.next_packet_read_index = 0;
                    } else {
                        // The processor was not lagging, just idle. All packets in the removed
                        // block were for other streams. Silently catch it up.
                        LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: Idle processor %s caught up. Its read index %zu was shifted past the removed block of size %zu.",
                                      id.c_str(), proc_info.next_packet_read_index, remove_count);
                        proc_info.next_packet_read_index = 0; // The new start of the buffer
                    }
                } else {
                    // The processor was ahead of the removed section, just shift its index back.
                    proc_info.next_packet_read_index -= remove_count;
                }
            }
        }
        while (remove_count-- > 0 && !global_timeshift_buffer_.empty()) {
            global_timeshift_buffer_.pop_front();
        }
    } else {
        LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: No packets older than max duration to remove.");
    }
    LOG_CPP_DEBUG("[TimeshiftManager] Cleanup: Global buffer size after cleanup: %zu", global_timeshift_buffer_.size());
}

/**
 * @brief Calculates the time point for the next event to occur.
 * @return The time point of the next scheduled event.
 * @note This function assumes the caller holds the data_mutex.
 */
std::chrono::steady_clock::time_point TimeshiftManager::calculate_next_wakeup_time() {
    const auto now = std::chrono::steady_clock::now();
    const auto reference_now = (last_iteration_finish_time_.time_since_epoch().count() != 0)
                                   ? std::max(now, last_iteration_finish_time_)
                                   : now;

    auto next_cleanup_time = last_cleanup_time_ + std::chrono::milliseconds(m_settings->timeshift_tuning.cleanup_interval_ms);
    auto earliest_time = std::chrono::steady_clock::time_point::max();

    const auto max_sleep_time = reference_now + std::chrono::milliseconds(m_settings->timeshift_tuning.loop_max_sleep_ms);

    for (const auto& [source_tag, source_map] : processor_targets_) {
        for (const auto& [instance_id, target_info] : source_map) {
            
            if (target_info.next_packet_read_index >= global_timeshift_buffer_.size()) {
                continue;
            }

            const auto& next_packet = global_timeshift_buffer_[target_info.next_packet_read_index];
            if (!next_packet.rtp_timestamp.has_value() || next_packet.sample_rate == 0) {
                continue;
            }
            
            auto timing_access = get_timing_state(source_tag);
            if (!timing_access.state || !timing_access.state->clock) {
                continue;
            }
            const StreamTimingState& timing_state = *timing_access.state;

            auto expected_arrival_time = timing_state.clock->get_expected_arrival_time(next_packet.rtp_timestamp.value());
            const double timeshift_backshift_ms = std::max(0.0f, target_info.current_timeshift_backshift_sec) * 1000.0;
            double base_latency_ms = std::max<double>(
                target_info.current_delay_ms,
                m_settings->timeshift_tuning.target_buffer_level_ms);
            const double max_adaptive_delay_ms = m_settings->timeshift_tuning.max_adaptive_delay_ms;
            if (max_adaptive_delay_ms > 0.0) {
                base_latency_ms = std::min(base_latency_ms, max_adaptive_delay_ms);
            }
            const double desired_latency_ms = base_latency_ms + timeshift_backshift_ms;

            const double state_target_ms = (timing_state.target_buffer_level_ms > 0.0)
                                               ? timing_state.target_buffer_level_ms
                                               : desired_latency_ms;
            double effective_latency_ms = std::max(desired_latency_ms, state_target_ms);
            auto ideal_playout_time = expected_arrival_time + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                            std::chrono::duration<double, std::milli>(effective_latency_ms));

            auto candidate_time = ideal_playout_time;
            if (processing_budget_initialized_ && smoothed_processing_per_packet_us_ > 0.0) {
                const auto budget = std::chrono::microseconds(
                    static_cast<int64_t>(smoothed_processing_per_packet_us_));
                if (ideal_playout_time > reference_now) {
                    if (budget < (ideal_playout_time - reference_now)) {
                        candidate_time = ideal_playout_time - budget;
                    } else {
                        candidate_time = reference_now;
                    }
                }
            }

            if (candidate_time < earliest_time) {
                earliest_time = candidate_time;
            }
        }
    }

    if (earliest_time == std::chrono::steady_clock::time_point::max()) {
        earliest_time = reference_now;
    } else if (earliest_time < reference_now) {
        earliest_time = reference_now;
    }

    return std::min({earliest_time, next_cleanup_time, max_sleep_time});
}


} // namespace audio
} // namespace screamrouter
