#include "stats_manager.h"
#include "../utils/cpp_logger.h"
#include "../senders/webrtc/webrtc_sender.h"
#include <algorithm>

namespace screamrouter {
namespace audio {

StatsManager::StatsManager(
    TimeshiftManager* timeshift_manager,
    SourceManager* source_manager,
    SinkManager* sink_manager)
    : m_timeshift_manager(timeshift_manager),
      m_source_manager(source_manager),
      m_sink_manager(sink_manager) {
    LOG_CPP_INFO("[StatsManager] Initialized");
}

StatsManager::~StatsManager() {
    if (!stop_flag_) {
        stop();
    }
}

void StatsManager::start() {
    if (is_running()) {
        return;
    }
    LOG_CPP_INFO("[StatsManager] Starting...");
    stop_flag_ = false;
    m_last_poll_time = std::chrono::steady_clock::now();
    component_thread_ = std::thread(&StatsManager::run, this);
}

void StatsManager::stop() {
    if (stop_flag_) {
        return;
    }
    LOG_CPP_INFO("[StatsManager] Stopping...");
    stop_flag_ = true;
    if (component_thread_.joinable()) {
        component_thread_.join();
    }
}

AudioEngineStats StatsManager::get_current_stats() {
    std::scoped_lock lock(m_stats_mutex);
    return m_stats;
}

void StatsManager::run() {
    while (!stop_flag_) {
        collect_stats();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void StatsManager::collect_stats() {
    auto now = std::chrono::steady_clock::now();
    double elapsed_seconds = std::chrono::duration<double>(now - m_last_poll_time).count();
    m_last_poll_time = now;

    AudioEngineStats new_stats;
    TimeshiftManagerStats tm_stats;
    bool have_tm_stats = false;
    uint64_t prev_total_packets_added = m_last_total_packets_added;
    double target_buffer_ms = 0.0;

    // Collect from TimeshiftManager
    if (m_timeshift_manager) {
        tm_stats = m_timeshift_manager->get_stats();
        have_tm_stats = true;
        new_stats.global_stats.timeshift_buffer_total_size = tm_stats.global_buffer_size;

        uint64_t total_added_now = tm_stats.total_packets_added;
        if (prev_total_packets_added > 0) { // Avoid huge spike on first run
            new_stats.global_stats.packets_added_to_timeshift_per_second = (total_added_now - prev_total_packets_added) / elapsed_seconds;
        }
        new_stats.global_stats.timeshift_inbound_buffer.size = tm_stats.inbound_queue_size;
        new_stats.global_stats.timeshift_inbound_buffer.high_watermark = tm_stats.inbound_queue_high_water;
        if (m_last_inbound_received > 0) {
            new_stats.global_stats.timeshift_inbound_buffer.push_rate_per_second =
                (tm_stats.total_inbound_received - m_last_inbound_received) / elapsed_seconds;
        }
        if (prev_total_packets_added > 0) {
            new_stats.global_stats.timeshift_inbound_buffer.pop_rate_per_second =
                (total_added_now - prev_total_packets_added) / elapsed_seconds;
        }
        m_last_inbound_received = tm_stats.total_inbound_received;
        m_last_inbound_dropped = tm_stats.total_inbound_dropped;
        m_last_total_packets_added = total_added_now;

        if (auto settings_ptr = m_timeshift_manager->get_settings()) {
            target_buffer_ms = settings_ptr->timeshift_tuning.target_buffer_level_ms;
        }

        for(auto const& [tag, jitter] : tm_stats.jitter_estimates) {
            new_stats.stream_stats[tag].jitter_estimate_ms = jitter;
        }
        for (auto const& [tag, val] : tm_stats.stream_system_jitter_ms) {
            new_stats.stream_stats[tag].system_jitter_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_last_system_delay_ms) {
            new_stats.stream_stats[tag].last_system_delay_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_playback_rate) {
            new_stats.stream_stats[tag].playback_rate = val;
        }

        for (auto const& [tag, val] : tm_stats.stream_late_packets) {
            new_stats.stream_stats[tag].timeshift_buffer_late_packets = val;
        }

        for (auto const& [tag, val] : tm_stats.stream_lagging_events) {
            new_stats.stream_stats[tag].timeshift_buffer_lagging_events = val;
        }

        for (auto const& [tag, val] : tm_stats.stream_tm_buffer_underruns) {
            new_stats.stream_stats[tag].tm_buffer_underruns = val;
        }

        for (auto const& [tag, val] : tm_stats.stream_tm_packets_discarded) {
            new_stats.stream_stats[tag].tm_packets_discarded = val;
        }

        for (auto const& [tag, val] : tm_stats.stream_last_arrival_time_error_ms) {
            new_stats.stream_stats[tag].last_arrival_time_error_ms = val;
        }

        for (auto const& [tag, val] : tm_stats.stream_avg_arrival_error_ms) {
            new_stats.stream_stats[tag].avg_arrival_error_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_avg_abs_arrival_error_ms) {
            new_stats.stream_stats[tag].avg_abs_arrival_error_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_max_arrival_error_ms) {
            new_stats.stream_stats[tag].max_arrival_error_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_min_arrival_error_ms) {
            new_stats.stream_stats[tag].min_arrival_error_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_arrival_error_sample_count) {
            new_stats.stream_stats[tag].arrival_error_sample_count = val;
        }

        for (auto const& [tag, val] : tm_stats.stream_avg_playout_deviation_ms) {
            new_stats.stream_stats[tag].avg_playout_deviation_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_avg_abs_playout_deviation_ms) {
            new_stats.stream_stats[tag].avg_abs_playout_deviation_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_max_playout_deviation_ms) {
            new_stats.stream_stats[tag].max_playout_deviation_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_min_playout_deviation_ms) {
            new_stats.stream_stats[tag].min_playout_deviation_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_playout_deviation_sample_count) {
            new_stats.stream_stats[tag].playout_deviation_sample_count = val;
        }

        for (auto const& [tag, val] : tm_stats.stream_avg_head_playout_lag_ms) {
            new_stats.stream_stats[tag].avg_head_playout_lag_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_max_head_playout_lag_ms) {
            new_stats.stream_stats[tag].max_head_playout_lag_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_head_playout_lag_sample_count) {
            new_stats.stream_stats[tag].head_playout_lag_sample_count = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_last_head_playout_lag_ms) {
            new_stats.stream_stats[tag].last_head_playout_lag_ms = val;
        }

        for (auto const& [tag, val] : tm_stats.stream_clock_offset_ms) {
            new_stats.stream_stats[tag].clock_offset_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_clock_drift_ppm) {
            new_stats.stream_stats[tag].clock_drift_ppm = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_clock_last_innovation_ms) {
            new_stats.stream_stats[tag].clock_last_innovation_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_clock_avg_abs_innovation_ms) {
            new_stats.stream_stats[tag].clock_avg_abs_innovation_ms = val;
        }
        for (auto const& [tag, val] : tm_stats.stream_clock_last_measured_offset_ms) {
            new_stats.stream_stats[tag].clock_last_measured_offset_ms = val;
        }

        for (auto const& [tag, buffered_packets] : tm_stats.stream_buffered_packets) {
            new_stats.stream_stats[tag].timeshift_buffer_size = buffered_packets;
            new_stats.stream_stats[tag].timeshift_buffer.size = buffered_packets;
        }
        for (auto const& [tag, duration_ms] : tm_stats.stream_buffered_duration_ms) {
            new_stats.stream_stats[tag].timeshift_buffer.depth_ms = duration_ms;
            new_stats.stream_stats[tag].target_buffer_level_ms = target_buffer_ms;
            if (target_buffer_ms > 0.0) {
                double fill = (duration_ms / target_buffer_ms) * 100.0;
                new_stats.stream_stats[tag].buffer_target_fill_percentage = fill;
                new_stats.stream_stats[tag].timeshift_buffer.fill_percent = fill;
            }
        }

        for (auto const& [tag, total_packets] : tm_stats.stream_total_packets) {
            uint64_t last_packets = m_last_stream_packets.count(tag) ? m_last_stream_packets[tag] : 0;
            if (last_packets > 0) {
               new_stats.stream_stats[tag].packets_per_second = (total_packets - last_packets) / elapsed_seconds;
            }
            m_last_stream_packets[tag] = total_packets;
            new_stats.stream_stats[tag].total_packets_in_stream = total_packets;
            new_stats.stream_stats[tag].timeshift_buffer.pop_rate_per_second = new_stats.stream_stats[tag].packets_per_second;
        }
    }

    // Collect from SourceManager
    if (m_source_manager) {
        auto sources = m_source_manager->get_all_processors();
        for (auto& source : sources) {
            SourceStats s_stats;
            auto raw_stats = source->get_stats();
            s_stats.instance_id = source->get_instance_id();
            s_stats.source_tag = source->get_source_tag();
            s_stats.input_queue_size = raw_stats.input_queue_size;
            s_stats.output_queue_size = raw_stats.output_queue_size;
            s_stats.reconfigurations = raw_stats.reconfigurations;
            s_stats.input_buffer.size = raw_stats.input_queue_size;
            s_stats.input_buffer.depth_ms = raw_stats.input_queue_ms;
            s_stats.input_buffer.high_watermark = raw_stats.input_queue_high_water;
            s_stats.output_buffer.size = raw_stats.output_queue_size;
            s_stats.output_buffer.depth_ms = raw_stats.output_queue_ms;
            s_stats.output_buffer.high_watermark = raw_stats.output_queue_high_water;
            s_stats.process_buffer.size = raw_stats.process_buffer_samples;
            s_stats.process_buffer.depth_ms = raw_stats.process_buffer_ms;
            s_stats.process_buffer.high_watermark = raw_stats.peak_process_buffer_samples;
            s_stats.chunks_pushed = raw_stats.total_chunks_pushed;
            s_stats.discarded_packets = raw_stats.total_discarded_packets;
            s_stats.avg_processing_ms = raw_stats.avg_loop_ms;
            s_stats.peak_process_buffer_samples = raw_stats.peak_process_buffer_samples;
            s_stats.last_packet_age_ms = raw_stats.last_packet_age_ms;
            s_stats.last_origin_age_ms = raw_stats.last_origin_age_ms;
            s_stats.playback_rate = raw_stats.playback_rate;
            s_stats.input_samplerate = raw_stats.input_samplerate;
            s_stats.output_samplerate = raw_stats.output_samplerate;
            s_stats.resample_ratio = raw_stats.resample_ratio;
            
            uint64_t processed_now = raw_stats.total_packets_processed;
            if (m_last_source_packets_processed.count(s_stats.instance_id)) {
                s_stats.packets_processed_per_second = (processed_now - m_last_source_packets_processed[s_stats.instance_id]) / elapsed_seconds;
            }
            m_last_source_packets_processed[s_stats.instance_id] = processed_now;

            uint64_t chunks_now = raw_stats.total_chunks_pushed;
            if (m_last_source_chunks_pushed.count(s_stats.instance_id)) {
                s_stats.output_buffer.push_rate_per_second =
                    (chunks_now - m_last_source_chunks_pushed[s_stats.instance_id]) / elapsed_seconds;
            }
            m_last_source_chunks_pushed[s_stats.instance_id] = chunks_now;

            s_stats.input_buffer.pop_rate_per_second = s_stats.packets_processed_per_second;

            if (have_tm_stats) {
                auto proc_it = tm_stats.processor_stats.find(s_stats.instance_id);
                if (proc_it != tm_stats.processor_stats.end()) {
                    const auto& proc = proc_it->second;
                    s_stats.timeshift_buffer.size = proc.pending_packets;
                    s_stats.timeshift_buffer.depth_ms = proc.pending_ms;
                    s_stats.input_buffer.high_watermark = std::max(s_stats.input_buffer.high_watermark, proc.target_queue_high_water);
                    if (target_buffer_ms > 0.0) {
                        s_stats.timeshift_buffer.fill_percent = (s_stats.timeshift_buffer.depth_ms / target_buffer_ms) * 100.0;
                    }

                    double dispatch_rate = 0.0;
                    auto last_disp_it = m_last_processor_dispatched.find(s_stats.instance_id);
                    if (last_disp_it != m_last_processor_dispatched.end()) {
                        dispatch_rate = (proc.dispatched_packets - last_disp_it->second) / elapsed_seconds;
                    }
                    m_last_processor_dispatched[s_stats.instance_id] = proc.dispatched_packets;
                    m_last_processor_dropped[s_stats.instance_id] = proc.dropped_packets;

                    s_stats.input_buffer.push_rate_per_second = dispatch_rate;
                    s_stats.timeshift_buffer.push_rate_per_second = dispatch_rate;
                    s_stats.timeshift_buffer.pop_rate_per_second = s_stats.packets_processed_per_second;
                }
            }

            auto& stream_ref = new_stats.stream_stats[s_stats.source_tag];
            stream_ref.timeshift_buffer_size = s_stats.timeshift_buffer.size;
            stream_ref.timeshift_buffer.depth_ms = s_stats.timeshift_buffer.depth_ms;
            stream_ref.timeshift_buffer.fill_percent = s_stats.timeshift_buffer.fill_percent;
            stream_ref.buffer_target_fill_percentage = stream_ref.timeshift_buffer.fill_percent;

            new_stats.source_stats.push_back(s_stats);
        }
    }

    // Collect from SinkManager
    if (m_sink_manager) {
        auto sinks = m_sink_manager->get_all_mixers();
        for (auto& sink : sinks) {
            SinkStats s_stats;
            auto raw_stats = sink->get_stats();
            s_stats.sink_id = sink->get_config().sink_id;
            s_stats.active_input_streams = raw_stats.active_input_streams;
            s_stats.total_input_streams = raw_stats.total_input_streams;
            s_stats.sink_buffer_underruns = raw_stats.buffer_underruns;
            s_stats.sink_buffer_overflows = raw_stats.buffer_overflows;
            s_stats.mp3_buffer_overflows = raw_stats.mp3_buffer_overflows;
            s_stats.payload_buffer = raw_stats.payload_buffer;
            s_stats.mp3_output_buffer = raw_stats.mp3_output_buffer;
            s_stats.mp3_pcm_buffer = raw_stats.mp3_pcm_buffer;
            s_stats.last_chunk_dwell_ms = raw_stats.last_chunk_dwell_ms;
            s_stats.avg_chunk_dwell_ms = raw_stats.avg_chunk_dwell_ms;
            s_stats.avg_send_gap_ms = raw_stats.avg_send_gap_ms;
            s_stats.last_send_gap_ms = raw_stats.last_send_gap_ms;

            uint64_t mixed_now = raw_stats.total_chunks_mixed;
            if (m_last_sink_chunks_mixed.count(s_stats.sink_id)) {
                s_stats.packets_mixed_per_second = (mixed_now - m_last_sink_chunks_mixed[s_stats.sink_id]) / elapsed_seconds;
            }
            m_last_sink_chunks_mixed[s_stats.sink_id] = mixed_now;
            s_stats.payload_buffer.pop_rate_per_second = s_stats.packets_mixed_per_second;
            s_stats.payload_buffer.push_rate_per_second = s_stats.packets_mixed_per_second;

            for (auto& lane : raw_stats.input_lanes) {
                std::string lane_key = s_stats.sink_id + ":" + lane.instance_id;
                if (m_last_ready_chunks_popped.count(lane_key)) {
                    lane.ready_queue.pop_rate_per_second =
                        (lane.ready_total_popped - m_last_ready_chunks_popped[lane_key]) / elapsed_seconds;
                }
                if (m_last_ready_chunks_received.count(lane_key)) {
                    lane.ready_queue.push_rate_per_second =
                        (lane.ready_total_received - m_last_ready_chunks_received[lane_key]) / elapsed_seconds;
                }
                m_last_ready_chunks_popped[lane_key] = lane.ready_total_popped;
                m_last_ready_chunks_received[lane_key] = lane.ready_total_received;
                lane.source_output_queue.pop_rate_per_second = lane.ready_queue.push_rate_per_second;
                s_stats.inputs.push_back(std::move(lane));
            }

            for (const auto& listener_id : raw_stats.listener_ids) {
                INetworkSender* sender = sink->get_listener(listener_id);
                if (sender) {
                    if (WebRtcSender* webrtc_sender = dynamic_cast<WebRtcSender*>(sender)) {
                        WebRtcListenerStats l_stats;
                        auto raw_listener_stats = webrtc_sender->get_stats();
                        l_stats.listener_id = listener_id;
                        l_stats.connection_state = raw_listener_stats.connection_state;
                        l_stats.pcm_buffer_size = raw_listener_stats.pcm_buffer_size;

                        uint64_t sent_now = raw_listener_stats.total_packets_sent;
                        if (m_last_webrtc_packets_sent.count(listener_id)) {
                            l_stats.packets_sent_per_second = (sent_now - m_last_webrtc_packets_sent[listener_id]) / elapsed_seconds;
                        }
                        m_last_webrtc_packets_sent[listener_id] = sent_now;
                        
                        s_stats.webrtc_listeners.push_back(l_stats);
                    }
                }
            }
            new_stats.sink_stats.push_back(s_stats);
        }
    }

    {
        std::scoped_lock lock(m_stats_mutex);
        m_stats = new_stats;
    }
}

} // namespace audio
} // namespace screamrouter
