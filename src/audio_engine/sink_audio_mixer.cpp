#include "sink_audio_mixer.h"
#include <iostream> // For logging
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

// Simple logger helper (replace with a proper logger if available)
#define LOG(sink_id, msg) std::cout << "[SinkMixer:" << sink_id << "] " << msg << std::endl
//#define LOG_ERROR(sink_id, msg) std::cerr << "[SinkMixer Error:" << sink_id << "] " << msg << " (errno: " << errno << ")" << std::endl
//#define LOG_WARN(sink_id, msg) std::cout << "[SinkMixer Warn:" << sink_id << "] " << msg << std::endl // Added WARN level
//#define LOG_DEBUG(sink_id, msg) std::cout << "[SinkMixer Debug:" << sink_id << "] " << msg << std::endl
//#define LOG(sink_id, msg)
#define LOG_ERROR(sink_id, msg)
#define LOG_WARN(sink_id, msg)
#define LOG_DEBUG(sink_id, msg)

// Define how long to wait for input data before mixing silence/last known data
const std::chrono::milliseconds INPUT_WAIT_TIMEOUT(20); // e.g., 20ms
const int DEFAULT_MP3_BITRATE = 192; // Default bitrate if MP3 enabled


SinkAudioMixer::SinkAudioMixer(
    SinkMixerConfig config,
    std::shared_ptr<Mp3OutputQueue> mp3_output_queue)
    : config_(config),
      mp3_output_queue_(mp3_output_queue), // Store the shared_ptr (can be null)
      udp_socket_fd_(-1),
      tcp_socket_fd_(-1),
      lame_global_flags_(nullptr),
      lame_preprocessor_(nullptr), // Initialize the new member
      // Fix mixing buffer size to be constant based on SINK_MIXING_BUFFER_SAMPLES
      mixing_buffer_(SINK_MIXING_BUFFER_SAMPLES, 0),
      // Fix output buffer size for double buffering (Packet Size * 2)
      output_network_buffer_(SINK_PACKET_SIZE_BYTES * 2, 0),
      mp3_encode_buffer_(SINK_MP3_BUFFER_SIZE) // Allocate MP3 buffer
 {
    LOG(config_.sink_id, "Initializing...");

    // Validate config
    if (config_.output_bitdepth != 8 && config_.output_bitdepth != 16 && config_.output_bitdepth != 24 && config_.output_bitdepth != 32) {
         LOG_ERROR(config_.sink_id, "Unsupported output bit depth: " + std::to_string(config_.output_bitdepth) + ". Defaulting to 16.");
         config_.output_bitdepth = 16;
    }
    if (config_.output_channels <= 0 || config_.output_channels > 8) { // Assuming max 8 channels based on old code
         LOG_ERROR(config_.sink_id, "Invalid output channels: " + std::to_string(config_.output_channels) + ". Defaulting to 2.");
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
            1.0f                        // volume
        );
        if (!lame_preprocessor_) {
             LOG_ERROR(config_.sink_id, "Failed to create AudioProcessor for LAME preprocessing.");
        } else {
             LOG(config_.sink_id, "Created AudioProcessor for LAME preprocessing.");
        }
        initialize_lame();
    }

    LOG(config_.sink_id, "Initialization complete.");
}

SinkAudioMixer::~SinkAudioMixer() {
    if (!stop_flag_) {
        stop();
    }
    if (component_thread_.joinable()) {
        LOG(config_.sink_id, "Warning: Joining thread in destructor, stop() might not have been called properly.");
        component_thread_.join();
    }
    close_networking();
    //close_lame();
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
    LOG(config_.sink_id, "Built Scream header for Rate: " + std::to_string(config_.output_samplerate) +
                         ", Depth: " + std::to_string(config_.output_bitdepth) +
                         ", Channels: " + std::to_string(config_.output_channels));
}

void SinkAudioMixer::initialize_lame() {
    if (!mp3_output_queue_) return; // Don't initialize if not enabled

    LOG(config_.sink_id, "Initializing LAME MP3 encoder...");
    lame_global_flags_ = lame_init();
    if (!lame_global_flags_) {
        LOG_ERROR(config_.sink_id, "lame_init() failed.");
        return;
    }

    lame_set_in_samplerate(lame_global_flags_, config_.output_samplerate);
    // Matching c_utils: Rely on LAME defaults/inference for other parameters
    lame_set_VBR(lame_global_flags_, vbr_off); // Use CBR for streaming (matches c_utils)

    int ret = lame_init_params(lame_global_flags_);
    if (ret < 0) {
        LOG_ERROR(config_.sink_id, "lame_init_params() failed with code: " + std::to_string(ret));
        lame_close(lame_global_flags_);
        lame_global_flags_ = nullptr;
        return; // Return early if params init failed
    }
    lame_active_ = true; // Assume active initially
    LOG(config_.sink_id, "LAME initialized successfully.");
}

// Updated to use instance_id
void SinkAudioMixer::add_input_queue(const std::string& instance_id, std::shared_ptr<InputChunkQueue> queue) {
    if (!queue) {
        LOG_ERROR(config_.sink_id, "Attempted to add null input queue for instance: " + instance_id);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(queues_mutex_);
        input_queues_[instance_id] = queue;
        input_active_state_[instance_id] = false; // Start as inactive
        // Initialize buffer for this source instance (e.g., with silence)
        source_buffers_[instance_id].audio_data.assign(SINK_MIXING_BUFFER_SAMPLES, 0); // Size is total samples, not channels * samples
        LOG(config_.sink_id, "Added input queue for source instance: " + instance_id);
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
        LOG(config_.sink_id, "Removed input queue for source instance: " + instance_id);
    }
}

bool SinkAudioMixer::setup_networking() {
    LOG(config_.sink_id, "Setting up networking...");
    // UDP Setup
    udp_socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket_fd_ < 0) {
        LOG_ERROR(config_.sink_id, "Failed to create UDP socket");
        return false;
    }

    // Set DSCP/TOS value (Best Effort is default, EF for Expedited Forwarding is 46)
    int dscp = 46; // EF PHB for low latency audio
    int tos_value = dscp << 2;
    if (setsockopt(udp_socket_fd_, IPPROTO_IP, IP_TOS, &tos_value, sizeof(tos_value)) < 0) {
        LOG_ERROR(config_.sink_id, "Failed to set UDP socket TOS/DSCP");
        // Non-fatal, continue anyway
    }

    // Prepare UDP destination address
    memset(&udp_dest_addr_, 0, sizeof(udp_dest_addr_));
    udp_dest_addr_.sin_family = AF_INET;
    udp_dest_addr_.sin_port = htons(config_.output_port);
    if (inet_pton(AF_INET, config_.output_ip.c_str(), &udp_dest_addr_.sin_addr) <= 0) {
        LOG_ERROR(config_.sink_id, "Invalid UDP destination IP address: " + config_.output_ip);
        close(udp_socket_fd_);
        udp_socket_fd_ = -1;
        return false;
    }

    // TCP setup is handled externally via set_tcp_fd()

    LOG(config_.sink_id, "Networking setup complete (UDP target: " + config_.output_ip + ":" + std::to_string(config_.output_port) + ")");
    return true;
}

void SinkAudioMixer::close_networking() {
    if (udp_socket_fd_ != -1) {
        LOG(config_.sink_id, "Closing UDP socket fd " + std::to_string(udp_socket_fd_));
        close(udp_socket_fd_);
        udp_socket_fd_ = -1;
    }
    // Don't close tcp_socket_fd_ here, as it's managed externally
}

void SinkAudioMixer::start() {
    if (is_running()) {
        LOG(config_.sink_id, "Already running.");
        return;
    }
    LOG(config_.sink_id, "Starting...");
    stop_flag_ = false;
    output_buffer_write_pos_ = 0; // Reset write position

    if (!setup_networking()) {
        LOG_ERROR(config_.sink_id, "Networking setup failed. Cannot start mixer thread.");
        return;
    }

    // Launch the thread
    try {
        component_thread_ = std::thread(&SinkAudioMixer::run, this);
        LOG(config_.sink_id, "Thread started.");
    } catch (const std::system_error& e) {
        LOG_ERROR(config_.sink_id, "Failed to start thread: " + std::string(e.what()));
        close_networking();
        //close_lame();
        throw;
    }
}

void SinkAudioMixer::stop() {
     if (stop_flag_) {
        LOG(config_.sink_id, "Already stopped or stopping.");
        return;
    }
    LOG(config_.sink_id, "Stopping...");
    stop_flag_ = true;

    // Notify condition variables to wake up waiting threads
    input_cv_.notify_all();

    // Flush LAME buffer before joining thread
    if (mp3_output_queue_ && lame_global_flags_) {
        LOG(config_.sink_id, "Flushing LAME buffer...");
        int flush_bytes = lame_encode_flush(lame_global_flags_, mp3_encode_buffer_.data(), mp3_encode_buffer_.size());
        if (flush_bytes < 0) {
            LOG_ERROR(config_.sink_id, "LAME flush failed with code: " + std::to_string(flush_bytes));
        } else if (flush_bytes > 0) {
            LOG(config_.sink_id, "LAME flushed " + std::to_string(flush_bytes) + " bytes.");
            EncodedMP3Data mp3_data;
            mp3_data.mp3_data.assign(mp3_encode_buffer_.begin(), mp3_encode_buffer_.begin() + flush_bytes);
            mp3_output_queue_->push(std::move(mp3_data));
        } else {
             LOG(config_.sink_id, "LAME flush produced 0 bytes.");
        }
    }

    if (component_thread_.joinable()) {
         try {
            component_thread_.join();
            LOG(config_.sink_id, "Thread joined.");
        } catch (const std::system_error& e) {
            LOG_ERROR(config_.sink_id, "Error joining thread: " + std::string(e.what()));
        }
    } else {
         LOG(config_.sink_id, "Thread was not joinable.");
    }

    // Cleanup resources after thread has stopped
    close_networking();
    //close_lame();
}

void SinkAudioMixer::set_tcp_fd(int fd) {
    // This function might be called from a different thread (e.g., network management thread)
    // No lock needed if tcp_socket_fd_ is atomic, but it's just an int here.
    // Assuming this is called infrequently and potential race condition is acceptable,
    // or that external synchronization is handled by the caller.
    // For robustness, a mutex could be added if concurrent calls are expected.
    if (fd != tcp_socket_fd_) {
        LOG(config_.sink_id, "Setting TCP FD to " + std::to_string(fd));
        tcp_socket_fd_ = fd;
    }
}

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
    LOG_DEBUG(config_.sink_id, "WaitForData: Initial non-blocking check...");
    // Use instance_id as the key/loop variable
    for (auto const& [instance_id, queue_ptr] : input_queues_) {
        ProcessedAudioChunk chunk;
        // Check active state using instance_id
        bool previously_active = input_active_state_.count(instance_id) ? input_active_state_[instance_id] : false;

        if (queue_ptr->try_pop(chunk)) { // Data is immediately available
             if (chunk.audio_data.size() != SINK_MIXING_BUFFER_SAMPLES) {
                 LOG_ERROR(config_.sink_id, "WaitForData: Received chunk from instance " + instance_id + " with unexpected sample count: " + std::to_string(chunk.audio_data.size()) + ". Discarding.");
                 ready_this_cycle[instance_id] = false; // Not ready with valid data
                 // Don't mark active if data is invalid
             } else {
                 LOG_DEBUG(config_.sink_id, "WaitForData: Pop SUCCESS (Initial) for instance " << instance_id);
                 source_buffers_[instance_id] = std::move(chunk); // Store valid chunk using instance_id
                 ready_this_cycle[instance_id] = true;
                 data_actually_popped_this_cycle = true;
                 if (!previously_active) {
                     LOG(config_.sink_id, "Input instance " + instance_id + " became active");
                 }
                 input_active_state_[instance_id] = true; // Mark/confirm as active using instance_id
             }
        } else { // No data immediately available
            ready_this_cycle[instance_id] = false;
            if (previously_active) {
                // This source *was* active, but doesn't have data right now. Add to lagging list.
                LOG_DEBUG(config_.sink_id, "WaitForData: Pop FAILED (Initial) for ACTIVE instance " << instance_id << ". Adding to grace period check.");
                lagging_active_sources.push_back(instance_id); // Add instance_id to lagging list
            } else {
                 // Source was inactive and still has no data. Keep it inactive.
                 input_active_state_[instance_id] = false;
            }
        }
    }

    // --- Step 2 & 3: Grace Period for Lagging Active Sources ---
    if (!lagging_active_sources.empty()) {
        LOG_DEBUG(config_.sink_id, "WaitForData: Entering grace period check for " << lagging_active_sources.size() << " sources.");
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
                         LOG_ERROR(config_.sink_id, "WaitForData: Received chunk (Grace Period) from instance " + instance_id + " with unexpected sample count: " + std::to_string(chunk.audio_data.size()) + ". Discarding.");
                         // Still remove from lagging list, but don't mark ready
                    } else {
                        LOG_DEBUG(config_.sink_id, "WaitForData: Pop SUCCESS (Grace Period) for instance " << instance_id);
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
            LOG_DEBUG(config_.sink_id, "WaitForData: Grace period ended. " << lagging_active_sources.size() << " instances still lagging.");
            for (const auto& instance_id : lagging_active_sources) { // Use instance_id
                if (input_active_state_.count(instance_id) && input_active_state_[instance_id]) {
                     LOG(config_.sink_id, "Input instance " + instance_id + " timed out grace period, marking inactive.");
                     input_active_state_[instance_id] = false; // Mark as definitively inactive using instance_id
                }
            }
        } else {
             LOG_DEBUG(config_.sink_id, "WaitForData: Grace period ended. All lagging sources caught up.");
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
    LOG_DEBUG(config_.sink_id, "MixBuffers: Starting mix. Target samples=" + std::to_string(total_samples_to_mix) + " (Mixing buffer size).");


    for (auto const& [instance_id, is_active] : input_active_state_) { // Iterate through all potential sources using instance_id
        if (is_active) { // Only process sources marked active in wait_for_source_data
            active_source_count++;
            // Check if the source buffer actually exists in the map (should always exist if active)
            auto buf_it = source_buffers_.find(instance_id); // Find buffer by instance_id
            if (buf_it == source_buffers_.end()) {
                 LOG_ERROR(config_.sink_id, "Mixing error: Source buffer not found for active instance " + instance_id);
                 continue; // Skip this source
            }
            const auto& source_data = buf_it->second.audio_data; // Use iterator

            size_t samples_in_source = source_data.size(); // Get actual sample count from the stored chunk
            LOG_DEBUG(config_.sink_id, "MixBuffers: Mixing instance " + instance_id + ". Source samples=" + std::to_string(samples_in_source) + ". Expected=" + std::to_string(total_samples_to_mix) + ".");

            // *** Check source data size against the fixed mixing buffer size ***
            // This check is redundant if wait_for_source_data correctly discards invalid chunks, but kept for safety.
            if (samples_in_source != total_samples_to_mix) {
                 LOG_ERROR(config_.sink_id, "MixBuffers: Source buffer for instance " + instance_id + " size mismatch! Expected " + std::to_string(total_samples_to_mix) + ", got " + std::to_string(samples_in_source) + ". Skipping source.");
                 continue; // Skip this source
            }

            // Now we expect samples_in_source == total_samples_to_mix
            LOG_DEBUG(config_.sink_id, "MixBuffers: Accumulating " + std::to_string(total_samples_to_mix) + " samples from instance " + instance_id);

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
    LOG_DEBUG(config_.sink_id, "MixBuffers: Mix complete. Mixed " + std::to_string(active_source_count) + " active sources into mixing_buffer_ (" + std::to_string(total_samples_to_mix) + " samples).");
}


// Restored original bit-shifting logic (copies MSBs)
void SinkAudioMixer::downscale_buffer() {
    // Converts 32-bit mixing_buffer_ to target bit depth into output_network_buffer_
    size_t output_byte_depth = config_.output_bitdepth / 8; // Bytes per sample in target format (e.g., 16-bit -> 2 bytes)
    // Calculate the number of samples based on the mixing buffer size (int32_t)
    size_t samples_to_convert = mixing_buffer_.size(); // Should be SINK_MIXING_BUFFER_SAMPLES (e.g., 576)

    // Calculate the total number of bytes this conversion *should* produce
    size_t expected_bytes_to_write = samples_to_convert * output_byte_depth; // e.g., 576 samples * 2 bytes/sample = 1152 bytes for 16-bit stereo
    LOG_DEBUG(config_.sink_id, "Downscale: Converting " + std::to_string(samples_to_convert) + " samples (int32) to " + std::to_string(config_.output_bitdepth) + "-bit. Expected output bytes=" + std::to_string(expected_bytes_to_write) + ".");


    // Ensure we don't write past the end of the output buffer's allocated space
    // Note: output_network_buffer_ is double buffered (size = SINK_PACKET_SIZE_BYTES * 2 = (1152+5)*2 = 2314 bytes)
    // We write data *after* the header space reserved at the beginning.
    size_t available_space = output_network_buffer_.size() - SINK_HEADER_SIZE - output_buffer_write_pos_;

    if (expected_bytes_to_write > available_space) {
        LOG_ERROR(config_.sink_id, "Downscale buffer overflow detected! Available space=" + std::to_string(available_space) + ", needed=" + std::to_string(expected_bytes_to_write) + ". WritePos=" + std::to_string(output_buffer_write_pos_) + ". BufferSize=" + std::to_string(output_network_buffer_.size()));
        // Limit the operation to prevent overflow, but data will be lost/corrupted.
        // Calculate how many full samples can fit in the available space.
        size_t max_samples_possible = available_space / output_byte_depth;
        samples_to_convert = max_samples_possible; // Limit samples
        expected_bytes_to_write = samples_to_convert * output_byte_depth; // Adjust expected bytes accordingly
        LOG_ERROR(config_.sink_id, "Downscale: Limiting conversion to " + std::to_string(samples_to_convert) + " samples (" + std::to_string(expected_bytes_to_write) + " bytes) due to space limit.");
        if (samples_to_convert == 0) {
             LOG_ERROR(config_.sink_id, "Downscale buffer has no space left. available=" + std::to_string(available_space));
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
    LOG_DEBUG(config_.sink_id, "Downscale: Conversion loop finished. Bytes written=" + std::to_string(bytes_written) + ". Expected=" + std::to_string(expected_bytes_to_write) + ".");
    if (bytes_written != expected_bytes_to_write) {
         LOG_ERROR(config_.sink_id, "Downscale: Mismatch between bytes written (" + std::to_string(bytes_written) + ") and expected bytes (" + std::to_string(expected_bytes_to_write) + ").");
    }

    // Update the write position in the output_network_buffer_
    output_buffer_write_pos_ += bytes_written;
    LOG_DEBUG(config_.sink_id, "Downscale complete. output_buffer_write_pos_=" + std::to_string(output_buffer_write_pos_));
}


void SinkAudioMixer::send_network_buffer(size_t length) {
    // This function sends a complete network packet of the specified length.
    // The length should typically be SINK_PACKET_SIZE_BYTES (header + payload).
    LOG_DEBUG(config_.sink_id, "SendNet: Preparing to send buffer. Requested length=" + std::to_string(length) + " bytes. Expected packet size=" + std::to_string(SINK_PACKET_SIZE_BYTES) + " bytes.");
    if (length == 0) {
        LOG_ERROR(config_.sink_id, "SendNet: Attempted to send network buffer with length 0.");
        return;
    }
    // Ensure length includes header and doesn't exceed buffer capacity
    if (length < SINK_HEADER_SIZE) {
         LOG_ERROR(config_.sink_id, "SendNet: Attempted to send network buffer with length " + std::to_string(length) + " < header size " + std::to_string(SINK_HEADER_SIZE));
         return; // Invalid length
    }
     // Check against expected packet size (SINK_PACKET_SIZE_BYTES = 1157)
     if (length != SINK_PACKET_SIZE_BYTES) {
         LOG_WARN(config_.sink_id, "SendNet: Sending packet with length " + std::to_string(length) + " which differs from expected SINK_PACKET_SIZE_BYTES (" + std::to_string(SINK_PACKET_SIZE_BYTES) + ").");
     }
    // Ensure length doesn't exceed the *total* allocated buffer size (double buffer)
    if (length > output_network_buffer_.size()) {
         LOG_ERROR(config_.sink_id, "SendNet: Attempted to send network buffer with length " + std::to_string(length) + " > total buffer size " + std::to_string(output_network_buffer_.size()));
         length = output_network_buffer_.size(); // Prevent overflow, but indicates an issue elsewhere
         LOG_ERROR(config_.sink_id, "SendNet: Clamping send length to buffer size: " + std::to_string(length));
    }

    // Add the pre-built Scream header to the start of the buffer *before* sending
    memcpy(output_network_buffer_.data(), scream_header_, SINK_HEADER_SIZE); // Copy header to the very beginning
    LOG_DEBUG(config_.sink_id, "SendNet: Header (" + std::to_string(SINK_HEADER_SIZE) + " bytes) copied to buffer start.");

    // Send via UDP
    if (udp_socket_fd_ != -1) {
        LOG_DEBUG(config_.sink_id, "SendNet: Sending " + std::to_string(length) + " bytes via UDP to " + config_.output_ip + ":" + std::to_string(config_.output_port));
        // Send from the beginning of the buffer, including the header
        ssize_t sent_bytes = sendto(udp_socket_fd_,
                                    output_network_buffer_.data(),
                                    length, // Send the full packet length (header + payload)
                                    0, // Flags
                                    (struct sockaddr *)&udp_dest_addr_,
                                    sizeof(udp_dest_addr_));
        if (sent_bytes < 0) {
            // EAGAIN or EWOULDBLOCK might be acceptable if non-blocking, but UDP usually doesn't block here.
            LOG_ERROR(config_.sink_id, "SendNet: UDP sendto failed");
        } else if (static_cast<size_t>(sent_bytes) != length) {
             LOG_ERROR(config_.sink_id, "SendNet: UDP sendto sent partial data: " + std::to_string(sent_bytes) + "/" + std::to_string(length));
        } else {
             LOG_DEBUG(config_.sink_id, "SendNet: UDP send successful (" + std::to_string(sent_bytes) + " bytes).");
        }
    } else {
         LOG_DEBUG(config_.sink_id, "SendNet: UDP socket not valid, skipping UDP send.");
    }

    // Send via TCP (if connected)
    if (tcp_socket_fd_ != -1) {
        LOG_DEBUG(config_.sink_id, "SendNet: Sending " + std::to_string(length) + " bytes via TCP (fd=" + std::to_string(tcp_socket_fd_) + ").");
        // Send from the beginning of the buffer, including the header
        ssize_t sent_bytes = send(tcp_socket_fd_,
                                  output_network_buffer_.data(),
                                  length, // Send the full packet length (header + payload)
                                  MSG_NOSIGNAL); // Prevent SIGPIPE if connection closed
        if (sent_bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                 LOG_WARN(config_.sink_id, "SendNet: TCP send would block (EAGAIN/EWOULDBLOCK). Data dropped for TCP.");
            } else {
                LOG_ERROR(config_.sink_id, "SendNet: TCP send failed");
                // Consider closing or signaling error for this TCP connection
                // set_tcp_fd(-1); // Example: Mark TCP as disconnected
            }
            // If EAGAIN/EWOULDBLOCK, buffer is full, data is dropped for TCP in this model
        } else if (static_cast<size_t>(sent_bytes) != length) {
             LOG_ERROR(config_.sink_id, "SendNet: TCP send sent partial data: " + std::to_string(sent_bytes) + "/" + std::to_string(length));
             // Handle partial send if necessary (e.g., retry)
        } else {
             LOG_DEBUG(config_.sink_id, "SendNet: TCP send successful (" + std::to_string(sent_bytes) + " bytes).");
        }
    } else {
         LOG_DEBUG(config_.sink_id, "SendNet: TCP socket not valid (fd=" + std::to_string(tcp_socket_fd_) + "), skipping TCP send.");
    }
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
            LOG(config_.sink_id, "MP3 output queue full, pausing encoding.");
            lame_active_ = false;
        }
        return; // Don't encode if consumer likely inactive
    } else {
         if (!lame_active_) {
             LOG(config_.sink_id, "MP3 output queue draining, resuming encoding.");
             lame_active_ = true;
         }
    }

    // If LAME encoding is paused due to full queue, don't proceed
    if (!lame_active_) {
        return;
    }

    // *** ADDED CHECK: Ensure mixing_buffer_ has 1152 bytes (288 samples) before processing ***
    const size_t required_samples = SINK_CHUNK_SIZE_BYTES / sizeof(int32_t);
    if (mixing_buffer_.size() < required_samples) {
        LOG_ERROR(config_.sink_id, "MP3 Encode: Skipping processing. mixing_buffer_ size is " + std::to_string(mixing_buffer_.size()) + ", expected " + std::to_string(required_samples) + " samples (1152 bytes).");
        return; // Do not process or encode if the buffer size is wrong
    }
    LOG_DEBUG(config_.sink_id, "MP3 Encode: mixing_buffer_ size check passed (" + std::to_string(mixing_buffer_.size()) + " samples). Proceeding to AudioProcessor.");


    // 1. Preprocess the mixed buffer using AudioProcessor.
    //    AudioProcessor internally processes SINK_MIXING_BUFFER_SAMPLES input frames.
    //    The output is stereo (2 channels), so the buffer needs space for SINK_MIXING_BUFFER_SAMPLES * 2 samples.
    std::vector<int32_t> stereo_int32_buffer(SINK_MIXING_BUFFER_SAMPLES * 2); // Correctly sized for stereo output

    // Pass the input buffer. processAudio will read CHUNK_SIZE bytes internally based on its inputBitDepth setting.
    int processed_total_samples = lame_preprocessor_->processAudio(
        reinterpret_cast<const uint8_t*>(mixing_buffer_.data()),
        stereo_int32_buffer.data()
    );

    if (processed_total_samples <= 0) {
        LOG_ERROR(config_.sink_id, "AudioProcessor failed to process audio for LAME. Samples processed: " + std::to_string(processed_total_samples));
        return;
    }
    // Assuming processAudio returns the total number of stereo samples written.
    LOG_DEBUG(config_.sink_id, "AudioProcessor produced " + std::to_string(processed_total_samples) + " total stereo int32 samples for LAME.");

    // 2. NO CONVERSION NEEDED: We will use lame_encode_buffer_interleaved_int which takes int32_t directly.

    // 3. Encode the stereo int32_t buffer using LAME
    // Calculate the number of frames (samples per channel).
    // Assuming processAudio returns total stereo samples, divide by 2 for frames per channel.
    int processed_frames_per_channel = processed_total_samples / 2;
    LOG_DEBUG(config_.sink_id, "Processed frames per channel for LAME: " + std::to_string(processed_frames_per_channel));

    // Ensure the allocated MP3 buffer is large enough (LAME recommendation: 1.25 * num_samples + 7200)
    // Use the number of frames per channel being passed to LAME.
    size_t required_mp3_buffer_size = static_cast<size_t>(1.25 * processed_frames_per_channel + 7200);
    if (mp3_encode_buffer_.size() < required_mp3_buffer_size) {
         LOG_WARN(config_.sink_id, "MP3 encode buffer might be too small. Size: " + std::to_string(mp3_encode_buffer_.size()) + ", Recommended: " + std::to_string(required_mp3_buffer_size));
         // Consider resizing or logging a more critical error if issues occur
    }

    // Ensure stereo_int32_buffer has enough samples for the processed frames
    // Check if the buffer size is at least the number of samples returned by processAudio
    if (stereo_int32_buffer.size() < static_cast<size_t>(processed_total_samples)) {
         LOG_ERROR(config_.sink_id, "AudioProcessor output buffer size mismatch. Has: " + std::to_string(stereo_int32_buffer.size()) + ", Needs at least: " + std::to_string(processed_total_samples));
         return; // Avoid encoding potentially corrupted data
    }

    // Use lame_encode_buffer_interleaved_int for direct int32_t input
    int mp3_bytes_encoded = lame_encode_buffer_interleaved_int(
        lame_global_flags_,
        stereo_int32_buffer.data(),         // Pass the int32_t stereo buffer directly
        processed_frames_per_channel,       // Pass the number of frames actually processed
        mp3_encode_buffer_.data(),
        mp3_encode_buffer_.size()           // Max size of output buffer
    );

    if (mp3_bytes_encoded < 0) {
        LOG_ERROR(config_.sink_id, "LAME encoding failed with code: " + std::to_string(mp3_bytes_encoded));
    } else if (mp3_bytes_encoded > 0) {
        EncodedMP3Data mp3_data;
        mp3_data.mp3_data.assign(mp3_encode_buffer_.begin(), mp3_encode_buffer_.begin() + mp3_bytes_encoded);
        mp3_output_queue_->push(std::move(mp3_data));
    }
}


void SinkAudioMixer::run() {
    LOG(config_.sink_id, "Entering run loop.");

    LOG_DEBUG(config_.sink_id, "RunLoop: Starting iteration.");
    while (!stop_flag_) {
        // 1. Wait for and retrieve data from source queues
        LOG_DEBUG(config_.sink_id, "RunLoop: Waiting for source data...");
        bool data_available = wait_for_source_data(INPUT_WAIT_TIMEOUT);
        LOG_DEBUG(config_.sink_id, "RunLoop: Wait finished. Data available: " << data_available);

        if (stop_flag_) {
             LOG_DEBUG(config_.sink_id, "RunLoop: Stop flag checked after wait, breaking.");
             break; // Check flag again after potentially waiting
        }

        // Lock queues mutex for mixing and state access
        std::unique_lock<std::mutex> lock(queues_mutex_);

        if (data_available || !input_queues_.empty()) { // Mix even if no new data, using last known buffer state
            LOG_DEBUG(config_.sink_id, "RunLoop: Data available or queues not empty, proceeding to mix.");
            // 2. Mix data from active source buffers
            LOG_DEBUG(config_.sink_id, "RunLoop: Mixing buffers...");
            mix_buffers();
            LOG_DEBUG(config_.sink_id, "RunLoop: Mixing complete.");

            // Unlock queues mutex before potentially long operations
            lock.unlock();
            LOG_DEBUG(config_.sink_id, "RunLoop: Queues mutex unlocked.");

            // 3. Encode to MP3 (if enabled)
            encode_and_push_mp3();

            // 4. Downscale mixed buffer to network format
            LOG_DEBUG(config_.sink_id, "RunLoop: Downscaling buffer...");
            downscale_buffer(); // Appends to output_network_buffer_
            LOG_DEBUG(config_.sink_id, "RunLoop: Downscaling complete. WritePos=" << output_buffer_write_pos_);

            // 5. Send network data if a full packet's worth of *payload* bytes has been accumulated
            // We check against SINK_CHUNK_SIZE_BYTES (1152) because that's the payload size we accumulate via downscale_buffer
            if (output_buffer_write_pos_ >= SINK_CHUNK_SIZE_BYTES) {
                LOG_DEBUG(config_.sink_id, "RunLoop: Output buffer ready to send. WritePos=" << output_buffer_write_pos_ << " bytes. ChunkSizeBytes=" << SINK_CHUNK_SIZE_BYTES << " bytes.");
                // Send the first packet (from start of double buffer), total size includes header
                send_network_buffer(SINK_PACKET_SIZE_BYTES); // Send 1157 bytes (header + 1152 payload)
                LOG_DEBUG(config_.sink_id, "RunLoop: Network buffer sent (size=" << SINK_PACKET_SIZE_BYTES << ").");

                // Shift the remaining data (if any) from the second half of the buffer to the beginning (after header space)
                size_t bytes_remaining_after_send = output_buffer_write_pos_ - SINK_CHUNK_SIZE_BYTES;
                LOG_DEBUG(config_.sink_id, "RunLoop: Rotating output buffer... Shifting " << bytes_remaining_after_send << " bytes from second half to start.");
                if (bytes_remaining_after_send > 0) {
                    // Use memmove for potentially overlapping regions.
                    // Source: Start of the data *after* the first sent chunk (at index SINK_PACKET_SIZE_BYTES)
                    // Destination: Start of the payload area (at index SINK_HEADER_SIZE)
                    // Length: The number of bytes remaining after sending the first chunk
                    memmove(output_network_buffer_.data() + SINK_HEADER_SIZE,      // Dest
                           output_network_buffer_.data() + SINK_PACKET_SIZE_BYTES, // Src
                           bytes_remaining_after_send);                            // Len
                }
                LOG_DEBUG(config_.sink_id, "RunLoop: Output buffer rotated.");

                // Adjust write position to reflect the remaining data
                output_buffer_write_pos_ = bytes_remaining_after_send; // New write pos is the number of bytes shifted
                LOG_DEBUG(config_.sink_id, "RunLoop: Adjusted write pos to " << output_buffer_write_pos_);
            } else {
                 LOG_DEBUG(config_.sink_id, "RunLoop: Output buffer not full enough yet for payload. WritePos=" << output_buffer_write_pos_ << " bytes. Need=" << SINK_CHUNK_SIZE_BYTES << " bytes.");
            }
        } else {
            // No input queues connected or no data available from wait_for_source_data
            LOG_DEBUG(config_.sink_id, "RunLoop: No data available and input queues empty. Sleeping briefly.");
            lock.unlock();
            // Add a small sleep to prevent busy-waiting if there are truly no inputs or no data
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Sleep 10ms
        }
        LOG_DEBUG(config_.sink_id, "RunLoop: End of iteration.");
    } // End while loop

    LOG(config_.sink_id, "Exiting run loop.");
}
