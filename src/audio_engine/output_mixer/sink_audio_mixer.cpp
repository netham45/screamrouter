/**
 * @file sink_audio_mixer.cpp
 * @brief Implements the SinkAudioMixer class for mixing and outputting audio.
 * @details This file contains the implementation of the SinkAudioMixer, which is a
 *          core component responsible for collecting audio from multiple sources,
 *          mixing it, and dispatching it to various network senders.
 */
#include "sink_audio_mixer.h"
#include "mix_scheduler.h"
#include "../utils/cpp_logger.h"
#include "../configuration/audio_engine_settings.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <vector>
#include <chrono>
#include <system_error>
#include <algorithm>
#include <cmath>
#include <thread>
#include <limits>
#include <unordered_set>
#include "../audio_processor/audio_processor.h"
#include "../senders/scream/scream_sender.h"
#include "../senders/rtp/rtp_sender.h"
#include "../senders/rtp/multi_device_rtp_sender.h"
#include "../senders/webrtc/webrtc_sender.h"
#include "../senders/system/alsa_playback_sender.h"
#if defined(__linux__)
#include "../senders/system/screamrouter_fifo_sender.h"
#endif
#if defined(_WIN32)
#include "../senders/system/wasapi_playback_sender.h"
#endif
#include "../synchronization/sink_synchronization_coordinator.h"

using namespace screamrouter::audio;
using namespace screamrouter::audio::utils;

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #ifdef __SSE2__
    #include <emmintrin.h>
    #endif
    #ifdef __AVX2__
    #include <immintrin.h>
    #endif
#endif

/** @brief Default bitrate for MP3 encoding if enabled. */
/**
 * @brief Constructs a SinkAudioMixer.
 * @param config The configuration for this sink.
 * @param mp3_output_queue A queue for sending out encoded MP3 data. Can be nullptr.
 */
SinkAudioMixer::SinkAudioMixer(
    SinkMixerConfig config,
    std::shared_ptr<Mp3OutputQueue> mp3_output_queue,
    std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings)
    : config_(config),
      m_settings(settings),
      chunk_size_bytes_(resolve_chunk_size_bytes(m_settings)),
      mixing_buffer_samples_(compute_processed_chunk_samples(chunk_size_bytes_)),
      mp3_buffer_size_(chunk_size_bytes_ * 8),
      mp3_output_queue_(mp3_output_queue),
      network_sender_(nullptr),
      mix_scheduler_(std::make_unique<MixScheduler>(config_.sink_id, m_settings)),
      mixing_buffer_(mixing_buffer_samples_, 0),
      stereo_buffer_(mixing_buffer_samples_ * 2, 0),
      payload_buffer_(mp3_buffer_size_, 0),
      lame_global_flags_(nullptr),
      stereo_preprocessor_(nullptr),
      mp3_encode_buffer_(mp3_buffer_size_),
      profiling_last_log_time_(std::chrono::steady_clock::now())
{
    LOG_CPP_INFO("[SinkMixer:%s] Initializing...", config_.sink_id.c_str());

    if (config_.output_bitdepth != 8 && config_.output_bitdepth != 16 && config_.output_bitdepth != 24 && config_.output_bitdepth != 32) {
         LOG_CPP_ERROR("[SinkMixer:%s] Unsupported output bit depth: %d. Defaulting to 16.", config_.sink_id.c_str(), config_.output_bitdepth);
         config_.output_bitdepth = 16;
    }
    if (config_.output_channels <= 0 || config_.output_channels > 8) {
         LOG_CPP_ERROR("[SinkMixer:%s] Invalid output channels: %d. Defaulting to 2.", config_.sink_id.c_str(), config_.output_channels);
         config_.output_channels = 2;
    }

    set_playback_format(config_.output_samplerate, config_.output_channels, config_.output_bitdepth);

    if (config_.protocol == "rtp") {
        if (config_.multi_device_mode && !config_.rtp_receivers.empty()) {
            LOG_CPP_INFO("[SinkMixer:%s] Creating MultiDeviceRtpSender with %zu receivers.",
                        config_.sink_id.c_str(), config_.rtp_receivers.size());
            network_sender_ = std::make_unique<MultiDeviceRtpSender>(config_);
        } else {
            LOG_CPP_INFO("[SinkMixer:%s] Creating RtpSender.", config_.sink_id.c_str());
            network_sender_ = std::make_unique<RtpSender>(config_);
        }
    } else if (config_.protocol == "scream") {
        LOG_CPP_INFO("[SinkMixer:%s] Creating ScreamSender.", config_.sink_id.c_str());
        network_sender_ = std::make_unique<ScreamSender>(config_);
    } else if (config_.protocol == "system_audio") {
#if defined(__linux__)
        const bool is_fifo_path = !config_.output_ip.empty() &&
                                  config_.output_ip.rfind("/var/run/screamrouter/", 0) == 0;
        const bool is_fifo_tag = !config_.output_ip.empty() &&
                                 config_.output_ip.rfind("sr_in:", 0) == 0;
        if (is_fifo_path || is_fifo_tag) {
            LOG_CPP_INFO("[SinkMixer:%s] Creating ScreamrouterFifoSender for FIFO %s.",
                         config_.sink_id.c_str(), config_.output_ip.c_str());
            network_sender_ = std::make_unique<ScreamrouterFifoSender>(config_);
        } else {
            LOG_CPP_INFO("[SinkMixer:%s] Creating AlsaPlaybackSender for device %s.",
                         config_.sink_id.c_str(), config_.output_ip.c_str());
            network_sender_ = std::make_unique<AlsaPlaybackSender>(config_);
        }
#elif defined(_WIN32)
        LOG_CPP_INFO("[SinkMixer:%s] Creating WasapiPlaybackSender for endpoint %s.",
                     config_.sink_id.c_str(), config_.output_ip.c_str());
        network_sender_ = std::make_unique<screamrouter::audio::system_audio::WasapiPlaybackSender>(config_);
#else
        LOG_CPP_ERROR("[SinkMixer:%s] system_audio protocol requested, but no host backend is compiled in.",
                      config_.sink_id.c_str());
        network_sender_ = nullptr;
#endif
    } else if (config_.protocol == "web_receiver") {
        LOG_CPP_INFO("[SinkMixer:%s] Protocol is 'web_receiver', skipping default sender creation.", config_.sink_id.c_str());
        network_sender_ = nullptr;
    } else {
        LOG_CPP_WARNING("[SinkMixer:%s] Unknown protocol '%s', defaulting to ScreamSender.", config_.sink_id.c_str(), config_.protocol.c_str());
        network_sender_ = std::make_unique<ScreamSender>(config_);
    }

    if (config_.protocol != "web_receiver" && !network_sender_) {
        LOG_CPP_ERROR("[SinkMixer:%s] Failed to create network sender for protocol '%s'.", config_.sink_id.c_str(), config_.protocol.c_str());
        throw std::runtime_error("Failed to create network sender.");
    }

    stereo_preprocessor_ = std::make_unique<AudioProcessor>(
        config_.output_channels, 2, 32, config_.output_samplerate, config_.output_samplerate, 1.0f, std::map<int, CppSpeakerLayout>(), m_settings
    );

    if (!stereo_preprocessor_) {
        LOG_CPP_ERROR("[SinkMixer:%s] Failed to create stereo preprocessor.", config_.sink_id.c_str());
        throw std::runtime_error("Failed to create stereo preprocessor.");
    } else {
        LOG_CPP_INFO("[SinkMixer:%s] Created stereo preprocessor.", config_.sink_id.c_str());
    }

    if (mp3_output_queue_) {
        initialize_lame();
    }

    LOG_CPP_INFO("[SinkMixer:%s] Initialization complete.", config_.sink_id.c_str());
}

/**
 * @brief Destructor for the SinkAudioMixer.
 */
SinkAudioMixer::~SinkAudioMixer() {
    if (!stop_flag_) {
        stop();
    }
    if (mix_scheduler_) {
        mix_scheduler_->shutdown();
    }
    join_startup_thread();
    if (component_thread_.joinable()) {
        LOG_CPP_WARNING("[SinkMixer:%s] Warning: Joining thread in destructor, stop() might not have been called properly.", config_.sink_id.c_str());
        component_thread_.join();
    }
}

/**
 * @brief Initializes the LAME MP3 encoder if MP3 output is enabled.
 */
void SinkAudioMixer::initialize_lame() {
    if (!mp3_output_queue_) return;

    LOG_CPP_INFO("[SinkMixer:%s] Initializing LAME MP3 encoder...", config_.sink_id.c_str());
    lame_global_flags_ = lame_init();
    if (!lame_global_flags_) {
        LOG_CPP_ERROR("[SinkMixer:%s] lame_init() failed.", config_.sink_id.c_str());
        return;
    }

    lame_set_in_samplerate(lame_global_flags_, config_.output_samplerate);
    lame_set_brate(lame_global_flags_, m_settings->mixer_tuning.mp3_bitrate_kbps);
    lame_set_VBR(lame_global_flags_, m_settings->mixer_tuning.mp3_vbr_enabled ? vbr_default : vbr_off);

    int ret = lame_init_params(lame_global_flags_);
    if (ret < 0) {
        LOG_CPP_ERROR("[SinkMixer:%s] lame_init_params() failed with code: %d", config_.sink_id.c_str(), ret);
        lame_close(lame_global_flags_);
        lame_global_flags_ = nullptr;
        return;
    }
    LOG_CPP_INFO("[SinkMixer:%s] LAME initialized successfully.", config_.sink_id.c_str());
}

/**
 * @brief Adds an input queue from a source processor.
 * @param instance_id The unique ID of the source processor instance.
 * @param queue A shared pointer to the source's output queue.
 */
void SinkAudioMixer::add_input_queue(const std::string& instance_id, std::shared_ptr<InputChunkQueue> queue) {
    if (!queue) {
        LOG_CPP_ERROR("[SinkMixer:%s] Attempted to add null input queue for instance: %s", config_.sink_id.c_str(), instance_id.c_str());
        return;
    }
    {
        std::lock_guard<std::mutex> lock(queues_mutex_);
        input_queues_[instance_id] = queue;
        input_active_state_[instance_id] = false;
        source_buffers_[instance_id].audio_data.assign(mixing_buffer_samples_, 0);
        LOG_CPP_INFO("[SinkMixer:%s] Added input queue for source instance: %s", config_.sink_id.c_str(), instance_id.c_str());
    }

    if (mix_scheduler_) {
        mix_scheduler_->attach_source(instance_id, std::move(queue));
    }
}

/**
 * @brief Removes an input queue associated with a source processor.
 * @param instance_id The unique ID of the source processor instance.
 */
void SinkAudioMixer::remove_input_queue(const std::string& instance_id) {
    {
        std::lock_guard<std::mutex> lock(queues_mutex_);
        input_queues_.erase(instance_id);
        input_active_state_.erase(instance_id);
        source_buffers_.erase(instance_id);
        LOG_CPP_INFO("[SinkMixer:%s] Removed input queue for source instance: %s", config_.sink_id.c_str(), instance_id.c_str());
    }

    if (mix_scheduler_) {
        mix_scheduler_->detach_source(instance_id);
    }
}

/**
 * @brief Adds a network listener (e.g., a WebRTC peer) to this sink.
 * @param listener_id A unique ID for the listener.
 * @param sender A unique pointer to the listener's network sender implementation.
 */
void SinkAudioMixer::add_listener(const std::string& listener_id, std::unique_ptr<INetworkSender> sender) {
    if (!sender) {
        LOG_CPP_ERROR("[SinkMixer:%s] Attempted to add null listener sender for ID: %s", config_.sink_id.c_str(), listener_id.c_str());
        return;
    }
    
    if (WebRtcSender* webrtc_sender = dynamic_cast<WebRtcSender*>(sender.get())) {
        webrtc_sender->set_cleanup_callback(listener_id, [this](const std::string& id) {
            LOG_CPP_INFO("[SinkMixer:%s] Cleanup callback triggered for listener: %s", config_.sink_id.c_str(), id.c_str());
        });
    }
    
    // IMPORTANT: Do NOT call setup() here for WebRTC senders!
    // WebRtcSender::setup() triggers callbacks that need the Python GIL.
    // If we call it here while Python is still in the middle of add_webrtc_listener,
    // it will deadlock. Instead, we'll call setup() separately after Python has
    // released the GIL.
    bool needs_deferred_setup = (dynamic_cast<WebRtcSender*>(sender.get()) != nullptr);
    
    if (!needs_deferred_setup) {
        // For non-WebRTC senders, setup immediately as before
        if (!sender->setup()) {
            LOG_CPP_ERROR("[SinkMixer:%s] Failed to setup listener sender for ID: %s", config_.sink_id.c_str(), listener_id.c_str());
            return;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(listener_senders_mutex_);
        listener_senders_[listener_id] = std::move(sender);
        LOG_CPP_INFO("[SinkMixer:%s] Added listener sender with ID: %s (setup %s)",
                     config_.sink_id.c_str(), listener_id.c_str(),
                     needs_deferred_setup ? "deferred" : "completed");
    }
}

/**
 * @brief Removes a network listener from this sink.
 * @param listener_id The ID of the listener to remove.
 */
void SinkAudioMixer::remove_listener(const std::string& listener_id) {
    std::unique_ptr<INetworkSender> sender_to_remove;
    {
        std::lock_guard<std::mutex> lock(listener_senders_mutex_);
        auto it = listener_senders_.find(listener_id);
        if (it != listener_senders_.end()) {
            sender_to_remove = std::move(it->second);
            listener_senders_.erase(it);
            LOG_CPP_INFO("[SinkMixer:%s] Removed listener sender with ID: %s", config_.sink_id.c_str(), listener_id.c_str());
        } else {
            LOG_CPP_DEBUG("[SinkMixer:%s] Listener sender with ID already removed: %s", config_.sink_id.c_str(), listener_id.c_str());
            return;
        }
    } // Release listener_senders_mutex_ before calling close()

    // Close the sender WITHOUT holding the mutex to prevent deadlock
    // close() can trigger libdatachannel callbacks that need the GIL
    if (sender_to_remove) {
        if (WebRtcSender* webrtc_sender = dynamic_cast<WebRtcSender*>(sender_to_remove.get())) {
            LOG_CPP_INFO("[SinkMixer:%s] Force closing WebRTC connection for listener: %s", config_.sink_id.c_str(), listener_id.c_str());
        }
        sender_to_remove->close();
    }
}

/**
 * @brief Gets a raw pointer to a listener's network sender.
 * @param listener_id The ID of the listener.
 * @return A pointer to the `INetworkSender`, or `nullptr` if not found.
 */
INetworkSender* SinkAudioMixer::get_listener(const std::string& listener_id) {
    std::lock_guard<std::mutex> lock(listener_senders_mutex_);
    auto it = listener_senders_.find(listener_id);
    if (it != listener_senders_.end()) {
        return it->second.get();
    }
    return nullptr;
}

SinkAudioMixerStats SinkAudioMixer::get_stats() {
    SinkAudioMixerStats stats;
    stats.total_chunks_mixed = m_total_chunks_mixed.load();
    stats.buffer_underruns = m_buffer_underruns.load();
    stats.buffer_overflows = m_buffer_overflows.load();
    stats.mp3_buffer_overflows = m_mp3_buffer_overflows.load();

    {
        std::lock_guard<std::mutex> lock(queues_mutex_);
        stats.total_input_streams = input_queues_.size();
        size_t active_count = 0;
        for (const auto& [id, is_active] : input_active_state_) {
            if (is_active) {
                active_count++;
            }
        }
        stats.active_input_streams = active_count;
    }

    {
        std::lock_guard<std::mutex> lock(listener_senders_mutex_);
        for (const auto& [id, sender] : listener_senders_) {
            stats.listener_ids.push_back(id);
        }
    }

    return stats;
}

const SinkMixerConfig& SinkAudioMixer::get_config() const {
    return config_;
}

/**
 * @brief Enables or disables coordination mode for synchronized multi-sink playback.
 * @param enable True to enable coordination, false to disable.
 */
void SinkAudioMixer::set_coordination_mode(bool enable) {
    coordination_mode_ = enable;
    LOG_CPP_INFO("[SinkMixer:%s] Coordination mode %s",
                 config_.sink_id.c_str(),
                 enable ? "ENABLED" : "DISABLED");
}

/**
 * @brief Sets the synchronization coordinator for this mixer.
 * @param coord Pointer to the coordinator (not owned by mixer, must outlive mixer).
 */
void SinkAudioMixer::set_coordinator(SinkSynchronizationCoordinator* coord) {
    if (!coord) {
        LOG_CPP_WARNING("[SinkMixer:%s] Attempted to set null coordinator",
                        config_.sink_id.c_str());
        return;
    }
    coordinator_ = coord;
    LOG_CPP_INFO("[SinkMixer:%s] Coordinator set", config_.sink_id.c_str());
}

/**
 * @brief Checks if coordination mode is currently enabled.
 * @return True if coordination is enabled, false otherwise.
 */
bool SinkAudioMixer::is_coordination_enabled() const {
    return coordination_mode_;
}

/**
 * @brief Starts the mixer's processing thread.
 */
void SinkAudioMixer::start() {
    if (is_running()) {
        LOG_CPP_INFO("[SinkMixer:%s] Already running.", config_.sink_id.c_str());
        return;
    }
    if (startup_in_progress_.load(std::memory_order_acquire)) {
        LOG_CPP_INFO("[SinkMixer:%s] Startup already in progress.", config_.sink_id.c_str());
        return;
    }

    if (startup_thread_.joinable()) {
        try {
            startup_thread_.join();
        } catch (const std::system_error& e) {
            LOG_CPP_ERROR("[SinkMixer:%s] Error joining previous startup thread: %s", config_.sink_id.c_str(), e.what());
        }
    }

    startup_in_progress_.store(true, std::memory_order_release);
    try {
        startup_thread_ = std::thread(&SinkAudioMixer::start_async, this);
    } catch (const std::system_error& e) {
        startup_in_progress_.store(false, std::memory_order_release);
        LOG_CPP_ERROR("[SinkMixer:%s] Failed to launch startup thread: %s", config_.sink_id.c_str(), e.what());
        throw;
    }
}

/**
 * @brief Stops the mixer's processing thread and cleans up resources.
 */
void SinkAudioMixer::stop() {
    LOG_CPP_INFO("[SinkMixer:%s] stop(): enter", config_.sink_id.c_str());
    join_startup_thread();

    if (stop_flag_) {
        LOG_CPP_INFO("[SinkMixer:%s] Already stopped or stopping.", config_.sink_id.c_str());
        return;
    }

    size_t inputs = 0;
    size_t listeners = 0;
    {
        std::lock_guard<std::mutex> lock(queues_mutex_);
        inputs = input_queues_.size();
    }
    {
        std::lock_guard<std::mutex> lock(listener_senders_mutex_);
        listeners = listener_senders_.size();
    }
    LOG_CPP_INFO("[SinkMixer:%s] Stopping... input_queues=%zu listeners=%zu startup_in_progress=%d component_joinable=%d payload_bytes=%zu clock_enabled=%d", config_.sink_id.c_str(), inputs, listeners, startup_in_progress_.load() ? 1 : 0, component_thread_.joinable() ? 1 : 0, payload_buffer_write_pos_, clock_manager_enabled_.load() ? 1 : 0);
    stop_flag_ = true;

    if (mix_scheduler_) {
        mix_scheduler_->shutdown();
    }

    if (clock_condition_handle_.condition) {
        clock_condition_handle_.condition->cv.notify_all();
    }

    unregister_mix_timer();
    LOG_CPP_INFO("[SinkMixer:%s] Mix timer unregistered", config_.sink_id.c_str());

    if (mp3_output_queue_ && lame_global_flags_) {
        LOG_CPP_INFO("[SinkMixer:%s] Flushing LAME buffer...", config_.sink_id.c_str());
        int flush_bytes = lame_encode_flush(lame_global_flags_, mp3_encode_buffer_.data(), mp3_encode_buffer_.size());
        if (flush_bytes > 0) {
            EncodedMP3Data mp3_data;
            mp3_data.mp3_data.assign(mp3_encode_buffer_.begin(), mp3_encode_buffer_.begin() + flush_bytes);
            mp3_output_queue_->push(std::move(mp3_data));
        }
    }

    if (component_thread_.joinable()) {
        try {
            component_thread_.join();
            LOG_CPP_INFO("[SinkMixer:%s] Thread joined.", config_.sink_id.c_str());
        } catch (const std::system_error& e) {
            LOG_CPP_ERROR("[SinkMixer:%s] Error joining thread: %s", config_.sink_id.c_str(), e.what());
        }
    }

    if (network_sender_) {
        LOG_CPP_INFO("[SinkMixer:%s] Closing primary network sender...", config_.sink_id.c_str());
        network_sender_->close();
    }

    {
        std::lock_guard<std::mutex> lock(listener_senders_mutex_);
        for (auto& pair : listener_senders_) {
            if (pair.second) {
                LOG_CPP_INFO("[SinkMixer:%s] Closing listener sender id=%s", config_.sink_id.c_str(), pair.first.c_str());
                pair.second->close();
            }
        }
        listener_senders_.clear();
        LOG_CPP_INFO("[SinkMixer:%s] All listener senders closed and cleared.", config_.sink_id.c_str());
    }
    LOG_CPP_INFO("[SinkMixer:%s] stop(): exit", config_.sink_id.c_str());
}

void SinkAudioMixer::start_async() {
    bool started = false;
    try {
        started = start_internal();
    } catch (const std::exception& ex) {
        LOG_CPP_ERROR("[SinkMixer:%s] Exception during startup: %s", config_.sink_id.c_str(), ex.what());
    } catch (...) {
        LOG_CPP_ERROR("[SinkMixer:%s] Unknown exception during startup.", config_.sink_id.c_str());
    }

    if (!started) {
        stop_flag_ = true;
    }

    startup_in_progress_.store(false, std::memory_order_release);
}

bool SinkAudioMixer::start_internal() {
    LOG_CPP_INFO("[SinkMixer:%s] Starting...", config_.sink_id.c_str());
    const auto t0 = std::chrono::steady_clock::now();

    stop_flag_ = false;
    payload_buffer_write_pos_ = 0;
    reset_profiler_counters();
    set_playback_format(config_.output_samplerate, config_.output_channels, config_.output_bitdepth);

    clear_pending_audio();

    const auto t_setup0 = std::chrono::steady_clock::now();
    if (network_sender_ && !network_sender_->setup()) {
        LOG_CPP_ERROR("[SinkMixer:%s] Network sender setup failed.", config_.sink_id.c_str());
        if (config_.protocol == "system_audio") {
            LOG_CPP_WARNING("[SinkMixer:%s] System audio playback sender setup failed; continuing anyway.",
                            config_.sink_id.c_str());
        } else {
            if (network_sender_) {
                network_sender_->close();
            }
            return false;
        }
    }
    const auto t_setup1 = std::chrono::steady_clock::now();
    if (network_sender_) {
        LOG_CPP_INFO("[SinkMixer:%s] Network sender setup in %lld ms", config_.sink_id.c_str(),
                     (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_setup1 - t_setup0).count());
    }

    update_playback_format_from_sender();

    register_mix_timer();

    try {
        const auto t_thr0 = std::chrono::steady_clock::now();
        component_thread_ = std::thread(&SinkAudioMixer::run, this);
        const auto t_thr1 = std::chrono::steady_clock::now();
        LOG_CPP_INFO("[SinkMixer:%s] Thread started in %lld ms (total so far %lld ms).",
                     config_.sink_id.c_str(),
                     (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_thr1 - t_thr0).count(),
                     (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_thr1 - t0).count());
    } catch (const std::system_error& e) {
        LOG_CPP_ERROR("[SinkMixer:%s] Failed to start thread: %s", config_.sink_id.c_str(), e.what());
        unregister_mix_timer();
        if (network_sender_) {
            network_sender_->close();
        }
        return false;
    }

    return true;
}

void SinkAudioMixer::join_startup_thread() {
    if (!startup_thread_.joinable()) {
        return;
    }

    try {
        startup_thread_.join();
    } catch (const std::system_error& e) {
        LOG_CPP_ERROR("[SinkMixer:%s] Error joining startup thread: %s", config_.sink_id.c_str(), e.what());
    }

    startup_in_progress_.store(false, std::memory_order_release);
}

/**
 * @brief Waits for data from input queues and determines which sources are active.
 * @param ignored_timeout A timeout value (currently ignored in implementation).
 * @return `true` if any data was popped from any queue, `false` otherwise.
 */
bool SinkAudioMixer::wait_for_source_data() {
    MixScheduler::HarvestResult harvest;
    if (mix_scheduler_) {
        harvest = mix_scheduler_->collect_ready_chunks();
    }

    std::lock_guard<std::mutex> lock(queues_mutex_);

    bool data_actually_popped_this_cycle = false;
    std::map<std::string, bool> ready_this_cycle;
    size_t lagging_sources = 0;

    bool had_any_active_sources = false;
    for (const auto& [instance_id, is_active] : input_active_state_) {
        (void)instance_id;
        if (is_active) {
            had_any_active_sources = true;
            break;
        }
    }

    bool was_holding_silence = underrun_silence_active_;
    std::unordered_set<std::string> drained_ids(harvest.drained_sources.begin(), harvest.drained_sources.end());

    for (const auto& drained_id : drained_ids) {
        auto active_it = input_active_state_.find(drained_id);
        if (active_it != input_active_state_.end()) {
            active_it->second = false;
        }
        auto buffer_it = source_buffers_.find(drained_id);
        if (buffer_it != source_buffers_.end()) {
            buffer_it->second = ProcessedAudioChunk{};
        }
    }

    for (auto& pair : harvest.ready_chunks) {
        const std::string& instance_id = pair.first;
        auto& ready_entry = pair.second;

        bool previously_active = input_active_state_.count(instance_id) ? input_active_state_[instance_id] : false;
        ProcessedAudioChunk chunk = std::move(ready_entry.chunk);
        const size_t sample_count = chunk.audio_data.size();

        if (sample_count != mixing_buffer_samples_) {
            LOG_CPP_ERROR("[SinkMixer:%s] WaitForData: Received chunk from instance %s with unexpected sample count: %zu. Discarding.",
                          config_.sink_id.c_str(), instance_id.c_str(), sample_count);
            ready_this_cycle[instance_id] = false;
            input_active_state_[instance_id] = false;
            continue;
        }

        if (chunk.produced_time.time_since_epoch().count() != 0) {
            double dwell_ms = std::chrono::duration<double, std::milli>(ready_entry.arrival_time - chunk.produced_time).count();
            profiling_last_chunk_dwell_ms_ = dwell_ms;
            profiling_chunk_dwell_sum_ms_ += dwell_ms;
            profiling_chunk_dwell_samples_++;
            if (profiling_chunk_dwell_samples_ == 1) {
                profiling_chunk_dwell_min_ms_ = dwell_ms;
                profiling_chunk_dwell_max_ms_ = dwell_ms;
            } else {
                profiling_chunk_dwell_min_ms_ = std::min(profiling_chunk_dwell_min_ms_, dwell_ms);
                profiling_chunk_dwell_max_ms_ = std::max(profiling_chunk_dwell_max_ms_, dwell_ms);
            }
        }

        m_total_chunks_mixed++;
        source_buffers_[instance_id] = std::move(chunk);
        ready_this_cycle[instance_id] = true;
        data_actually_popped_this_cycle = true;
        if (!previously_active) {
            LOG_CPP_DEBUG("[SinkMixer:%s] Input instance %s became active", config_.sink_id.c_str(), instance_id.c_str());
        }
        input_active_state_[instance_id] = true;
        drained_ids.erase(instance_id);
    }

    for (const auto& [instance_id, queue_ptr] : input_queues_) {
        (void)queue_ptr;
        if (!ready_this_cycle.count(instance_id)) {
            ready_this_cycle[instance_id] = false;
        }

        bool drained = drained_ids.count(instance_id) > 0;
        bool currently_ready = ready_this_cycle[instance_id];
        bool previously_active = input_active_state_.count(instance_id) ? input_active_state_[instance_id] : false;

        if (!currently_ready && previously_active && !drained) {
            LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Instance %s did not provide a chunk this cycle, marking inactive.",
                          config_.sink_id.c_str(), instance_id.c_str());
            input_active_state_[instance_id] = false;
            m_buffer_underruns++;
            lagging_sources++;
            profiling_source_underruns_[instance_id]++;
        }
    }

    bool has_active_sources_now = false;
    for (const auto& [instance_id, is_active] : input_active_state_) {
        (void)instance_id;
        if (is_active) {
            has_active_sources_now = true;
            break;
        }
    }

    auto now = std::chrono::steady_clock::now();
    bool hold_window_expired = false;

    if (data_actually_popped_this_cycle) {
        underrun_silence_active_ = false;
    } else {
        long hold_ms = std::max<long>(0, m_settings->mixer_tuning.underrun_hold_timeout_ms);

        if (underrun_silence_active_) {
            if (hold_ms <= 0 || now >= underrun_silence_deadline_) {
                hold_window_expired = true;
                underrun_silence_active_ = false;
            }
        }

        if (!underrun_silence_active_ && hold_ms > 0 && !input_queues_.empty() && had_any_active_sources &&
            !has_active_sources_now) {
            underrun_silence_active_ = true;
            underrun_silence_deadline_ = now + std::chrono::milliseconds(hold_ms);
        }
    }

    if (!was_holding_silence && underrun_silence_active_) {
        long hold_ms = std::max<long>(0, m_settings->mixer_tuning.underrun_hold_timeout_ms);
        LOG_CPP_INFO("[SinkMixer:%s] Underrun detected. Injecting silence for up to %ld ms.",
                     config_.sink_id.c_str(), hold_ms);
        profiling_underrun_events_++;
        profiling_underrun_active_since_ = now;
    } else if (was_holding_silence && !underrun_silence_active_) {
        if (profiling_underrun_active_since_.time_since_epoch().count() != 0) {
            double hold_duration_ms = std::chrono::duration<double, std::milli>(now - profiling_underrun_active_since_).count();
            profiling_underrun_hold_time_ms_ += hold_duration_ms;
            profiling_last_underrun_hold_ms_ = hold_duration_ms;
            profiling_underrun_active_since_ = {};
        }
        if (data_actually_popped_this_cycle) {
            LOG_CPP_INFO("[SinkMixer:%s] Underrun cleared. Audio resumed before silence window elapsed.",
                         config_.sink_id.c_str());
        } else if (hold_window_expired) {
            LOG_CPP_INFO("[SinkMixer:%s] Underrun silence window expired without new audio.",
                         config_.sink_id.c_str());
        } else {
            LOG_CPP_INFO("[SinkMixer:%s] Underrun silence cleared.", config_.sink_id.c_str());
        }
    }

    size_t ready_count = 0;
    for (const auto& [instance_id, is_ready] : ready_this_cycle) {
        (void)instance_id;
        if (is_ready) {
            ready_count++;
        }
    }

    profiling_ready_sources_sum_ += ready_count;
    profiling_lagging_sources_sum_ += lagging_sources;
    profiling_samples_count_++;
    if (data_actually_popped_this_cycle) {
        profiling_data_ready_cycles_++;
    }

    return data_actually_popped_this_cycle;
}

/**
 * @brief Mixes audio from all active source buffers into the main mixing buffer.
 */
void SinkAudioMixer::mix_buffers() {
    auto t0 = std::chrono::steady_clock::now();
    std::fill(mixing_buffer_.begin(), mixing_buffer_.end(), 0);
    
    std::vector<uint32_t> collected_csrcs;
    size_t active_source_count = 0;

    size_t total_samples_to_mix = mixing_buffer_.size();
    LOG_CPP_DEBUG("[SinkMixer:%s] MixBuffers: Starting mix. Target samples=%zu (Mixing buffer size).", config_.sink_id.c_str(), total_samples_to_mix);

    for (auto const& [instance_id, is_active] : input_active_state_) {
        if (is_active) {
            active_source_count++;
            auto buf_it = source_buffers_.find(instance_id);
            if (buf_it == source_buffers_.end()) {
                 LOG_CPP_ERROR("[SinkMixer:%s] Mixing error: Source buffer not found for active instance %s", config_.sink_id.c_str(), instance_id.c_str());
                 continue;
            }
            const auto& source_data = buf_it->second.audio_data;
            const auto& ssrcs = buf_it->second.ssrcs;
            collected_csrcs.insert(collected_csrcs.end(), ssrcs.begin(), ssrcs.end());
 
             size_t samples_in_source = source_data.size();
             LOG_CPP_DEBUG("[SinkMixer:%s] MixBuffers: Mixing instance %s. Source samples=%zu. Expected=%zu.", config_.sink_id.c_str(), instance_id.c_str(), samples_in_source, total_samples_to_mix);

            if (samples_in_source != total_samples_to_mix) {
                 LOG_CPP_ERROR("[SinkMixer:%s] MixBuffers: Source buffer for instance %s size mismatch! Expected %zu, got %zu. Skipping source.", config_.sink_id.c_str(), instance_id.c_str(), total_samples_to_mix, samples_in_source);
                 continue;
            }

            LOG_CPP_DEBUG("[SinkMixer:%s] MixBuffers: Accumulating %zu samples from instance %s", config_.sink_id.c_str(), total_samples_to_mix, instance_id.c_str());

            for (size_t i = 0; i < total_samples_to_mix; ++i) {
                int64_t sum = static_cast<int64_t>(mixing_buffer_[i]) + source_data[i];
                if (sum > INT32_MAX) {
                    mixing_buffer_[i] = INT32_MAX;
                } else if (sum < INT32_MIN) {
                    mixing_buffer_[i] = INT32_MIN;
                } else {
                    mixing_buffer_[i] = static_cast<int32_t>(sum);
                }
            }
        }
    }
    
    std::sort(collected_csrcs.begin(), collected_csrcs.end());
    collected_csrcs.erase(std::unique(collected_csrcs.begin(), collected_csrcs.end()), collected_csrcs.end());
    
    {
        std::lock_guard<std::mutex> lock(csrc_mutex_);
        current_csrcs_ = collected_csrcs;
    }

    LOG_CPP_DEBUG("[SinkMixer:%s] MixBuffers: Mix complete. Mixed %zu active sources into mixing_buffer_ (%zu samples).", config_.sink_id.c_str(), active_source_count, total_samples_to_mix);
    auto t1 = std::chrono::steady_clock::now();
    uint64_t dt = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    profiling_mix_calls_++;
    profiling_mix_ns_sum_ += static_cast<long double>(dt);
    if (dt > profiling_mix_ns_max_) profiling_mix_ns_max_ = dt;
    if (dt < profiling_mix_ns_min_) profiling_mix_ns_min_ = dt;
}

/**
 * @brief Downscales the 32-bit mixed audio to the target bit depth for network output.
 */
void SinkAudioMixer::downscale_buffer() {
    auto t0 = std::chrono::steady_clock::now();
    int target_bit_depth = playback_bit_depth_ > 0 ? playback_bit_depth_ : config_.output_bitdepth;
    if (target_bit_depth <= 0) {
        target_bit_depth = 16;
    }
    size_t output_byte_depth = static_cast<size_t>(target_bit_depth) / 8;
    if (output_byte_depth == 0) {
        LOG_CPP_ERROR("[SinkMixer:%s] Invalid target bit depth %d during downscale.",
                      config_.sink_id.c_str(), target_bit_depth);
        return;
    }
    size_t samples_to_convert = mixing_buffer_.size();

    size_t expected_bytes_to_write = samples_to_convert * output_byte_depth;
    LOG_CPP_DEBUG("[SinkMixer:%s] Downscale: Converting %zu samples (int32) to %d-bit. Expected output bytes=%zu.",
                  config_.sink_id.c_str(), samples_to_convert, target_bit_depth, expected_bytes_to_write);

    size_t available_space = payload_buffer_.size() - payload_buffer_write_pos_;

    if (expected_bytes_to_write > available_space) {
        LOG_CPP_ERROR("[SinkMixer:%s] Downscale buffer overflow detected! Available space=%zu, needed=%zu. WritePos=%zu. BufferSize=%zu",
                      config_.sink_id.c_str(), available_space, expected_bytes_to_write, payload_buffer_write_pos_, payload_buffer_.size());
        m_buffer_overflows++;
        size_t max_samples_possible = available_space / output_byte_depth;
        samples_to_convert = max_samples_possible;
        expected_bytes_to_write = samples_to_convert * output_byte_depth;
        LOG_CPP_ERROR("[SinkMixer:%s] Downscale: Limiting conversion to %zu samples (%zu bytes) due to space limit.",
                      config_.sink_id.c_str(), samples_to_convert, expected_bytes_to_write);
        if (samples_to_convert == 0) {
             LOG_CPP_ERROR("[SinkMixer:%s] Downscale buffer has no space left. available=%zu", config_.sink_id.c_str(), available_space);
             return;
        }
    }

    uint8_t* write_ptr_start = payload_buffer_.data() + payload_buffer_write_pos_;
    uint8_t* write_ptr = write_ptr_start;
    const int32_t* read_ptr = mixing_buffer_.data();

    for (size_t i = 0; i < samples_to_convert; ++i) {
        int32_t sample = read_ptr[i];
        switch (target_bit_depth) {
            case 16:
                *write_ptr++ = static_cast<uint8_t>((sample >> 16) & 0xFF);
                *write_ptr++ = static_cast<uint8_t>((sample >> 24) & 0xFF);
                break;
            case 24:
                *write_ptr++ = static_cast<uint8_t>((sample >> 8) & 0xFF);
                *write_ptr++ = static_cast<uint8_t>((sample >> 16) & 0xFF);
                *write_ptr++ = static_cast<uint8_t>((sample >> 24) & 0xFF);
                break;
            case 32:
                *write_ptr++ = static_cast<uint8_t>((sample) & 0xFF);
                *write_ptr++ = static_cast<uint8_t>((sample >> 8) & 0xFF);
                *write_ptr++ = static_cast<uint8_t>((sample >> 16) & 0xFF);
                *write_ptr++ = static_cast<uint8_t>((sample >> 24) & 0xFF);
                break;
            default:
                LOG_CPP_ERROR("[SinkMixer:%s] Unsupported target bit depth %d during downscale.",
                              config_.sink_id.c_str(), target_bit_depth);
                return;
        }
    }
    size_t bytes_written = write_ptr - write_ptr_start;
    LOG_CPP_DEBUG("[SinkMixer:%s] Downscale: Conversion loop finished. Bytes written=%zu. Expected=%zu.",
                  config_.sink_id.c_str(), bytes_written, expected_bytes_to_write);
    if (bytes_written != expected_bytes_to_write) {
         LOG_CPP_ERROR("[SinkMixer:%s] Downscale: Mismatch between bytes written (%zu) and expected bytes (%zu).",
                       config_.sink_id.c_str(), bytes_written, expected_bytes_to_write);
    }

    payload_buffer_write_pos_ += bytes_written;
    profiling_max_payload_buffer_bytes_ = std::max(profiling_max_payload_buffer_bytes_, payload_buffer_write_pos_);
    LOG_CPP_DEBUG("[SinkMixer:%s] Downscale complete. payload_buffer_write_pos_=%zu", config_.sink_id.c_str(), payload_buffer_write_pos_);
    auto t1 = std::chrono::steady_clock::now();
    uint64_t dt = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    profiling_downscale_calls_++;
    profiling_downscale_ns_sum_ += static_cast<long double>(dt);
    if (dt > profiling_downscale_ns_max_) profiling_downscale_ns_max_ = dt;
    if (dt < profiling_downscale_ns_min_) profiling_downscale_ns_min_ = dt;
}

/**
 * @brief Pre-processes the mixed audio into a stereo format for listeners and MP3 encoding.
 * @return The number of stereo samples processed.
 */
size_t SinkAudioMixer::preprocess_for_listeners_and_mp3() {
    auto t0 = std::chrono::steady_clock::now();
    if (!stereo_preprocessor_) {
        return 0;
    }

    const size_t total_bytes_in_mixing_buffer = mixing_buffer_.size() * sizeof(int32_t);
    const size_t input_chunk_bytes = chunk_size_bytes_;
    size_t processed_samples_count = 0;

    for (size_t offset = 0; offset + input_chunk_bytes <= total_bytes_in_mixing_buffer; offset += input_chunk_bytes) {
        const uint8_t* input_chunk_ptr = reinterpret_cast<const uint8_t*>(mixing_buffer_.data()) + offset;
        
        if (processed_samples_count > stereo_buffer_.size()) {
            LOG_CPP_ERROR("[SinkMixer:%s] Preprocessing error: processed_samples_count exceeds stereo_buffer size", config_.sink_id.c_str());
            break;
        }
        int32_t* output_chunk_ptr = stereo_buffer_.data() + processed_samples_count;

        int processed_stereo_samples = stereo_preprocessor_->processAudio(input_chunk_ptr, output_chunk_ptr);
        if (processed_stereo_samples > 0) {
            processed_samples_count += processed_stereo_samples;
        } else {
            LOG_CPP_ERROR("[SinkMixer:%s] Stereo preprocessor failed at offset %zu", config_.sink_id.c_str(), offset);
            break;
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    uint64_t dt = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    profiling_preprocess_calls_++;
    profiling_preprocess_ns_sum_ += static_cast<long double>(dt);
    if (dt > profiling_preprocess_ns_max_) profiling_preprocess_ns_max_ = dt;
    if (dt < profiling_preprocess_ns_min_) profiling_preprocess_ns_min_ = dt;
    return processed_samples_count;
}

/**
 * @brief Dispatches the pre-processed stereo audio to all registered listeners.
 * @param samples_to_dispatch The number of stereo samples to send.
 */
void SinkAudioMixer::dispatch_to_listeners(size_t samples_to_dispatch) {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::string> closed_listeners_to_remove;

    {
        std::lock_guard<std::mutex> lock(listener_senders_mutex_);
        if (listener_senders_.empty() || samples_to_dispatch == 0) {
            return;
        }

        const uint8_t* payload_data = reinterpret_cast<const uint8_t*>(stereo_buffer_.data());
        size_t payload_size = samples_to_dispatch * sizeof(int32_t);

        if (payload_size > stereo_buffer_.size() * sizeof(int32_t)) {
            LOG_CPP_ERROR("[SinkMixer:%s] Dispatch error: payload_size > stereo_buffer_ size", config_.sink_id.c_str());
            return;
        }

        std::vector<uint32_t> empty_csrcs;

        for (auto const& [id, sender] : listener_senders_) {
            if (sender) {
                if (WebRtcSender* webrtc_sender = dynamic_cast<WebRtcSender*>(sender.get())) {
                    if (webrtc_sender->is_closed()) {
                        closed_listeners_to_remove.push_back(id);
                        LOG_CPP_INFO("[SinkMixer:%s] Found closed listener during dispatch: %s", config_.sink_id.c_str(), id.c_str());
                        // Don't call close() here while holding the lock!
                        // It will be called in remove_listener() without the lock
                        continue;
                    }
                }
                sender->send_payload(payload_data, payload_size, empty_csrcs);
            }
        }
    } // Release listener_senders_mutex_ before calling remove_listener

    for (const auto& listener_id : closed_listeners_to_remove) {
        remove_listener(listener_id);  // This will close() the sender without holding the lock
        LOG_CPP_INFO("[SinkMixer:%s] Immediately removed closed listener: %s", config_.sink_id.c_str(), listener_id.c_str());
    }
    auto t1 = std::chrono::steady_clock::now();
    uint64_t dt = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    profiling_dispatch_calls_++;
    profiling_dispatch_ns_sum_ += static_cast<long double>(dt);
    if (dt > profiling_dispatch_ns_max_) profiling_dispatch_ns_max_ = dt;
    if (dt < profiling_dispatch_ns_min_) profiling_dispatch_ns_min_ = dt;
}

/**
 * @brief Encodes the pre-processed stereo audio to MP3 and pushes it to the output queue.
 * @param samples_to_encode The number of stereo samples to encode.
 */
void SinkAudioMixer::encode_and_push_mp3(size_t samples_to_encode) {
    auto t0 = std::chrono::steady_clock::now();
    if (!mp3_output_queue_ || !lame_global_flags_ || samples_to_encode == 0) {
        return;
    }

    if (mp3_output_queue_->size() > static_cast<size_t>(m_settings->mixer_tuning.mp3_output_queue_max_size)) {
        LOG_CPP_DEBUG("[SinkMixer:%s] MP3 output queue full, skipping encoding for this cycle.", config_.sink_id.c_str());
        m_mp3_buffer_overflows++;
        return;
    }

    int frames_per_channel = samples_to_encode / 2;
    if (frames_per_channel <= 0) {
        return;
    }

    int mp3_bytes_encoded = lame_encode_buffer_interleaved_int(
        lame_global_flags_,
        stereo_buffer_.data(),
        frames_per_channel,
        mp3_encode_buffer_.data(),
        static_cast<int>(mp3_encode_buffer_.size())
    );

    if (mp3_bytes_encoded < 0) {
        LOG_CPP_ERROR("[SinkMixer:%s] LAME encoding failed with code: %d", config_.sink_id.c_str(), mp3_bytes_encoded);
    } else if (mp3_bytes_encoded > 0) {
        EncodedMP3Data mp3_data;
        mp3_data.mp3_data.assign(mp3_encode_buffer_.begin(), mp3_encode_buffer_.begin() + mp3_bytes_encoded);
        mp3_output_queue_->push(std::move(mp3_data));
    }
    auto t1 = std::chrono::steady_clock::now();
    uint64_t dt = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    profiling_mp3_calls_++;
    profiling_mp3_ns_sum_ += static_cast<long double>(dt);
    if (dt > profiling_mp3_ns_max_) profiling_mp3_ns_max_ = dt;
    if (dt < profiling_mp3_ns_min_) profiling_mp3_ns_min_ = dt;
}

/**
 * @brief Periodically checks for and removes closed or timed-out WebRTC listeners.
 */
void SinkAudioMixer::reset_profiler_counters() {
    profiling_last_log_time_ = std::chrono::steady_clock::now();
    profiling_cycles_ = 0;
    profiling_data_ready_cycles_ = 0;
    profiling_chunks_sent_ = 0;
    profiling_payload_bytes_sent_ = 0;
    profiling_ready_sources_sum_ = 0;
    profiling_lagging_sources_sum_ = 0;
    profiling_samples_count_ = 0;
    profiling_max_payload_buffer_bytes_ = payload_buffer_write_pos_;
    profiling_chunk_dwell_sum_ms_ = 0.0;
    profiling_chunk_dwell_max_ms_ = 0.0;
    profiling_chunk_dwell_min_ms_ = std::numeric_limits<double>::infinity();
    profiling_last_chunk_dwell_ms_ = 0.0;
    profiling_chunk_dwell_samples_ = 0;
    profiling_underrun_hold_time_ms_ = 0.0;
    profiling_last_underrun_hold_ms_ = 0.0;
    profiling_underrun_events_ = 0;
    if (underrun_silence_active_) {
        profiling_underrun_active_since_ = std::chrono::steady_clock::now();
    } else {
        profiling_underrun_active_since_ = {};
    }
    profiling_send_gap_sum_ms_ = 0.0;
    profiling_send_gap_max_ms_ = 0.0;
    profiling_send_gap_min_ms_ = std::numeric_limits<double>::infinity();
    profiling_last_send_gap_ms_ = 0.0;
    profiling_send_gap_samples_ = 0;
    profiling_last_chunk_send_time_ = std::chrono::steady_clock::now();
    profiling_mix_ns_sum_ = 0.0L;
    profiling_mix_calls_ = 0;
    profiling_mix_ns_max_ = 0;
    profiling_mix_ns_min_ = std::numeric_limits<uint64_t>::max();
    profiling_downscale_ns_sum_ = 0.0L;
    profiling_downscale_calls_ = 0;
    profiling_downscale_ns_max_ = 0;
    profiling_downscale_ns_min_ = std::numeric_limits<uint64_t>::max();
    profiling_preprocess_ns_sum_ = 0.0L;
    profiling_preprocess_calls_ = 0;
    profiling_preprocess_ns_max_ = 0;
    profiling_preprocess_ns_min_ = std::numeric_limits<uint64_t>::max();
    profiling_dispatch_ns_sum_ = 0.0L;
    profiling_dispatch_calls_ = 0;
    profiling_dispatch_ns_max_ = 0;
    profiling_dispatch_ns_min_ = std::numeric_limits<uint64_t>::max();
    profiling_mp3_ns_sum_ = 0.0L;
    profiling_mp3_calls_ = 0;
    profiling_mp3_ns_max_ = 0;
    profiling_mp3_ns_min_ = std::numeric_limits<uint64_t>::max();
    profiling_source_underruns_.clear();
}

void SinkAudioMixer::maybe_log_profiler() {
    if (!m_settings || !m_settings->profiler.enabled) {
        return;
    }

    long interval_ms = m_settings->profiler.log_interval_ms;
    if (interval_ms <= 0) {
        interval_ms = 1000;
    }

    auto now = std::chrono::steady_clock::now();
    auto interval = std::chrono::milliseconds(interval_ms);
    if (now - profiling_last_log_time_ < interval) {
        return;
    }

    size_t total_inputs = 0;
    size_t active_inputs = 0;
    size_t total_queue_depth = 0;
    size_t max_queue_depth = 0;
    {
        std::lock_guard<std::mutex> lock(queues_mutex_);
        total_inputs = input_queues_.size();
        for (const auto& [instance_id, is_active] : input_active_state_) {
            (void)instance_id;
            if (is_active) {
                active_inputs++;
            }
        }
        for (const auto& [instance_id, queue_ptr] : input_queues_) {
            (void)instance_id;
            size_t queue_size = queue_ptr ? queue_ptr->size() : 0;
            total_queue_depth += queue_size;
            if (queue_size > max_queue_depth) {
                max_queue_depth = queue_size;
            }
        }
    }

    double avg_queue_depth = total_inputs > 0 ? static_cast<double>(total_queue_depth) / static_cast<double>(total_inputs) : 0.0;
    double avg_ready_sources = profiling_samples_count_ > 0
                                   ? static_cast<double>(profiling_ready_sources_sum_) / static_cast<double>(profiling_samples_count_)
                                   : 0.0;
    double avg_lagging_sources = profiling_samples_count_ > 0
                                     ? static_cast<double>(profiling_lagging_sources_sum_) / static_cast<double>(profiling_samples_count_)
                                     : 0.0;
    double payload_kib = profiling_payload_bytes_sent_ / 1024.0;
    double avg_dwell_ms = profiling_chunk_dwell_samples_ > 0
                              ? profiling_chunk_dwell_sum_ms_ / static_cast<double>(profiling_chunk_dwell_samples_)
                              : 0.0;
    double min_dwell_ms = (profiling_chunk_dwell_samples_ > 0 && std::isfinite(profiling_chunk_dwell_min_ms_))
                               ? profiling_chunk_dwell_min_ms_
                               : 0.0;
    double max_dwell_ms = profiling_chunk_dwell_samples_ > 0 ? profiling_chunk_dwell_max_ms_ : 0.0;
    double avg_send_gap_ms = profiling_send_gap_samples_ > 0
                                 ? profiling_send_gap_sum_ms_ / static_cast<double>(profiling_send_gap_samples_)
                                 : 0.0;
    double min_send_gap_ms = (profiling_send_gap_samples_ > 0 && std::isfinite(profiling_send_gap_min_ms_))
                                  ? profiling_send_gap_min_ms_
                                  : 0.0;
    double max_send_gap_ms = profiling_send_gap_samples_ > 0 ? profiling_send_gap_max_ms_ : 0.0;
    double active_hold_ms = 0.0;
    if (underrun_silence_active_ && profiling_underrun_active_since_.time_since_epoch().count() != 0) {
        active_hold_ms = std::chrono::duration<double, std::milli>(now - profiling_underrun_active_since_).count();
    }
    double total_hold_ms = profiling_underrun_hold_time_ms_ + active_hold_ms;

    // Operation timing averages in ms
    auto avg_mix_ms = (profiling_mix_calls_ > 0 && profiling_mix_ns_sum_ > 0.0L) ? static_cast<double>(profiling_mix_ns_sum_ / 1'000'000.0L) / static_cast<double>(profiling_mix_calls_) : 0.0;
    auto avg_downscale_ms = (profiling_downscale_calls_ > 0 && profiling_downscale_ns_sum_ > 0.0L) ? static_cast<double>(profiling_downscale_ns_sum_ / 1'000'000.0L) / static_cast<double>(profiling_downscale_calls_) : 0.0;
    auto avg_preprocess_ms = (profiling_preprocess_calls_ > 0 && profiling_preprocess_ns_sum_ > 0.0L) ? static_cast<double>(profiling_preprocess_ns_sum_ / 1'000'000.0L) / static_cast<double>(profiling_preprocess_calls_) : 0.0;
    auto avg_dispatch_ms = (profiling_dispatch_calls_ > 0 && profiling_dispatch_ns_sum_ > 0.0L) ? static_cast<double>(profiling_dispatch_ns_sum_ / 1'000'000.0L) / static_cast<double>(profiling_dispatch_calls_) : 0.0;
    auto avg_mp3_ms = (profiling_mp3_calls_ > 0 && profiling_mp3_ns_sum_ > 0.0L) ? static_cast<double>(profiling_mp3_ns_sum_ / 1'000'000.0L) / static_cast<double>(profiling_mp3_calls_) : 0.0;

    LOG_CPP_INFO(
        "[Profiler][SinkMixer:%s] cycles=%llu data_cycles=%llu chunks_sent=%llu payload_kib=%.2f active_inputs=%zu/%zu avg_ready=%.2f avg_lagging=%.2f avg_queue=%.2f max_queue=%zu buffer_bytes(current/peak)=(%zu/%zu) underruns=%llu overflows=%llu mp3_overflows=%llu dwell_ms(last/avg/max/min/samples)=%.2f/%.2f/%.2f/%.2f/%llu send_gap_ms(last/avg/max/min/samples)=%.2f/%.2f/%.2f/%.2f/%llu underrun_hold_ms(total=%.2f active=%.2f last=%.2f events=%llu active=%s) timings_ms[mix(avg/max/min)=%.3f/%.3f/%.3f downscale(avg/max/min)=%.3f/%.3f/%.3f preprocess(avg/max/min)=%.3f/%.3f/%.3f dispatch(avg/max/min)=%.3f/%.3f/%.3f mp3(avg/max/min)=%.3f/%.3f/%.3f]",
        config_.sink_id.c_str(),
        static_cast<unsigned long long>(profiling_cycles_),
        static_cast<unsigned long long>(profiling_data_ready_cycles_),
        static_cast<unsigned long long>(profiling_chunks_sent_),
        payload_kib,
        active_inputs,
        total_inputs,
        avg_ready_sources,
        avg_lagging_sources,
        avg_queue_depth,
        max_queue_depth,
        payload_buffer_write_pos_,
        profiling_max_payload_buffer_bytes_,
        m_buffer_underruns.load(),
        m_buffer_overflows.load(),
        m_mp3_buffer_overflows.load(),
        profiling_last_chunk_dwell_ms_,
        avg_dwell_ms,
        max_dwell_ms,
        min_dwell_ms,
        static_cast<unsigned long long>(profiling_chunk_dwell_samples_),
        profiling_last_send_gap_ms_,
        avg_send_gap_ms,
        max_send_gap_ms,
        min_send_gap_ms,
        static_cast<unsigned long long>(profiling_send_gap_samples_),
        total_hold_ms,
        active_hold_ms,
        profiling_last_underrun_hold_ms_,
        static_cast<unsigned long long>(profiling_underrun_events_),
        underrun_silence_active_ ? "true" : "false",
        avg_mix_ms,
        static_cast<double>(profiling_mix_ns_max_) / 1'000'000.0,
        profiling_mix_ns_min_ == std::numeric_limits<uint64_t>::max() ? 0.0 : static_cast<double>(profiling_mix_ns_min_) / 1'000'000.0,
        avg_downscale_ms,
        static_cast<double>(profiling_downscale_ns_max_) / 1'000'000.0,
        profiling_downscale_ns_min_ == std::numeric_limits<uint64_t>::max() ? 0.0 : static_cast<double>(profiling_downscale_ns_min_) / 1'000'000.0,
        avg_preprocess_ms,
        static_cast<double>(profiling_preprocess_ns_max_) / 1'000'000.0,
        profiling_preprocess_ns_min_ == std::numeric_limits<uint64_t>::max() ? 0.0 : static_cast<double>(profiling_preprocess_ns_min_) / 1'000'000.0,
        avg_dispatch_ms,
        static_cast<double>(profiling_dispatch_ns_max_) / 1'000'000.0,
        profiling_dispatch_ns_min_ == std::numeric_limits<uint64_t>::max() ? 0.0 : static_cast<double>(profiling_dispatch_ns_min_) / 1'000'000.0,
        avg_mp3_ms,
        static_cast<double>(profiling_mp3_ns_max_) / 1'000'000.0,
        profiling_mp3_ns_min_ == std::numeric_limits<uint64_t>::max() ? 0.0 : static_cast<double>(profiling_mp3_ns_min_) / 1'000'000.0);

    reset_profiler_counters();
    profiling_last_log_time_ = now;
    profiling_last_chunk_send_time_ = now;
    if (underrun_silence_active_) {
        profiling_underrun_active_since_ = now;
    }

    // Emit per-source underrun summary once per interval
    if (!profiling_source_underruns_.empty()) {
        for (const auto& kv : profiling_source_underruns_) {
            LOG_CPP_INFO("[Profiler][SinkMixer:%s] source=%s underruns=%llu",
                         config_.sink_id.c_str(), kv.first.c_str(), static_cast<unsigned long long>(kv.second));
        }
    }
}

void SinkAudioMixer::maybe_log_telemetry(std::chrono::steady_clock::time_point now) {
    if (!m_settings || !m_settings->telemetry.enabled) {
        return;
    }

    long interval_ms = m_settings->telemetry.log_interval_ms;
    if (interval_ms <= 0) {
        interval_ms = 30000;
    }
    auto interval = std::chrono::milliseconds(interval_ms);
    if (telemetry_last_log_time_.time_since_epoch().count() != 0 && now - telemetry_last_log_time_ < interval) {
        return;
    }

    telemetry_last_log_time_ = now;

    size_t ready_sources = 0;
    size_t ready_total = 0;
    size_t ready_max = 0;
    double ready_total_ms = 0.0;
    double ready_max_ms = 0.0;
    double chunk_duration_ms = 0.0;
    const int active_channels = std::max(playback_channels_, 1);
    if (playback_sample_rate_ > 0 && playback_bit_depth_ > 0 && (playback_bit_depth_ % 8) == 0) {
        const std::size_t frame_bytes = static_cast<std::size_t>(active_channels) * static_cast<std::size_t>(playback_bit_depth_ / 8);
        if (frame_bytes > 0) {
            chunk_duration_ms = (static_cast<double>(chunk_size_bytes_) / static_cast<double>(frame_bytes)) *
                                (1000.0 / static_cast<double>(playback_sample_rate_));
        }
    }
    if (mix_scheduler_) {
        auto ready_depths = mix_scheduler_->get_ready_depths();
        ready_sources = ready_depths.size();
        for (const auto& [instance_id, depth] : ready_depths) {
            (void)instance_id;
            ready_total += depth;
            if (depth > ready_max) {
                ready_max = depth;
            }
            if (chunk_duration_ms > 0.0 && depth > 0) {
                const double backlog_ms = static_cast<double>(depth) * chunk_duration_ms;
                ready_total_ms += backlog_ms;
                if (backlog_ms > ready_max_ms) {
                    ready_max_ms = backlog_ms;
                }
                LOG_CPP_INFO(
                    "[Telemetry][SinkMixer:%s][Source %s] ready_chunks=%zu backlog_ms=%.3f",
                    config_.sink_id.c_str(),
                    instance_id.c_str(),
                    depth,
                    backlog_ms);
            }
        }
    }

    double payload_ms = 0.0;
    if (playback_sample_rate_ > 0 && playback_bit_depth_ > 0 && (playback_bit_depth_ % 8) == 0) {
        const std::size_t frame_bytes = static_cast<std::size_t>(active_channels) * static_cast<std::size_t>(playback_bit_depth_ / 8);
        if (frame_bytes > 0) {
            const double frames = static_cast<double>(payload_buffer_write_pos_) / static_cast<double>(frame_bytes);
            payload_ms = (frames * 1000.0) / static_cast<double>(playback_sample_rate_);
        }
    }

    const double ready_avg_ms = (ready_sources > 0 && chunk_duration_ms > 0.0)
        ? ready_total_ms / static_cast<double>(ready_sources)
        : 0.0;

    size_t mp3_queue_size = 0;
    if (mp3_output_queue_) {
        mp3_queue_size = mp3_output_queue_->size();
    }

    double head_skew_ms = std::chrono::duration<double, std::milli>(now - next_mix_time_).count();
    if (head_skew_ms < 0.0) {
        head_skew_ms = 0.0;
    }

    double source_avg_age_ms = 0.0;
    double source_max_age_ms = 0.0;
    size_t source_chunks_count = 0;
    {
        std::lock_guard<std::mutex> lock(queues_mutex_);
        for (const auto& [instance_id, chunk] : source_buffers_) {
            (void)instance_id;
            if (chunk.audio_data.empty() || chunk.produced_time.time_since_epoch().count() == 0) {
                continue;
            }
            double age_ms = std::chrono::duration<double, std::milli>(now - chunk.produced_time).count();
            if (age_ms < 0.0) {
                age_ms = 0.0;
            }
            source_avg_age_ms += age_ms;
            if (age_ms > source_max_age_ms) {
                source_max_age_ms = age_ms;
            }
            source_chunks_count++;
        }
    }
    if (source_chunks_count > 0) {
        source_avg_age_ms /= static_cast<double>(source_chunks_count);
    }

    LOG_CPP_INFO(
        "[Telemetry][SinkMixer:%s] payload_bytes=%zu (%.3f ms) ready_sources=%zu ready_total=%zu ready_max=%zu ready_avg_ms=%.3f ready_max_ms=%.3f head_skew_ms=%.3f source_avg_age_ms=%.3f source_max_age_ms=%.3f underrun_active=%d mp3_queue=%zu",
        config_.sink_id.c_str(),
        payload_buffer_write_pos_,
        payload_ms,
        ready_sources,
        ready_total,
        ready_max,
        ready_avg_ms,
        ready_max_ms,
        head_skew_ms,
        source_avg_age_ms,
        source_max_age_ms,
        underrun_silence_active_ ? 1 : 0,
        mp3_queue_size);
}

void SinkAudioMixer::set_playback_format(int sample_rate, int channels, int bit_depth) {
    int sanitized_rate = sample_rate > 0 ? sample_rate : 48000;
    int sanitized_channels = std::clamp(channels, 1, 8);
    int sanitized_bit_depth = bit_depth > 0 ? bit_depth : 16;
    if ((sanitized_bit_depth % 8) != 0) {
        sanitized_bit_depth = 16;
    }

    playback_sample_rate_ = sanitized_rate;
    playback_channels_ = sanitized_channels;
    playback_bit_depth_ = sanitized_bit_depth;

    mix_period_ = calculate_mix_period(playback_sample_rate_, playback_channels_, playback_bit_depth_);
    dynamic_mix_interval_ = mix_period_;
    next_mix_time_ = std::chrono::steady_clock::now();
}

void SinkAudioMixer::update_playback_format_from_sender() {
#if defined(__linux__)
    if (auto alsa_sender = dynamic_cast<AlsaPlaybackSender*>(network_sender_.get())) {
        unsigned int device_rate = alsa_sender->get_effective_sample_rate();
        unsigned int device_channels = alsa_sender->get_effective_channels();
        unsigned int device_bit_depth = alsa_sender->get_effective_bit_depth();

        int new_sample_rate = device_rate > 0 ? static_cast<int>(device_rate) : playback_sample_rate_;
        int new_channels = device_channels > 0 ? static_cast<int>(device_channels) : playback_channels_;
        int new_bit_depth = device_bit_depth > 0 ? static_cast<int>(device_bit_depth) : playback_bit_depth_;

        bool format_changed = (new_sample_rate != playback_sample_rate_) ||
                              (new_channels != playback_channels_) ||
                              (new_bit_depth != playback_bit_depth_);

        if (format_changed) {
            set_playback_format(new_sample_rate, new_channels, new_bit_depth);
            LOG_CPP_INFO("[SinkMixer:%s] Updated playback pacing to match ALSA device (rate=%d Hz, channels=%d, bit_depth=%d).",
                         config_.sink_id.c_str(), playback_sample_rate_, playback_channels_, playback_bit_depth_);
        }
        return;
    }
#endif
}

std::chrono::microseconds SinkAudioMixer::calculate_mix_period(int samplerate, int channels, int bit_depth) const {
    int sanitized_rate = std::max(1, samplerate);
    int sanitized_channels = std::max(1, channels);
    int sanitized_bit_depth = bit_depth;
    if (sanitized_bit_depth <= 0 || (sanitized_bit_depth % 8) != 0) {
        sanitized_bit_depth = 16;
    }

    const std::size_t bytes_per_sample = static_cast<std::size_t>(sanitized_bit_depth) / 8;
    const std::size_t frame_bytes = bytes_per_sample * static_cast<std::size_t>(sanitized_channels);
    if (frame_bytes == 0) {
        return std::chrono::microseconds(6000);
    }

    const std::size_t frames_per_chunk = chunk_size_bytes_ / frame_bytes;
    if (frames_per_chunk == 0) {
        return std::chrono::microseconds(6000);
    }

    const long long numerator = static_cast<long long>(frames_per_chunk) * 1000000LL;
    long long period_us = numerator / static_cast<long long>(sanitized_rate);
    if (period_us <= 0) {
        period_us = 6000;
    }

    return std::chrono::microseconds(period_us);
}

void SinkAudioMixer::register_mix_timer() {
    clock_condition_handle_ = {};
    clock_last_sequence_ = 0;
    clock_pending_ticks_ = 0;
    clock_manager_enabled_.store(false, std::memory_order_release);

    timer_sample_rate_ = playback_sample_rate_ > 0 ? playback_sample_rate_ : 48000;
    timer_channels_ = std::clamp(playback_channels_, 1, 8);
    timer_bit_depth_ = playback_bit_depth_ > 0 ? playback_bit_depth_ : 16;
    if ((timer_bit_depth_ % 8) != 0) {
        timer_bit_depth_ = 16;
    }

    if (!clock_manager_) {
        try {
            const auto chunk_size_bytes = resolve_chunk_size_bytes(m_settings);
            clock_manager_ = std::make_unique<ClockManager>(chunk_size_bytes);
        } catch (const std::exception& ex) {
            LOG_CPP_ERROR("[SinkMixer:%s] Failed to create ClockManager: %s",
                          config_.sink_id.c_str(), ex.what());
            return;
        }
    }

    try {
        clock_condition_handle_ = clock_manager_->register_clock_condition(
            timer_sample_rate_, timer_channels_, timer_bit_depth_);
        if (!clock_condition_handle_.valid()) {
            throw std::runtime_error("ClockManager returned invalid condition handle");
        }
        if (auto condition = clock_condition_handle_.condition) {
            std::lock_guard<std::mutex> condition_lock(condition->mutex);
            condition->sequence = 0;
        }
        clock_last_sequence_ = 0;
        clock_pending_ticks_ = 0;
        clock_manager_enabled_.store(true, std::memory_order_release);
        LOG_CPP_DEBUG("[SinkMixer:%s] Registered clock-managed mix timer (sr=%d ch=%d bit=%d) using conditions",
                      config_.sink_id.c_str(), timer_sample_rate_, timer_channels_, timer_bit_depth_);
    } catch (const std::exception& ex) {
        LOG_CPP_ERROR("[SinkMixer:%s] Failed to register mix timer condition with ClockManager: %s. Falling back to internal pacing.",
                      config_.sink_id.c_str(), ex.what());
        clock_condition_handle_ = {};
        clock_manager_enabled_.store(false, std::memory_order_release);
    }
}

void SinkAudioMixer::unregister_mix_timer() {
    if (clock_manager_ && clock_condition_handle_.valid()) {
        try {
            clock_manager_->unregister_clock_condition(clock_condition_handle_);
        } catch (const std::exception& ex) {
            LOG_CPP_WARNING("[SinkMixer:%s] Failed to unregister mix timer: %s",
                            config_.sink_id.c_str(), ex.what());
        }
    }

    clock_manager_enabled_.store(false, std::memory_order_release);
    clock_condition_handle_ = {};
    clock_last_sequence_ = 0;
    clock_pending_ticks_ = 0;
    timer_sample_rate_ = 0;
    timer_channels_ = 0;
    timer_bit_depth_ = 0;
}

bool SinkAudioMixer::wait_for_mix_tick() {
    const auto target_time = next_mix_time_ + dynamic_mix_interval_;

    if (!clock_manager_enabled_.load(std::memory_order_acquire)) {
        auto now = std::chrono::steady_clock::now();
        if (target_time > now) {
            std::this_thread::sleep_until(target_time);
        }
        next_mix_time_ = target_time;
        return !stop_flag_;
    }

    std::this_thread::sleep_until(target_time);
    next_mix_time_ = target_time;
    return !stop_flag_;
}

void SinkAudioMixer::cleanup_closed_listeners() {
    std::vector<std::string> listeners_to_remove;
    {
        std::lock_guard<std::mutex> lock(listener_senders_mutex_);
        for (const auto& pair : listener_senders_) {
            if (WebRtcSender* webrtc_sender = dynamic_cast<WebRtcSender*>(pair.second.get())) {
                if (webrtc_sender->is_closed() || webrtc_sender->should_cleanup_due_to_timeout()) {
                    listeners_to_remove.push_back(pair.first);
                    LOG_CPP_INFO("[SinkMixer:%s] Found closed/timed-out listener to cleanup: %s", config_.sink_id.c_str(), pair.first.c_str());
                    // Don't call close() here while holding the lock!
                    // It will be called in remove_listener() without the lock
                }
            }
        }
    } // Release listener_senders_mutex_ before calling remove_listener

    for (const auto& listener_id : listeners_to_remove) {
        remove_listener(listener_id);  // This will close() the sender without holding the lock
        LOG_CPP_INFO("[SinkMixer:%s] Successfully cleaned up listener: %s", config_.sink_id.c_str(), listener_id.c_str());
    }

    if (!listeners_to_remove.empty()) {
        std::lock_guard<std::mutex> lock(listener_senders_mutex_);
        LOG_CPP_INFO("[SinkMixer:%s] Cleanup complete. Remaining listeners: %zu", config_.sink_id.c_str(), listener_senders_.size());
    }
}

void SinkAudioMixer::clear_pending_audio() {
    if (mix_scheduler_) {
        MixScheduler::HarvestResult discard;
        do {
            discard = mix_scheduler_->collect_ready_chunks();
        } while (!discard.ready_chunks.empty());
    }

    std::lock_guard<std::mutex> lock(queues_mutex_);

    for (auto& [instance_id, queue_ptr] : input_queues_) {
        (void)queue_ptr;
        input_active_state_[instance_id] = false;
        source_buffers_[instance_id] = ProcessedAudioChunk{};
    }

    underrun_silence_active_ = false;
    payload_buffer_write_pos_ = 0;
    profiling_max_payload_buffer_bytes_ = 0;
}

/**
 * @brief The main processing loop for the mixer thread.
 */
void SinkAudioMixer::run() {
    LOG_CPP_INFO("[SinkMixer:%s] Entering run loop.", config_.sink_id.c_str());
    next_mix_time_ = std::chrono::steady_clock::now();

    while (!stop_flag_) {
        if (!wait_for_mix_tick()) {
            continue;
        }

        if (stop_flag_) {
            break;
        }

        profiling_cycles_++;
        cleanup_closed_listeners();

        bool data_available = wait_for_source_data();
        LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Poll complete. Data available this cycle: %s",
                      config_.sink_id.c_str(), data_available ? "true" : "false");

        if (stop_flag_) {
            break;
        }

        std::unique_lock<std::mutex> lock(queues_mutex_);
        bool has_active_sources = false;
        for (const auto& [instance_id, is_active] : input_active_state_) {
            (void)instance_id;
            if (is_active) {
                has_active_sources = true;
                break;
            }
        }

        bool should_mix = has_active_sources || underrun_silence_active_;

        if (!should_mix) {
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: No active sources and no underrun hold. Emitting silence.",
                          config_.sink_id.c_str());
        }

        const bool coordination_active = coordination_mode_ && coordinator_;
        SinkSynchronizationCoordinator::DispatchTimingInfo dispatch_timing{};

        if (coordination_active) {
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Coordination enabled, waiting on barrier...", config_.sink_id.c_str());
            if (!coordinator_->begin_dispatch()) {
                LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Coordinator requested skip, yielding cycle.",
                              config_.sink_id.c_str());
                lock.unlock();
                auto telemetry_now = std::chrono::steady_clock::now();
                maybe_log_profiler();
                maybe_log_telemetry(telemetry_now);
                continue;
            }

            dispatch_timing.dispatch_start = std::chrono::steady_clock::now();
            dispatch_timing.dispatch_end = dispatch_timing.dispatch_start;

            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Barrier cleared, proceeding with mix.", config_.sink_id.c_str());
        }

        LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Mixing buffers...", config_.sink_id.c_str());
        mix_buffers();
        LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Mixing complete.", config_.sink_id.c_str());

        lock.unlock();

        downscale_buffer();

        const int effective_bit_depth = (playback_bit_depth_ > 0 && (playback_bit_depth_ % 8) == 0)
                                            ? playback_bit_depth_
                                            : 16;
        const std::size_t bytes_per_sample = static_cast<std::size_t>(effective_bit_depth) / 8;
        const int effective_channels = std::max(playback_channels_, 1);
        const std::size_t frame_bytes = bytes_per_sample * static_cast<std::size_t>(effective_channels);
        const bool frame_metrics_valid = frame_bytes > 0 && (chunk_size_bytes_ % frame_bytes) == 0;
        const std::size_t frames_per_chunk = frame_metrics_valid ? (chunk_size_bytes_ / frame_bytes) : 0;
        uint64_t frames_dispatched = 0;

        if (!frame_metrics_valid && coordination_active) {
            LOG_CPP_WARNING("[SinkMixer:%s] RunLoop: Unable to derive frames_per_chunk (bit_depth=%d, channels=%d).",
                            config_.sink_id.c_str(), playback_bit_depth_, playback_channels_);
        }

        size_t chunks_dispatched = 0;
        while (payload_buffer_write_pos_ >= chunk_size_bytes_) {
            auto send_time = std::chrono::steady_clock::now();
            if (profiling_last_chunk_send_time_.time_since_epoch().count() != 0) {
                double gap_ms = std::chrono::duration<double, std::milli>(send_time - profiling_last_chunk_send_time_).count();
                profiling_last_send_gap_ms_ = gap_ms;
                profiling_send_gap_sum_ms_ += gap_ms;
                profiling_send_gap_samples_++;
                if (profiling_send_gap_samples_ == 1) {
                    profiling_send_gap_min_ms_ = gap_ms;
                    profiling_send_gap_max_ms_ = gap_ms;
                } else {
                    profiling_send_gap_min_ms_ = std::min(profiling_send_gap_min_ms_, gap_ms);
                    profiling_send_gap_max_ms_ = std::max(profiling_send_gap_max_ms_, gap_ms);
                }
            }
            profiling_last_chunk_send_time_ = send_time;
            if (network_sender_) {
                std::lock_guard<std::mutex> lock(csrc_mutex_);
                network_sender_->send_payload(payload_buffer_.data(), chunk_size_bytes_, current_csrcs_);
            }
            profiling_chunks_sent_++;
            profiling_payload_bytes_sent_ += chunk_size_bytes_;

            if (frame_metrics_valid) {
                frames_dispatched += frames_per_chunk;
            }
            chunks_dispatched++;

            size_t bytes_remaining = payload_buffer_write_pos_ - chunk_size_bytes_;
            if (bytes_remaining > 0) {
                memmove(payload_buffer_.data(), payload_buffer_.data() + chunk_size_bytes_, bytes_remaining);
            }
            payload_buffer_write_pos_ = bytes_remaining;

            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Sent chunk, remaining bytes in buffer: %zu",
                          config_.sink_id.c_str(), payload_buffer_write_pos_);
        }

        if (coordination_active) {
            dispatch_timing.dispatch_end = std::chrono::steady_clock::now();
            if (!frame_metrics_valid) {
                if (chunks_dispatched > 0 && playback_sample_rate_ > 0) {
                    const double period_seconds = std::chrono::duration<double>(mix_period_).count();
                    const double frames_per_chunk_estimate = period_seconds > 0.0
                                                                 ? static_cast<double>(playback_sample_rate_) * period_seconds
                                                                 : 0.0;
                    frames_dispatched = static_cast<uint64_t>(
                        std::llround(frames_per_chunk_estimate * static_cast<double>(chunks_dispatched)));
                } else {
                    frames_dispatched = 0;
                }
            }
            coordinator_->complete_dispatch(frames_dispatched, dispatch_timing);
        }

        bool has_listeners;
        {
            std::lock_guard<std::mutex> lock(listener_senders_mutex_);
            has_listeners = !listener_senders_.empty();
        }
        bool mp3_enabled = (mp3_output_queue_ != nullptr);

        if (has_listeners || mp3_enabled) {
            size_t processed_samples = preprocess_for_listeners_and_mp3();
            if (processed_samples > 0) {
                if (has_listeners) {
                    dispatch_to_listeners(processed_samples);
                }
                if (mp3_enabled) {
                    encode_and_push_mp3(processed_samples);
                }
            }
        }

        auto telemetry_now = std::chrono::steady_clock::now();
        maybe_log_profiler();
        maybe_log_telemetry(telemetry_now);
    }

    LOG_CPP_INFO("[SinkMixer:%s] Exiting run loop.", config_.sink_id.c_str());
}
