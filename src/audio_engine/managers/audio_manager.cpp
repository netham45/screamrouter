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
        enumerator.reset(new system_audio::AlsaDeviceEnumerator(m_notification_queue));
#elif defined(_WIN32)
        enumerator.reset(new system_audio::WasapiDeviceEnumerator(m_notification_queue));
#else
        enumerator.reset(nullptr);
#endif
        m_system_device_enumerator = std::move(enumerator);
        if (m_system_device_enumerator) {
            m_system_device_enumerator->start();
            std::lock_guard<std::mutex> device_lock(device_registry_mutex_);
            system_device_registry_ = m_system_device_enumerator->get_registry_snapshot();
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
        m_sink_manager = std::make_unique<SinkManager>(m_manager_mutex, m_settings);
        m_receiver_manager = std::make_unique<ReceiverManager>(m_manager_mutex, m_timeshift_manager.get());
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
    std::scoped_lock lock(m_manager_mutex);
    if (!m_running) {
        LOG_CPP_INFO("AudioManager already shut down.");
        return;
    }
    m_running = false;

    LOG_CPP_INFO("Shutting down AudioManager...");

    if (m_system_device_enumerator) {
        m_system_device_enumerator->stop();
    }

    if (m_notification_queue) {
        m_notification_queue->stop();
    }

    if (m_notification_thread.joinable()) {
        m_notification_thread.join();
    }

    if (m_receiver_manager) {
        m_receiver_manager->stop_receivers();
    }

    if (m_timeshift_manager) {
        m_timeshift_manager->stop();
    }

    if (m_stats_manager) {
        m_stats_manager->stop();
    }

    // The managers will be destroyed automatically via unique_ptr,
    // which will stop any components they own.
    m_receiver_manager.reset();
    m_webrtc_manager.reset();
    m_mp3_data_api_manager.reset();
    m_control_api_manager.reset();
    m_connection_manager.reset();
    m_sink_manager.reset();
    m_source_manager.reset();
    m_timeshift_manager.reset();
    m_stats_manager.reset();
    m_system_device_enumerator.reset();

    // Clean up synchronization coordinators
    sink_coordinators_.clear();

    // Clean up sync clocks
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
    if (!m_sink_manager) {
        return false;
    }
    
    // First, create the sink through SinkManager
    if (!m_sink_manager->add_sink(config, m_running)) {
        return false;
    }
    
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
    
    return true;
}

bool AudioManager::remove_sink(const std::string& sink_id) {
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
    return m_sink_manager->remove_sink(sink_id);
}

std::string AudioManager::configure_source(SourceConfig config) {
    return m_source_manager ? m_source_manager->configure_source(config, m_running) : "";
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
    return m_connection_manager ? m_connection_manager->connect_source_sink(source_instance_id, sink_id, m_running) : false;
}

bool AudioManager::disconnect_source_sink(const std::string& source_instance_id, const std::string& sink_id) {
    return m_connection_manager ? m_connection_manager->disconnect_source_sink(source_instance_id, sink_id, m_running) : false;
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

bool AudioManager::add_system_capture_reference(const std::string& device_tag, CaptureParams params) {
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

    return m_receiver_manager->ensure_capture_receiver(device_tag, params);
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
