#ifndef SOURCE_MANAGER_H
#define SOURCE_MANAGER_H

#include "../audio_types.h"
#include "../configuration/audio_engine_config_types.h"
#include "../input_processor/source_input_processor.h"
#include "../utils/thread_safe_queue.h"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>

namespace screamrouter {
namespace audio {


class TimeshiftManager;

class SourceManager {
public:
    SourceManager(std::mutex& manager_mutex, TimeshiftManager* timeshift_manager);
    ~SourceManager();

    std::string configure_source(const SourceConfig& config, bool running);
    bool remove_source(const std::string& instance_id);

    // Other public methods to be added as needed...

    // Getter methods for AudioManager
    std::map<std::string, std::unique_ptr<SourceInputProcessor>>& get_sources();
    std::map<std::string, std::shared_ptr<ChunkQueue>>& get_source_to_sink_queues();
    std::map<std::string, std::shared_ptr<CommandQueue>>& get_command_queues();

private:
    std::string generate_unique_instance_id(const std::string& base_tag);

    std::mutex& m_manager_mutex;
    TimeshiftManager* m_timeshift_manager;

    std::map<std::string, std::unique_ptr<SourceInputProcessor>> m_sources;
    std::map<std::string, std::shared_ptr<PacketQueue>> m_rtp_to_source_queues;
    std::map<std::string, std::shared_ptr<ChunkQueue>> m_source_to_sink_queues;
    std::map<std::string, std::shared_ptr<CommandQueue>> m_command_queues;
};

} // namespace audio
} // namespace screamrouter

#endif // SOURCE_MANAGER_H