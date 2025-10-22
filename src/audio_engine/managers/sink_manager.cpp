#include "sink_manager.h"
#include "../utils/cpp_logger.h"
#include "../utils/lock_guard_profiler.h"

namespace screamrouter {
namespace audio {

SinkManager::SinkManager(std::recursive_mutex& manager_mutex, std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings)
    : m_manager_mutex(manager_mutex), m_settings(settings) {
    LOG_CPP_INFO("SinkManager created.");
}

SinkManager::~SinkManager() {
    LOG_CPP_INFO("SinkManager destroyed.");
}

bool SinkManager::add_sink(const SinkConfig& config, bool running) {
    LOG_CPP_INFO("Adding sink: %s", config.id.c_str());
    const auto t0 = std::chrono::steady_clock::now();

    if (!running) {
        LOG_CPP_ERROR("Cannot add sink, manager is not running.");
        return false;
    }

    std::unique_ptr<SinkAudioMixer> new_sink;
    auto mp3_queue = std::make_shared<Mp3Queue>();

    try {
        SinkMixerConfig mixer_config;
        mixer_config.sink_id = config.id;
        mixer_config.protocol = config.protocol;
        mixer_config.output_ip = config.output_ip;
        mixer_config.output_port = config.output_port;
        mixer_config.output_bitdepth = config.bitdepth;
        mixer_config.output_samplerate = config.samplerate;
        mixer_config.output_channels = config.channels;
        mixer_config.output_chlayout1 = config.chlayout1;
        mixer_config.output_chlayout2 = config.chlayout2;
        mixer_config.speaker_layout = config.speaker_layout;
        mixer_config.time_sync_enabled = config.time_sync_enabled;
        mixer_config.time_sync_delay_ms = config.time_sync_delay_ms;
        mixer_config.rtp_receivers = config.rtp_receivers;
        mixer_config.multi_device_mode = config.multi_device_mode;
        new_sink = std::make_unique<SinkAudioMixer>(mixer_config, mp3_queue, m_settings);
    } catch (const std::exception& e) {
        LOG_CPP_ERROR("Failed to create SinkAudioMixer for %s: %s", config.id.c_str(), e.what());
        return false;
    }

    {
        std::scoped_lock lock(m_manager_mutex);
        if (m_sinks.count(config.id)) {
            LOG_CPP_ERROR("Sink ID already exists: %s", config.id.c_str());
            return false;
        }
        m_mp3_output_queues[config.id] = mp3_queue;
        m_sinks[config.id] = std::move(new_sink);
        m_sink_configs[config.id] = config;
    }

    const auto t_make1 = std::chrono::steady_clock::now();
    m_sinks.at(config.id)->start();
    const auto t_start1 = std::chrono::steady_clock::now();
    LOG_CPP_INFO("Sink %s added and started successfully. (construct=%lld ms start=%lld ms total=%lld ms)",
                 config.id.c_str(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_make1 - t0).count(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_start1 - t_make1).count(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_start1 - t0).count());
    return true;
}

bool SinkManager::remove_sink(const std::string& sink_id) {
    LOG_CPP_INFO("Removing sink: %s", sink_id.c_str());
    std::unique_ptr<SinkAudioMixer> sink_to_remove;

    {
        std::scoped_lock lock(m_manager_mutex);
        auto it = m_sinks.find(sink_id);
        if (it == m_sinks.end()) {
            LOG_CPP_ERROR("Sink not found: %s", sink_id.c_str());
            return false;
        }
        sink_to_remove = std::move(it->second);
        m_sinks.erase(it);
        m_sink_configs.erase(sink_id);
        m_mp3_output_queues.erase(sink_id);
    }

    if (sink_to_remove) {
        LOG_CPP_INFO("[SinkManager] Stopping mixer for sink: %s", sink_id.c_str());
        sink_to_remove->stop();
    }

    LOG_CPP_INFO("Sink %s removed successfully.", sink_id.c_str());
    return true;
}

void SinkManager::add_input_queue_to_sink(const std::string& sink_id, const std::string& source_instance_id, std::shared_ptr<ChunkQueue> queue) {
    std::scoped_lock lock(m_manager_mutex);
    auto sink_it = m_sinks.find(sink_id);
    if (sink_it != m_sinks.end() && sink_it->second) {
        sink_it->second->add_input_queue(source_instance_id, queue);
    } else {
        LOG_CPP_ERROR("Sink not found or invalid: %s", sink_id.c_str());
    }
}

void SinkManager::remove_input_queue_from_sink(const std::string& sink_id, const std::string& source_instance_id) {
    std::scoped_lock lock(m_manager_mutex);
    auto sink_it = m_sinks.find(sink_id);
    if (sink_it != m_sinks.end() && sink_it->second) {
        sink_it->second->remove_input_queue(source_instance_id);
    } else {
        LOG_CPP_ERROR("Sink not found or invalid for disconnection: %s", sink_id.c_str());
    }
}

void SinkManager::add_listener_to_sink(const std::string& sink_id, const std::string& listener_id, std::unique_ptr<INetworkSender> sender) {
    std::scoped_lock lock(m_manager_mutex);
    auto sink_it = m_sinks.find(sink_id);
    if (sink_it != m_sinks.end() && sink_it->second) {
        sink_it->second->add_listener(listener_id, std::move(sender));
    } else {
        LOG_CPP_ERROR("[SinkManager] Sink not found for WebRTC listener: %s", sink_id.c_str());
    }
}

void SinkManager::remove_listener_from_sink(const std::string& sink_id, const std::string& listener_id) {
    std::scoped_lock lock(m_manager_mutex);
    auto sink_it = m_sinks.find(sink_id);
    if (sink_it != m_sinks.end() && sink_it->second) {
        sink_it->second->remove_listener(listener_id);
    } else {
        LOG_CPP_WARNING("[SinkManager] Sink not found for WebRTC listener removal: %s", sink_id.c_str());
    }
}

INetworkSender* SinkManager::get_listener_from_sink(const std::string& sink_id, const std::string& listener_id) {
    std::scoped_lock lock(m_manager_mutex);
    auto sink_it = m_sinks.find(sink_id);
    if (sink_it != m_sinks.end() && sink_it->second) {
        return sink_it->second->get_listener(listener_id);
    }
    return nullptr;
}

std::map<std::string, SinkConfig>& SinkManager::get_sink_configs() {
    return m_sink_configs;
}

std::map<std::string, std::shared_ptr<Mp3Queue>>& SinkManager::get_mp3_output_queues() {
    return m_mp3_output_queues;
}

std::vector<std::string> SinkManager::get_sink_ids() {
    std::vector<std::string> ids;
    for (const auto& pair : m_sinks) {
        ids.push_back(pair.first);
    }
    return ids;
}

std::vector<SinkAudioMixer*> SinkManager::get_all_mixers() {
    std::vector<SinkAudioMixer*> mixers;
    std::scoped_lock lock(m_manager_mutex);
    for (auto const& [id, mixer] : m_sinks) {
        mixers.push_back(mixer.get());
    }
    return mixers;
}

void SinkManager::stop_all() {
    std::vector<std::unique_ptr<SinkAudioMixer>> to_stop;
    {
        std::scoped_lock lock(m_manager_mutex);
        LOG_CPP_INFO("[SinkManager] stop_all(): stopping %zu sinks", m_sinks.size());
        for (auto &pair : m_sinks) {
            to_stop.push_back(std::move(pair.second));
        }
        m_sinks.clear();
        m_sink_configs.clear();
        m_mp3_output_queues.clear();
    }
    for (auto &mixer : to_stop) {
        if (mixer) {
            LOG_CPP_INFO("[SinkManager] stop_all(): stopping mixer id=%s", mixer->get_config().sink_id.c_str());
            mixer->stop();
        }
    }
}

} // namespace audio
} // namespace screamrouter
