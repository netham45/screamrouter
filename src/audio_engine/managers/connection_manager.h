#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include "source_manager.h"
#include "sink_manager.h"
#include "../audio_types.h"
#include "../input_processor/source_input_processor.h"
#include <string>
#include <mutex>

namespace screamrouter {
namespace audio {

class ConnectionManager {
public:
    ConnectionManager(
        std::mutex& manager_mutex,
        SourceManager* source_manager,
        SinkManager* sink_manager,
        std::map<std::string, std::shared_ptr<ChunkQueue>>& source_to_sink_queues,
        std::map<std::string, std::unique_ptr<SourceInputProcessor>>& sources
    );
    ~ConnectionManager();

    bool connect_source_sink(const std::string& source_instance_id, const std::string& sink_id, bool running);
    bool disconnect_source_sink(const std::string& source_instance_id, const std::string& sink_id, bool running);

private:
    std::mutex& m_manager_mutex;
    SourceManager* m_source_manager;
    SinkManager* m_sink_manager;
    std::map<std::string, std::shared_ptr<ChunkQueue>>& m_source_to_sink_queues;
    std::map<std::string, std::unique_ptr<SourceInputProcessor>>& m_sources;
};

} // namespace audio
} // namespace screamrouter

#endif // CONNECTION_MANAGER_H