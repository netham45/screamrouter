#include "connection_manager.h"
#include "../utils/cpp_logger.h"
#include "../utils/lock_guard_profiler.h"
#include "../input_processor/timeshift_manager.h"

namespace screamrouter {
namespace audio {

ConnectionManager::ConnectionManager(
    std::recursive_mutex& manager_mutex,
    SourceManager* source_manager,
    SinkManager* sink_manager,
    std::map<std::string, std::unique_ptr<SourceInputProcessor>>& sources)
    : m_manager_mutex(manager_mutex),
      m_source_manager(source_manager),
      m_sink_manager(sink_manager),
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

    auto source_it = m_sources.find(source_instance_id);
    if (source_it == m_sources.end() || !source_it->second) {
        LOG_CPP_ERROR("Source processor instance not found for ID: %s", source_instance_id.c_str());
        return false;
    }

    auto* sip = source_it->second.get();
    if (!sip) {
        LOG_CPP_ERROR("Null SourceInputProcessor for instance ID: %s", source_instance_id.c_str());
        return false;
    }

    auto ring = std::make_shared<utils::PacketRing<TaggedAudioPacket>>(128);
    std::string ring_key = sink_id + "|" + source_instance_id;
    m_ready_rings[ring_key] = ring;

    const auto t_add0 = std::chrono::steady_clock::now();
    m_sink_manager->add_input_queue_to_sink(sink_id, source_instance_id, ring, sip);
    if (auto* ts = m_source_manager ? m_source_manager->get_timeshift_manager() : nullptr) {
        ts->attach_sink_ring(source_instance_id, sip->get_source_tag(), sink_id, ring);
    }
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
    std::string ring_key = sink_id + "|" + source_instance_id;
    auto ring_it = m_ready_rings.find(ring_key);
    if (ring_it != m_ready_rings.end()) {
        if (auto* ts = m_source_manager ? m_source_manager->get_timeshift_manager() : nullptr) {
            ts->detach_sink_ring(source_instance_id, source_it->second->get_source_tag(), sink_id);
        }
        m_ready_rings.erase(ring_it);
    }
    const auto t_rm1 = std::chrono::steady_clock::now();
    LOG_CPP_INFO("Disconnection successful: Source instance %s -x Sink %s (remove=%lld ms total=%lld ms)",
                 source_instance_id.c_str(), sink_id.c_str(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_rm1 - t_rm0).count(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_rm1 - t0).count());
    return true;
}

} // namespace audio
} // namespace screamrouter
