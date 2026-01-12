#include "source_input_processor.h"
#include "../utils/cpp_logger.h"
#include <iostream> // For logging (cpp_logger fallback)
#include <stdexcept>
#include <cstring> // For memcpy
#include <algorithm> // For std::min, std::max
#include <cmath>     // For std::chrono durations
#include "../utils/profiler.h"
#include "../utils/thread_priority.h"
#include "../utils/sentinel_logging.h"

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
constexpr double kPlaybackRateEpsilon = 1e-6;  // Allow rate changes as small as 1 ppm
}

const std::chrono::milliseconds TIMESIFT_CLEANUP_INTERVAL(1000);

SourceInputProcessor::SourceInputProcessor(
    SourceProcessorConfig config, // config now includes instance_id
    std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings)
    : config_(std::move(config)), // Use std::move for config
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
    LOG_CPP_INFO("[SourceProc:%s] Destructor finished.", config_.instance_id.c_str());
}

// --- Getters ---

const std::string& SourceInputProcessor::get_source_tag() const {
    // This getter is needed by AudioManager to interact with RtpReceiver/SinkMixer
    // which might still rely on the original source tag (IP) until fully refactored.
    return config_.source_tag;
}

bool SourceInputProcessor::matches_source_tag(const std::string& actual_tag) const {
    if (config_.source_tag.empty()) {
        return false;
    }
    const bool config_is_wildcard = !config_.source_tag.empty() && config_.source_tag.back() == '*';
    if (!config_is_wildcard) {
        return actual_tag == config_.source_tag;
    }
    const std::string config_prefix = config_.source_tag.substr(0, config_.source_tag.size() - 1);
    return actual_tag.size() >= config_prefix.size() &&
        actual_tag.compare(0, config_prefix.size(), config_prefix) == 0;
}

SourceInputProcessorStats SourceInputProcessor::get_stats() {
    SourceInputProcessorStats stats;
    stats.total_packets_processed = m_total_packets_processed.load();
    stats.input_queue_size = 0;
    stats.output_queue_size = 0;
    stats.reconfigurations = m_reconfigurations.load();
    stats.input_queue_ms = 0.0;
    stats.output_queue_ms = 0.0;
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
    stats.output_queue_high_water = 0;
    stats.input_queue_high_water = 0;
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
    process_buffer_.clear();
    pending_sentinel_samples_ = 0;
    stop_flag_ = false;
    reset_profiler_counters();
    LOG_CPP_INFO("[SourceProc:%s] start(): now synchronous, no threads launched.", config_.instance_id.c_str());
}


void SourceInputProcessor::stop() {
    PROFILE_FUNCTION();
    stop_flag_ = true;
    LOG_CPP_INFO("[SourceProc:%s] stop(): synchronous processor stopped.", config_.instance_id.c_str());
}

void SourceInputProcessor::set_volume(float vol) {
    std::lock_guard<std::mutex> lock(processor_config_mutex_);
    current_volume_ = vol;
    if (audio_processor_) {
        audio_processor_->setVolume(current_volume_);
    }
}

void SourceInputProcessor::set_eq(const std::vector<float>& eq_values) {
    if (eq_values.size() != EQ_BANDS) {
        LOG_CPP_ERROR("[SourceProc:%s] set_eq called with invalid band count: %zu", config_.instance_id.c_str(), eq_values.size());
        return;
    }
    std::lock_guard<std::mutex> lock(processor_config_mutex_);
    current_eq_ = eq_values;
    if (audio_processor_) {
        audio_processor_->setEqualizer(current_eq_.data());
    }
}

void SourceInputProcessor::set_delay(int delay_ms) {
    current_delay_ms_ = delay_ms;
}

void SourceInputProcessor::set_timeshift(float timeshift_sec) {
    current_timeshift_backshift_sec_config_ = timeshift_sec;
}

void SourceInputProcessor::set_eq_normalization(bool enabled) {
    std::lock_guard<std::mutex> lock(processor_config_mutex_);
    eq_normalization_enabled_ = enabled;
    if (audio_processor_) {
        audio_processor_->setEqNormalization(enabled);
    }
}

void SourceInputProcessor::set_volume_normalization(bool enabled) {
    std::lock_guard<std::mutex> lock(processor_config_mutex_);
    volume_normalization_enabled_ = enabled;
    if (audio_processor_) {
        audio_processor_->setVolumeNormalization(enabled);
    }
}

void SourceInputProcessor::set_speaker_mix(int input_channel_key, const CppSpeakerLayout& layout) {
    std::lock_guard<std::mutex> lock(processor_config_mutex_);
    current_speaker_layouts_map_[input_channel_key] = layout;
    if (audio_processor_) {
        audio_processor_->update_speaker_layouts_config(current_speaker_layouts_map_);
    }
}

float SourceInputProcessor::get_current_volume() const {
    std::lock_guard<std::mutex> lock(processor_config_mutex_);
    return current_volume_;
}

std::vector<float> SourceInputProcessor::get_current_eq() const {
    std::lock_guard<std::mutex> lock(processor_config_mutex_);
    return current_eq_;
}

int SourceInputProcessor::get_current_delay_ms() const {
    std::lock_guard<std::mutex> lock(processor_config_mutex_);
    return current_delay_ms_;
}

float SourceInputProcessor::get_current_timeshift_sec() const {
    std::lock_guard<std::mutex> lock(processor_config_mutex_);
    return current_timeshift_backshift_sec_config_;
}

bool SourceInputProcessor::is_eq_normalization_enabled() const {
    std::lock_guard<std::mutex> lock(processor_config_mutex_);
    return eq_normalization_enabled_;
}

bool SourceInputProcessor::is_volume_normalization_enabled() const {
    std::lock_guard<std::mutex> lock(processor_config_mutex_);
    return volume_normalization_enabled_;
}

std::map<int, screamrouter::audio::CppSpeakerLayout> SourceInputProcessor::get_current_speaker_layouts() const {
    std::lock_guard<std::mutex> lock(processor_config_mutex_);
    return current_speaker_layouts_map_;
}

void SourceInputProcessor::apply_control_command(const ControlCommand& cmd) {
    switch (cmd.type) {
    case CommandType::SET_PLAYBACK_RATE_SCALE:
        // Ignored: playback rate now driven solely by timeshift manager.
        break;
    case CommandType::SET_VOLUME:
        set_volume(cmd.float_value);
        break;
    case CommandType::SET_EQ:
        set_eq(cmd.eq_values);
        break;
    case CommandType::SET_DELAY:
        set_delay(cmd.int_value);
        break;
    case CommandType::SET_TIMESHIFT:
        set_timeshift(cmd.float_value);
        break;
    case CommandType::SET_EQ_NORMALIZATION:
        set_eq_normalization(cmd.float_value != 0.0f);
        break;
    case CommandType::SET_VOLUME_NORMALIZATION:
        set_volume_normalization(cmd.float_value != 0.0f);
        break;
    case CommandType::SET_SPEAKER_MIX:
        set_speaker_mix(cmd.input_channel_key, cmd.speaker_layout_for_key);
        break;
    default:
        break;
    }
}

void SourceInputProcessor::ingest_packet(const TaggedAudioPacket& timed_packet, std::vector<ProcessedAudioChunk>& out_chunks) {
    PROFILE_FUNCTION();
    m_total_packets_processed++;
    profiling_packets_received_++;
    auto loop_start = std::chrono::steady_clock::now();
    utils::log_sentinel("sip_ingest", timed_packet, " [instance=" + config_.instance_id + "]");

    // --- Discontinuity Detection ---
    auto now = std::chrono::steady_clock::now();
    const long configured_discontinuity_ms =
        (m_settings && m_settings->source_processor_tuning.discontinuity_threshold_ms > 0)
            ? m_settings->source_processor_tuning.discontinuity_threshold_ms
            : 100;
    /*if (!m_is_first_packet_after_discontinuity) {
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
    }*/
    m_last_packet_time = now;
    m_is_first_packet_after_discontinuity = false;

    const uint8_t* audio_payload_ptr = nullptr;
    size_t audio_payload_size = 0;

    if (timed_packet.audio_data.empty()) {
        const auto now_empty = std::chrono::steady_clock::now();
        if (last_empty_packet_log_.time_since_epoch().count() == 0 ||
            now_empty - last_empty_packet_log_ >= std::chrono::milliseconds(500)) {
            LOG_CPP_WARNING("[SourceProc:%s] Received empty audio payload; ignoring.",
                            config_.instance_id.c_str());
            last_empty_packet_log_ = now_empty;
        }
        return;
    }

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
        bool chunk_is_sentinel = false;

        while (try_dequeue_input_chunk(
            chunk_data_for_processing,
            chunk_origin,
            chunk_rtp,
            chunk_ssrcs,
            chunk_is_sentinel)) {
            current_packet_ssrcs_ = chunk_ssrcs.empty() ? timed_packet.ssrcs : chunk_ssrcs;
            m_last_packet_origin_time = chunk_origin;

            if (chunk_is_sentinel) {
                ProcessedAudioChunk marker{};
                marker.is_sentinel = true;
                marker.origin_time = chunk_origin;
                utils::log_sentinel("sip_chunk_dequeued", marker, " [instance=" + config_.instance_id + "]");
            }

            process_audio_chunk(chunk_data_for_processing, chunk_is_sentinel);
            push_output_chunk_if_ready(out_chunks);
            chunk_data_for_processing.clear();
        }
        (void)chunk_rtp;
    } else {
        profiling_discarded_packets_++;
        m_total_discarded_packets++;
        LOG_CPP_WARNING("[SourceProc:%s] Packet discarded by ingest_packet due to format/size issues or no audio processor.", config_.instance_id.c_str());
    }

    auto loop_end = std::chrono::steady_clock::now();
    profiling_processing_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(loop_end - loop_start).count();
    profiling_processing_samples_++;
    maybe_log_profiler();
    maybe_log_telemetry(loop_end);
}

void SourceInputProcessor::process_audio_chunk(const std::vector<uint8_t>& input_chunk_data, bool is_sentinel_chunk) {
    PROFILE_FUNCTION();
    if (!audio_processor_) {
        LOG_CPP_ERROR("[SourceProc:%s] AudioProcessor not initialized. Cannot process chunk.", config_.instance_id.c_str());
        return;
    }
    const size_t input_bytes = input_chunk_data.size();
    LOG_CPP_DEBUG("[SourceProc:%s] ProcessAudio: Processing chunk. Input Size=%zu bytes (variable input resampling).",
                  config_.instance_id.c_str(), input_bytes);
    // Variable input resampling: input size varies based on playback_rate, no fixed size check
    
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
            if (is_sentinel_chunk && samples_to_insert > 0) {
                pending_sentinel_samples_ += samples_to_insert;
            }
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

void SourceInputProcessor::push_output_chunk_if_ready(std::vector<ProcessedAudioChunk>& out_chunks) {
    PROFILE_FUNCTION();
    // Check if we have enough samples for a full output chunk
    const size_t required_samples = compute_processed_chunk_samples(base_frames_per_chunk_, std::max(1, config_.output_channels));
    size_t current_buffer_size = process_buffer_.size();

    LOG_CPP_DEBUG("[SourceProc:%s] PushOutput: Checking buffer. Current=%zu samples. Required=%zu samples.", config_.instance_id.c_str(), current_buffer_size, required_samples);

    while (current_buffer_size >= required_samples) {
        ProcessedAudioChunk output_chunk;
        // Copy the required number of samples
         output_chunk.audio_data.assign(process_buffer_.begin(), process_buffer_.begin() + required_samples);
         output_chunk.ssrcs = current_packet_ssrcs_;
         output_chunk.produced_time = std::chrono::steady_clock::now();
         
         // Adjust origin_time for playback rate dilation
         // When rate > 1.0, we're consuming audio faster than real-time
         // Each chunk's nominal duration is stretched/compressed by playback_rate
         const double nominal_chunk_ms = (static_cast<double>(base_frames_per_chunk_) * 1000.0) / 
                                         static_cast<double>(config_.output_samplerate);
         // Time dilation: actual wall-clock time = nominal / rate
         // Accumulated shift = sum of (nominal - nominal/rate) = nominal * (1 - 1/rate)
         const double dilation_this_chunk_ms = nominal_chunk_ms * (1.0 - 1.0 / current_playback_rate_);
         cumulative_time_dilation_ms_ += dilation_this_chunk_ms;
         
         // Apply cumulative dilation to origin_time
         auto adjusted_origin = m_last_packet_origin_time + 
             std::chrono::microseconds(static_cast<int64_t>(cumulative_time_dilation_ms_ * 1000.0));
         output_chunk.origin_time = adjusted_origin;
         
         output_chunk.playback_rate = current_playback_rate_;
         output_chunk.is_sentinel = pending_sentinel_samples_ > 0;
         size_t pushed_samples = output_chunk.audio_data.size();
         if (pending_sentinel_samples_ > 0) {
             const std::size_t consumed = std::min<std::size_t>(pending_sentinel_samples_, pushed_samples);
             pending_sentinel_samples_ -= consumed;
         }
         utils::log_sentinel("sip_output_chunk", output_chunk, " [instance=" + config_.instance_id + "]");

         out_chunks.emplace_back(std::move(output_chunk));
         profiling_chunks_pushed_++;
         m_total_chunks_pushed++;

         // Remove the copied samples from the process buffer
        process_buffer_.erase(process_buffer_.begin(), process_buffer_.begin() + required_samples);
        current_buffer_size = process_buffer_.size(); // Update size after erasing

         LOG_CPP_DEBUG("[SourceProc:%s] PushOutput: Enqueued chunk. Remaining process_buffer_ size=%zu samples.",
                       config_.instance_id.c_str(), current_buffer_size);
    }
}

void SourceInputProcessor::reset_input_accumulator() {
    PROFILE_FUNCTION();
    input_ring_buffer_.clear();
    input_ring_base_offset_ = 0;
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

    input_ring_buffer_.write(packet.audio_data.data(), packet.audio_data.size());

    InputFragmentMetadata meta;
    meta.bytes = packet.audio_data.size();
    meta.consumed_bytes = 0;
    meta.received_time = packet.received_time;
    meta.rtp_timestamp = packet.rtp_timestamp;
    meta.ssrcs = packet.ssrcs;
    meta.is_sentinel = packet.is_sentinel;
    input_fragments_.push_back(std::move(meta));
    utils::log_sentinel("sip_append", packet, " [instance=" + config_.instance_id + "]");
}

bool SourceInputProcessor::try_dequeue_input_chunk(std::vector<uint8_t>& chunk_data,
                                                   std::chrono::steady_clock::time_point& chunk_time,
                                                   std::optional<uint32_t>& chunk_timestamp,
                                                   std::vector<uint32_t>& chunk_ssrcs,
                                                   bool& chunk_is_sentinel) {
    PROFILE_FUNCTION();
    if (current_input_chunk_bytes_ == 0 || input_bytes_per_frame_ == 0) {
        return false;
    }

    // ===== VARIABLE INPUT RESAMPLING =====
    // Calculate how many input frames we need to produce base_frames_per_chunk_ output frames
    // When playback_rate > 1.0: ratio increases, we need fewer input frames
    // When playback_rate < 1.0: ratio decreases, we need more input frames
    double resample_ratio = 1.0;
    if (m_current_ap_input_samplerate > 0 && config_.output_samplerate > 0) {
        resample_ratio = (static_cast<double>(config_.output_samplerate) / 
                          static_cast<double>(m_current_ap_input_samplerate)) * current_playback_rate_;
    }
    resample_ratio = std::max(0.1, std::min(10.0, resample_ratio));  // Clamp for safety
    
    // Input frames needed = output_frames / ratio
    // Add margin for libsamplerate's internal state
    const size_t target_output_frames = base_frames_per_chunk_;
    size_t required_input_frames = static_cast<size_t>(
        std::ceil(static_cast<double>(target_output_frames) / resample_ratio)) + 8;
    size_t required_input_bytes = required_input_frames * input_bytes_per_frame_;
    
    // Use the calculated variable input size instead of fixed current_input_chunk_bytes_
    if (input_ring_buffer_.size() < required_input_bytes) {
        return false;
    }

    chunk_time = {};
    chunk_timestamp.reset();
    chunk_ssrcs.clear();
    chunk_is_sentinel = false;

    chunk_data.resize(required_input_bytes);
    const std::size_t bytes_popped = input_ring_buffer_.pop(chunk_data.data(), required_input_bytes);
    if (bytes_popped != required_input_bytes) {
        LOG_CPP_ERROR("[SourceProc:%s] Ring buffer underflow while dequeuing chunk. Expected %zu, got %zu.",
                      config_.instance_id.c_str(), required_input_bytes, bytes_popped);
        chunk_data.clear();
        reset_input_accumulator();
        return false;
    }
    input_ring_base_offset_ += bytes_popped;

    // Track fragment consumption based on actual variable bytes popped
    std::size_t remaining = bytes_popped;  // Use actual bytes, not fixed chunk size
    while (remaining > 0 && !input_fragments_.empty()) {
        auto& fragment = input_fragments_.front();
        if (fragment.consumed_bytes >= fragment.bytes) {
            input_fragments_.pop_front();
            continue;
        }
        if (fragment.is_sentinel) {
            chunk_is_sentinel = true;
        }

        const std::size_t consumed_before = fragment.consumed_bytes;
        if (chunk_time == std::chrono::steady_clock::time_point{}) {
            chunk_time = fragment.received_time;
            chunk_ssrcs = fragment.ssrcs;
            if (fragment.rtp_timestamp.has_value()) {
                const std::size_t frame_offset = consumed_before / input_bytes_per_frame_;
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
    size_t input_queue_size = 0;
    size_t output_queue_size = 0;

    double avg_loop_ms = profiling_processing_samples_ > 0
                              ? (static_cast<double>(profiling_processing_ns_) / (1'000'000.0 * static_cast<double>(profiling_processing_samples_)))
                              : 0.0;
    double avg_input_queue = 0.0;
    double avg_output_queue = 0.0;

    LOG_CPP_INFO(
        "[Profiler][SourceProc:%s] packets=%llu chunks=%llu discarded=%llu avg_loop_ms=%.3f buffer_samples(current/peak)=(%zu/%zu)",
        config_.instance_id.c_str(),
        static_cast<unsigned long long>(profiling_packets_received_),
        static_cast<unsigned long long>(profiling_chunks_pushed_),
        static_cast<unsigned long long>(profiling_discarded_packets_),
        avg_loop_ms,
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

    const size_t process_buf_size = process_buffer_.size();

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
        "[Telemetry][SourceProc:%s] process_buf_samples=%zu (%.3f ms) last_packet_age_ms=%.3f last_origin_age_ms=%.3f",
        config_.instance_id.c_str(),
        process_buf_size,
        process_buf_ms,
        last_packet_age_ms,
        last_origin_age_ms);
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

void SourceInputProcessor::run() {
     LOG_CPP_INFO("[SourceProc:%s] run() called (no-op; synchronous processor).", config_.instance_id.c_str());
}
