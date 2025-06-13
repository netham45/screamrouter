#include "sink_audio_mixer.h"
#include "cpp_logger.h" // For new C++ logger
#include <iostream> // For logging (cpp_logger fallback)
#include <stdexcept>
#include <cstring> // For memcpy, memset
#include <vector>
#include <chrono>
#include <system_error> // For thread/socket errors
#include <algorithm> // For std::fill, std::max
#include <cmath>     // For std::chrono durations
#include "audio_processor.h" // Include the AudioProcessor header
#include "scream_sender.h"
#include "rtp_sender.h"

// Use namespaces for clarity
namespace screamrouter { namespace audio { using namespace utils; } }
using namespace screamrouter::audio;
using namespace screamrouter::utils;

// Include SIMD headers if available
#ifdef __SSE2__
#include <emmintrin.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif

// Old logger macros are removed. New macros (LOG_CPP_INFO, etc.) are in cpp_logger.h
// The sink_id from config_ will be manually prepended in the new log calls.

// Define how long to wait for input data before mixing silence/last known data
const std::chrono::milliseconds INPUT_WAIT_TIMEOUT(20); // e.g., 20ms
const int DEFAULT_MP3_BITRATE = 192; // Default bitrate if MP3 enabled


SinkAudioMixer::SinkAudioMixer(
    SinkMixerConfig config,
    std::shared_ptr<Mp3OutputQueue> mp3_output_queue)
    : config_(config),
      mp3_output_queue_(mp3_output_queue),
      lame_global_flags_(nullptr),
      lame_preprocessor_(nullptr),
      mixing_buffer_(SINK_MIXING_BUFFER_SAMPLES, 0),
      payload_buffer_(SINK_CHUNK_SIZE_BYTES * 2, 0),
      mp3_encode_buffer_(SINK_MP3_BUFFER_SIZE)
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
        LOG_CPP_INFO("[SinkMixer:%s] Creating RtpSender.", config_.sink_id.c_str());
        network_sender_ = std::make_unique<RtpSender>(config_);
    } else {
        LOG_CPP_INFO("[SinkMixer:%s] Creating ScreamSender.", config_.sink_id.c_str());
        network_sender_ = std::make_unique<ScreamSender>(config_);
    }

    if (!network_sender_) {
        LOG_CPP_ERROR("[SinkMixer:%s] Failed to create network sender.", config_.sink_id.c_str());
        throw std::runtime_error("Failed to create network sender.");
    }

    if (mp3_output_queue_) {
        // Instantiate the AudioProcessor for LAME preprocessing
        // Input: config_.output_channels, 32-bit, config_.output_samplerate
        // Output: 2 channels (Stereo), 32-bit, config_.output_samplerate
        lame_preprocessor_ = std::make_unique<AudioProcessor>(
            config_.output_channels,    // inputChannels
            2,                          // outputChannels (Stereo for LAME)
            32,                         // inputBitDepth (mixing_buffer_ is int32_t)
            config_.output_samplerate,  // inputSampleRate
            config_.output_samplerate,  // outputSampleRate
            1.0f,                       // volume
            std::map<int, CppSpeakerLayout>() // initial_layouts_config
        );
        if (!lame_preprocessor_) {
             LOG_CPP_ERROR("[SinkMixer:%s] Failed to create AudioProcessor for LAME preprocessing.", config_.sink_id.c_str());
        } else {
             LOG_CPP_INFO("[SinkMixer:%s] Created AudioProcessor for LAME preprocessing.", config_.sink_id.c_str());
        }
        initialize_lame();
    }

    LOG_CPP_INFO("[SinkMixer:%s] Initialization complete.", config_.sink_id.c_str());
}

SinkAudioMixer::~SinkAudioMixer() {
    if (!stop_flag_) {
        stop();
    }
    if (component_thread_.joinable()) {
        LOG_CPP_WARNING("[SinkMixer:%s] Warning: Joining thread in destructor, stop() might not have been called properly.", config_.sink_id.c_str());
        component_thread_.join();
    }
    // network_sender_ is a unique_ptr and will be cleaned up automatically.
}

void SinkAudioMixer::initialize_lame() {
    if (!mp3_output_queue_) return; // Don't initialize if not enabled

    LOG_CPP_INFO("[SinkMixer:%s] Initializing LAME MP3 encoder...", config_.sink_id.c_str());
    lame_global_flags_ = lame_init();
    if (!lame_global_flags_) {
        LOG_CPP_ERROR("[SinkMixer:%s] lame_init() failed.", config_.sink_id.c_str());
        return;
    }

    lame_set_in_samplerate(lame_global_flags_, config_.output_samplerate);
    // Matching c_utils: Rely on LAME defaults/inference for other parameters
    lame_set_VBR(lame_global_flags_, vbr_off); // Use CBR for streaming (matches c_utils)

    int ret = lame_init_params(lame_global_flags_);
    if (ret < 0) {
        LOG_CPP_ERROR("[SinkMixer:%s] lame_init_params() failed with code: %d", config_.sink_id.c_str(), ret);
        lame_close(lame_global_flags_);
        lame_global_flags_ = nullptr;
        return; // Return early if params init failed
    }
    lame_active_ = true; // Assume active initially
    LOG_CPP_INFO("[SinkMixer:%s] LAME initialized successfully.", config_.sink_id.c_str());
}

// Updated to use instance_id
void SinkAudioMixer::add_input_queue(const std::string& instance_id, std::shared_ptr<InputChunkQueue> queue) {
    if (!queue) {
        LOG_CPP_ERROR("[SinkMixer:%s] Attempted to add null input queue for instance: %s", config_.sink_id.c_str(), instance_id.c_str());
        return;
    }
    {
        std::lock_guard<std::mutex> lock(queues_mutex_);
        input_queues_[instance_id] = queue;
        input_active_state_[instance_id] = false; // Start as inactive
        // Initialize buffer for this source instance (e.g., with silence)
        source_buffers_[instance_id].audio_data.assign(SINK_MIXING_BUFFER_SAMPLES, 0); // Size is total samples, not channels * samples
        LOG_CPP_INFO("[SinkMixer:%s] Added input queue for source instance: %s", config_.sink_id.c_str(), instance_id.c_str());
    }
    input_cv_.notify_one(); // Notify run loop in case it was waiting with no sources
}

// Updated to use instance_id
void SinkAudioMixer::remove_input_queue(const std::string& instance_id) {
     {
        std::lock_guard<std::mutex> lock(queues_mutex_);
        input_queues_.erase(instance_id);
        input_active_state_.erase(instance_id);
        source_buffers_.erase(instance_id);
        LOG_CPP_INFO("[SinkMixer:%s] Removed input queue for source instance: %s", config_.sink_id.c_str(), instance_id.c_str());
    }
}


void SinkAudioMixer::start() {
    if (is_running()) {
        LOG_CPP_INFO("[SinkMixer:%s] Already running.", config_.sink_id.c_str());
        return;
    }
    LOG_CPP_INFO("[SinkMixer:%s] Starting...", config_.sink_id.c_str());
    stop_flag_ = false;
    payload_buffer_write_pos_ = 0;

    if (!network_sender_ || !network_sender_->setup()) {
        LOG_CPP_ERROR("[SinkMixer:%s] Network sender setup failed. Cannot start mixer thread.", config_.sink_id.c_str());
        return;
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
}

// Removed SinkAudioMixer::set_tcp_fd

bool SinkAudioMixer::wait_for_source_data(std::chrono::milliseconds /* ignored_timeout */) {
    // This function replicates the logic of mark_fds_active_inactive and
    // parts of handle_receive_buffers from the old c_utils version.
    // It determines which sources are active, gives lagging active sources a grace period,
    // marks persistently lagging sources as inactive, checks if inactive sources became active,
    // and attempts to pop data from all sources deemed ready for this cycle.

    std::lock_guard<std::mutex> lock(queues_mutex_); // Lock for the duration

    bool data_actually_popped_this_cycle = false;
    std::map<std::string, bool> ready_this_cycle; // Tracks sources ready in this specific cycle
    std::vector<std::string> lagging_active_sources; // Sources active but not immediately ready

    // --- Step 1: Initial Check (like first part of mark_fds_active_inactive) ---
    // Check all sources we *expect* to be active first, plus check inactive ones becoming active.
    LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Initial non-blocking check...", config_.sink_id.c_str());
    // Use instance_id as the key/loop variable
    for (auto const& [instance_id, queue_ptr] : input_queues_) {
        ProcessedAudioChunk chunk;
        // Check active state using instance_id
        bool previously_active = input_active_state_.count(instance_id) ? input_active_state_[instance_id] : false;

        if (queue_ptr->try_pop(chunk)) { // Data is immediately available
             if (chunk.audio_data.size() != SINK_MIXING_BUFFER_SAMPLES) {
                 LOG_CPP_ERROR("[SinkMixer:%s] WaitForData: Received chunk from instance %s with unexpected sample count: %zu. Discarding.", config_.sink_id.c_str(), instance_id.c_str(), chunk.audio_data.size());
                 ready_this_cycle[instance_id] = false; // Not ready with valid data
                 // Don't mark active if data is invalid
             } else {
                 LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Pop SUCCESS (Initial) for instance %s", config_.sink_id.c_str(), instance_id.c_str());
                 source_buffers_[instance_id] = std::move(chunk); // Store valid chunk using instance_id
                 ready_this_cycle[instance_id] = true;
                 data_actually_popped_this_cycle = true;
                 if (!previously_active) {
                     LOG_CPP_INFO("[SinkMixer:%s] Input instance %s became active", config_.sink_id.c_str(), instance_id.c_str());
                 }
                 input_active_state_[instance_id] = true; // Mark/confirm as active using instance_id
             }
        } else { // No data immediately available
            ready_this_cycle[instance_id] = false;
            if (previously_active) {
                // This source *was* active, but doesn't have data right now. Add to lagging list.
                LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Pop FAILED (Initial) for ACTIVE instance %s. Adding to grace period check.", config_.sink_id.c_str(), instance_id.c_str());
                lagging_active_sources.push_back(instance_id); // Add instance_id to lagging list
            } else {
                 // Source was inactive and still has no data. Keep it inactive.
                 input_active_state_[instance_id] = false;
            }
        }
    }

    // --- Step 2 & 3: Grace Period for Lagging Active Sources ---
    if (!lagging_active_sources.empty()) {
        LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Entering grace period check for %zu sources.", config_.sink_id.c_str(), lagging_active_sources.size());
        auto grace_period_start = std::chrono::steady_clock::now();
        long elapsed_us = 0;

        while (!lagging_active_sources.empty() && elapsed_us <= GRACE_PERIOD_TIMEOUT.count() * 1000) {
             // Polling with short sleep - less efficient than select/CV but mimics the old logic structure
            std::this_thread::sleep_for(GRACE_PERIOD_POLL_INTERVAL);

            // Check remaining lagging sources
            for (auto it = lagging_active_sources.begin(); it != lagging_active_sources.end(); /* manually increment */) {
                const std::string& instance_id = *it; // Use instance_id
                auto queue_it = input_queues_.find(instance_id); // Find queue by instance_id
                if (queue_it == input_queues_.end()) { // Should not happen
                    it = lagging_active_sources.erase(it);
                    continue;
                }

                ProcessedAudioChunk chunk;
                if (queue_it->second->try_pop(chunk)) {
                    if (chunk.audio_data.size() != SINK_MIXING_BUFFER_SAMPLES) {
                         LOG_CPP_ERROR("[SinkMixer:%s] WaitForData: Received chunk (Grace Period) from instance %s with unexpected sample count: %zu. Discarding.", config_.sink_id.c_str(), instance_id.c_str(), chunk.audio_data.size());
                         // Still remove from lagging list, but don't mark ready
                    } else {
                        LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Pop SUCCESS (Grace Period) for instance %s", config_.sink_id.c_str(), instance_id.c_str());
                        source_buffers_[instance_id] = std::move(chunk); // Use instance_id
                        ready_this_cycle[instance_id] = true;
                        data_actually_popped_this_cycle = true;
                    }
                    it = lagging_active_sources.erase(it); // Remove from lagging list
                } else {
                    ++it; // Move to next lagging source
                }
            }
            elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - grace_period_start).count();
        }

        // --- Step 3 Continued: Mark remaining lagged sources as inactive ---
        if (!lagging_active_sources.empty()) {
            LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Grace period ended. %zu instances still lagging.", config_.sink_id.c_str(), lagging_active_sources.size());
            for (const auto& instance_id : lagging_active_sources) { // Use instance_id
                if (input_active_state_.count(instance_id) && input_active_state_[instance_id]) {
                     LOG_CPP_INFO("[SinkMixer:%s] Input instance %s timed out grace period, marking inactive.", config_.sink_id.c_str(), instance_id.c_str());
                     input_active_state_[instance_id] = false; // Mark as definitively inactive using instance_id
                }
            }
        } else {
             LOG_CPP_DEBUG("[SinkMixer:%s] WaitForData: Grace period ended. All lagging sources caught up.", config_.sink_id.c_str());
        }
    } // End grace period check


    // --- Final Result ---
    // The mixing logic will use `input_active_state_` and `source_buffers_`.
    // We need to return whether *any* data was successfully retrieved and stored
    // in `source_buffers_` during this call to signal the run loop to mix.

    // Update the internal state map for mixing based on who was ready *this cycle*
    // (We already updated the persistent input_active_state_ above)
    for(auto const& [instance_id, is_ready] : ready_this_cycle) { // Use instance_id
        // This map is implicitly used by mix_buffers which checks source_buffers_ map.
        // If ready_this_cycle[id] is false, mix_buffers won't find a *new* chunk for it,
        // but might mix old data if we didn't clear source_buffers_. Let's clear
        // buffers for sources not ready this cycle.
         if (!is_ready && source_buffers_.count(instance_id)) { // Use instance_id
             // Optional: Decide if you want to mix stale data or silence for sources
             // that weren't ready this cycle. Clearing ensures silence/no contribution.
             // source_buffers_.erase(instance_id); // To ensure no stale data is mixed
             // Or fill with silence:
             // source_buffers_[source_id].audio_data.assign(SINK_MIXING_BUFFER_SAMPLES, 0);
         }
    }

    return data_actually_popped_this_cycle;
}


void SinkAudioMixer::mix_buffers() {
    // Assumes queues_mutex_ is held by the caller (run loop)
    // Clear the mixing buffer (vector of int32_t) before accumulating samples
    std::fill(mixing_buffer_.begin(), mixing_buffer_.end(), 0);
    
    std::vector<uint32_t> collected_csrcs;
    size_t active_source_count = 0;

    // The total number of samples to mix is fixed by the mixing_buffer_ size
    size_t total_samples_to_mix = mixing_buffer_.size(); // Should be SINK_MIXING_BUFFER_SAMPLES (e.g., 576)
    LOG_CPP_DEBUG("[SinkMixer:%s] MixBuffers: Starting mix. Target samples=%zu (Mixing buffer size).", config_.sink_id.c_str(), total_samples_to_mix);


    for (auto const& [instance_id, is_active] : input_active_state_) { // Iterate through all potential sources using instance_id
        if (is_active) { // Only process sources marked active in wait_for_source_data
            active_source_count++;
            // Check if the source buffer actually exists in the map (should always exist if active)
            auto buf_it = source_buffers_.find(instance_id); // Find buffer by instance_id
            if (buf_it == source_buffers_.end()) {
                 LOG_CPP_ERROR("[SinkMixer:%s] Mixing error: Source buffer not found for active instance %s", config_.sink_id.c_str(), instance_id.c_str());
                 continue; // Skip this source
            }
            const auto& source_data = buf_it->second.audio_data; // Use iterator
            const auto& ssrcs = buf_it->second.ssrcs;
            collected_csrcs.insert(collected_csrcs.end(), ssrcs.begin(), ssrcs.end());
 
             size_t samples_in_source = source_data.size(); // Get actual sample count from the stored chunk
             LOG_CPP_DEBUG("[SinkMixer:%s] MixBuffers: Mixing instance %s. Source samples=%zu. Expected=%zu.", config_.sink_id.c_str(), instance_id.c_str(), samples_in_source, total_samples_to_mix);

            // *** Check source data size against the fixed mixing buffer size ***
            // This check is redundant if wait_for_source_data correctly discards invalid chunks, but kept for safety.
            if (samples_in_source != total_samples_to_mix) {
                 LOG_CPP_ERROR("[SinkMixer:%s] MixBuffers: Source buffer for instance %s size mismatch! Expected %zu, got %zu. Skipping source.", config_.sink_id.c_str(), instance_id.c_str(), total_samples_to_mix, samples_in_source);
                 continue; // Skip this source
            }

            // Now we expect samples_in_source == total_samples_to_mix
            LOG_CPP_DEBUG("[SinkMixer:%s] MixBuffers: Accumulating %zu samples from instance %s", config_.sink_id.c_str(), total_samples_to_mix, instance_id.c_str());

            // *** Iterate over the entire mixing buffer size ***
            for (size_t i = 0; i < total_samples_to_mix; ++i) {
                // Accessing mixing_buffer_[i]. Size is total_samples_to_mix. Safe.
                // Accessing source_data[i]. Size >= total_samples_to_mix. Safe.
                int64_t sum = static_cast<int64_t>(mixing_buffer_[i]) + source_data[i];
                // Clamp the sum to 32-bit integer range
                if (sum > INT32_MAX) {
                    mixing_buffer_[i] = INT32_MAX;
                } else if (sum < INT32_MIN) { // Check against INT32_MIN for underflow
                    mixing_buffer_[i] = INT32_MIN;
                } else {
                    mixing_buffer_[i] = static_cast<int32_t>(sum);
                }
            }
        } // end if(is_active)
    } // end for loop over sources
    
    // Store the unique CSRCs
    std::sort(collected_csrcs.begin(), collected_csrcs.end());
    collected_csrcs.erase(std::unique(collected_csrcs.begin(), collected_csrcs.end()), collected_csrcs.end());
    
    {
        std::lock_guard<std::mutex> lock(csrc_mutex_);
        current_csrcs_ = collected_csrcs;
    }

    LOG_CPP_DEBUG("[SinkMixer:%s] MixBuffers: Mix complete. Mixed %zu active sources into mixing_buffer_ (%zu samples).", config_.sink_id.c_str(), active_source_count, total_samples_to_mix);
}


// Restored original bit-shifting logic (copies MSBs)
void SinkAudioMixer::downscale_buffer() {
    // Converts 32-bit mixing_buffer_ to target bit depth into payload_buffer_
    size_t output_byte_depth = config_.output_bitdepth / 8; // Bytes per sample in target format (e.g., 16-bit -> 2 bytes)
    // Calculate the number of samples based on the mixing buffer size (int32_t)
    size_t samples_to_convert = mixing_buffer_.size(); // Should be SINK_MIXING_BUFFER_SAMPLES (e.g., 576)

    // Calculate the total number of bytes this conversion *should* produce
    size_t expected_bytes_to_write = samples_to_convert * output_byte_depth; // e.g., 576 samples * 2 bytes/sample = 1152 bytes for 16-bit stereo
    LOG_CPP_DEBUG("[SinkMixer:%s] Downscale: Converting %zu samples (int32) to %d-bit. Expected output bytes=%zu.",
                  config_.sink_id.c_str(), samples_to_convert, config_.output_bitdepth, expected_bytes_to_write);


    // Ensure we don't write past the end of the payload buffer's allocated space
    size_t available_space = payload_buffer_.size() - payload_buffer_write_pos_;

    if (expected_bytes_to_write > available_space) {
        LOG_CPP_ERROR("[SinkMixer:%s] Downscale buffer overflow detected! Available space=%zu, needed=%zu. WritePos=%zu. BufferSize=%zu",
                      config_.sink_id.c_str(), available_space, expected_bytes_to_write, payload_buffer_write_pos_, payload_buffer_.size());
        // Limit the operation to prevent overflow, but data will be lost/corrupted.
        // Calculate how many full samples can fit in the available space.
        size_t max_samples_possible = available_space / output_byte_depth;
        samples_to_convert = max_samples_possible; // Limit samples
        expected_bytes_to_write = samples_to_convert * output_byte_depth; // Adjust expected bytes accordingly
        LOG_CPP_ERROR("[SinkMixer:%s] Downscale: Limiting conversion to %zu samples (%zu bytes) due to space limit.",
                      config_.sink_id.c_str(), samples_to_convert, expected_bytes_to_write);
        if (samples_to_convert == 0) {
             LOG_CPP_ERROR("[SinkMixer:%s] Downscale buffer has no space left. available=%zu", config_.sink_id.c_str(), available_space);
             return; // Nothing can be written
        }
    }

    // Get pointers for reading (from mixing_buffer_) and writing (to payload_buffer_)
    uint8_t* write_ptr_start = payload_buffer_.data() + payload_buffer_write_pos_;
    uint8_t* write_ptr = write_ptr_start;
    const int32_t* read_ptr = mixing_buffer_.data(); // Read from the start of the 32-bit mixed buffer

    // Perform the conversion using bit-shifting
    for (size_t i = 0; i < samples_to_convert; ++i) {
        int32_t sample = read_ptr[i]; // Read the full 32-bit sample
        switch (config_.output_bitdepth) { // Convert based on the sink's configured output bit depth
            case 16: // Target: 16-bit
                // Writes MSB first (e.g., AA), then next MSB (e.g., BB) assuming network byte order matters?
                // Let's stick to the original order: BB then AA for 0xAABBCCDD
                // Write 2 bytes for 16-bit output
                *write_ptr++ = static_cast<uint8_t>((sample >> 16) & 0xFF); // BB (Byte 1)
                *write_ptr++ = static_cast<uint8_t>((sample >> 24) & 0xFF); // AA (Byte 0)
                break;
            case 24: // Target: 24-bit
                 // Original order: CC, BB, AA for 0xAABBCCDD
                 // Write 3 bytes for 24-bit output
                *write_ptr++ = static_cast<uint8_t>((sample >> 8) & 0xFF);  // CC (Byte 2)
                *write_ptr++ = static_cast<uint8_t>((sample >> 16) & 0xFF); // BB (Byte 1)
                *write_ptr++ = static_cast<uint8_t>((sample >> 24) & 0xFF); // AA (Byte 0)
                break;
            case 32: // Target: 32-bit
                 // Original order: DD, CC, BB, AA for 0xAABBCCDD
                 // Write 4 bytes for 32-bit output
                *write_ptr++ = static_cast<uint8_t>((sample) & 0xFF);       // DD (Byte 3)
                *write_ptr++ = static_cast<uint8_t>((sample >> 8) & 0xFF);  // CC (Byte 2)
                *write_ptr++ = static_cast<uint8_t>((sample >> 16) & 0xFF); // BB (Byte 1)
                *write_ptr++ = static_cast<uint8_t>((sample >> 24) & 0xFF); // AA (Byte 0)
                break;
             // Note: Bit depth 8 is not handled here, but was validated in constructor
        }
    }
    // Calculate bytes actually written based on pointer difference
    size_t bytes_written = write_ptr - write_ptr_start;
    LOG_CPP_DEBUG("[SinkMixer:%s] Downscale: Conversion loop finished. Bytes written=%zu. Expected=%zu.",
                  config_.sink_id.c_str(), bytes_written, expected_bytes_to_write);
    if (bytes_written != expected_bytes_to_write) {
         LOG_CPP_ERROR("[SinkMixer:%s] Downscale: Mismatch between bytes written (%zu) and expected bytes (%zu).",
                       config_.sink_id.c_str(), bytes_written, expected_bytes_to_write);
    }

    // Update the write position in the payload_buffer_
    payload_buffer_write_pos_ += bytes_written;
    LOG_CPP_DEBUG("[SinkMixer:%s] Downscale complete. payload_buffer_write_pos_=%zu", config_.sink_id.c_str(), payload_buffer_write_pos_);
}


void SinkAudioMixer::encode_and_push_mp3() {
    if (!mp3_output_queue_ || !lame_global_flags_ || !lame_preprocessor_) {
        // MP3 output not enabled, LAME not initialized, or preprocessor failed to initialize
        return;
    }

    // Check if the MP3 queue is being consumed (simple check based on size)
    // A more robust check might involve feedback from the consumer.
    // This approximates the old select() check on the write FD.
    if (mp3_output_queue_->size() > 10) { // If queue is backing up, assume inactive consumer
        if (lame_active_) {
            LOG_CPP_INFO("[SinkMixer:%s] MP3 output queue full, pausing encoding.", config_.sink_id.c_str());
            lame_active_ = false;
        }
        return; // Don't encode if consumer likely inactive
    } else {
         if (!lame_active_) {
             LOG_CPP_INFO("[SinkMixer:%s] MP3 output queue draining, resuming encoding.", config_.sink_id.c_str());
             lame_active_ = true;
         }
    }

    // If LAME encoding is paused due to full queue, don't proceed
    if (!lame_active_) {
        return;
    }

    // 1. Preprocess the mixed buffer using AudioProcessor.
    //    lame_preprocessor_ (AudioProcessor) consumes SINK_CHUNK_SIZE_BYTES from the 32-bit mixing_buffer_ per call.
    //    It outputs SINK_MIXING_BUFFER_SAMPLES stereo frames (32-bit).
    
    // Output buffer for preprocessor: SINK_MIXING_BUFFER_SAMPLES stereo frames * 2 channels * sizeof(int32_t)
    std::vector<int32_t> stereo_int32_buffer(SINK_MIXING_BUFFER_SAMPLES * 2); // Holds 576 stereo frames = 1152 int32_t samples

    const size_t total_bytes_in_mixing_buffer = mixing_buffer_.size() * sizeof(int32_t); // mixing_buffer_ has SINK_MIXING_BUFFER_SAMPLES (576) int32_t samples = 2304 bytes
    const size_t input_chunk_bytes_for_preprocessor = SINK_CHUNK_SIZE_BYTES; // 1152 bytes

    if (input_chunk_bytes_for_preprocessor == 0) {
        LOG_CPP_ERROR("[SinkMixer:%s] MP3 Encode: input_chunk_bytes_for_preprocessor is zero. This should not happen.", config_.sink_id.c_str());
        return;
    }
    
    size_t current_byte_offset = 0;
    while (current_byte_offset + input_chunk_bytes_for_preprocessor <= total_bytes_in_mixing_buffer) {
        // Check queue status at the beginning of processing each chunk
        if (mp3_output_queue_->size() > 10) { // If queue is backing up
            if (lame_active_) {
                LOG_CPP_INFO("[SinkMixer:%s] MP3 output queue full, pausing encoding. Processed %zu bytes out of %zu",
                             config_.sink_id.c_str(), current_byte_offset, total_bytes_in_mixing_buffer);
                lame_active_ = false;
            }
            return; // Stop processing more chunks in this call
        } else {
            if (!lame_active_) {
                LOG_CPP_INFO("[SinkMixer:%s] MP3 output queue draining, resuming encoding.", config_.sink_id.c_str());
                lame_active_ = true;
            }
        }
        // If LAME encoding was paused and queue is still full, lame_active_ will be false.
        if (!lame_active_) {
             LOG_CPP_DEBUG("[SinkMixer:%s] MP3 encoding paused, skipping chunk at offset %zu", config_.sink_id.c_str(), current_byte_offset);
             return;
        }

        LOG_CPP_DEBUG("[SinkMixer:%s] MP3 Encode: Processing chunk from mixing_buffer_ at offset %zu with size %zu",
                      config_.sink_id.c_str(), current_byte_offset, input_chunk_bytes_for_preprocessor);
        const uint8_t* input_chunk_ptr = reinterpret_cast<const uint8_t*>(mixing_buffer_.data()) + current_byte_offset;

        // lame_preprocessor_->processAudio consumes input_chunk_bytes_for_preprocessor
        // and produces SINK_MIXING_BUFFER_SAMPLES stereo frames into stereo_int32_buffer.
        int processed_total_stereo_samples = lame_preprocessor_->processAudio(
            input_chunk_ptr,
            stereo_int32_buffer.data()
        );

        if (processed_total_stereo_samples <= 0) {
            LOG_CPP_ERROR("[SinkMixer:%s] AudioProcessor failed to process audio for LAME. Offset: %zu. Samples processed: %d",
                          config_.sink_id.c_str(), current_byte_offset, processed_total_stereo_samples);
            break; // Stop processing further chunks if an error occurs
        }
        
        // AudioProcessor should output SINK_MIXING_BUFFER_SAMPLES stereo frames, which is SINK_MIXING_BUFFER_SAMPLES * 2 total int32_t samples.
        if (static_cast<size_t>(processed_total_stereo_samples) != stereo_int32_buffer.size()) {
            LOG_CPP_WARNING("[SinkMixer:%s] AudioProcessor output %d stereo samples, but buffer was sized for %zu. Using actual count for LAME.",
                            config_.sink_id.c_str(), processed_total_stereo_samples, stereo_int32_buffer.size());
        }
        LOG_CPP_DEBUG("[SinkMixer:%s] AudioProcessor produced %d total stereo int32 samples for LAME from offset %zu",
                      config_.sink_id.c_str(), processed_total_stereo_samples, current_byte_offset);

        // 2. NO CONVERSION NEEDED: lame_encode_buffer_interleaved_int takes int32_t directly.

        // 3. Encode the stereo int32_t buffer using LAME
        // LAME expects frames per channel.
        int processed_frames_per_channel = processed_total_stereo_samples / 2; // If 1152 samples (576 stereo frames) -> 576 frames/channel
        LOG_CPP_DEBUG("[SinkMixer:%s] Processed frames per channel for LAME: %d", config_.sink_id.c_str(), processed_frames_per_channel);

        size_t required_mp3_buffer_size = static_cast<size_t>(1.25 * processed_frames_per_channel + 7200);
        if (mp3_encode_buffer_.size() < required_mp3_buffer_size) {
            LOG_CPP_WARNING("[SinkMixer:%s] MP3 encode buffer might be too small for %d frames. Current size: %zu, Recommended: %zu. Resizing.",
                            config_.sink_id.c_str(), processed_frames_per_channel, mp3_encode_buffer_.size(), required_mp3_buffer_size);
            mp3_encode_buffer_.resize(required_mp3_buffer_size);
        }
        
        // Ensure stereo_int32_buffer has enough samples for the processed frames.
        // This check is more of a sanity check as processed_frames_per_channel is derived from processed_total_stereo_samples.
        if (stereo_int32_buffer.size() < static_cast<size_t>(processed_total_stereo_samples)) {
             LOG_CPP_ERROR("[SinkMixer:%s] Internal error: stereo_int32_buffer too small. Has: %zu, Needs: %d",
                           config_.sink_id.c_str(), stereo_int32_buffer.size(), processed_total_stereo_samples);
             break;
        }

        int mp3_bytes_encoded = lame_encode_buffer_interleaved_int(
            lame_global_flags_,
            stereo_int32_buffer.data(),
            processed_frames_per_channel,
            mp3_encode_buffer_.data(),
            static_cast<int>(mp3_encode_buffer_.size())
        );

        if (mp3_bytes_encoded < 0) {
            LOG_CPP_ERROR("[SinkMixer:%s] LAME encoding failed with code: %d for chunk at offset %zu",
                          config_.sink_id.c_str(), mp3_bytes_encoded, current_byte_offset);
            break;
        } else if (mp3_bytes_encoded > 0) {
            EncodedMP3Data mp3_data;
            mp3_data.mp3_data.assign(mp3_encode_buffer_.begin(), mp3_encode_buffer_.begin() + mp3_bytes_encoded);
            mp3_output_queue_->push(std::move(mp3_data));
        }
        
        current_byte_offset += input_chunk_bytes_for_preprocessor;
    }
    
    if (current_byte_offset < total_bytes_in_mixing_buffer) {
        LOG_CPP_DEBUG("[SinkMixer:%s] MP3 Encode: %zu bytes remaining in mixing_buffer_ after processing. (Not a full chunk)",
                      config_.sink_id.c_str(), total_bytes_in_mixing_buffer - current_byte_offset);
    }
}


void SinkAudioMixer::run() {
    LOG_CPP_INFO("[SinkMixer:%s] Entering run loop.", config_.sink_id.c_str());

    LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Starting iteration.", config_.sink_id.c_str());
    while (!stop_flag_) {
        // 1. Wait for and retrieve data from source queues
        LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Waiting for source data...", config_.sink_id.c_str());
        bool data_available = wait_for_source_data(INPUT_WAIT_TIMEOUT);
        LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Wait finished. Data available: %s", config_.sink_id.c_str(), (data_available ? "true" : "false"));

        if (stop_flag_) {
             LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Stop flag checked after wait, breaking.", config_.sink_id.c_str());
             break; // Check flag again after potentially waiting
        }

        // Lock queues mutex for mixing and state access
        std::unique_lock<std::mutex> lock(queues_mutex_);

        if (data_available || !input_queues_.empty()) { // Mix even if no new data, using last known buffer state
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Data available or queues not empty, proceeding to mix.", config_.sink_id.c_str());
            // 2. Mix data from active source buffers
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Mixing buffers...", config_.sink_id.c_str());
            mix_buffers();
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Mixing complete.", config_.sink_id.c_str());

            // Unlock queues mutex before potentially long operations
            lock.unlock();
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Queues mutex unlocked.", config_.sink_id.c_str());

            // 3. Encode to MP3 (if enabled)
            encode_and_push_mp3();

            // 4. Downscale mixed buffer to network format
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Downscaling buffer...", config_.sink_id.c_str());
            downscale_buffer();
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Downscaling complete. WritePos=%zu", config_.sink_id.c_str(), payload_buffer_write_pos_);

            // 5. Send network data if a full payload chunk has been accumulated
            if (payload_buffer_write_pos_ >= SINK_CHUNK_SIZE_BYTES) {
                LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Payload buffer ready to send. WritePos=%zu bytes.",
                              config_.sink_id.c_str(), payload_buffer_write_pos_);
                
                if (network_sender_) {
                    std::lock_guard<std::mutex> lock(csrc_mutex_);
                    network_sender_->send_payload(payload_buffer_.data(), SINK_CHUNK_SIZE_BYTES, current_csrcs_);
                }
                
                size_t bytes_remaining = payload_buffer_write_pos_ - SINK_CHUNK_SIZE_BYTES;
                if (bytes_remaining > 0) {
                    memmove(payload_buffer_.data(), payload_buffer_.data() + SINK_CHUNK_SIZE_BYTES, bytes_remaining);
                }
                
                payload_buffer_write_pos_ = bytes_remaining;
                LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Adjusted write pos to %zu", config_.sink_id.c_str(), payload_buffer_write_pos_);
            } else {
                 LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Payload buffer not full enough yet. WritePos=%zu bytes. Need=%zu bytes.",
                               config_.sink_id.c_str(), payload_buffer_write_pos_, static_cast<size_t>(SINK_CHUNK_SIZE_BYTES));
            }
        } else {
            // No input queues connected or no data available from wait_for_source_data
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: No data available and input queues empty. Sleeping briefly.", config_.sink_id.c_str());
            lock.unlock();
            // Add a small sleep to prevent busy-waiting if there are truly no inputs or no data
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Sleep 10ms
        }
        LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: End of iteration.", config_.sink_id.c_str());
    } // End while loop

    LOG_CPP_INFO("[SinkMixer:%s] Exiting run loop.", config_.sink_id.c_str());
}
