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
      mp3_output_queue_(mp3_output_queue), // Store the shared_ptr (can be null)
      udp_socket_fd_(INVALID_SOCKET_VALUE), // Initialize with platform-specific invalid value
      // tcp_socket_fd_(-1), // Removed
      lame_global_flags_(nullptr),
      lame_preprocessor_(nullptr), // Initialize the new member
      // Fix mixing buffer size to be constant based on SINK_MIXING_BUFFER_SAMPLES
      mixing_buffer_(SINK_MIXING_BUFFER_SAMPLES, 0),
      // Fix output buffer size for double buffering (Packet Size * 2)
      output_network_buffer_(SINK_PACKET_SIZE_BYTES * 2, 0),
      mp3_encode_buffer_(SINK_MP3_BUFFER_SIZE) // Allocate MP3 buffer
 {
    #ifdef _WIN32
        // WSAStartup might have already been called by RtpReceiver if both exist.
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            LOG_CPP_ERROR("[SinkMixer:%s] WSAStartup failed: %d", config_.sink_id.c_str(), iResult);
            throw std::runtime_error("WSAStartup failed.");
        }
    #endif
    LOG_CPP_INFO("[SinkMixer:%s] Initializing...", config_.sink_id.c_str());

    // Validate config
    if (config_.output_bitdepth != 8 && config_.output_bitdepth != 16 && config_.output_bitdepth != 24 && config_.output_bitdepth != 32) {
         LOG_CPP_ERROR("[SinkMixer:%s] Unsupported output bit depth: %d. Defaulting to 16.", config_.sink_id.c_str(), config_.output_bitdepth);
         config_.output_bitdepth = 16;
    }
    if (config_.output_channels <= 0 || config_.output_channels > 8) { // Assuming max 8 channels based on old code
         LOG_CPP_ERROR("[SinkMixer:%s] Invalid output channels: %d. Defaulting to 2.", config_.sink_id.c_str(), config_.output_channels);
         config_.output_channels = 2;
    }

    build_scream_header();

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
    close_networking();
    //close_lame(); // LAME cleanup should happen here if initialized

    #ifdef _WIN32
        // WSACleanup(); // Managed globally ideally
    #endif
}

void SinkAudioMixer::build_scream_header() {
    bool output_samplerate_44100_base = (config_.output_samplerate % 44100) == 0;
    // Ensure divisor is not zero
    uint8_t output_samplerate_mult = (config_.output_samplerate > 0) ?
        ((output_samplerate_44100_base ? 44100 : 48000) / config_.output_samplerate) : 1;

    scream_header_[0] = output_samplerate_mult + (output_samplerate_44100_base << 7);
    scream_header_[1] = static_cast<uint8_t>(config_.output_bitdepth);
    scream_header_[2] = static_cast<uint8_t>(config_.output_channels);
    scream_header_[3] = config_.output_chlayout1;
    scream_header_[4] = config_.output_chlayout2;
    LOG_CPP_INFO("[SinkMixer:%s] Built Scream header for Rate: %d, Depth: %d, Channels: %d",
                 config_.sink_id.c_str(), config_.output_samplerate, config_.output_bitdepth, config_.output_channels);
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

bool SinkAudioMixer::setup_networking() {
    LOG_CPP_INFO("[SinkMixer:%s] Setting up networking...", config_.sink_id.c_str());
    // UDP Setup
    udp_socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); // socket() is generally cross-platform
    if (udp_socket_fd_ == INVALID_SOCKET_VALUE) { // Check against platform-specific invalid value
        LOG_CPP_ERROR("[SinkMixer:%s] Failed to create UDP socket", config_.sink_id.c_str());
        return false;
    }

    // Set DSCP/TOS value (Best Effort is default, EF for Expedited Forwarding is 46)
    // IP_TOS might require different handling or privileges on Windows.
    #ifdef _WIN32
        //DWORD tos_value_dword = static_cast<DWORD>(tos_value); // Example if DWORD needed
        //if (setsockopt(udp_socket_fd_, IPPROTO_IP, IP_TOS, reinterpret_cast<const char*>(&tos_value_dword), sizeof(tos_value_dword)) < 0) {
        // For now, skip TOS setting on Windows for simplicity, requires more investigation
        LOG_CPP_WARNING("[SinkMixer:%s] Skipping TOS/DSCP setting on Windows.", config_.sink_id.c_str());
    #else // POSIX
        int dscp = 46; // EF PHB for low latency audio
        int tos_value = dscp << 2;
        if (setsockopt(udp_socket_fd_, IPPROTO_IP, IP_TOS, &tos_value, sizeof(tos_value)) < 0) {
            LOG_CPP_ERROR("[SinkMixer:%s] Failed to set UDP socket TOS/DSCP", config_.sink_id.c_str());
            // Non-fatal, continue anyway
        }
    #endif

    // Prepare UDP destination address
    memset(&udp_dest_addr_, 0, sizeof(udp_dest_addr_));
    udp_dest_addr_.sin_family = AF_INET;
    udp_dest_addr_.sin_port = htons(config_.output_port);
    // Use inet_pton for cross-platform compatibility (preferred over inet_addr)
    if (inet_pton(AF_INET, config_.output_ip.c_str(), &udp_dest_addr_.sin_addr) <= 0) {
        LOG_CPP_ERROR("[SinkMixer:%s] Invalid UDP destination IP address (inet_pton failed): %s", config_.sink_id.c_str(), config_.output_ip.c_str());
        _close_socket(udp_socket_fd_); // Use corrected macro
        udp_socket_fd_ = INVALID_SOCKET_VALUE;
        return false;
    }

    // TCP setup is handled externally via set_tcp_fd()

    LOG_CPP_INFO("[SinkMixer:%s] Networking setup complete (UDP target: %s:%d)", config_.sink_id.c_str(), config_.output_ip.c_str(), config_.output_port);
    return true;
}

void SinkAudioMixer::close_networking() {
    if (udp_socket_fd_ != INVALID_SOCKET_VALUE) { // Check against platform-specific invalid value
        LOG_CPP_INFO("[SinkMixer:%s] Closing UDP socket", config_.sink_id.c_str()); // Simplified log
        _close_socket(udp_socket_fd_); // Use corrected macro
        udp_socket_fd_ = INVALID_SOCKET_VALUE; // Set to platform-specific invalid value
    }
    // Don't close tcp_socket_fd_ here, as it's managed externally // This comment is now misleading
}

void SinkAudioMixer::start() {
    if (is_running()) {
        LOG_CPP_INFO("[SinkMixer:%s] Already running.", config_.sink_id.c_str());
        return;
    }
    LOG_CPP_INFO("[SinkMixer:%s] Starting...", config_.sink_id.c_str());
    stop_flag_ = false;
    output_buffer_write_pos_ = 0; // Reset write position

    if (!setup_networking()) {
        LOG_CPP_ERROR("[SinkMixer:%s] Networking setup failed. Cannot start mixer thread.", config_.sink_id.c_str());
        return;
    }

    // Launch the thread
    try {
        component_thread_ = std::thread(&SinkAudioMixer::run, this);
        LOG_CPP_INFO("[SinkMixer:%s] Thread started.", config_.sink_id.c_str());
    } catch (const std::system_error& e) {
        LOG_CPP_ERROR("[SinkMixer:%s] Failed to start thread: %s", config_.sink_id.c_str(), e.what());
        close_networking();
        //close_lame();
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

    // Notify condition variables to wake up waiting threads
    input_cv_.notify_all();

    // Flush LAME buffer before joining thread
    if (mp3_output_queue_ && lame_global_flags_) {
        LOG_CPP_INFO("[SinkMixer:%s] Flushing LAME buffer...", config_.sink_id.c_str());
        int flush_bytes = lame_encode_flush(lame_global_flags_, mp3_encode_buffer_.data(), mp3_encode_buffer_.size());
        if (flush_bytes < 0) {
            LOG_CPP_ERROR("[SinkMixer:%s] LAME flush failed with code: %d", config_.sink_id.c_str(), flush_bytes);
        } else if (flush_bytes > 0) {
            LOG_CPP_INFO("[SinkMixer:%s] LAME flushed %d bytes.", config_.sink_id.c_str(), flush_bytes);
            EncodedMP3Data mp3_data;
            mp3_data.mp3_data.assign(mp3_encode_buffer_.begin(), mp3_encode_buffer_.begin() + flush_bytes);
            mp3_output_queue_->push(std::move(mp3_data));
        } else {
             LOG_CPP_INFO("[SinkMixer:%s] LAME flush produced 0 bytes.", config_.sink_id.c_str());
        }
    }

    if (component_thread_.joinable()) {
         try {
            component_thread_.join();
            LOG_CPP_INFO("[SinkMixer:%s] Thread joined.", config_.sink_id.c_str());
        } catch (const std::system_error& e) {
            LOG_CPP_ERROR("[SinkMixer:%s] Error joining thread: %s", config_.sink_id.c_str(), e.what());
        }
    } else {
         LOG_CPP_INFO("[SinkMixer:%s] Thread was not joinable.", config_.sink_id.c_str());
    }

    // Cleanup resources after thread has stopped
    close_networking();
    //close_lame();
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
    LOG_CPP_DEBUG("[SinkMixer:%s] MixBuffers: Mix complete. Mixed %zu active sources into mixing_buffer_ (%zu samples).", config_.sink_id.c_str(), active_source_count, total_samples_to_mix);
}


// Restored original bit-shifting logic (copies MSBs)
void SinkAudioMixer::downscale_buffer() {
    // Converts 32-bit mixing_buffer_ to target bit depth into output_network_buffer_
    size_t output_byte_depth = config_.output_bitdepth / 8; // Bytes per sample in target format (e.g., 16-bit -> 2 bytes)
    // Calculate the number of samples based on the mixing buffer size (int32_t)
    size_t samples_to_convert = mixing_buffer_.size(); // Should be SINK_MIXING_BUFFER_SAMPLES (e.g., 576)

    // Calculate the total number of bytes this conversion *should* produce
    size_t expected_bytes_to_write = samples_to_convert * output_byte_depth; // e.g., 576 samples * 2 bytes/sample = 1152 bytes for 16-bit stereo
    LOG_CPP_DEBUG("[SinkMixer:%s] Downscale: Converting %zu samples (int32) to %d-bit. Expected output bytes=%zu.",
                  config_.sink_id.c_str(), samples_to_convert, config_.output_bitdepth, expected_bytes_to_write);


    // Ensure we don't write past the end of the output buffer's allocated space
    // Note: output_network_buffer_ is double buffered (size = SINK_PACKET_SIZE_BYTES * 2 = (1152+5)*2 = 2314 bytes)
    // We write data *after* the header space reserved at the beginning.
    size_t available_space = output_network_buffer_.size() - SINK_HEADER_SIZE - output_buffer_write_pos_;

    if (expected_bytes_to_write > available_space) {
        LOG_CPP_ERROR("[SinkMixer:%s] Downscale buffer overflow detected! Available space=%zu, needed=%zu. WritePos=%zu. BufferSize=%zu",
                      config_.sink_id.c_str(), available_space, expected_bytes_to_write, output_buffer_write_pos_, output_network_buffer_.size());
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

    // Get pointers for reading (from mixing_buffer_) and writing (to output_network_buffer_)
    uint8_t* write_ptr_start = output_network_buffer_.data() + SINK_HEADER_SIZE + output_buffer_write_pos_; // Start writing after header space + current position
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

    // Update the write position in the output_network_buffer_
    output_buffer_write_pos_ += bytes_written;
    LOG_CPP_DEBUG("[SinkMixer:%s] Downscale complete. output_buffer_write_pos_=%zu", config_.sink_id.c_str(), output_buffer_write_pos_);
}


void SinkAudioMixer::send_network_buffer(size_t length) {
    // This function sends a complete network packet of the specified length.
    // The length should typically be SINK_PACKET_SIZE_BYTES (header + payload).
    LOG_CPP_DEBUG("[SinkMixer:%s] SendNet: Preparing to send buffer. Requested length=%zu bytes. Expected packet size=%zu bytes.",
                  config_.sink_id.c_str(), length, static_cast<size_t>(SINK_PACKET_SIZE_BYTES));
    if (length == 0) {
        LOG_CPP_ERROR("[SinkMixer:%s] SendNet: Attempted to send network buffer with length 0.", config_.sink_id.c_str());
        return;
    }
    // Ensure length includes header and doesn't exceed buffer capacity
    if (length < SINK_HEADER_SIZE) {
         LOG_CPP_ERROR("[SinkMixer:%s] SendNet: Attempted to send network buffer with length %zu < header size %zu",
                       config_.sink_id.c_str(), length, static_cast<size_t>(SINK_HEADER_SIZE));
         return; // Invalid length
    }
     // Check against expected packet size (SINK_PACKET_SIZE_BYTES = 1157)
     if (length != SINK_PACKET_SIZE_BYTES) {
         LOG_CPP_WARNING("[SinkMixer:%s] SendNet: Sending packet with length %zu which differs from expected SINK_PACKET_SIZE_BYTES (%zu).",
                         config_.sink_id.c_str(), length, static_cast<size_t>(SINK_PACKET_SIZE_BYTES));
     }
    // Ensure length doesn't exceed the *total* allocated buffer size (double buffer)
    if (length > output_network_buffer_.size()) {
         LOG_CPP_ERROR("[SinkMixer:%s] SendNet: Attempted to send network buffer with length %zu > total buffer size %zu",
                       config_.sink_id.c_str(), length, output_network_buffer_.size());
         length = output_network_buffer_.size(); // Prevent overflow, but indicates an issue elsewhere
         LOG_CPP_ERROR("[SinkMixer:%s] SendNet: Clamping send length to buffer size: %zu", config_.sink_id.c_str(), length);
    }

    // Add the pre-built Scream header to the start of the buffer *before* sending
    memcpy(output_network_buffer_.data(), scream_header_, SINK_HEADER_SIZE); // Copy header to the very beginning
    LOG_CPP_DEBUG("[SinkMixer:%s] SendNet: Header (%zu bytes) copied to buffer start.", config_.sink_id.c_str(), static_cast<size_t>(SINK_HEADER_SIZE));

    // ---- START SILENCE CHECK ----
    // For every packet it sends out, it samples five even points across the output packet's audio payload.
    // If all sampled points are zero, it does not send the packet.
    bool all_samples_zero = true;
    if (length > SINK_HEADER_SIZE) { // Only check if there's an audio payload
        const uint8_t* payload_ptr = output_network_buffer_.data() + SINK_HEADER_SIZE;
        size_t payload_size_bytes = length - SINK_HEADER_SIZE;
        size_t bytes_per_sample = config_.output_bitdepth / 8;

        // Ensure parameters are valid for checking samples
        if (bytes_per_sample > 0 && payload_size_bytes > 0 && (payload_size_bytes % bytes_per_sample == 0)) {
            size_t num_payload_samples = payload_size_bytes / bytes_per_sample;

            if (num_payload_samples >= 1) { // Must have at least one sample to check
                size_t indices_to_check[5];
                indices_to_check[0] = 0; // First sample
                
                if (num_payload_samples > 1) { // For num_payload_samples = 1, all indices will be 0.
                    indices_to_check[1] = static_cast<size_t>(std::floor(1.0 * (num_payload_samples - 1) / 4.0));
                    indices_to_check[2] = static_cast<size_t>(std::floor(2.0 * (num_payload_samples - 1) / 4.0));
                    indices_to_check[3] = static_cast<size_t>(std::floor(3.0 * (num_payload_samples - 1) / 4.0));
                    indices_to_check[4] = num_payload_samples - 1; // Last sample
                } else { // num_payload_samples == 1
                    indices_to_check[1] = 0;
                    indices_to_check[2] = 0;
                    indices_to_check[3] = 0;
                    indices_to_check[4] = 0;
                }
                
                LOG_CPP_DEBUG("[SinkMixer:%s] SendNet: Silence check on %zu samples. Indices: %zu,%zu,%zu,%zu,%zu",
                              config_.sink_id.c_str(), num_payload_samples, indices_to_check[0], indices_to_check[1],
                              indices_to_check[2], indices_to_check[3], indices_to_check[4]);

                for (int i = 0; i < 5; ++i) {
                    size_t sample_idx = indices_to_check[i];
                    // sample_idx is confirmed to be < num_payload_samples by construction if num_payload_samples >=1

                    const uint8_t* current_sample_ptr = payload_ptr + (sample_idx * bytes_per_sample);
                    bool current_sample_is_zero = true;
                    for (size_t byte_k = 0; byte_k < bytes_per_sample; ++byte_k) {
                        if (current_sample_ptr[byte_k] < 1024) {
                            current_sample_is_zero = false;
                            break;
                        }
                    }
                    if (!current_sample_is_zero) {
                        all_samples_zero = false; // If one sample point is not zero, the packet is not silent
                        break;
                    }
                }
            } else {
                // This case implies num_payload_samples is 0, but payload_size_bytes > 0.
                // This means payload_size_bytes < bytes_per_sample.
                // No full samples to check, so it's effectively silent for this check.
                LOG_CPP_DEBUG("[SinkMixer:%s] SendNet: Not enough data for a single sample in payload for silence check. Payload bytes: %zu, bytes/sample: %zu. Considered silent.",
                              config_.sink_id.c_str(), payload_size_bytes, bytes_per_sample);
                // all_samples_zero remains true.
            }
        } else {
            // This 'else' covers:
            // 1. bytes_per_sample == 0 (should not happen due to constructor validation)
            // 2. payload_size_bytes == 0 (already handled by outer 'if (length > SINK_HEADER_SIZE)', so all_samples_zero is true)
            // 3. payload_size_bytes > 0 BUT (payload_size_bytes % bytes_per_sample != 0) -> malformed packet for this check.
            if (payload_size_bytes > 0 && (bytes_per_sample == 0 || (payload_size_bytes % bytes_per_sample != 0) ) ) {
                LOG_CPP_WARNING("[SinkMixer:%s] SendNet: Cannot perform silence check due to invalid sample/payload parameters (e.g., non-integer samples or zero bytes_per_sample). Payload bytes: %zu. Sending packet.",
                                config_.sink_id.c_str(), payload_size_bytes);
                all_samples_zero = false; // Force send if payload exists but check is problematic
            }
            // If payload_size_bytes is 0, all_samples_zero remains true (correct for empty payload).
        }
    } else {
        // No payload (length == SINK_HEADER_SIZE), effectively silent.
        LOG_CPP_DEBUG("[SinkMixer:%s] SendNet: No payload to check for silence. Packet considered silent.", config_.sink_id.c_str());
        // all_samples_zero is already true.
    }

    if (all_samples_zero) {
        LOG_CPP_DEBUG("[SinkMixer:%s] SendNet: Packet identified as silent (either no payload or 5-point sample check passed). Skipping send.", config_.sink_id.c_str());
        // The run loop will effectively discard this chunk from output_network_buffer_
        // because it adjusts output_buffer_write_pos_ regardless of whether sendto was called.
        return; // Do not send the packet
    }
    // ---- END SILENCE CHECK ----

    // Send via UDP
    if (udp_socket_fd_ != INVALID_SOCKET_VALUE) { // Check against platform-specific invalid value
        LOG_CPP_DEBUG("[SinkMixer:%s] SendNet: Sending %zu bytes via UDP to %s:%d",
                      config_.sink_id.c_str(), length, config_.output_ip.c_str(), config_.output_port);
        // Send from the beginning of the buffer, including the header
        #ifdef _WIN32
            // Windows sendto uses char* buffer and int length
            int sent_bytes = sendto(udp_socket_fd_,
                                        reinterpret_cast<const char*>(output_network_buffer_.data()),
                                        static_cast<int>(length),
                                        0, // Flags
                                        (struct sockaddr *)&udp_dest_addr_,
                                        sizeof(udp_dest_addr_));
        #else
            // POSIX sendto uses const void* buffer and size_t length
            int sent_bytes = sendto(udp_socket_fd_,
                                        output_network_buffer_.data(),
                                        length,
                                        0, // Flags
                                        (struct sockaddr *)&udp_dest_addr_,
                                        sizeof(udp_dest_addr_));
        #endif

        if (sent_bytes < 0) {
            // EAGAIN or EWOULDBLOCK might be acceptable if non-blocking, but UDP usually doesn't block here.
            // Check specific Windows errors like WSAEWOULDBLOCK if needed.
            LOG_CPP_ERROR("[SinkMixer:%s] SendNet: UDP sendto failed", config_.sink_id.c_str());
        } else if (static_cast<size_t>(sent_bytes) != length) {
             LOG_CPP_ERROR("[SinkMixer:%s] SendNet: UDP sendto sent partial data: %d/%zu", config_.sink_id.c_str(), sent_bytes, length);
        } else {
             LOG_CPP_DEBUG("[SinkMixer:%s] SendNet: UDP send successful (%d bytes).", config_.sink_id.c_str(), sent_bytes);
        }
    } else {
         LOG_CPP_DEBUG("[SinkMixer:%s] SendNet: UDP socket not valid, skipping UDP send.", config_.sink_id.c_str());
    }

    // Send via TCP (if connected)
    // TCP sending logic removed
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
            downscale_buffer(); // Appends to output_network_buffer_
            LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Downscaling complete. WritePos=%zu", config_.sink_id.c_str(), output_buffer_write_pos_);

            // 5. Send network data if a full packet's worth of *payload* bytes has been accumulated
            // We check against SINK_CHUNK_SIZE_BYTES (1152) because that's the payload size we accumulate via downscale_buffer
            if (output_buffer_write_pos_ >= SINK_CHUNK_SIZE_BYTES) {
                LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Output buffer ready to send. WritePos=%zu bytes. ChunkSizeBytes=%zu bytes.",
                              config_.sink_id.c_str(), output_buffer_write_pos_, static_cast<size_t>(SINK_CHUNK_SIZE_BYTES));
                // Send the first packet (from start of double buffer), total size includes header
                send_network_buffer(SINK_PACKET_SIZE_BYTES); // Send 1157 bytes (header + 1152 payload)
                LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Network buffer sent (size=%zu).", config_.sink_id.c_str(), static_cast<size_t>(SINK_PACKET_SIZE_BYTES));

                // Shift the remaining data (if any) from the second half of the buffer to the beginning (after header space)
                size_t bytes_remaining_after_send = output_buffer_write_pos_ - SINK_CHUNK_SIZE_BYTES;
                LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Rotating output buffer... Shifting %zu bytes from second half to start.", config_.sink_id.c_str(), bytes_remaining_after_send);
                if (bytes_remaining_after_send > 0) {
                    // Use memmove for potentially overlapping regions.
                    // Source: Start of the data *after* the first sent chunk (at index SINK_PACKET_SIZE_BYTES)
                    // Destination: Start of the payload area (at index SINK_HEADER_SIZE)
                    // Length: The number of bytes remaining after sending the first chunk
                    memmove(output_network_buffer_.data() + SINK_HEADER_SIZE,      // Dest
                           output_network_buffer_.data() + SINK_PACKET_SIZE_BYTES, // Src
                           bytes_remaining_after_send);                            // Len
                }
                LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Output buffer rotated.", config_.sink_id.c_str());

                // Adjust write position to reflect the remaining data
                output_buffer_write_pos_ = bytes_remaining_after_send; // New write pos is the number of bytes shifted
                LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Adjusted write pos to %zu", config_.sink_id.c_str(), output_buffer_write_pos_);
            } else {
                 LOG_CPP_DEBUG("[SinkMixer:%s] RunLoop: Output buffer not full enough yet for payload. WritePos=%zu bytes. Need=%zu bytes.",
                               config_.sink_id.c_str(), output_buffer_write_pos_, static_cast<size_t>(SINK_CHUNK_SIZE_BYTES));
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
