#ifndef STATS_MANAGER_H
#define STATS_MANAGER_H

#include "../utils/audio_component.h"
#include "../audio_types.h"
#include "source_manager.h"
#include "sink_manager.h"
#include "../input_processor/timeshift_manager.h"
#include <chrono>
#include <mutex>
#include <thread>

namespace screamrouter {
namespace audio {

class StatsManager : public AudioComponent {
public:
    StatsManager(
        TimeshiftManager* timeshift_manager,
        SourceManager* source_manager,
        SinkManager* sink_manager
    );
    ~StatsManager() override;

    void start() override;
    void stop() override;

    AudioEngineStats get_current_stats();

protected:
    void run() override;

private:
    void collect_stats();

    TimeshiftManager* m_timeshift_manager;
    SourceManager* m_source_manager;
    SinkManager* m_sink_manager;

    std::mutex m_stats_mutex;
    AudioEngineStats m_stats;

    // For calculating rates
    std::map<std::string, uint64_t> m_last_stream_packets;
    uint64_t m_last_total_packets_added = 0;
    uint64_t m_last_inbound_received = 0;
    uint64_t m_last_inbound_dropped = 0;
    std::map<std::string, uint64_t> m_last_source_packets_processed;
    std::map<std::string, uint64_t> m_last_source_chunks_pushed;
    std::map<std::string, uint64_t> m_last_processor_dispatched;
    std::map<std::string, uint64_t> m_last_processor_dropped;
    std::map<std::string, uint64_t> m_last_sink_chunks_mixed;
    std::map<std::string, uint64_t> m_last_ready_chunks_popped;
    std::map<std::string, uint64_t> m_last_ready_chunks_received;
    std::map<std::string, uint64_t> m_last_webrtc_packets_sent;
    
    std::chrono::steady_clock::time_point m_last_poll_time;
};

} // namespace audio
} // namespace screamrouter

#endif // STATS_MANAGER_H
