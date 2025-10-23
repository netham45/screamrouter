#include "connection_manager.h"
#include "../utils/cpp_logger.h"
#include "../utils/lock_guard_profiler.h"

namespace screamrouter {
namespace audio {

ConnectionManager::ConnectionManager(
    std::recursive_mutex& manager_mutex,
    SourceManager* source_manager,
    SinkManager* sink_manager,
    std::map<std::string, std::shared_ptr<ChunkQueue>>& source_to_sink_queues,
    std::map<std::string, std::unique_ptr<SourceInputProcessor>>& sources)
    : m_manager_mutex(manager_mutex),
      m_source_manager(source_manager),
      m_sink_manager(sink_manager),
      m_source_to_sink_queues(source_to_sink_queues),
      m_sources(sources) {
    LOG_CPP_INFO("ConnectionManager created.");
}

ConnectionManager::~ConnectionManager() {
    LOG_CPP_INFO("ConnectionManager destroyed.");
}

bool ConnectionManager::connect_source_sink(const std::string& source_instance_id, const std::string& sink_id, bool running) {
    const auto t0 = std::chrono::steady_clock::now();
    std::scoped_lock lock(m_manager_mutex);

    if (!running) {
        return false;
    }

    auto queue_it = m_source_to_sink_queues.find(source_instance_id);
    if (queue_it == m_source_to_sink_queues.end() || !queue_it->second) {
        LOG_CPP_ERROR("Source output queue not found for instance ID: %s", source_instance_id.c_str());
        return false;
    }

    auto source_it = m_sources.find(source_instance_id);
    if (source_it == m_sources.end() || !source_it->second) {
        LOG_CPP_ERROR("Source processor instance not found for ID: %s", source_instance_id.c_str());
        return false;
    }

    const auto t_add0 = std::chrono::steady_clock::now();
    m_sink_manager->add_input_queue_to_sink(sink_id, source_instance_id, queue_it->second);
    const auto t_add1 = std::chrono::steady_clock::now();
    LOG_CPP_INFO("Connection successful: Source instance %s -> Sink %s (enqueue=%lld ms total=%lld ms)",
                 source_instance_id.c_str(), sink_id.c_str(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_add1 - t_add0).count(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_add1 - t0).count());
    return true;
}

bool ConnectionManager::disconnect_source_sink(const std::string& source_instance_id, const std::string& sink_id, bool running) {
    const auto t0 = std::chrono::steady_clock::now();
    std::scoped_lock lock(m_manager_mutex);

    if (!running) {
        return false;
    }

    auto source_it = m_sources.find(source_instance_id);
    if (source_it == m_sources.end() || !source_it->second) {
        LOG_CPP_WARNING("Source processor instance not found for disconnection: %s. Assuming already disconnected.", source_instance_id.c_str());
        return true;
    }

    const auto t_rm0 = std::chrono::steady_clock::now();
    m_sink_manager->remove_input_queue_from_sink(sink_id, source_instance_id);
    const auto t_rm1 = std::chrono::steady_clock::now();
    LOG_CPP_INFO("Disconnection successful: Source instance %s -x Sink %s (remove=%lld ms total=%lld ms)",
                 source_instance_id.c_str(), sink_id.c_str(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_rm1 - t_rm0).count(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_rm1 - t0).count());
    return true;
}

} // namespace audio
} // namespace screamrouter
