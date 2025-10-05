#include "mp3_data_api_manager.h"
#include "../audio_types.h"
#include "../utils/cpp_logger.h"
#include "../utils/lock_guard_profiler.h"
#include "../utils/thread_safe_queue.h"

namespace screamrouter {
namespace audio {

MP3DataApiManager::MP3DataApiManager(
    std::mutex& manager_mutex,
    std::map<std::string, std::shared_ptr<Mp3Queue>>& mp3_output_queues,
    std::map<std::string, SinkConfig>& sink_configs)
    : m_manager_mutex(manager_mutex),
      m_mp3_output_queues(mp3_output_queues),
      m_sink_configs(sink_configs) {
    LOG_CPP_INFO("MP3DataApiManager created.");
}

MP3DataApiManager::~MP3DataApiManager() {
    LOG_CPP_INFO("MP3DataApiManager destroyed.");
}

std::vector<uint8_t> MP3DataApiManager::get_mp3_data(const std::string& sink_id, bool running) {
    std::shared_ptr<Mp3Queue> target_queue;
    {
        std::lock_guard<std::mutex> lock(m_manager_mutex);
        if (!running) return {};

        auto it = m_mp3_output_queues.find(sink_id);
        if (it == m_mp3_output_queues.end()) {
            return {};
        }
        target_queue = it->second;
    }

    if (target_queue) {
        EncodedMP3Data mp3_data;
        if (target_queue->try_pop(mp3_data)) {
            return mp3_data.mp3_data;
        }
    }
    return {};
}

std::vector<uint8_t> MP3DataApiManager::get_mp3_data_by_ip(const std::string& ip_address, bool running) {
    std::lock_guard<std::mutex> lock(m_manager_mutex);

    if (!running) {
        return {};
    }

    for (const auto& pair : m_sink_configs) {
        const SinkConfig& config = pair.second;
        if (config.output_ip == ip_address) {
            auto queue_it = m_mp3_output_queues.find(config.id);
            if (queue_it != m_mp3_output_queues.end()) {
                std::shared_ptr<Mp3Queue> target_queue = queue_it->second;
                if (target_queue) {
                    EncodedMP3Data mp3_data_item;
                    if (target_queue->try_pop(mp3_data_item)) {
                        return mp3_data_item.mp3_data;
                    }
                }
            }
            return {};
        }
    }
    return {};
}

} // namespace audio
} // namespace screamrouter