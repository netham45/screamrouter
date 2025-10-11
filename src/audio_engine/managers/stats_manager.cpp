#include "stats_manager.h"
#include "../utils/cpp_logger.h"
#include "../senders/webrtc/webrtc_sender.h"

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

    // Collect from TimeshiftManager
    if (m_timeshift_manager) {
        auto tm_stats = m_timeshift_manager->get_stats();
        new_stats.global_stats.timeshift_buffer_total_size = tm_stats.global_buffer_size;
        
        uint64_t total_added_now = tm_stats.total_packets_added;
        if (m_last_total_packets_added > 0) { // Avoid huge spike on first run
            new_stats.global_stats.packets_added_to_timeshift_per_second = (total_added_now - m_last_total_packets_added) / elapsed_seconds;
        }
        m_last_total_packets_added = total_added_now;

        for(auto const& [tag, jitter] : tm_stats.jitter_estimates) {
            new_stats.stream_stats[tag].jitter_estimate_ms = jitter;
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

        for (auto const& [tag, val] : tm_stats.stream_target_buffer_level_ms) {
            new_stats.stream_stats[tag].target_buffer_level_ms = val;
        }

        for (auto const& [tag, val] : tm_stats.stream_buffer_target_fill_percentage) {
            new_stats.stream_stats[tag].buffer_target_fill_percentage = val;
        }

        for (auto const& [tag, total_packets] : tm_stats.stream_total_packets) {
            uint64_t last_packets = m_last_stream_packets.count(tag) ? m_last_stream_packets[tag] : 0;
            if (last_packets > 0) {
               new_stats.stream_stats[tag].packets_per_second = (total_packets - last_packets) / elapsed_seconds;
            }
            m_last_stream_packets[tag] = total_packets;
            new_stats.stream_stats[tag].total_packets_in_stream = total_packets;
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
            
            uint64_t processed_now = raw_stats.total_packets_processed;
            if (m_last_source_packets_processed.count(s_stats.instance_id)) {
                s_stats.packets_processed_per_second = (processed_now - m_last_source_packets_processed[s_stats.instance_id]) / elapsed_seconds;
            }
            m_last_source_packets_processed[s_stats.instance_id] = processed_now;

            // Correctly associate the timeshift buffer size with the source instance
            if (m_timeshift_manager) {
                auto tm_stats = m_timeshift_manager->get_stats();
                if (tm_stats.processor_read_indices.count(s_stats.instance_id)) {
                    size_t read_idx = tm_stats.processor_read_indices.at(s_stats.instance_id);
                    new_stats.stream_stats[s_stats.source_tag].timeshift_buffer_size = tm_stats.global_buffer_size > read_idx ? tm_stats.global_buffer_size - read_idx : 0;
                }
            }

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

            uint64_t mixed_now = raw_stats.total_chunks_mixed;
            if (m_last_sink_chunks_mixed.count(s_stats.sink_id)) {
                s_stats.packets_mixed_per_second = (mixed_now - m_last_sink_chunks_mixed[s_stats.sink_id]) / elapsed_seconds;
            }
            m_last_sink_chunks_mixed[s_stats.sink_id] = mixed_now;

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