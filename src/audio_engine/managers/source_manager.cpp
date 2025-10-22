#include "source_manager.h"
#include "../audio_types.h"
#include "../utils/cpp_logger.h"
#include "../utils/lock_guard_profiler.h"
#include "../input_processor/timeshift_manager.h"
#include <sstream>
#include <atomic>
#include <algorithm>

namespace screamrouter {
namespace audio {


// Static counter for generating unique instance IDs, moved from audio_manager
static std::atomic<uint64_t> instance_id_counter{0};

SourceManager::SourceManager(std::recursive_mutex& manager_mutex, TimeshiftManager* timeshift_manager, std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings)
    : m_manager_mutex(manager_mutex), m_timeshift_manager(timeshift_manager), m_settings(settings) {
    LOG_CPP_INFO("SourceManager created.");
}

void SourceManager::set_capture_device_callbacks(
    std::function<bool(const std::string&)> ensure_callback,
    std::function<void(const std::string&)> release_callback) {
    m_ensure_capture_callback = ensure_callback;
    m_release_capture_callback = release_callback;
    LOG_CPP_INFO("SourceManager capture device callbacks set.");
}

SourceManager::~SourceManager() {
    LOG_CPP_INFO("SourceManager destroyed.");
}

std::string SourceManager::generate_unique_instance_id(const std::string& base_tag) {
    uint64_t id_num = instance_id_counter.fetch_add(1);
    std::stringstream ss;
    if (!base_tag.empty()) {
        ss << base_tag << "-";
    }
    ss << "instance-" << id_num;
    return ss.str();
}

std::string SourceManager::configure_source(const SourceConfig& config, bool running) {
    const auto t0 = std::chrono::steady_clock::now();
    if (!running) {
        LOG_CPP_ERROR("Cannot configure source, manager is not running.");
        return "";
    }

    std::string instance_id = generate_unique_instance_id(config.tag);
    LOG_CPP_INFO("Generated unique instance ID: %s", instance_id.c_str());

    std::unique_ptr<SourceInputProcessor> new_source;
    std::shared_ptr<PacketQueue> rtp_queue;
    std::shared_ptr<ChunkQueue> sink_queue;
    std::shared_ptr<CommandQueue> cmd_queue;

    const auto t_construct0 = std::chrono::steady_clock::now();
    try {
        SourceConfig validated_config = config;
        if (validated_config.initial_eq.empty() || validated_config.initial_eq.size() != EQ_BANDS) {
            validated_config.initial_eq.assign(EQ_BANDS, 1.0f);
        }

        rtp_queue = std::make_shared<PacketQueue>();
        sink_queue = std::make_shared<ChunkQueue>();
        cmd_queue = std::make_shared<CommandQueue>();

        SourceProcessorConfig proc_config;
        proc_config.instance_id = instance_id;
        proc_config.source_tag = validated_config.tag;
        proc_config.output_channels = validated_config.target_output_channels > 0 && validated_config.target_output_channels <= 8 ? validated_config.target_output_channels : 2;
        const std::vector<int> valid_samplerates = {8000, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 192000};
        proc_config.output_samplerate = std::find(valid_samplerates.begin(), valid_samplerates.end(), validated_config.target_output_samplerate) != valid_samplerates.end() ? validated_config.target_output_samplerate : 48000;
        proc_config.initial_volume = validated_config.initial_volume;
        proc_config.initial_eq = validated_config.initial_eq;
        proc_config.initial_delay_ms = validated_config.initial_delay_ms;
        proc_config.initial_timeshift_sec = validated_config.initial_timeshift_sec;

        new_source = std::make_unique<SourceInputProcessor>(proc_config, rtp_queue, sink_queue, cmd_queue, m_settings);
    } catch (const std::exception& e) {
        LOG_CPP_ERROR("Failed to create SourceInputProcessor for instance %s (tag: %s): %s", instance_id.c_str(), config.tag.c_str(), e.what());
        return "";
    }
    const auto t_construct1 = std::chrono::steady_clock::now();

    {
        std::scoped_lock lock(m_manager_mutex);
        m_rtp_to_source_queues[instance_id] = rtp_queue;
        m_source_to_sink_queues[instance_id] = sink_queue;
        m_command_queues[instance_id] = cmd_queue;
        m_sources[instance_id] = std::move(new_source);
    }

    if (m_timeshift_manager) {
        const auto t_ts0 = std::chrono::steady_clock::now();
        m_timeshift_manager->register_processor(instance_id, config.tag, rtp_queue, config.initial_delay_ms, config.initial_timeshift_sec);
        const auto t_ts1 = std::chrono::steady_clock::now();
        LOG_CPP_INFO("Registered instance %s with TimeshiftManager in %lld ms.", instance_id.c_str(),
                     (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_ts1 - t_ts0).count());
    } else {
        LOG_CPP_ERROR("TimeshiftManager is null. Cannot register source instance %s", instance_id.c_str());
        std::scoped_lock lock(m_manager_mutex);
        m_sources.erase(instance_id);
        m_rtp_to_source_queues.erase(instance_id);
        m_source_to_sink_queues.erase(instance_id);
        m_command_queues.erase(instance_id);
        return "";
    }

    // Check if this is a system audio capture source and activate the capture device
    if (m_ensure_capture_callback && !config.tag.empty()) {
        bool is_system_tag = false;
        const char* backend_label = "ALSA";
#if defined(_WIN32)
        backend_label = "WASAPI";
        is_system_tag = config.tag.rfind("wc:", 0) == 0 ||
                        config.tag.rfind("ws:", 0) == 0;
#else
        is_system_tag = config.tag.rfind("ac:", 0) == 0;
#endif

        if (is_system_tag) {
            const auto t_cap0 = std::chrono::steady_clock::now();
            LOG_CPP_INFO("Source instance %s uses %s capture device: %s",
                         instance_id.c_str(), backend_label, config.tag.c_str());

            if (m_ensure_capture_callback(config.tag)) {
                const auto t_cap1 = std::chrono::steady_clock::now();
                std::scoped_lock lock(m_manager_mutex);
                m_instance_to_capture_tag[instance_id] = config.tag;
                LOG_CPP_INFO("%s capture device %s activated for instance %s (in %lld ms)",
                             backend_label,
                             config.tag.c_str(),
                             instance_id.c_str(),
                             (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_cap1 - t_cap0).count());
            } else {
                const auto t_cap1 = std::chrono::steady_clock::now();
                LOG_CPP_ERROR("Failed to activate %s capture device %s for instance %s (attempt %lld ms)",
                              backend_label,
                              config.tag.c_str(),
                              instance_id.c_str(),
                              (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_cap1 - t_cap0).count());
            }
        }
    }

    m_sources.at(instance_id)->start();
    const auto t1 = std::chrono::steady_clock::now();
    LOG_CPP_INFO("Source instance %s (tag: %s) configured and started successfully. (construct=%lld ms, total=%lld ms)",
                 instance_id.c_str(), config.tag.c_str(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_construct1 - t_construct0).count(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    return instance_id;
}

bool SourceManager::remove_source(const std::string& instance_id) {
    std::unique_ptr<SourceInputProcessor> source_to_remove;
    std::string source_tag_for_removal;

    {
        std::scoped_lock lock(m_manager_mutex);
        auto it = m_sources.find(instance_id);
        if (it == m_sources.end()) {
            LOG_CPP_ERROR("Source processor instance not found: %s", instance_id.c_str());
            return false;
        }

        if (it->second) {
            source_tag_for_removal = it->second->get_source_tag();
        } else {
            m_sources.erase(it);
            return false;
        }

        source_to_remove = std::move(it->second);
        m_sources.erase(it);

        m_rtp_to_source_queues.erase(instance_id);
        m_source_to_sink_queues.erase(instance_id);
        m_command_queues.erase(instance_id);

        // Release system audio capture device if this source was using one
        auto capture_it = m_instance_to_capture_tag.find(instance_id);
        if (capture_it != m_instance_to_capture_tag.end()) {
            std::string capture_tag = capture_it->second;
            m_instance_to_capture_tag.erase(capture_it);
            
            if (m_release_capture_callback) {
                m_release_capture_callback(capture_tag);
                const char* backend_label = "ALSA";
#if defined(_WIN32)
                backend_label = "WASAPI";
#endif
                LOG_CPP_INFO("Released %s capture device %s for instance %s",
                             backend_label,
                             capture_tag.c_str(),
                             instance_id.c_str());
            }
        }

        if (m_timeshift_manager && !source_tag_for_removal.empty()) {
            m_timeshift_manager->unregister_processor(instance_id, source_tag_for_removal);
            LOG_CPP_INFO("Unregistered instance %s (tag: %s) from TimeshiftManager.", instance_id.c_str(), source_tag_for_removal.c_str());
        }
    }

    if (source_to_remove) {
        source_to_remove->stop();
        LOG_CPP_INFO("Source processor instance %s stopped and removed.", instance_id.c_str());
        return true;
    }

    return false;
}

std::map<std::string, std::unique_ptr<SourceInputProcessor>>& SourceManager::get_sources() {
    return m_sources;
}

std::map<std::string, std::shared_ptr<ChunkQueue>>& SourceManager::get_source_to_sink_queues() {
    return m_source_to_sink_queues;
}

std::map<std::string, std::shared_ptr<CommandQueue>>& SourceManager::get_command_queues() {
    return m_command_queues;
}

std::vector<SourceInputProcessor*> SourceManager::get_all_processors() {
    std::vector<SourceInputProcessor*> processors;
    std::scoped_lock lock(m_manager_mutex);
    for (auto const& [id, proc] : m_sources) {
        processors.push_back(proc.get());
    }
    return processors;
}

void SourceManager::stop_all() {
    std::vector<std::pair<std::string, std::string>> to_unregister; // (instance_id, source_tag)
    std::vector<std::unique_ptr<SourceInputProcessor>> to_stop;
    std::vector<std::string> capture_tags;

    {
        std::scoped_lock lock(m_manager_mutex);
        for (auto &entry : m_sources) {
            const std::string &instance_id = entry.first;
            if (entry.second) {
                to_unregister.emplace_back(instance_id, entry.second->get_source_tag());
            }
            to_stop.push_back(std::move(entry.second));
        }
        m_sources.clear();

        // Clear queues for each source
        m_rtp_to_source_queues.clear();
        m_source_to_sink_queues.clear();
        m_command_queues.clear();

        // Gather capture tags to release
        for (auto const &kv : m_instance_to_capture_tag) {
            capture_tags.push_back(kv.second);
        }
        m_instance_to_capture_tag.clear();
    }

    // Unregister processors from TimeshiftManager outside the lock
    for (auto const &p : to_unregister) {
        if (m_timeshift_manager && !p.second.empty()) {
            m_timeshift_manager->unregister_processor(p.first, p.second);
            LOG_CPP_INFO("Unregistered instance %s (tag: %s) from TimeshiftManager (shutdown).",
                         p.first.c_str(), p.second.c_str());
        }
    }

    // Stop each processor thread cleanly
    for (auto &proc : to_stop) {
        if (proc) {
            proc->stop();
        }
    }

    // Release system capture device references
    if (m_release_capture_callback) {
        for (const auto &tag : capture_tags) {
            m_release_capture_callback(tag);
            const char* backend_label = "ALSA";
#if defined(_WIN32)
            backend_label = "WASAPI";
#endif
            LOG_CPP_INFO("Released %s capture device %s during shutdown", backend_label, tag.c_str());
        }
    }
}

} // namespace audio
} // namespace screamrouter
