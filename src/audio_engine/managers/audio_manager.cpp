#include "audio_manager.h"
#include "../utils/cpp_logger.h"
#include "../synchronization/global_synchronization_clock.h"
#include "../synchronization/sink_synchronization_coordinator.h"
#include "../system_audio/system_audio_tags.h"

#if defined(__linux__)
#include "../system_audio/alsa_device_enumerator.h"
#endif

#ifdef _WIN32
#include "../system_audio/wasapi_device_enumerator.h"
#endif
#include <algorithm>
#include <stdexcept>
#include <system_error>
#include <utility>

using namespace screamrouter::audio;

AudioManager::AudioManager() : m_running(false) {
    LOG_CPP_INFO("AudioManager created.");
}

AudioManager::~AudioManager() {
    LOG_CPP_INFO("AudioManager destroying...");
    if (m_running) {
        shutdown();
    }
    LOG_CPP_INFO("AudioManager destroyed.");
}

bool AudioManager::initialize(int rtp_listen_port, int global_timeshift_buffer_duration_sec) {
    std::scoped_lock lock(m_manager_mutex);
    if (m_running) {
        LOG_CPP_INFO("AudioManager already initialized.");
        return true;
    }

    LOG_CPP_INFO("Initializing AudioManager with rtp_listen_port: %d, timeshift_buffer_duration: %ds", rtp_listen_port, global_timeshift_buffer_duration_sec);

    try {
        m_settings = std::make_shared<AudioEngineSettings>();
        m_timeshift_manager = std::make_unique<TimeshiftManager>(std::chrono::seconds(global_timeshift_buffer_duration_sec), m_settings);
        m_notification_queue = std::make_shared<NotificationQueue>();

        std::unique_ptr<system_audio::SystemDeviceEnumerator> enumerator;
#if defined(__linux__)
        try {
            enumerator.reset(new system_audio::AlsaDeviceEnumerator(m_notification_queue));
        } catch (const std::exception& e) {
            LOG_CPP_WARNING("[AudioManager] Failed to construct ALSA device enumerator: %s. Continuing without system device enumeration.", e.what());
        }
#elif defined(_WIN32)
        try {
            enumerator.reset(new system_audio::WasapiDeviceEnumerator(m_notification_queue));
        } catch (const std::exception& e) {
            LOG_CPP_WARNING("[AudioManager] Failed to construct WASAPI device enumerator: %s. Continuing without system device enumeration.", e.what());
        }
#else
        enumerator.reset(nullptr);
#endif
        m_system_device_enumerator = std::move(enumerator);
        if (m_system_device_enumerator) {
            try {
                m_system_device_enumerator->start();
                std::lock_guard<std::mutex> device_lock(device_registry_mutex_);
                system_device_registry_ = m_system_device_enumerator->get_registry_snapshot();
            } catch (const std::exception& e) {
                LOG_CPP_WARNING("[AudioManager] System audio enumerator failed to start: %s. Disabling system device monitoring.", e.what());
                try {
                    m_system_device_enumerator->stop();
                } catch (const std::exception& stop_err) {
                    LOG_CPP_DEBUG("[AudioManager] Ignoring enumerator stop exception after start failure: %s", stop_err.what());
                }
                m_system_device_enumerator.reset();
                std::lock_guard<std::mutex> device_lock(device_registry_mutex_);
                system_device_registry_.clear();
            }
        }

        m_source_manager = std::make_unique<SourceManager>(m_manager_mutex, m_timeshift_manager.get(), m_settings);
        
        // Set up callbacks for system capture device management
        m_source_manager->set_capture_device_callbacks(
            [this](const std::string& tag) -> bool {
                return this->ensure_system_capture_device(tag);
            },
            [this](const std::string& tag) {
                this->release_system_capture_device(tag);
            }
        );
        m_sink_manager = std::make_unique<SinkManager>(m_manager_mutex, m_settings, m_timeshift_manager.get());
        m_receiver_manager = std::make_unique<ReceiverManager>(m_manager_mutex, m_timeshift_manager.get());
        m_receiver_manager->set_stream_tag_callbacks(
            [this](const std::string& wildcard, const std::string& concrete) {
                this->handle_stream_tag_resolved(wildcard, concrete);
            },
            [this](const std::string& wildcard) {
                this->handle_stream_tag_removed(wildcard);
            });
        m_webrtc_manager = std::make_unique<WebRtcManager>(m_manager_mutex, m_sink_manager.get(), m_sink_manager->get_sink_configs());
        m_connection_manager = std::make_unique<ConnectionManager>(m_manager_mutex, m_source_manager.get(), m_sink_manager.get(), m_source_manager->get_source_to_sink_queues(), m_source_manager->get_sources());
        m_control_api_manager = std::make_unique<ControlApiManager>(m_manager_mutex, m_source_manager->get_command_queues(), m_timeshift_manager.get(), m_source_manager->get_sources());
        m_mp3_data_api_manager = std::make_unique<MP3DataApiManager>(m_manager_mutex, m_sink_manager->get_mp3_output_queues(), m_sink_manager->get_sink_configs());
        m_stats_manager = std::make_unique<StatsManager>(m_timeshift_manager.get(), m_source_manager.get(), m_sink_manager.get());

        if (!m_receiver_manager->initialize_receivers(rtp_listen_port, m_notification_queue)) {
            throw std::runtime_error("Failed to initialize receivers");
        }

        m_timeshift_manager->start();
        m_receiver_manager->start_receivers();
        m_stats_manager->start();
        
        m_notification_thread = std::thread(&AudioManager::process_notifications, this);

    } catch (const std::exception& e) {
        LOG_CPP_ERROR("Failed to initialize AudioManager: %s", e.what());
        // Clean up partially initialized components
        shutdown();
        return false;
    }

    m_running = true;
    LOG_CPP_INFO("AudioManager initialization successful.");
    return true;
}

void AudioManager::shutdown() {
    std::unique_lock<std::recursive_mutex> lock(m_manager_mutex);
    if (!m_running) {
        LOG_CPP_INFO("AudioManager already shut down.");
        return;
    }
    m_running = false;

    // Ensure C++ logs go to stderr during shutdown for visibility
    screamrouter::audio::logging::set_cpp_log_level(screamrouter::audio::logging::LogLevel::DEBUG);
    screamrouter::audio::logging::set_cpp_log_stderr_mirror(true);

    LOG_CPP_INFO("Shutting down AudioManager...");

    // Stop notification processing early to avoid new events
    if (m_notification_queue) {
        m_notification_queue->stop();
    }
    if (m_notification_thread.joinable()) {
        m_notification_thread.join();
    }

    // Release the manager mutex to avoid deadlocks when joining threads
    // (e.g., WebRTC setup threads that may need this mutex via SinkManager)
    lock.unlock();

    // Emit initial state snapshot
    debug_dump_state("BEGIN_SHUTDOWN");

    // Disable and remove sink coordinators via per-sink removal
    if (m_sink_manager) {
        auto sink_ids = m_sink_manager->get_sink_ids();
        LOG_CPP_INFO("[Shutdown] Removing %zu sinks...", sink_ids.size());
        for (const auto &sid : sink_ids) {
            LOG_CPP_INFO("[Shutdown] Removing sink '%s'", sid.c_str());
            // remove_sink disables coordinator and stops mixer
            remove_sink(sid);
        }
    }

    // Stop all remaining sinks defensively (in case any were added but not in configs)
    if (m_sink_manager) {
        LOG_CPP_INFO("[Shutdown] Calling SinkManager::stop_all()...");
        m_sink_manager->stop_all();
    }

    // Disconnect and stop all sources; unregister from timeshift and release capture devices
    if (m_source_manager) {
        LOG_CPP_INFO("[Shutdown] Calling SourceManager::stop_all()...");
        m_source_manager->stop_all();
    }

    // Stop network receivers and clean them up
    if (m_receiver_manager) {
        LOG_CPP_INFO("[Shutdown] Stopping receivers...");
        m_receiver_manager->log_status();
        m_receiver_manager->stop_receivers();
        LOG_CPP_INFO("[Shutdown] Receivers stopped. Cleaning up...");
        m_receiver_manager->cleanup_receivers();
    }

    // Stop stats and timeshift after producers/consumers are quiet
    if (m_stats_manager) {
        LOG_CPP_INFO("[Shutdown] Stopping StatsManager...");
        m_stats_manager->stop();
    }
    if (m_timeshift_manager) {
        LOG_CPP_INFO("[Shutdown] Stopping TimeshiftManager...");
        m_timeshift_manager->stop();
    }

    // Stop system device enumerator last
    if (m_system_device_enumerator) {
        LOG_CPP_INFO("[Shutdown] Stopping SystemDeviceEnumerator...");
        m_system_device_enumerator->stop();
    }

    // The managers will be destroyed automatically via unique_ptr
    LOG_CPP_INFO("[Shutdown] Stopping Receiver Manager...");
    m_receiver_manager.reset();
    LOG_CPP_INFO("[Shutdown] Stopping WebRTC Manager...");
    m_webrtc_manager.reset();
    LOG_CPP_INFO("[Shutdown] Stopping MP3 Data API...");
    m_mp3_data_api_manager.reset();
    LOG_CPP_INFO("[Shutdown] Stopping Control API ...");
    m_control_api_manager.reset();
    LOG_CPP_INFO("[Shutdown] Stopping Connection Manager...");
    m_connection_manager.reset();
    LOG_CPP_INFO("[Shutdown] Stopping Sink Manager...");
    m_sink_manager.reset();
    LOG_CPP_INFO("[Shutdown] Stopping Source Manager...");
    m_source_manager.reset();
    LOG_CPP_INFO("[Shutdown] Stopping Timeshift Manager...");
    m_timeshift_manager.reset();
    LOG_CPP_INFO("[Shutdown] Stopping Stats Manager...");
    m_stats_manager.reset();
    LOG_CPP_INFO("[Shutdown] Stopping System Device Enumerator...");
    m_system_device_enumerator.reset();

    // Clean up synchronization coordinators
    LOG_CPP_INFO("[Shutdown] Clearing Sink Coordinators...");
    sink_coordinators_.clear();

    // Clean up sync clocks
    LOG_CPP_INFO("[Shutdown] Clearing Clocks...");
    sync_clocks_.clear();

    {
        std::lock_guard<std::mutex> device_lock(device_registry_mutex_);
        system_device_registry_.clear();
    }
    {
        std::lock_guard<std::mutex> event_lock(pending_device_events_mutex_);
        pending_device_events_.clear();
    }

    LOG_CPP_INFO("AudioManager shutdown complete.");
}

void AudioManager::debug_dump_state(const char* label) {
    LOG_CPP_INFO("[DebugDump] Label=%s running=%d", label ? label : "", m_running ? 1 : 0);
    // Sinks
    if (m_sink_manager) {
        auto ids = m_sink_manager->get_sink_ids();
        LOG_CPP_INFO("[DebugDump] Sinks: %zu", ids.size());
        for (auto const& id : ids) {
            LOG_CPP_INFO("  - sink id='%s'", id.c_str());
        }
        auto mixers = m_sink_manager->get_all_mixers();
        for (auto* m : mixers) {
            auto cfg = m->get_config();
            auto stats = m->get_stats();
            LOG_CPP_INFO("  mixer '%s' running=%d inputs=%zu active_inputs=%zu listeners=%zu chunks_mixed=%llu", cfg.sink_id.c_str(), m->is_running() ? 1 : 0, stats.total_input_streams, stats.active_input_streams, stats.listener_ids.size(), (unsigned long long)stats.total_chunks_mixed);
        }
    } else {
        LOG_CPP_INFO("[DebugDump] Sinks: manager=null");
    }

    // Sources
    if (m_source_manager) {
        auto procs = m_source_manager->get_all_processors();
        LOG_CPP_INFO("[DebugDump] Sources: %zu", procs.size());
        for (auto* p : procs) {
            auto st = p->get_stats();
            auto q = p->get_input_queue();
            size_t qsize = q ? q->size() : 0;
            LOG_CPP_INFO("  source id='%s' tag='%s' input_q=%zu total_packets=%llu reconfigs=%llu", p->get_instance_id().c_str(), p->get_source_tag().c_str(), qsize, (unsigned long long)st.total_packets_processed, (unsigned long long)st.reconfigurations);
        }
    } else {
        LOG_CPP_INFO("[DebugDump] Sources: manager=null");
    }

    // Timeshift
    if (m_timeshift_manager) {
        auto ts = m_timeshift_manager->get_stats();
        LOG_CPP_INFO("[DebugDump] Timeshift: running=%d buffer_size=%zu packets_added=%llu", m_timeshift_manager->is_running() ? 1 : 0, ts.global_buffer_size, (unsigned long long)ts.total_packets_added);
    } else {
        LOG_CPP_INFO("[DebugDump] Timeshift: none");
    }

    // Receivers
    if (m_receiver_manager) {
        m_receiver_manager->log_status();
    } else {
        LOG_CPP_INFO("[DebugDump] Receivers: none");
    }

    // Stats manager
    if (m_stats_manager) {
        LOG_CPP_INFO("[DebugDump] StatsManager running=%d", m_stats_manager->is_running() ? 1 : 0);
    } else {
        LOG_CPP_INFO("[DebugDump] StatsManager: none");
    }
}

GlobalSynchronizationClock* AudioManager::get_or_create_sync_clock(int sample_rate) {
    auto it = sync_clocks_.find(sample_rate);
    if (it != sync_clocks_.end()) {
        return it->second.get();
    }
    
    // Create new clock for this sample rate
    auto clock = std::make_unique<GlobalSynchronizationClock>(sample_rate);
    auto* clock_ptr = clock.get();
    sync_clocks_[sample_rate] = std::move(clock);
    
    LOG_CPP_INFO("[AudioManager] Created GlobalSyncClock for %d Hz", sample_rate);
    return clock_ptr;
}

bool AudioManager::add_sink(SinkConfig config) {
    const auto t0 = std::chrono::steady_clock::now();
    if (!m_sink_manager) {
        return false;
    }
    
    // First, create the sink through SinkManager
    if (!m_sink_manager->add_sink(config, m_running)) {
        return false;
    }
    const auto t_after_add = std::chrono::steady_clock::now();
    LOG_CPP_INFO("[AudioManager] add_sink id='%s' proto='%s' created in %lld ms",
                 config.id.c_str(), config.protocol.c_str(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_after_add - t0).count());
    
    // Now add synchronization if enabled
    if (m_settings && m_settings->synchronization.enable_multi_sink_sync) {
        int output_rate = config.samplerate;
        auto* global_clock = get_or_create_sync_clock(output_rate);
        
        // Get the mixer that was just created
        auto mixers = m_sink_manager->get_all_mixers();
        SinkAudioMixer* mixer = nullptr;
        for (auto* m : mixers) {
            if (m->get_config().sink_id == config.id) {
                mixer = m;
                break;
            }
        }
        
        if (mixer) {
            // Create synchronization coordinator
            auto coordinator = std::make_unique<SinkSynchronizationCoordinator>(
                config.id,
                mixer,
                global_clock,
                m_settings->synchronization_tuning.barrier_timeout_ms
            );
            
            // Configure the mixer for coordination
            mixer->set_coordination_mode(true);
            mixer->set_coordinator(coordinator.get());
            
            // Initialize reference timestamp on first sink for this clock
            if (global_clock->get_stats().active_sinks == 0) {
                // Use current time as reference - actual RTP timestamp will be set when first audio arrives
                global_clock->initialize_reference(0, std::chrono::steady_clock::now());
                global_clock->set_enabled(true);
            }
            
            // Register sink with global clock and enable coordinator
            coordinator->enable();
            
            // Store the coordinator
            sink_coordinators_[config.id] = std::move(coordinator);
            
            LOG_CPP_INFO("[AudioManager] Sink '%s' registered for synchronized playback at %d Hz",
                         config.id.c_str(), output_rate);
        } else {
            LOG_CPP_ERROR("[AudioManager] Failed to get mixer for sink '%s' after creation",
                          config.id.c_str());
        }
    }
    
    const auto t1 = std::chrono::steady_clock::now();
    LOG_CPP_INFO("[AudioManager] add_sink id='%s' total %lld ms",
                 config.id.c_str(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    return true;
}

bool AudioManager::remove_sink(const std::string& sink_id) {
    const auto t0 = std::chrono::steady_clock::now();
    if (!m_sink_manager) {
        return false;
    }
    
    // First, disable and remove the coordinator if it exists
    auto coord_it = sink_coordinators_.find(sink_id);
    if (coord_it != sink_coordinators_.end()) {
        // Disable coordinator (this will unregister from global clock)
        coord_it->second->disable();
        
        // Remove the coordinator (destructor will also unregister if needed)
        sink_coordinators_.erase(coord_it);
        
        LOG_CPP_INFO("[AudioManager] Removed synchronization coordinator for sink '%s'",
                     sink_id.c_str());
    }
    
    // Now remove the sink through SinkManager
    const bool ok = m_sink_manager->remove_sink(sink_id);
    const auto t1 = std::chrono::steady_clock::now();
    LOG_CPP_INFO("[AudioManager] remove_sink id='%s' -> %s (%lld ms)",
                 sink_id.c_str(), ok ? "OK" : "FAIL",
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    return ok;
}

std::string AudioManager::configure_source(SourceConfig config) {
    const auto t0 = std::chrono::steady_clock::now();
    if (!m_source_manager) {
        return "";
    }
    const std::string id = m_source_manager->configure_source(config, m_running);
    const auto t1 = std::chrono::steady_clock::now();
    LOG_CPP_INFO("[AudioManager] configure_source tag='%s' -> instance='%s' (%lld ms)",
                 config.tag.c_str(), id.c_str(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    return id;
}

bool AudioManager::remove_source(const std::string& instance_id) {
    if (!m_source_manager) return false;
    
    // Before removing the source, disconnect it from all sinks
    if (m_sink_manager) {
        auto sink_ids = m_sink_manager->get_sink_ids();
        for (const auto& sink_id : sink_ids) {
            disconnect_source_sink(instance_id, sink_id);
        }
    }
    
    return m_source_manager->remove_source(instance_id);
}

bool AudioManager::connect_source_sink(const std::string& source_instance_id, const std::string& sink_id) {
    const auto t0 = std::chrono::steady_clock::now();
    bool ok = m_connection_manager ? m_connection_manager->connect_source_sink(source_instance_id, sink_id, m_running) : false;
    const auto t1 = std::chrono::steady_clock::now();
    LOG_CPP_INFO("[AudioManager] connect %s -> %s : %s (%lld ms)",
                 source_instance_id.c_str(), sink_id.c_str(), ok ? "OK" : "FAIL",
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    return ok;
}

bool AudioManager::disconnect_source_sink(const std::string& source_instance_id, const std::string& sink_id) {
    const auto t0 = std::chrono::steady_clock::now();
    bool ok = m_connection_manager ? m_connection_manager->disconnect_source_sink(source_instance_id, sink_id, m_running) : false;
    const auto t1 = std::chrono::steady_clock::now();
    LOG_CPP_INFO("[AudioManager] disconnect %s -/-> %s : %s (%lld ms)",
                 source_instance_id.c_str(), sink_id.c_str(), ok ? "OK" : "FAIL",
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    return ok;
}

void AudioManager::update_source_parameters(const std::string& instance_id, SourceParameterUpdates params) {
    if (m_control_api_manager) {
        m_control_api_manager->update_source_parameters(instance_id, params, m_running);
    }
}

std::vector<uint8_t> AudioManager::get_mp3_data(const std::string& sink_id) {
    return m_mp3_data_api_manager ? m_mp3_data_api_manager->get_mp3_data(sink_id, m_running) : std::vector<uint8_t>();
}

std::vector<uint8_t> AudioManager::get_mp3_data_by_ip(const std::string& ip_address) {
    return m_mp3_data_api_manager ? m_mp3_data_api_manager->get_mp3_data_by_ip(ip_address, m_running) : std::vector<uint8_t>();
}

std::vector<std::string> AudioManager::get_rtp_receiver_seen_tags() {
    return m_receiver_manager ? m_receiver_manager->get_rtp_receiver_seen_tags() : std::vector<std::string>();
}

pybind11::list AudioManager::get_rtp_sap_announcements() {
    pybind11::list result;
    if (!m_receiver_manager) {
        return result;
    }

    auto announcements = m_receiver_manager->get_rtp_sap_announcements();
    for (const auto& announcement : announcements) {
        pybind11::dict entry;
        entry["ip"] = announcement.stream_ip;
        entry["announcer_ip"] = announcement.announcer_ip;
        entry["port"] = announcement.port;
        entry["sample_rate"] = announcement.properties.sample_rate;
        entry["channels"] = announcement.properties.channels;
        entry["bit_depth"] = announcement.properties.bit_depth;
        entry["endianness"] = (announcement.properties.endianness == Endianness::LITTLE) ? "little" : "big";
        result.append(entry);
    }

    return result;
}

std::vector<std::string> AudioManager::get_raw_scream_receiver_seen_tags(int listen_port) {
    return m_receiver_manager ? m_receiver_manager->get_raw_scream_receiver_seen_tags(listen_port) : std::vector<std::string>();
}

std::vector<std::string> AudioManager::get_per_process_scream_receiver_seen_tags(int listen_port) {
    return m_receiver_manager ? m_receiver_manager->get_per_process_scream_receiver_seen_tags(listen_port) : std::vector<std::string>();
}

#if !defined(_WIN32)
std::vector<std::string> AudioManager::get_pulse_receiver_seen_tags() {
    return m_receiver_manager ? m_receiver_manager->get_pulse_receiver_seen_tags() : std::vector<std::string>();
}
#endif

std::optional<std::string> AudioManager::resolve_stream_tag(const std::string& tag) {
    LOG_CPP_DEBUG("[AudioManager] resolve_stream_tag('%s')", tag.c_str());
    if (!m_receiver_manager) {
        LOG_CPP_DEBUG("[AudioManager] resolve_stream_tag('%s') => <no receiver manager>", tag.c_str());
        return std::nullopt;
    }
    auto resolved = m_receiver_manager->resolve_stream_tag(tag);
    if (resolved) {
        LOG_CPP_INFO("[AudioManager] resolve_stream_tag('%s') => '%s'", tag.c_str(), resolved->c_str());
    } else {
        LOG_CPP_DEBUG("[AudioManager] resolve_stream_tag('%s') => <none>", tag.c_str());
    }
    return resolved;
}

std::vector<std::string> AudioManager::list_stream_tags_for_wildcard(const std::string& wildcard_tag) {
    if (!m_receiver_manager) {
        return {};
    }
    return m_receiver_manager->list_stream_tags_for_wildcard(wildcard_tag);
}

void AudioManager::handle_stream_tag_resolved(const std::string& wildcard_tag,
                                              const std::string& concrete_tag) {
    LOG_CPP_INFO("[AudioManager] Stream tag resolved: '%s' -> '%s'",
                 wildcard_tag.c_str(), concrete_tag.c_str());
    std::function<void(const std::string&, const std::string&)> listener;
    {
        std::lock_guard<std::mutex> lock(stream_tag_listener_mutex_);
        listener = stream_tag_listener_on_resolved_;
    }
    if (listener) {
        listener(wildcard_tag, concrete_tag);
    } else {
        LOG_CPP_DEBUG("[AudioManager] No stream tag listener registered for resolution events.");
    }
}

void AudioManager::handle_stream_tag_removed(const std::string& wildcard_tag) {
    LOG_CPP_INFO("[AudioManager] Stream tag removed: '%s'", wildcard_tag.c_str());
    std::function<void(const std::string&)> listener;
    {
        std::lock_guard<std::mutex> lock(stream_tag_listener_mutex_);
        listener = stream_tag_listener_on_removed_;
    }
    if (listener) {
        listener(wildcard_tag);
    } else {
        LOG_CPP_DEBUG("[AudioManager] No stream tag listener registered for removal events.");
    }
}

void AudioManager::set_stream_tag_listener(
    std::function<void(const std::string&, const std::string&)> on_resolved,
    std::function<void(const std::string&)> on_removed) {
    std::lock_guard<std::mutex> lock(stream_tag_listener_mutex_);
    stream_tag_listener_on_resolved_ = std::move(on_resolved);
    stream_tag_listener_on_removed_ = std::move(on_removed);
}

void AudioManager::clear_stream_tag_listener() {
    std::lock_guard<std::mutex> lock(stream_tag_listener_mutex_);
    stream_tag_listener_on_resolved_ = nullptr;
    stream_tag_listener_on_removed_ = nullptr;
}

bool AudioManager::add_system_capture_reference(const std::string& device_tag, CaptureParams params) {
    const auto t0 = std::chrono::steady_clock::now();
    std::scoped_lock lock(m_manager_mutex);
    if (!m_receiver_manager) {
        LOG_CPP_ERROR("AudioManager add_system_capture_reference called before receiver manager initialization.");
        return false;
    }

    constexpr unsigned int kDefaultChannels = 2;
    constexpr unsigned int kDefaultSampleRate = 48000;

    SystemDeviceInfo device_info{};
    bool have_device_info = false;
    {
        std::lock_guard<std::mutex> device_lock(device_registry_mutex_);
        auto it = system_device_registry_.find(device_tag);
        if (it != system_device_registry_.end()) {
            device_info = it->second;
            have_device_info = true;
            if (params.hw_id.empty()) {
                params.hw_id = device_info.hw_id;
            }
        }
    }

    if (params.hw_id.empty()) {
        if (device_tag.rfind("hw:", 0) == 0) {
            params.hw_id = device_tag;
        } else if (device_tag.rfind("ac:", 0) == 0) {
            const std::string body = device_tag.substr(3);
            const auto dot_pos = body.find('.');
            if (dot_pos != std::string::npos) {
                try {
                    int card = std::stoi(body.substr(0, dot_pos));
                    int device = std::stoi(body.substr(dot_pos + 1));
                    params.hw_id = "hw:" + std::to_string(card) + "," + std::to_string(device);
                } catch (const std::exception&) {
                    params.hw_id = body;
                }
            } else {
                params.hw_id = body;
            }
        }
    }

#if defined(__linux__)
    if (system_audio::tag_has_prefix(device_tag, system_audio::kScreamrouterCapturePrefix) && params.hw_id.empty()) {
        LOG_CPP_ERROR("AudioManager cannot resolve FIFO path for capture device %s.", device_tag.c_str());
        return false;
    }
#endif

#if defined(_WIN32)
    if (params.endpoint_id.empty()) {
        if (have_device_info && !device_info.endpoint_id.empty()) {
            params.endpoint_id = device_info.endpoint_id;
        } else if (device_tag.rfind("wp:", 0) == 0 ||
                   device_tag.rfind("wc:", 0) == 0 ||
                   device_tag.rfind("ws:", 0) == 0) {
            params.endpoint_id = device_tag.substr(3);
        }
    }
#endif

    auto clamp_within_caps = [](unsigned int requested,
                                unsigned int cap_min,
                                unsigned int cap_max,
                                unsigned int fallback) {
        unsigned int min_val = cap_min;
        unsigned int max_val = cap_max;

        if (min_val == 0 && max_val == 0) {
            unsigned int value = requested ? requested : fallback;
            bool changed = (value != requested);
            return std::make_pair(value, changed);
        }

        if (min_val == 0) {
            min_val = max_val;
        }
        if (max_val == 0) {
            max_val = min_val;
        }
        if (min_val > max_val) {
            std::swap(min_val, max_val);
        }

        unsigned int effective = requested ? requested : min_val;
        unsigned int clamped = std::clamp(effective, min_val, max_val);
        bool changed = requested && (clamped != requested);
        return std::make_pair(clamped, changed);
    };

    if (have_device_info) {
        unsigned int original_channels = params.channels;
        auto [adjusted_channels, channel_changed] = clamp_within_caps(
            original_channels,
            device_info.channels.min,
            device_info.channels.max,
            kDefaultChannels);
        if (channel_changed) {
            LOG_CPP_INFO("AudioManager adjusted capture channel count for %s from %u to %u to match device capabilities.",
                         device_tag.c_str(),
                         static_cast<unsigned int>(original_channels),
                         adjusted_channels);
        }
        params.channels = adjusted_channels;

        unsigned int original_rate = params.sample_rate;
        auto [adjusted_rate, rate_changed] = clamp_within_caps(
            original_rate,
            device_info.sample_rates.min,
            device_info.sample_rates.max,
            kDefaultSampleRate);
        if (rate_changed) {
            LOG_CPP_INFO("AudioManager adjusted capture sample rate for %s from %u Hz to %u Hz to match device capabilities.",
                         device_tag.c_str(),
                         static_cast<unsigned int>(original_rate),
                         adjusted_rate);
        }
        params.sample_rate = adjusted_rate;

        if (device_info.bit_depth > 0) {
            params.bit_depth = device_info.bit_depth;
        }
    }

    if (params.channels == 0) {
        params.channels = kDefaultChannels;
    }
    if (params.sample_rate == 0) {
        params.sample_rate = kDefaultSampleRate;
    }
    if (params.bit_depth != 16 && params.bit_depth != 32) {
        params.bit_depth = 16;
    }

#if defined(__linux__)
    if (params.hw_id.empty()) {
        LOG_CPP_ERROR("AudioManager cannot resolve hw_id for capture device %s.", device_tag.c_str());
        return false;
    }
#elif defined(_WIN32)
    if (params.endpoint_id.empty()) {
        LOG_CPP_ERROR("AudioManager cannot resolve endpoint id for capture device %s.", device_tag.c_str());
        return false;
    }
#endif

    const auto t_ensure0 = std::chrono::steady_clock::now();
    const bool ok = m_receiver_manager->ensure_capture_receiver(device_tag, params);
    const auto t1 = std::chrono::steady_clock::now();
    LOG_CPP_INFO("[AudioManager] ensure_capture tag='%s' -> %s (%lld ms total, %lld ms ensure)",
                 device_tag.c_str(), ok ? "OK" : "FAIL",
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t_ensure0).count());
    return ok;
}

void AudioManager::remove_system_capture_reference(const std::string& device_tag) {
    std::scoped_lock lock(m_manager_mutex);
    if (!m_receiver_manager) {
        return;
    }
    m_receiver_manager->release_capture_receiver(device_tag);
}

bool AudioManager::ensure_system_capture_device(const std::string& device_tag) {
    return add_system_capture_reference(device_tag, CaptureParams{});
}

void AudioManager::release_system_capture_device(const std::string& device_tag) {
    remove_system_capture_reference(device_tag);
}

bool AudioManager::write_plugin_packet(
    const std::string& source_instance_tag,
    const std::vector<uint8_t>& audio_payload,
    int channels,
    int sample_rate,
    int bit_depth,
    uint8_t chlayout1,
    uint8_t chlayout2)
{
    if (m_control_api_manager) {
        return m_control_api_manager->write_plugin_packet(source_instance_tag, audio_payload, channels, sample_rate, bit_depth, chlayout1, chlayout2, m_running);
    }
    return false;
}

void AudioManager::inject_plugin_packet_globally(
    const std::string& source_tag,
    const std::vector<uint8_t>& audio_payload,
    int channels,
    int sample_rate,
    int bit_depth,
    uint8_t chlayout1,
    uint8_t chlayout2)
{
    if (m_running && m_timeshift_manager) {
        TaggedAudioPacket packet;
        packet.source_tag = source_tag;
        packet.received_time = std::chrono::steady_clock::now();
        packet.sample_rate = sample_rate;
        packet.bit_depth = bit_depth;
        packet.channels = channels;
        packet.chlayout1 = chlayout1;
        packet.chlayout2 = chlayout2;
        packet.audio_data = audio_payload;
        m_timeshift_manager->add_packet(std::move(packet));
    }
}

bool AudioManager::add_webrtc_listener(
    const std::string& sink_id,
    const std::string& listener_id,
    const std::string& offer_sdp,
    std::function<void(const std::string& sdp)> on_local_description_callback,
    std::function<void(const std::string& candidate, const std::string& sdpMid)> on_ice_candidate_callback,
    const std::string& client_ip)
{
    return m_webrtc_manager ? m_webrtc_manager->add_webrtc_listener(sink_id, listener_id, offer_sdp, on_local_description_callback, on_ice_candidate_callback, m_running, client_ip) : false;
}

bool AudioManager::remove_webrtc_listener(const std::string& sink_id, const std::string& listener_id) {
    return m_webrtc_manager ? m_webrtc_manager->remove_webrtc_listener(sink_id, listener_id, m_running) : false;
}

void AudioManager::set_webrtc_remote_description(const std::string& sink_id, const std::string& listener_id, const std::string& sdp, const std::string& type) {
    if (m_webrtc_manager) {
        m_webrtc_manager->set_webrtc_remote_description(sink_id, listener_id, sdp, type, m_running);
    }
}

void AudioManager::add_webrtc_remote_ice_candidate(const std::string& sink_id, const std::string& listener_id, const std::string& candidate, const std::string& sdpMid) {
    if (m_webrtc_manager) {
        m_webrtc_manager->add_webrtc_remote_ice_candidate(sink_id, listener_id, candidate, sdpMid, m_running);
    }
}

AudioEngineStats AudioManager::get_audio_engine_stats() {
    if (m_stats_manager) {
        return m_stats_manager->get_current_stats();
    }
    return AudioEngineStats();
}

AudioEngineSettings AudioManager::get_audio_settings() {
    std::scoped_lock lock(m_manager_mutex);
    if (m_settings) {
        return *m_settings;
    }
    return AudioEngineSettings();
}

void AudioManager::set_audio_settings(const AudioEngineSettings& new_settings) {
    std::scoped_lock lock(m_manager_mutex);
    if (m_settings) {
        *m_settings = new_settings;
    }
}

pybind11::dict AudioManager::get_sync_statistics() {
    namespace py = pybind11;
    std::scoped_lock lock(m_manager_mutex);
    
    py::dict stats;
    
    for (const auto& [rate, clock] : sync_clocks_) {
        auto clock_stats = clock->get_stats();
        py::dict rate_stats;
        rate_stats["active_sinks"] = clock_stats.active_sinks;
        rate_stats["current_playback_timestamp"] = clock_stats.current_playback_timestamp;
        rate_stats["max_drift_ppm"] = clock_stats.max_drift_ppm;
        rate_stats["avg_barrier_wait_ms"] = clock_stats.avg_barrier_wait_ms;
        rate_stats["total_barrier_timeouts"] = clock_stats.total_barrier_timeouts;
        
        stats[py::cast(rate)] = rate_stats;
    }
    
    return stats;
}

SystemDeviceRegistry AudioManager::list_system_devices() {
    if (m_system_device_enumerator) {
        auto snapshot = m_system_device_enumerator->get_registry_snapshot();
        std::lock_guard<std::mutex> lock(device_registry_mutex_);
        system_device_registry_ = std::move(snapshot);
        return system_device_registry_;
    }
    std::lock_guard<std::mutex> lock(device_registry_mutex_);
    return system_device_registry_;
}

std::vector<DeviceDiscoveryNotification> AudioManager::drain_device_notifications() {
    std::lock_guard<std::mutex> lock(pending_device_events_mutex_);
    std::vector<DeviceDiscoveryNotification> events;
    events.swap(pending_device_events_);
    return events;
}

void AudioManager::process_notifications() {
    LOG_CPP_INFO("Notification processing thread started.");
    while (true) {
        if (!m_notification_queue) {
            LOG_CPP_ERROR("Notification queue not available. Exiting notification loop.");
            break;
        }

        DeviceDiscoveryNotification notification;
        if (!m_notification_queue->pop(notification)) {
            if (m_running) {
                LOG_CPP_ERROR("Notification queue pop failed unexpectedly.");
            }
            break;
        }
        {
            std::lock_guard<std::mutex> lock(pending_device_events_mutex_);
            pending_device_events_.push_back(notification);
        }

        const bool is_system_tag = system_audio::is_capture_tag(notification.tag) ||
                                   system_audio::is_playback_tag(notification.tag);
        if (is_system_tag && m_system_device_enumerator) {
            auto snapshot = m_system_device_enumerator->get_registry_snapshot();
            std::lock_guard<std::mutex> lock(device_registry_mutex_);
            system_device_registry_ = std::move(snapshot);
        }
        LOG_CPP_DEBUG("Device notification received: %s present=%d", notification.tag.c_str(), notification.present ? 1 : 0);
    }
    LOG_CPP_INFO("Notification processing thread finished.");
}
