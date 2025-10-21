/**
 * @file sink_audio_mixer.cpp
 * @brief Implements the SinkAudioMixer class for mixing and outputting audio.
 * @details This file contains the implementation of the SinkAudioMixer, which is a
 *          core component responsible for collecting audio from multiple sources,
 *          mixing it, and dispatching it to various network senders.
 */
#include "sink_audio_mixer.h"
#include "../utils/cpp_logger.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <vector>
#include <chrono>
#include <system_error>
#include <algorithm>
#include <cmath>
#include <thread>
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
      mp3_output_queue_(mp3_output_queue),
      network_sender_(nullptr),
      mixing_buffer_(SINK_MIXING_BUFFER_SAMPLES, 0),
      stereo_buffer_(SINK_MIXING_BUFFER_SAMPLES * 2, 0),
      payload_buffer_(SINK_CHUNK_SIZE_BYTES * 8, 0),
      lame_global_flags_(nullptr),
      stereo_preprocessor_(nullptr),
      mp3_encode_buffer_(SINK_MP3_BUFFER_SIZE),
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
        source_buffers_[instance_id].audio_data.assign(SINK_MIXING_BUFFER_SAMPLES, 0);
        LOG_CPP_INFO("[SinkMixer:%s] Added input queue for source instance: %s", config_.sink_id.c_str(), instance_id.c_str());
    }
    input_cv_.notify_one();
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
    LOG_CPP_INFO("[SinkMixer:%s] Starting...", config_.sink_id.c_str());
    stop_flag_ = false;
    payload_buffer_write_pos_ = 0;
    reset_profiler_counters();

    if (network_sender_ && !network_sender_->setup()) {
        LOG_CPP_ERROR("[SinkMixer:%s] Network sender setup failed. Cannot start mixer thread.", config_.sink_id.c_str());
        if (config_.protocol == "system_audio") {
            LOG_CPP_WARNING("[SinkMixer:%s] System audio playback sender setup failed; continuing anyway.",
                            config_.sink_id.c_str());
        } else {
            return;
        }
    }

    try {
        component_thread_ = std::thread(&SinkAudioMixer::run, this);
        LOG_CPP_INFO("[SinkMixer:%s] Thread started.", config_.sink_id.c_str());
    } catch (const std::system_error& e) {
        LOG_CPP_ERROR("[SinkMixer:%s] Failed to start thread: %s", config_.sink_id.c_str(), e.what());
        if (network_sender_) {
            network_sender_->close();
        }
        throw;
    }
}

/**
 * @brief Stops the mixer's processing thread and cleans up resources.
 */
void SinkAudioMixer::stop() {
     if (stop_flag_) {
        LOG_CPP_INFO("[SinkMixer:%s] Already stopped or stopping.", config_.sink_id.c_str());
        return;
    }
    LOG_CPP_INFO("[SinkMixer:%s] Stopping...", config_.sink_id.c_str());
    stop_flag_ = true;

    input_cv_.notify_all();

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
        network_sender_->close();
    }

    {
        std::lock_guard<std::mutex> lock(listener_senders_mutex_);
        for (auto& pair : listener_senders_) {
            if (pair.second) {
                pair.second->close();
            }
        }
        listener_senders_.clear();
        LOG_CPP_INFO("[SinkMixer:%s] All listener senders closed and cleared.", config_.sink_id.c_str());
    }
}

/**
 * @brief Waits for data from input queues and determines which sources are active.
 * @param ignored_timeout A timeout value (currently ignored in implementation).
 * @return `true` if any data was popped from any queue, `false` otherwise.
 */
bool SinkAudioMixer::wait_for_source_data() {
    std::lock_guard<std::mutex> lock(queues_mutex_);

    bool data_actually_popped_this_cycle = false;
    std::map<std::string, bool> ready_this_cycle;
    std::vector<std::string> lagging_active_sources;
    size_t initial_lagging_sources = 0;

    bool had_any_active_sources = false;
    for (const auto& [instance_id, is_active] : input_active_state_) {
        (void)instance_id;
        if (is_active) {
            had_any_active_sources = true;
            break;
        }
    }

    bool was_holding_silence = underrun_silence_active_;

    LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Initial non-blocking check...", config_.sink_id.c_str());
    for (auto const& [instance_id, queue_ptr] : input_queues_) {
        ProcessedAudioChunk chunk;
        bool previously_active = input_active_state_.count(instance_id) ? input_active_state_[instance_id] : false;

        if (queue_ptr->try_pop(chunk)) {
            if (chunk.audio_data.size() != SINK_MIXING_BUFFER_SAMPLES) {
                LOG_CPP_ERROR("[SinkMixer:%s] WaitForData: Received chunk from instance %s with unexpected sample count: %zu. Discarding.",
                              config_.sink_id.c_str(), instance_id.c_str(), chunk.audio_data.size());
                ready_this_cycle[instance_id] = false;
            } else {
                LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Pop SUCCESS (Initial) for instance %s", config_.sink_id.c_str(), instance_id.c_str());
                m_total_chunks_mixed++;
                source_buffers_[instance_id] = std::move(chunk);
                ready_this_cycle[instance_id] = true;
                data_actually_popped_this_cycle = true;
                if (!previously_active) {
                    LOG_CPP_DEBUG("[SinkMixer:%s] Input instance %s became active", config_.sink_id.c_str(), instance_id.c_str());
                }
                input_active_state_[instance_id] = true;
            }
        } else {
            ready_this_cycle[instance_id] = false;
            if (previously_active) {
                LOG_CPP_DEBUG(
                    "[SinkMixer:%s] WaitForData: Pop FAILED (Initial) for ACTIVE instance %s. Adding to grace period check.",
                    config_.sink_id.c_str(), instance_id.c_str());
                lagging_active_sources.push_back(instance_id);
            } else {
                input_active_state_[instance_id] = false;
            }
        }
    }

    initial_lagging_sources = lagging_active_sources.size();

    if (!lagging_active_sources.empty()) {
        LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Entering grace period check for %zu sources.", config_.sink_id.c_str(),
                      lagging_active_sources.size());
        auto grace_period_start = std::chrono::steady_clock::now();
        long elapsed_us = 0;

        while (!lagging_active_sources.empty() &&
               elapsed_us <= m_settings->mixer_tuning.grace_period_timeout_ms * 1000) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(m_settings->mixer_tuning.grace_period_poll_interval_ms));

            for (auto it = lagging_active_sources.begin(); it != lagging_active_sources.end();) {
                const std::string& instance_id = *it;
                auto queue_it = input_queues_.find(instance_id);
                if (queue_it == input_queues_.end()) {
                    it = lagging_active_sources.erase(it);
                    continue;
                }

                ProcessedAudioChunk chunk;
                if (queue_it->second->try_pop(chunk)) {
                    if (chunk.audio_data.size() != SINK_MIXING_BUFFER_SAMPLES) {
                        LOG_CPP_ERROR(
                            "[SinkMixer:%s] WaitForData: Received chunk (Grace Period) from instance %s with unexpected sample count: %zu. Discarding.",
                            config_.sink_id.c_str(), instance_id.c_str(), chunk.audio_data.size());
                    } else {
                        LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Pop SUCCESS (Grace Period) for instance %s",
                                      config_.sink_id.c_str(), instance_id.c_str());
                        m_total_chunks_mixed++;
                        source_buffers_[instance_id] = std::move(chunk);
                        ready_this_cycle[instance_id] = true;
                        data_actually_popped_this_cycle = true;
                    }
                    it = lagging_active_sources.erase(it);
                } else {
                    ++it;
                }
            }
            elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - grace_period_start)
                             .count();
        }

        if (!lagging_active_sources.empty()) {
            LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Grace period ended. %zu instances still lagging.",
                          config_.sink_id.c_str(), lagging_active_sources.size());
            for (const auto& instance_id : lagging_active_sources) {
                if (input_active_state_.count(instance_id) && input_active_state_[instance_id]) {
                    LOG_CPP_DEBUG("[SinkMixer:%s] Input instance %s timed out grace period, marking inactive.",
                                  config_.sink_id.c_str(), instance_id.c_str());
                    input_active_state_[instance_id] = false;
                    m_buffer_underruns++;
                }
            }
        } else {
            LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Grace period ended. All lagging sources caught up.",
                          config_.sink_id.c_str());
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
    } else if (was_holding_silence && !underrun_silence_active_) {
        if (data_actually_popped_this_cycle) {
            LOG_CPP_INFO(
                "[SinkMixer:%s] Underrun cleared. Audio resumed before silence window elapsed.",
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
    profiling_lagging_sources_sum_ += initial_lagging_sources;
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
}

/**
 * @brief Downscales the 32-bit mixed audio to the target bit depth for network output.
 */
void SinkAudioMixer::downscale_buffer() {
    size_t output_byte_depth = config_.output_bitdepth / 8;
    size_t samples_to_convert = mixing_buffer_.size();

    size_t expected_bytes_to_write = samples_to_convert * output_byte_depth;
    LOG_CPP_DEBUG("[SinkMixer:%s] Downscale: Converting %zu samples (int32) to %d-bit. Expected output bytes=%zu.",
                  config_.sink_id.c_str(), samples_to_convert, config_.output_bitdepth, expected_bytes_to_write);

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
        switch (config_.output_bitdepth) {
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
}

/**
 * @brief Pre-processes the mixed audio into a stereo format for listeners and MP3 encoding.
 * @return The number of stereo samples processed.
 */
size_t SinkAudioMixer::preprocess_for_listeners_and_mp3() {
    if (!stereo_preprocessor_) {
        return 0;
    }

    const size_t total_bytes_in_mixing_buffer = mixing_buffer_.size() * sizeof(int32_t);
    const size_t input_chunk_bytes = SINK_CHUNK_SIZE_BYTES;
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
    return processed_samples_count;
}

/**
 * @brief Dispatches the pre-processed stereo audio to all registered listeners.
 * @param samples_to_dispatch The number of stereo samples to send.
 */
void SinkAudioMixer::dispatch_to_listeners(size_t samples_to_dispatch) {
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
}

/**
 * @brief Encodes the pre-processed stereo audio to MP3 and pushes it to the output queue.
 * @param samples_to_encode The number of stereo samples to encode.
 */
void SinkAudioMixer::encode_and_push_mp3(size_t samples_to_encode) {
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

    LOG_CPP_INFO(
        "[Profiler][SinkMixer:%s] cycles=%llu data_cycles=%llu chunks_sent=%llu payload_kib=%.2f active_inputs=%zu/%zu avg_ready=%.2f avg_lagging=%.2f avg_queue=%.2f max_queue=%zu buffer_bytes(current/peak)=(%zu/%zu) underruns=%llu overflows=%llu mp3_overflows=%llu",
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
        m_mp3_buffer_overflows.load());

    profiling_last_log_time_ = now;
    profiling_cycles_ = 0;
    profiling_data_ready_cycles_ = 0;
    profiling_chunks_sent_ = 0;
    profiling_payload_bytes_sent_ = 0;
    profiling_ready_sources_sum_ = 0;
    profiling_lagging_sources_sum_ = 0;
    profiling_samples_count_ = 0;
    profiling_max_payload_buffer_bytes_ = payload_buffer_write_pos_;
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

/**
 * @brief The main processing loop for the mixer thread.
 */
void SinkAudioMixer::run() {
    LOG_CPP_INFO("[SinkMixer:%s] Entering run loop.", config_.sink_id.c_str());

    LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Starting iteration.", config_.sink_id.c_str());
    while (!stop_flag_) {
        profiling_cycles_++;
        cleanup_closed_listeners();
        LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Waiting for source data...", config_.sink_id.c_str());
        bool data_available = wait_for_source_data();
        LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Wait finished. Data available: %s", config_.sink_id.c_str(), (data_available ? "true" : "false"));

        if (stop_flag_) {
             LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Stop flag checked after wait, breaking.", config_.sink_id.c_str());
             break;
        }

        std::unique_lock<std::mutex> lock(queues_mutex_);
        bool should_inject_silence = !data_available && underrun_silence_active_;

        if (data_available || should_inject_silence) {
            if (should_inject_silence) {
                LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: No data but underrun hold active. Injecting silence.",
                              config_.sink_id.c_str());
            } else {
                LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Data available or queues not empty, proceeding to mix.",
                              config_.sink_id.c_str());
            }
            
            // --- SYNCHRONIZATION COORDINATION POINT ---
            // If coordination is enabled, call the coordinator BEFORE mixing
            // This allows all sinks to wait at the barrier, then proceed together
            if (coordination_mode_ && coordinator_) {
                LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Coordination enabled, calling coordinator...", config_.sink_id.c_str());
                bool coord_success = coordinator_->coordinate_dispatch();
                
                if (!coord_success) {
                    LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Coordinator reported failure, skipping this cycle.", config_.sink_id.c_str());
                    lock.unlock();
                    maybe_log_profiler();
                    continue;
                }
                
                // Note: rate_adjustment application would happen here in future
                // For now, the coordinator's coordinate_dispatch() handles everything
                LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Coordination complete.", config_.sink_id.c_str());
            }
            
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Mixing buffers...", config_.sink_id.c_str());
            mix_buffers();
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Mixing complete.", config_.sink_id.c_str());

            lock.unlock();
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Queues mutex unlocked.", config_.sink_id.c_str());

            downscale_buffer();

            // Send data in chunks when we have enough accumulated
            // This loop ensures we drain the buffer properly even with high bit depths
            while (payload_buffer_write_pos_ >= SINK_CHUNK_SIZE_BYTES) {
                if (network_sender_) {
                    std::lock_guard<std::mutex> lock(csrc_mutex_);
                    network_sender_->send_payload(payload_buffer_.data(), SINK_CHUNK_SIZE_BYTES, current_csrcs_);
                }
                profiling_chunks_sent_++;
                profiling_payload_bytes_sent_ += SINK_CHUNK_SIZE_BYTES;

                size_t bytes_remaining = payload_buffer_write_pos_ - SINK_CHUNK_SIZE_BYTES;
                if (bytes_remaining > 0) {
                    memmove(payload_buffer_.data(), payload_buffer_.data() + SINK_CHUNK_SIZE_BYTES, bytes_remaining);
                }
                payload_buffer_write_pos_ = bytes_remaining;
                
                LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Sent chunk, remaining bytes in buffer: %zu", config_.sink_id.c_str(), payload_buffer_write_pos_);
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

            if (should_inject_silence) {
                uint32_t samplerate = config_.output_samplerate > 0 ? config_.output_samplerate : 48000;
                auto chunk_duration_us = static_cast<long long>(SINK_MIXING_BUFFER_SAMPLES) * 1000000LL /
                                         static_cast<long long>(samplerate);
                std::this_thread::sleep_for(std::chrono::microseconds(chunk_duration_us));
            }
        } else {
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: No data available and input queues empty. Sleeping briefly.", config_.sink_id.c_str());
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: End of iteration.", config_.sink_id.c_str());
        maybe_log_profiler();
    }

    LOG_CPP_INFO("[SinkMixer:%s] Exiting run loop.", config_.sink_id.c_str());
}
