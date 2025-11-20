#include "source_input_processor.h"
#include "../utils/cpp_logger.h"
#include <iostream> // For logging (cpp_logger fallback)
#include <stdexcept>
#include <cstring> // For memcpy
#include <algorithm> // For std::min, std::max
#include <cmath>     // For std::chrono durations
#include <thread>    // For sleep_for
#include "../utils/profiler.h"

#ifdef min

#endif
#ifdef max

#endif

// Use namespaces for clarity
using namespace screamrouter::audio;
using namespace screamrouter::audio::utils;

namespace {
constexpr double kMinPlaybackRate = 0.5;
constexpr double kMaxPlaybackRate = 2.0;
constexpr double kPlaybackRateEpsilon = 1e-4;
}

const std::chrono::milliseconds TIMESIFT_CLEANUP_INTERVAL(1000);

SourceInputProcessor::SourceInputProcessor(
    SourceProcessorConfig config, // config now includes instance_id
    std::shared_ptr<InputPacketQueue> input_queue,
    std::shared_ptr<OutputChunkQueue> output_queue,
    std::shared_ptr<CommandQueue> command_queue,
    std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings)
    : config_(std::move(config)), // Use std::move for config
      input_queue_(input_queue),
      output_queue_(output_queue),
      command_queue_(command_queue),
      m_settings(settings),
      base_frames_per_chunk_(resolve_base_frames_per_chunk(settings)),
      current_input_chunk_bytes_(resolve_chunk_size_bytes(settings)),
      profiling_last_log_time_(std::chrono::steady_clock::now()),
      current_volume_(config_.initial_volume), // Initialize from moved config_
      current_eq_(config_.initial_eq),
      current_delay_ms_(config_.initial_delay_ms),
      current_timeshift_backshift_sec_config_(config_.initial_timeshift_sec), // Initialize from config
      // current_speaker_layouts_map_ is default-initialized (empty)
      m_current_ap_input_channels(0), // Initialize current format state
      m_current_ap_input_samplerate(0),
      m_current_ap_input_bitdepth(0)
{
    // current_speaker_layouts_map_ will be populated by set_speaker_layouts_config
    // or when AudioProcessor is created if SourceProcessorConfig is updated.
    // For now, it starts empty.

    LOG_CPP_INFO("[SourceProc:%s] Initializing...", config_.instance_id.c_str());
    if (!input_queue_ || !output_queue_ || !command_queue_) {
        // Log before throwing
        LOG_CPP_ERROR("[SourceProc:%s] Initialization failed: Requires valid input, output, and command queues.", config_.instance_id.c_str());
        throw std::runtime_error("SourceInputProcessor requires valid input, output, and command queues.");
    }
    // Ensure EQ vector has the correct size if provided, otherwise initialize default
    if (current_eq_.size() != EQ_BANDS) {
        LOG_CPP_WARNING("[SourceProc:%s] Initial EQ size mismatch (%zu vs %d). Resetting to default (flat).", config_.instance_id.c_str(), current_eq_.size(), EQ_BANDS);
        current_eq_.assign(EQ_BANDS, 1.0f);
        config_.initial_eq = current_eq_; // Update config_ member
    }

    audio_processor_ = nullptr; // Set audio_processor_ to nullptr initially
    LOG_CPP_INFO("[SourceProc:%s] Initialization complete.", config_.instance_id.c_str());
}

SourceInputProcessor::~SourceInputProcessor() {
    LOG_CPP_INFO("[SourceProc:%s] Destroying...", config_.instance_id.c_str());
    if (!stop_flag_) {
        LOG_CPP_INFO("[SourceProc:%s] Destructor called while still running. Stopping...", config_.instance_id.c_str());
        stop(); // Ensure stop logic is triggered if not already stopped
    }
    if (input_thread_.joinable()) {
        LOG_CPP_INFO("[SourceProc:%s] Joining input thread in destructor...", config_.instance_id.c_str());
        try {
            input_thread_.join();
            LOG_CPP_INFO("[SourceProc:%s] Input thread joined.", config_.instance_id.c_str());
        } catch (const std::system_error& e) {
            LOG_CPP_ERROR("[SourceProc:%s] Error joining input thread in destructor: %s", config_.instance_id.c_str(), e.what());
        }
    }
    LOG_CPP_INFO("[SourceProc:%s] Destructor finished.", config_.instance_id.c_str());
}

// --- Getters ---

const std::string& SourceInputProcessor::get_source_tag() const {
    // This getter is needed by AudioManager to interact with RtpReceiver/SinkMixer
    // which might still rely on the original source tag (IP) until fully refactored.
    return config_.source_tag;
}

SourceInputProcessorStats SourceInputProcessor::get_stats() {
    SourceInputProcessorStats stats;
    stats.total_packets_processed = m_total_packets_processed.load();
    stats.input_queue_size = input_queue_ ? input_queue_->size() : 0;
    stats.output_queue_size = output_queue_ ? output_queue_->size() : 0;
    stats.reconfigurations = m_reconfigurations.load();
    stats.input_queue_ms = (m_current_input_chunk_ms > 0.0)
        ? m_current_input_chunk_ms * static_cast<double>(stats.input_queue_size)
        : 0.0;
    stats.output_queue_ms = (m_current_output_chunk_ms > 0.0)
        ? m_current_output_chunk_ms * static_cast<double>(stats.output_queue_size)
        : 0.0;
    stats.process_buffer_samples = process_buffer_.size();
    {
        size_t tracked_peak = m_process_buffer_high_water.load();
        if (stats.process_buffer_samples > tracked_peak) {
            m_process_buffer_high_water.store(stats.process_buffer_samples);
            tracked_peak = stats.process_buffer_samples;
        }
        stats.peak_process_buffer_samples = tracked_peak;
    }
    if (config_.output_samplerate > 0 && config_.output_channels > 0) {
        const double frames = static_cast<double>(stats.process_buffer_samples) / static_cast<double>(config_.output_channels);
        stats.process_buffer_ms = (frames * 1000.0) / static_cast<double>(config_.output_samplerate);
    }
    stats.total_chunks_pushed = m_total_chunks_pushed.load();
    stats.total_discarded_packets = m_total_discarded_packets.load();
    stats.output_queue_high_water = m_output_queue_high_water.load();
    stats.input_queue_high_water = m_input_queue_high_water.load();
    stats.playback_rate = current_playback_rate_;
    stats.input_samplerate = static_cast<double>(m_current_ap_input_samplerate);
    stats.output_samplerate = static_cast<double>(config_.output_samplerate);
    if (m_current_ap_input_samplerate > 0) {
        double base_ratio = static_cast<double>(config_.output_samplerate) / static_cast<double>(m_current_ap_input_samplerate);
        stats.resample_ratio = base_ratio * current_playback_rate_;
    } else {
        stats.resample_ratio = 0.0;
    }

    if (profiling_processing_samples_ > 0) {
        stats.avg_loop_ms = (static_cast<double>(profiling_processing_ns_) / 1'000'000.0) /
            static_cast<double>(profiling_processing_samples_);
    }

    auto now = std::chrono::steady_clock::now();
    if (m_last_packet_time.time_since_epoch().count() != 0) {
        stats.last_packet_age_ms = std::chrono::duration<double, std::milli>(now - m_last_packet_time).count();
        if (stats.last_packet_age_ms < 0.0) {
            stats.last_packet_age_ms = 0.0;
        }
    }
    if (m_last_packet_origin_time.time_since_epoch().count() != 0) {
        stats.last_origin_age_ms = std::chrono::duration<double, std::milli>(now - m_last_packet_origin_time).count();
        if (stats.last_origin_age_ms < 0.0) {
            stats.last_origin_age_ms = 0.0;
        }
    }
    return stats;
}
// --- Initialization & Configuration ---

void SourceInputProcessor::set_speaker_layouts_config(const std::map<int, screamrouter::audio::CppSpeakerLayout>& layouts_map) {
    PROFILE_FUNCTION();
    std::lock_guard<std::mutex> lock(processor_config_mutex_); // Protect map and processor access
    current_speaker_layouts_map_ = layouts_map;
    LOG_CPP_DEBUG("[SourceProc:%s] Received %zu speaker layouts.", config_.instance_id.c_str(), layouts_map.size());

    if (audio_processor_) {
        audio_processor_->update_speaker_layouts_config(current_speaker_layouts_map_);
        LOG_CPP_DEBUG("[SourceProc:%s] Updated AudioProcessor with new speaker layouts.", config_.instance_id.c_str());
    }
}

void SourceInputProcessor::start() {
     PROFILE_FUNCTION();
     if (is_running()) {
        LOG_CPP_INFO("[SourceProc:%s] Already running.", config_.instance_id.c_str());
        return;
    }
    LOG_CPP_INFO("[SourceProc:%s] Starting...", config_.instance_id.c_str());
    // Reset state specific to this component
    process_buffer_.clear();
    // Implementation for start: set flag, launch thread
    stop_flag_ = false; // Reset stop flag before launching threads
    reset_profiler_counters();
    try {
        // Only launch the main component thread here.
        // run() will launch the worker threads.
        component_thread_ = std::thread(&SourceInputProcessor::run, this);
        LOG_CPP_INFO("[SourceProc:%s] Component thread launched (will start workers).", config_.instance_id.c_str());
    } catch (const std::system_error& e) {
        LOG_CPP_ERROR("[SourceProc:%s] Failed to start component thread: %s", config_.instance_id.c_str(), e.what());
        stop_flag_ = true; // Ensure stopped state if launch fails
        if(input_queue_) input_queue_->stop(); // Ensure queues are stopped
        if(command_queue_) command_queue_->stop();
        // Rethrow or handle error appropriately
        throw; // Rethrow to signal failure
    }
}


void SourceInputProcessor::stop() {
    PROFILE_FUNCTION();
    if (stop_flag_) {
        LOG_CPP_INFO("[SourceProc:%s] Already stopped or stopping.", config_.instance_id.c_str());
        return;
    }
    bool comp_joinable = component_thread_.joinable();
    bool input_joinable = input_thread_.joinable();
    size_t in_q = input_queue_ ? input_queue_->size() : 0;
    size_t cmd_q = command_queue_ ? command_queue_->size() : 0;
    bool in_stopped = input_queue_ ? input_queue_->is_stopped() : true;
    bool cmd_stopped = command_queue_ ? command_queue_->is_stopped() : true;
    LOG_CPP_INFO("[SourceProc:%s] Stopping... comp_joinable=%d input_joinable=%d in_q=%zu cmd_q=%zu in_stopped=%d cmd_stopped=%d", config_.instance_id.c_str(), comp_joinable ? 1 : 0, input_joinable ? 1 : 0, in_q, cmd_q, in_stopped ? 1 : 0, cmd_stopped ? 1 : 0);

    // Set the stop flag FIRST (used by loops)
    stop_flag_ = true; // Set the atomic flag

    // Notify condition variables/queues AFTER setting stop_flag_
    if(input_queue_) input_queue_->stop(); // Signal the input queue to stop blocking pop calls
    if(command_queue_) command_queue_->stop(); // Stop command queue as well

    // Joining of component_thread_ (which runs run()) happens here.
    if (component_thread_.joinable()) {
        try {
            component_thread_.join();
            LOG_CPP_INFO("[SourceProc:%s] Component thread joined.", config_.instance_id.c_str());
        } catch (const std::system_error& e) {
            LOG_CPP_ERROR("[SourceProc:%s] Error joining component thread: %s", config_.instance_id.c_str(), e.what());
        }
    } else {
         LOG_CPP_INFO("[SourceProc:%s] Component thread was not joinable in stop().", config_.instance_id.c_str());
    }
    // Joining of input/output threads happens in run() or destructor.
}


void SourceInputProcessor::process_commands() {
    PROFILE_FUNCTION();
    ControlCommand cmd;
    // Use try_pop for non-blocking check. Loop while queue is valid and not stopped.
    while (command_queue_ && !command_queue_->is_stopped() && command_queue_->try_pop(cmd)) {
        LOG_CPP_DEBUG("[SourceProc:%s] Processing command: %d", config_.instance_id.c_str(), static_cast<int>(cmd.type));

        std::lock_guard<std::mutex> lock(processor_config_mutex_);
        switch (cmd.type) {
            case CommandType::SET_VOLUME:
                current_volume_ = cmd.float_value;
                if (audio_processor_) {
                    audio_processor_->setVolume(current_volume_);
                }
                break;
            case CommandType::SET_EQ:
                if (cmd.eq_values.size() == EQ_BANDS) {
                    current_eq_ = cmd.eq_values;
                    if (audio_processor_) {
                        audio_processor_->setEqualizer(current_eq_.data());
                    }
                } else {
                    LOG_CPP_ERROR("[SourceProc:%s] Invalid EQ size in command: %zu", config_.instance_id.c_str(), cmd.eq_values.size());
                }
                break;
            case CommandType::SET_DELAY:
                current_delay_ms_ = cmd.int_value;
                LOG_CPP_DEBUG("[SourceProc:%s] SET_DELAY command processed. New delay: %dms. AudioManager should be notified.", config_.instance_id.c_str(), current_delay_ms_);
                break;
            case CommandType::SET_TIMESHIFT:
                current_timeshift_backshift_sec_config_ = cmd.float_value;
                LOG_CPP_DEBUG("[SourceProc:%s] SET_TIMESHIFT command processed. New timeshift: %.2fs. AudioManager should be notified.", config_.instance_id.c_str(), current_timeshift_backshift_sec_config_);
                break;
            case CommandType::SET_EQ_NORMALIZATION:
                if (audio_processor_) {
                    audio_processor_->setEqNormalization(cmd.int_value != 0);
                }
                break;
            case CommandType::SET_VOLUME_NORMALIZATION:
                if (audio_processor_) {
                    audio_processor_->setVolumeNormalization(cmd.int_value != 0);
                }
                break;
            case CommandType::SET_SPEAKER_MIX:
                current_speaker_layouts_map_[cmd.input_channel_key] = cmd.speaker_layout_for_key;
                if (audio_processor_) {
                    audio_processor_->update_speaker_layouts_config(current_speaker_layouts_map_);
                }
                LOG_CPP_DEBUG("[SourceProc:%s] SET_SPEAKER_MIX command processed for key: %d. Auto mode: %s",
                              config_.instance_id.c_str(), cmd.input_channel_key, (cmd.speaker_layout_for_key.auto_mode ? "true" : "false"));
                break;
            case CommandType::SET_PLAYBACK_RATE_SCALE: {
                double new_scale = static_cast<double>(cmd.float_value);
                if (!std::isfinite(new_scale) || new_scale <= 0.0) {
                    new_scale = 1.0;
                }
                new_scale = std::clamp(new_scale, kMinPlaybackRate, kMaxPlaybackRate);
                drain_playback_rate_scale_.store(new_scale, std::memory_order_relaxed);
                LOG_CPP_DEBUG("[SourceProc:%s] SET_PLAYBACK_RATE_SCALE command processed. New scale=%.6f",
                              config_.instance_id.c_str(), new_scale);
                break;
            }
            default:
                LOG_CPP_ERROR("[SourceProc:%s] Unknown command type received.", config_.instance_id.c_str());
                break;
        }
    }
}

void SourceInputProcessor::process_audio_chunk(const std::vector<uint8_t>& input_chunk_data) {
    PROFILE_FUNCTION();
    if (!audio_processor_) {
        LOG_CPP_ERROR("[SourceProc:%s] AudioProcessor not initialized. Cannot process chunk.", config_.instance_id.c_str());
        return;
    }
    const size_t input_bytes = input_chunk_data.size();
    LOG_CPP_DEBUG("[SourceProc:%s] ProcessAudio: Processing chunk. Input Size=%zu bytes. Expected=%zu bytes.",
                  config_.instance_id.c_str(), input_bytes, current_input_chunk_bytes_);
    if (input_bytes != current_input_chunk_bytes_) {
         LOG_CPP_ERROR("[SourceProc:%s] process_audio_chunk called with incorrect data size: %zu. Skipping processing.", config_.instance_id.c_str(), input_bytes);
         return;
    }
    
    // Allocate a temporary output buffer large enough to hold the maximum possible output
    // from AudioProcessor::processAudio. Match the size of AudioProcessor's internal processed_buffer.
    std::vector<int32_t> processor_output_buffer(current_input_chunk_bytes_ * MAX_CHANNELS * 4);

    int actual_samples_processed = 0;
    { // Lock mutex for accessing AudioProcessor
        std::lock_guard<std::mutex> lock(processor_config_mutex_);
        if (!audio_processor_) {
             LOG_CPP_ERROR("[SourceProc:%s] AudioProcessor is null during process_audio_chunk call.", config_.instance_id.c_str());
             return; // Cannot proceed without a valid processor
        }
        // Pass the data pointer and size (current_input_chunk_bytes_)
        // processAudio now returns the actual number of samples written to processor_output_buffer.data()
        actual_samples_processed = audio_processor_->processAudio(input_chunk_data.data(), processor_output_buffer.data());
    }

    if (actual_samples_processed > 0) {
        // Ensure we don't read past the actual size of the temporary buffer, although actual_samples_processed should be <= its size.
        size_t samples_to_insert = std::min(static_cast<size_t>(actual_samples_processed), processor_output_buffer.size());
        
        // Append the correctly processed samples to the internal process_buffer_
        try {
            process_buffer_.insert(process_buffer_.end(),
                                   processor_output_buffer.begin(),
                                   processor_output_buffer.begin() + samples_to_insert);
            profiling_peak_process_buffer_samples_ = std::max(profiling_peak_process_buffer_samples_, process_buffer_.size());
            size_t current_samples = process_buffer_.size();
            size_t observed_peak = m_process_buffer_high_water.load();
            while (current_samples > observed_peak &&
                   !m_process_buffer_high_water.compare_exchange_weak(observed_peak, current_samples)) {
                // retry CAS
            }
        } catch (const std::bad_alloc& e) {
             LOG_CPP_ERROR("[SourceProc:%s] Failed to insert into process_buffer_: %s", config_.instance_id.c_str(), e.what());
             // Handle allocation failure, maybe clear buffer or stop processing?
             process_buffer_.clear(); // Example: clear buffer to prevent further issues
             return;
        }
        LOG_CPP_DEBUG("[SourceProc:%s] ProcessAudio: Appended %zu samples. process_buffer_ size=%zu samples.", config_.instance_id.c_str(), samples_to_insert, process_buffer_.size());
    } else if (actual_samples_processed < 0) {
         // processAudio returned an error code (e.g., -1)
         LOG_CPP_ERROR("[SourceProc:%s] AudioProcessor::processAudio returned an error code: %d", config_.instance_id.c_str(), actual_samples_processed);
    } else {
         // processAudio returned 0 samples (e.g., no data processed or output buffer was null)
         LOG_CPP_DEBUG("[SourceProc:%s] ProcessAudio: AudioProcessor returned 0 samples.", config_.instance_id.c_str());
    }
}

void SourceInputProcessor::push_output_chunk_if_ready() {
    PROFILE_FUNCTION();
    // Check if we have enough samples for a full output chunk
    const size_t required_samples = compute_processed_chunk_samples(base_frames_per_chunk_, std::max(1, config_.output_channels));
    size_t current_buffer_size = process_buffer_.size();

    LOG_CPP_DEBUG("[SourceProc:%s] PushOutput: Checking buffer. Current=%zu samples. Required=%zu samples.", config_.instance_id.c_str(), current_buffer_size, required_samples);

    while (output_queue_ && current_buffer_size >= required_samples) { // Check queue pointer
        ProcessedAudioChunk output_chunk;
        // Copy the required number of samples
         output_chunk.audio_data.assign(process_buffer_.begin(), process_buffer_.begin() + required_samples);
         output_chunk.ssrcs = current_packet_ssrcs_;
         output_chunk.produced_time = std::chrono::steady_clock::now();
         output_chunk.origin_time = m_last_packet_origin_time;
         output_chunk.playback_rate = current_playback_rate_;
         size_t pushed_samples = output_chunk.audio_data.size();

         // Push to the output queue
         LOG_CPP_DEBUG("[SourceProc:%s] PushOutput: Pushing chunk with %zu samples (Expected=%zu) to Sink queue.",
                       config_.instance_id.c_str(), pushed_samples, required_samples);
         if (pushed_samples != required_samples) {
             LOG_CPP_ERROR("[SourceProc:%s] PushOutput: Mismatch between pushed samples (%zu) and required samples (%zu).", config_.instance_id.c_str(), pushed_samples, required_samples);
         }
         if (!output_queue_) {
             profiling_discarded_packets_++;
             m_total_discarded_packets++;
             LOG_CPP_WARNING("[SourceProc:%s] Output queue missing; dropping chunk.",
                             config_.instance_id.c_str());
             continue;
         }

         auto compute_chunk_cap = [this](double duration_ms, int sample_rate) -> std::size_t {
             if (duration_ms <= 0.0 || sample_rate <= 0) {
                 return 0;
             }
             const double chunk_duration_ms = (static_cast<double>(base_frames_per_chunk_) * 1000.0) /
                 static_cast<double>(sample_rate);
             if (chunk_duration_ms <= 0.0) {
                 return 0;
             }
             return static_cast<std::size_t>(std::ceil(duration_ms / chunk_duration_ms));
         };

         std::size_t configured_cap = 0;
         std::size_t min_chunks = 0;
         if (m_settings) {
             if (m_settings->mixer_tuning.max_input_queue_duration_ms > 0.0) {
                 configured_cap = compute_chunk_cap(
                     m_settings->mixer_tuning.max_input_queue_duration_ms,
                     config_.output_samplerate);
             } else {
                 configured_cap = m_settings->mixer_tuning.max_input_queue_chunks;
             }
             if (m_settings->mixer_tuning.min_input_queue_duration_ms > 0.0) {
                 min_chunks = compute_chunk_cap(
                     m_settings->mixer_tuning.min_input_queue_duration_ms,
                     config_.output_samplerate);
             } else {
                 min_chunks = m_settings->mixer_tuning.min_input_queue_chunks;
             }
             if (min_chunks == 0) {
                 min_chunks = 1;
             }
         }
         std::size_t dynamic_cap = compute_chunk_cap(
             m_settings ? m_settings->timeshift_tuning.target_buffer_level_ms : 0.0,
             config_.output_samplerate);
         if (dynamic_cap == 0) {
             dynamic_cap = 1;
         }
         if (dynamic_cap < min_chunks) {
             dynamic_cap = min_chunks;
         }

         std::size_t effective_cap = configured_cap > 0
             ? std::min(configured_cap, dynamic_cap)
             : dynamic_cap;
         if (effective_cap < min_chunks) {
             effective_cap = min_chunks;
         }

         if (effective_cap > 0) {
                static thread_local int trim_throttle = 0;
                bool logged_trim = false;
                while (output_queue_->size() >= effective_cap) {
                    ProcessedAudioChunk discarded_chunk;
                    if (!output_queue_->try_pop(discarded_chunk)) {
                        break;
                    }
                 profiling_discarded_packets_++;
                 m_total_discarded_packets++;
                    if (!logged_trim) {
                        if ((trim_throttle++ % 50) == 0) {
                            LOG_CPP_WARNING("[SourceProc:%s] Trimmed mixer queue to cap (%zu chunks).",
                                            config_.instance_id.c_str(), effective_cap);
                        }
                        logged_trim = true;
                    }
                }
            }

         auto push_result = (effective_cap > 0)
             ? output_queue_->push_bounded(std::move(output_chunk), effective_cap, false)
             : output_queue_->push_bounded(std::move(output_chunk), 0, false);

         if (push_result == OutputChunkQueue::PushResult::QueueStopped) {
             profiling_discarded_packets_++;
             m_total_discarded_packets++;
             LOG_CPP_WARNING("[SourceProc:%s] Output queue stopped; dropping chunk.",
                             config_.instance_id.c_str());
             continue;
         }

         if (push_result != OutputChunkQueue::PushResult::Pushed) {
             profiling_discarded_packets_++;
             m_total_discarded_packets++;
             LOG_CPP_WARNING("[SourceProc:%s] Failed to enqueue chunk (result=%d, cap=%zu).",
                             config_.instance_id.c_str(), static_cast<int>(push_result), effective_cap);
             continue;
         }

         profiling_chunks_pushed_++;
         m_total_chunks_pushed++;
         if (output_queue_) {
             size_t depth = output_queue_->size();
             size_t observed_hw = m_output_queue_high_water.load();
             while (depth > observed_hw && !m_output_queue_high_water.compare_exchange_weak(observed_hw, depth)) {
                 // retry
             }
         }

         // Remove the copied samples from the process buffer
        process_buffer_.erase(process_buffer_.begin(), process_buffer_.begin() + required_samples);
        current_buffer_size = process_buffer_.size(); // Update size after erasing

         LOG_CPP_DEBUG("[SourceProc:%s] PushOutput: Enqueued chunk. Remaining process_buffer_ size=%zu samples.",
                       config_.instance_id.c_str(), current_buffer_size);
    }
}

void SourceInputProcessor::reset_input_accumulator() {
    PROFILE_FUNCTION();
    input_accumulator_buffer_.clear();
    input_fragments_.clear();
    input_chunk_active_ = false;
    first_fragment_time_ = {};
    first_fragment_rtp_timestamp_.reset();
}

void SourceInputProcessor::append_to_input_accumulator(const TaggedAudioPacket& packet) {
    PROFILE_FUNCTION();
    if (current_input_chunk_bytes_ == 0 || input_bytes_per_frame_ == 0) {
        LOG_CPP_WARNING("[SourceProc:%s] Input accumulator not configured; dropping packet.",
                        config_.instance_id.c_str());
        return;
    }

    if (packet.audio_data.empty()) {
        return;
    }

    if ((packet.audio_data.size() % input_bytes_per_frame_) != 0) {
        LOG_CPP_ERROR("[SourceProc:%s] Packet payload not frame aligned (%zu bytes, frame=%zu). Resetting accumulator.",
                      config_.instance_id.c_str(),
                      packet.audio_data.size(),
                      input_bytes_per_frame_);
        reset_input_accumulator();
        profiling_discarded_packets_++;
        m_total_discarded_packets++;
        return;
    }

    if (!input_chunk_active_) {
        first_fragment_time_ = packet.received_time;
        first_fragment_rtp_timestamp_ = packet.rtp_timestamp;
        input_chunk_active_ = true;
    }

    input_accumulator_buffer_.insert(input_accumulator_buffer_.end(),
                                     packet.audio_data.begin(),
                                     packet.audio_data.end());

    InputFragmentMetadata meta;
    meta.bytes = packet.audio_data.size();
    meta.consumed_bytes = 0;
    meta.received_time = packet.received_time;
    meta.rtp_timestamp = packet.rtp_timestamp;
    meta.ssrcs = packet.ssrcs;
    input_fragments_.push_back(std::move(meta));
}

bool SourceInputProcessor::try_dequeue_input_chunk(std::vector<uint8_t>& chunk_data,
                                                   std::chrono::steady_clock::time_point& chunk_time,
                                                   std::optional<uint32_t>& chunk_timestamp,
                                                   std::vector<uint32_t>& chunk_ssrcs) {
    PROFILE_FUNCTION();
    if (current_input_chunk_bytes_ == 0 || input_bytes_per_frame_ == 0) {
        return false;
    }

    if (input_accumulator_buffer_.size() < current_input_chunk_bytes_) {
        return false;
    }

    chunk_time = {};
    chunk_timestamp.reset();
    chunk_ssrcs.clear();

    std::size_t remaining = current_input_chunk_bytes_;
    while (remaining > 0 && !input_fragments_.empty()) {
        auto& fragment = input_fragments_.front();
        if (fragment.consumed_bytes >= fragment.bytes) {
            input_fragments_.pop_front();
            continue;
        }

        if (chunk_time == std::chrono::steady_clock::time_point{}) {
            chunk_time = fragment.received_time;
            chunk_ssrcs = fragment.ssrcs;
            if (fragment.rtp_timestamp.has_value()) {
                const std::size_t frame_offset = fragment.consumed_bytes / input_bytes_per_frame_;
                chunk_timestamp = fragment.rtp_timestamp.value() + static_cast<uint32_t>(frame_offset);
            } else if (first_fragment_rtp_timestamp_.has_value()) {
                chunk_timestamp = first_fragment_rtp_timestamp_;
            }
        }

        const std::size_t available = fragment.bytes - fragment.consumed_bytes;
        const std::size_t take = std::min(available, remaining);
        fragment.consumed_bytes += take;
        remaining -= take;

        if (fragment.consumed_bytes == fragment.bytes) {
            input_fragments_.pop_front();
        }
    }

    if (chunk_time == std::chrono::steady_clock::time_point{}) {
        chunk_time = first_fragment_time_;
        if (chunk_time == std::chrono::steady_clock::time_point{}) {
            chunk_time = std::chrono::steady_clock::now();
        }
    }

    chunk_data.assign(
        input_accumulator_buffer_.begin(),
        input_accumulator_buffer_.begin() + static_cast<std::ptrdiff_t>(current_input_chunk_bytes_));
    input_accumulator_buffer_.erase(
        input_accumulator_buffer_.begin(),
        input_accumulator_buffer_.begin() + static_cast<std::ptrdiff_t>(current_input_chunk_bytes_));

    if (!input_fragments_.empty()) {
        input_chunk_active_ = true;
        const auto& head = input_fragments_.front();
        first_fragment_time_ = head.received_time;
        if (head.rtp_timestamp.has_value()) {
            const std::size_t frame_offset = head.consumed_bytes / input_bytes_per_frame_;
            first_fragment_rtp_timestamp_ = head.rtp_timestamp.value() + static_cast<uint32_t>(frame_offset);
        } else {
            first_fragment_rtp_timestamp_.reset();
        }
    } else {
        input_chunk_active_ = false;
        first_fragment_time_ = {};
        first_fragment_rtp_timestamp_.reset();
    }

    return true;
}

void SourceInputProcessor::reset_profiler_counters() {
    profiling_last_log_time_ = std::chrono::steady_clock::now();
    profiling_packets_received_ = 0;
    profiling_chunks_pushed_ = 0;
    profiling_discarded_packets_ = 0;
    profiling_processing_ns_ = 0;
    profiling_processing_samples_ = 0;
    profiling_peak_process_buffer_samples_ = process_buffer_.size();
    profiling_input_queue_sum_ = 0;
    profiling_output_queue_sum_ = 0;
    profiling_queue_samples_ = 0;
}

void SourceInputProcessor::maybe_log_profiler() {
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

    size_t current_process_buffer = process_buffer_.size();
    size_t input_queue_size = input_queue_ ? input_queue_->size() : 0;
    size_t output_queue_size = output_queue_ ? output_queue_->size() : 0;

    double avg_loop_ms = profiling_processing_samples_ > 0
                              ? (static_cast<double>(profiling_processing_ns_) / (1'000'000.0 * static_cast<double>(profiling_processing_samples_)))
                              : 0.0;
    double avg_input_queue = profiling_queue_samples_ > 0
                                 ? static_cast<double>(profiling_input_queue_sum_) / static_cast<double>(profiling_queue_samples_)
                                 : static_cast<double>(input_queue_size);
    double avg_output_queue = profiling_queue_samples_ > 0
                                  ? static_cast<double>(profiling_output_queue_sum_) / static_cast<double>(profiling_queue_samples_)
                                  : static_cast<double>(output_queue_size);

    LOG_CPP_INFO(
        "[Profiler][SourceProc:%s] packets=%llu chunks=%llu discarded=%llu avg_loop_ms=%.3f input_q(avg/current)=(%.2f/%zu) output_q(avg/current)=(%.2f/%zu) buffer_samples(current/peak)=(%zu/%zu)",
        config_.instance_id.c_str(),
        static_cast<unsigned long long>(profiling_packets_received_),
        static_cast<unsigned long long>(profiling_chunks_pushed_),
        static_cast<unsigned long long>(profiling_discarded_packets_),
        avg_loop_ms,
        avg_input_queue,
        input_queue_size,
        avg_output_queue,
        output_queue_size,
        current_process_buffer,
        profiling_peak_process_buffer_samples_);

    profiling_last_log_time_ = now;
    profiling_packets_received_ = 0;
    profiling_chunks_pushed_ = 0;
    profiling_discarded_packets_ = 0;
    profiling_processing_ns_ = 0;
    profiling_processing_samples_ = 0;
    profiling_peak_process_buffer_samples_ = current_process_buffer;
    profiling_input_queue_sum_ = 0;
    profiling_output_queue_sum_ = 0;
    profiling_queue_samples_ = 0;
}

void SourceInputProcessor::maybe_log_telemetry(std::chrono::steady_clock::time_point now) {
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

    const size_t input_q_size = input_queue_ ? input_queue_->size() : 0;
    const size_t output_q_size = output_queue_ ? output_queue_->size() : 0;
    const size_t process_buf_size = process_buffer_.size();

    const double input_q_ms = m_current_input_chunk_ms > 0.0
        ? static_cast<double>(input_q_size) * m_current_input_chunk_ms
        : 0.0;

    const double output_q_ms = m_current_output_chunk_ms > 0.0
        ? static_cast<double>(output_q_size) * m_current_output_chunk_ms
        : 0.0;

    double process_buf_ms = 0.0;
    if (config_.output_samplerate > 0) {
        const int output_channels = std::max(1, config_.output_channels);
        const double frames = static_cast<double>(process_buf_size) / static_cast<double>(output_channels);
        if (frames > 0.0) {
            process_buf_ms = (frames * 1000.0) / static_cast<double>(config_.output_samplerate);
        }
    }

    double last_packet_age_ms = 0.0;
    if (m_last_packet_time.time_since_epoch().count() != 0) {
        last_packet_age_ms = std::chrono::duration<double, std::milli>(now - m_last_packet_time).count();
        if (last_packet_age_ms < 0.0) {
            last_packet_age_ms = 0.0;
        }
    }

    double last_origin_age_ms = 0.0;
    if (m_last_packet_origin_time.time_since_epoch().count() != 0) {
        last_origin_age_ms = std::chrono::duration<double, std::milli>(now - m_last_packet_origin_time).count();
        if (last_origin_age_ms < 0.0) {
            last_origin_age_ms = 0.0;
        }
    }

    LOG_CPP_INFO(
        "[Telemetry][SourceProc:%s] input_q=%zu (%.3f ms) output_q=%zu (%.3f ms) process_buf_samples=%zu (%.3f ms) last_packet_age_ms=%.3f last_origin_age_ms=%.3f",
        config_.instance_id.c_str(),
        input_q_size,
        input_q_ms,
        output_q_size,
        output_q_ms,
        process_buf_size,
        process_buf_ms,
        last_packet_age_ms,
        last_origin_age_ms);
}

// --- New/Modified Thread Loops ---

void SourceInputProcessor::input_loop() {
    PROFILE_FUNCTION();
    LOG_CPP_INFO("[SourceProc:%s] Input loop started (receives timed packets).", config_.instance_id.c_str());
    TaggedAudioPacket timed_packet;
    while (!stop_flag_ && input_queue_ && input_queue_->pop(timed_packet)) {
        if (input_queue_) {
            size_t post_pop_depth = input_queue_->size();
            size_t estimated_peak = post_pop_depth + 1; // account for the packet we just popped
            size_t observed = m_input_queue_high_water.load();
            while (estimated_peak > observed && !m_input_queue_high_water.compare_exchange_weak(observed, estimated_peak)) {
                // retry CAS until updated
            }
        }

        m_total_packets_processed++;
        profiling_packets_received_++;
        auto loop_start = std::chrono::steady_clock::now();

        // --- Discontinuity Detection ---
        auto now = std::chrono::steady_clock::now();
        const long configured_discontinuity_ms =
            (m_settings && m_settings->source_processor_tuning.discontinuity_threshold_ms > 0)
                ? m_settings->source_processor_tuning.discontinuity_threshold_ms
                : 100;
        if (!m_is_first_packet_after_discontinuity) {
            auto time_since_last_packet = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_packet_time).count();
            if (time_since_last_packet > configured_discontinuity_ms) {
                LOG_CPP_WARNING("[SourceProc:%s] Audio discontinuity detected (%lld ms > %ld ms). Flushing filters.",
                                config_.instance_id.c_str(),
                                time_since_last_packet,
                                configured_discontinuity_ms);
                if (audio_processor_) {
                    audio_processor_->flushFilters();
                }
                reset_input_accumulator();
            }
        }
        m_last_packet_time = now;
        m_is_first_packet_after_discontinuity = false;

        const uint8_t* audio_payload_ptr = nullptr;
        size_t audio_payload_size = 0;
        
        bool packet_ok_for_processing = check_format_and_reconfigure(
            timed_packet,
            &audio_payload_ptr,
            &audio_payload_size
        );
        (void)audio_payload_ptr;
        (void)audio_payload_size;

        m_last_packet_origin_time = timed_packet.received_time;

        double requested_rate = timed_packet.playback_rate;
        if (!std::isfinite(requested_rate) || requested_rate <= 0.0) {
            requested_rate = 1.0;
        }
        requested_rate = std::clamp(requested_rate, kMinPlaybackRate, kMaxPlaybackRate);

        double drain_scale = drain_playback_rate_scale_.load(std::memory_order_relaxed);
        if (!std::isfinite(drain_scale) || drain_scale <= 0.0) {
            drain_scale = 1.0;
        }
        requested_rate *= drain_scale;
        requested_rate = std::clamp(requested_rate, kMinPlaybackRate, kMaxPlaybackRate);

        if (std::abs(requested_rate - current_playback_rate_) > kPlaybackRateEpsilon) {
            current_playback_rate_ = requested_rate;
            if (audio_processor_) {
                audio_processor_->set_playback_rate(current_playback_rate_);
            }
        }

        if (packet_ok_for_processing && audio_processor_) {
            append_to_input_accumulator(timed_packet);

            std::vector<uint8_t> chunk_data_for_processing;
            std::chrono::steady_clock::time_point chunk_origin{};
            std::optional<uint32_t> chunk_rtp;
            std::vector<uint32_t> chunk_ssrcs;

            while (try_dequeue_input_chunk(
                chunk_data_for_processing,
                chunk_origin,
                chunk_rtp,
                chunk_ssrcs)) {
                current_packet_ssrcs_ = chunk_ssrcs.empty() ? timed_packet.ssrcs : chunk_ssrcs;
                m_last_packet_origin_time = chunk_origin;

                process_audio_chunk(chunk_data_for_processing);
                push_output_chunk_if_ready();
                chunk_data_for_processing.clear();
            }
            (void)chunk_rtp;
        } else {
            profiling_discarded_packets_++;
            m_total_discarded_packets++;
            LOG_CPP_WARNING("[SourceProc:%s] Packet discarded by input_loop due to format/size issues or no audio processor.", config_.instance_id.c_str());
        }

        size_t input_queue_size_snapshot = input_queue_ ? input_queue_->size() : 0;
        size_t output_queue_size_snapshot = output_queue_ ? output_queue_->size() : 0;
        profiling_input_queue_sum_ += input_queue_size_snapshot;
        profiling_output_queue_sum_ += output_queue_size_snapshot;
        profiling_queue_samples_++;

        auto loop_end = std::chrono::steady_clock::now();
        profiling_processing_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(loop_end - loop_start).count();
        profiling_processing_samples_++;
        maybe_log_profiler();
        maybe_log_telemetry(loop_end);
    }
    LOG_CPP_INFO("[SourceProc:%s] Input loop exiting. StopFlag=%d", config_.instance_id.c_str(), stop_flag_.load());
}


bool SourceInputProcessor::check_format_and_reconfigure(
    const TaggedAudioPacket& packet,
    const uint8_t** out_audio_payload_ptr,
    size_t* out_audio_payload_size)
{
    PROFILE_FUNCTION();
    LOG_CPP_DEBUG("[SourceProc:%s] Entering check_format_and_reconfigure for packet from tag: %s",
                  config_.instance_id.c_str(), packet.source_tag.c_str());

    const int target_ap_input_channels = packet.channels;
    const int target_ap_input_samplerate = packet.sample_rate;
    const int target_ap_input_bitdepth = packet.bit_depth;
    const uint8_t* audio_data_start = packet.audio_data.data();
    const size_t audio_data_len = packet.audio_data.size();

    if (target_ap_input_channels <= 0 || target_ap_input_channels > 8 ||
        (target_ap_input_bitdepth != 8 && target_ap_input_bitdepth != 16 && target_ap_input_bitdepth != 24 && target_ap_input_bitdepth != 32) ||
        target_ap_input_samplerate <= 0) {
        LOG_CPP_ERROR("[SourceProc:%s] Invalid format info in packet. SR=%d, BD=%d, CH=%d",
                      config_.instance_id.c_str(), target_ap_input_samplerate, target_ap_input_bitdepth, target_ap_input_channels);
        return false;
    }

    const std::size_t bytes_per_frame =
        static_cast<std::size_t>(target_ap_input_channels) * static_cast<std::size_t>(target_ap_input_bitdepth / 8);
    if (bytes_per_frame == 0 || (audio_data_len % bytes_per_frame) != 0) {
        LOG_CPP_ERROR("[SourceProc:%s] Audio payload not frame aligned (payload=%zu bytes, frame=%zu).",
                      config_.instance_id.c_str(),
                      audio_data_len,
                      bytes_per_frame);
        return false;
    }

    const std::size_t expected_chunk_bytes =
        compute_chunk_size_bytes_for_format(base_frames_per_chunk_, target_ap_input_channels, target_ap_input_bitdepth);
    if (expected_chunk_bytes == 0 || (expected_chunk_bytes % bytes_per_frame) != 0) {
        LOG_CPP_ERROR("[SourceProc:%s] Unable to compute chunk size for incoming packet format.", config_.instance_id.c_str());
        return false;
    }

    const bool chunk_size_changed = current_input_chunk_bytes_ != expected_chunk_bytes;

    bool needs_reconfig = chunk_size_changed ||
                          !audio_processor_ ||
                          m_current_ap_input_channels != target_ap_input_channels ||
                          m_current_ap_input_samplerate != target_ap_input_samplerate ||
                          m_current_ap_input_bitdepth != target_ap_input_bitdepth;

    if (needs_reconfig) {
        if (audio_processor_) {
            LOG_CPP_WARNING("[SourceProc:%s] Audio format changed! Reconfiguring AudioProcessor. Old Format: CH=%d SR=%d BD=%d. New Format: CH=%d SR=%d BD=%d",
                            config_.instance_id.c_str(), m_current_ap_input_channels, m_current_ap_input_samplerate, m_current_ap_input_bitdepth,
                            target_ap_input_channels, target_ap_input_samplerate, target_ap_input_bitdepth);
        } else {
            LOG_CPP_INFO("[SourceProc:%s] Initializing AudioProcessor. Format: CH=%d SR=%d BD=%d",
                         config_.instance_id.c_str(), target_ap_input_channels, target_ap_input_samplerate, target_ap_input_bitdepth);
        }

        std::lock_guard<std::mutex> lock(processor_config_mutex_);
        LOG_CPP_INFO("[SourceProc:%s] Reconfiguring AudioProcessor: Input CH=%d SR=%d BD=%d -> Output CH=%d SR=%d",
                     config_.instance_id.c_str(), target_ap_input_channels, target_ap_input_samplerate, target_ap_input_bitdepth,
                     config_.output_channels, config_.output_samplerate);
        try {
            audio_processor_ = std::make_unique<AudioProcessor>(
                target_ap_input_channels,
                config_.output_channels,
                target_ap_input_bitdepth,
                target_ap_input_samplerate,
                config_.output_samplerate,
                current_volume_,
                current_speaker_layouts_map_,
                m_settings,
                expected_chunk_bytes);

            audio_processor_->setEqualizer(current_eq_.data());
            const double safe_rate = std::clamp(current_playback_rate_, kMinPlaybackRate, kMaxPlaybackRate);
            audio_processor_->set_playback_rate(safe_rate);

            m_current_ap_input_channels = target_ap_input_channels;
            m_current_ap_input_samplerate = target_ap_input_samplerate;
            m_current_ap_input_bitdepth = target_ap_input_bitdepth;
            current_input_chunk_bytes_ = expected_chunk_bytes;
            input_bytes_per_frame_ = bytes_per_frame;
            reset_input_accumulator();

            m_current_input_chunk_ms = 0.0;
            if (target_ap_input_samplerate > 0) {
                m_current_input_chunk_ms =
                    (static_cast<double>(base_frames_per_chunk_) * 1000.0) / static_cast<double>(target_ap_input_samplerate);
            }

            m_current_output_chunk_ms = 0.0;
            if (config_.output_samplerate > 0) {
                m_current_output_chunk_ms =
                    (static_cast<double>(base_frames_per_chunk_) * 1000.0) / static_cast<double>(config_.output_samplerate);
            }
            m_reconfigurations++;
            LOG_CPP_INFO("[SourceProc:%s] AudioProcessor reconfigured successfully.", config_.instance_id.c_str());
        } catch (const std::exception& e) {
            LOG_CPP_ERROR("[SourceProc:%s] Failed to reconfigure AudioProcessor: %s", config_.instance_id.c_str(), e.what());
            audio_processor_.reset();
            return false;
        }
    } else if (chunk_size_changed) {
        current_input_chunk_bytes_ = expected_chunk_bytes;
        input_bytes_per_frame_ = bytes_per_frame;
        reset_input_accumulator();
    }

    if (input_bytes_per_frame_ == 0) {
        input_bytes_per_frame_ = bytes_per_frame;
    }

    *out_audio_payload_ptr = audio_data_start;
    *out_audio_payload_size = audio_data_len;
    return true;
}

// run() is executed by component_thread_. It now starts only input_thread_ and processes commands.
void SourceInputProcessor::run() {
     PROFILE_FUNCTION();
     LOG_CPP_INFO("[SourceProc:%s] Component run() started.", config_.instance_id.c_str());

     // Launch input_thread_ (which now contains the main processing logic)
     try {
        input_thread_ = std::thread(&SourceInputProcessor::input_loop, this);
        LOG_CPP_INFO("[SourceProc:%s] Input thread launched by run().", config_.instance_id.c_str());
     } catch (const std::system_error& e) {
         LOG_CPP_ERROR("[SourceProc:%s] Failed to start input_thread_ from run(): %s", config_.instance_id.c_str(), e.what());
         stop_flag_ = true; // Signal stop if thread failed to launch
         if(input_queue_) input_queue_->stop();
         if(command_queue_) command_queue_->stop();
         return; // Exit run() if input_thread_ failed
     }

     // Command processing loop
     LOG_CPP_INFO("[SourceProc:%s] Starting command processing loop.", config_.instance_id.c_str());
     while (!stop_flag_) {
         process_commands(); // Check for commands

         // Sleep briefly to prevent busy-waiting when no commands are pending
         std::this_thread::sleep_for(std::chrono::milliseconds(m_settings->source_processor_tuning.command_loop_sleep_ms)); // Or use command_queue_->wait_for_data() if available
      }
     LOG_CPP_INFO("[SourceProc:%s] Command processing loop finished (stop signaled).", config_.instance_id.c_str());

     // --- Cleanup after stop_flag_ is set ---
     // Ensure input_thread_ is signaled (already done in stop()) and join it here.
     LOG_CPP_INFO("[SourceProc:%s] Joining input_thread_ in run()...", config_.instance_id.c_str());
     if (input_thread_.joinable()) {
         try {
             input_thread_.join();
             LOG_CPP_INFO("[SourceProc:%s] Input thread joined in run().", config_.instance_id.c_str());
         } catch (const std::system_error& e) {
             LOG_CPP_ERROR("[SourceProc:%s] Error joining input thread in run(): %s", config_.instance_id.c_str(), e.what());
         }
     }

     LOG_CPP_INFO("[SourceProc:%s] Component run() exiting.", config_.instance_id.c_str());
}
