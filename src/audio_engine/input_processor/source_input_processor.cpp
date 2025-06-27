#include "source_input_processor.h"
#include "../utils/cpp_logger.h" // For new C++ logger
#include <iostream> // For logging (cpp_logger fallback)
#include <stdexcept>
#include <cstring> // For memcpy
#include <algorithm> // For std::min, std::max
#include <cmath>     // For std::chrono durations
#include <thread>    // For sleep_for

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// Use namespaces for clarity
using namespace screamrouter::audio;
using namespace screamrouter::audio::utils;
 
// Old logger macros are removed. New macros (LOG_CPP_INFO, etc.) are in cpp_logger.h
// The instance_id from config_ will be manually prepended in the new log calls.

// Define how often to cleanup the timeshift buffer (e.g., every second)
const std::chrono::milliseconds TIMESIFT_CLEANUP_INTERVAL(1000);

// Constants are now defined in the header file (.h)


SourceInputProcessor::SourceInputProcessor(
    SourceProcessorConfig config, // config now includes instance_id
    std::shared_ptr<InputPacketQueue> input_queue,
    std::shared_ptr<OutputChunkQueue> output_queue,
    std::shared_ptr<CommandQueue> command_queue)
    : config_(std::move(config)), // Use std::move for config
      input_queue_(input_queue),
      output_queue_(output_queue),
      command_queue_(command_queue),
      current_volume_(config_.initial_volume), // Initialize from moved config_
      current_eq_(config_.initial_eq),
      current_delay_ms_(config_.initial_delay_ms),
      // current_timeshift_backshift_sec_ is removed as a direct controller
      current_timeshift_backshift_sec_config_(config_.initial_timeshift_sec), // Initialize from config
      // current_speaker_layouts_map_ is default-initialized (empty)
      // Old speaker mix members initializations are removed:
      // current_use_auto_speaker_mix_(config_.use_auto_speaker_mix),
      // current_speaker_mix_matrix_(config_.speaker_mix_matrix),
      m_current_ap_input_channels(0), // Initialize current format state
      m_current_ap_input_samplerate(0),
      m_current_ap_input_bitdepth(0)
{
    // current_speaker_layouts_map_ will be populated by set_speaker_layouts_config
    // or when AudioProcessor is created if SourceProcessorConfig is updated.
    // For now, it starts empty.

    // Use the new LOG macro which includes instance_id
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

    // initialize_audio_processor(); // Removed
    audio_processor_ = nullptr; // Set audio_processor_ to nullptr initially
    LOG_CPP_INFO("[SourceProc:%s] Initialization complete.", config_.instance_id.c_str());
}

SourceInputProcessor::~SourceInputProcessor() {
    LOG_CPP_INFO("[SourceProc:%s] Destroying...", config_.instance_id.c_str());
    if (!stop_flag_) {
        LOG_CPP_INFO("[SourceProc:%s] Destructor called while still running. Stopping...", config_.instance_id.c_str());
        stop(); // Ensure stop logic is triggered if not already stopped
    }
    // Join input_thread_ here (output_thread_ is removed)
    if (input_thread_.joinable()) {
        LOG_CPP_INFO("[SourceProc:%s] Joining input thread in destructor...", config_.instance_id.c_str());
        try {
            input_thread_.join();
            LOG_CPP_INFO("[SourceProc:%s] Input thread joined.", config_.instance_id.c_str());
        } catch (const std::system_error& e) {
            LOG_CPP_ERROR("[SourceProc:%s] Error joining input thread in destructor: %s", config_.instance_id.c_str(), e.what());
        }
    }
    // timeshift_condition_ is removed, so no notification cleanup needed.
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
    return stats;
}
// --- Initialization & Configuration ---



void SourceInputProcessor::set_speaker_layouts_config(const std::map<int, screamrouter::audio::CppSpeakerLayout>& layouts_map) { // Changed to audio namespace
    std::lock_guard<std::mutex> lock(processor_config_mutex_); // Protect map and processor access
    current_speaker_layouts_map_ = layouts_map;
    LOG_CPP_DEBUG("[SourceProc:%s] Received %zu speaker layouts.", config_.instance_id.c_str(), layouts_map.size());

    if (audio_processor_) {
        // AudioProcessor needs a method to accept this map (will be added in Task 17.11)
        audio_processor_->update_speaker_layouts_config(current_speaker_layouts_map_);
        LOG_CPP_DEBUG("[SourceProc:%s] Updated AudioProcessor with new speaker layouts.", config_.instance_id.c_str());
    }
}

// void SourceInputProcessor::initialize_audio_processor() { // Removed

void SourceInputProcessor::start() {
     if (is_running()) {
        LOG_CPP_INFO("[SourceProc:%s] Already running.", config_.instance_id.c_str());
        return;
    }
    LOG_CPP_INFO("[SourceProc:%s] Starting...", config_.instance_id.c_str());
    // Reset state specific to this component
    // timeshift_buffer_read_idx_ = 0; // Removed
    process_buffer_.clear();
    // Implementation for start: set flag, launch thread
    stop_flag_ = false; // Reset stop flag before launching threads
    try {
        // Only launch the main component thread here.
        // run() will launch the worker threads.
        component_thread_ = std::thread(&SourceInputProcessor::run, this);
        LOG_CPP_INFO("[SourceProc:%s] Component thread launched (will start workers).", config_.instance_id.c_str());
    } catch (const std::system_error& e) {
        LOG_CPP_ERROR("[SourceProc:%s] Failed to start component thread: %s", config_.instance_id.c_str(), e.what());
        stop_flag_ = true; // Ensure stopped state if launch fails
        // timeshift_condition_ is removed
        if(input_queue_) input_queue_->stop(); // Ensure queues are stopped
        if(command_queue_) command_queue_->stop();
        // Rethrow or handle error appropriately
        throw; // Rethrow to signal failure
    }
}


void SourceInputProcessor::stop() {
    if (stop_flag_) {
        LOG_CPP_INFO("[SourceProc:%s] Already stopped or stopping.", config_.instance_id.c_str());
        return;
    }
    LOG_CPP_INFO("[SourceProc:%s] Stopping...", config_.instance_id.c_str());

    // Set the stop flag FIRST (used by loops)
    stop_flag_ = true; // Set the atomic flag

    // Notify condition variables/queues AFTER setting stop_flag_
    // timeshift_condition_.notify_all(); // Removed
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
            default:
                LOG_CPP_ERROR("[SourceProc:%s] Unknown command type received.", config_.instance_id.c_str());
                break;
        }
    }
}

// handle_new_input_packet, update_timeshift_target_time, cleanup_timeshift_buffer, check_readiness_condition are removed.

void SourceInputProcessor::process_audio_chunk(const std::vector<uint8_t>& input_chunk_data) {
    if (!audio_processor_) {
        LOG_CPP_ERROR("[SourceProc:%s] AudioProcessor not initialized. Cannot process chunk.", config_.instance_id.c_str());
        return;
    }
    size_t input_bytes = input_chunk_data.size();
    LOG_CPP_DEBUG("[SourceProc:%s] ProcessAudio: Processing chunk. Input Size=%zu bytes. Expected=%zu bytes.", config_.instance_id.c_str(), input_bytes, static_cast<size_t>(CHUNK_SIZE));
    if (input_bytes != CHUNK_SIZE) { // Use constant CHUNK_SIZE
         LOG_CPP_ERROR("[SourceProc:%s] process_audio_chunk called with incorrect data size: %zu. Skipping processing.", config_.instance_id.c_str(), input_bytes);
         return;
    }
    
    // Allocate a temporary output buffer large enough to hold the maximum possible output
    // from AudioProcessor::processAudio. Match the size of AudioProcessor's internal processed_buffer.
    // Size = CHUNK_SIZE * MAX_CHANNELS * 4 = 1152 * 8 * 4 = 36864 samples
    std::vector<int32_t> processor_output_buffer(CHUNK_SIZE * MAX_CHANNELS * 4);

    int actual_samples_processed = 0; // Renamed variable for clarity
    { // Lock mutex for accessing AudioProcessor
        std::lock_guard<std::mutex> lock(processor_config_mutex_);
        if (!audio_processor_) {
             LOG_CPP_ERROR("[SourceProc:%s] AudioProcessor is null during process_audio_chunk call.", config_.instance_id.c_str());
             return; // Cannot proceed without a valid processor
        }
        // Pass the data pointer and size (CHUNK_SIZE)
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
    // Check if we have enough samples for a full output chunk
    size_t required_samples = OUTPUT_CHUNK_SAMPLES; // Should be 576 for 16-bit stereo sink target
    size_t current_buffer_size = process_buffer_.size();

    LOG_CPP_DEBUG("[SourceProc:%s] PushOutput: Checking buffer. Current=%zu samples. Required=%zu samples.", config_.instance_id.c_str(), current_buffer_size, required_samples);

    while (output_queue_ && current_buffer_size >= required_samples) { // Check queue pointer
        ProcessedAudioChunk output_chunk;
        // Copy the required number of samples
         output_chunk.audio_data.assign(process_buffer_.begin(), process_buffer_.begin() + required_samples);
         output_chunk.ssrcs = current_packet_ssrcs_;
         size_t pushed_samples = output_chunk.audio_data.size();

         // Push to the output queue
         LOG_CPP_DEBUG("[SourceProc:%s] PushOutput: Pushing chunk with %zu samples (Expected=%zu) to Sink queue.", config_.instance_id.c_str(), pushed_samples, required_samples);
         if (pushed_samples != required_samples) {
             LOG_CPP_ERROR("[SourceProc:%s] PushOutput: Mismatch between pushed samples (%zu) and required samples (%zu).", config_.instance_id.c_str(), pushed_samples, required_samples);
         }
         output_queue_->push(std::move(output_chunk));

         // Remove the copied samples from the process buffer
        process_buffer_.erase(process_buffer_.begin(), process_buffer_.begin() + required_samples);
        current_buffer_size = process_buffer_.size(); // Update size after erasing

        LOG_CPP_DEBUG("[SourceProc:%s] PushOutput: Pushed chunk. Remaining process_buffer_ size=%zu samples.", config_.instance_id.c_str(), current_buffer_size);
    }
}

// --- New/Modified Thread Loops ---

void SourceInputProcessor::input_loop() {
    LOG_CPP_INFO("[SourceProc:%s] Input loop started.", config_.instance_id.c_str());
    TaggedAudioPacket new_packet;
    // Loop exits when pop returns false (queue stopped and empty) or stop_flag_ is set
    // This loop now receives packets already timed by TimeshiftManager.
    LOG_CPP_INFO("[SourceProc:%s] Input loop started (receives timed packets).", config_.instance_id.c_str());
    TaggedAudioPacket timed_packet;
    while (!stop_flag_ && input_queue_ && input_queue_->pop(timed_packet)) {
        m_total_packets_processed++;
        // Packet is already timed correctly by TimeshiftManager.
        // Directly proceed to format checking and processing.
        const uint8_t* audio_payload_ptr = nullptr;
        size_t audio_payload_size = 0;
        
        bool packet_ok_for_processing = check_format_and_reconfigure(
            timed_packet,
            &audio_payload_ptr,
            &audio_payload_size
        );

        if (packet_ok_for_processing && audio_processor_) {
            if (audio_payload_ptr && audio_payload_size == INPUT_CHUNK_BYTES) { // CHUNK_SIZE is INPUT_CHUNK_BYTES
                current_packet_ssrcs_ = timed_packet.ssrcs;
                std::vector<uint8_t> chunk_data_for_processing(audio_payload_ptr, audio_payload_ptr + audio_payload_size);
                process_audio_chunk(chunk_data_for_processing);
                push_output_chunk_if_ready();
            } else {
                LOG_CPP_ERROR("[SourceProc:%s] Audio payload invalid after check_format_and_reconfigure. Size: %zu", config_.instance_id.c_str(), audio_payload_size);
            }
        } else if (!packet_ok_for_processing) {
            LOG_CPP_WARNING("[SourceProc:%s] Packet discarded by input_loop due to format/size issues or no audio processor.", config_.instance_id.c_str());
        }
    }
    LOG_CPP_INFO("[SourceProc:%s] Input loop exiting. StopFlag=%d", config_.instance_id.c_str(), stop_flag_.load());
}

// output_loop() is removed entirely.

bool SourceInputProcessor::check_format_and_reconfigure(
    const TaggedAudioPacket& packet,
    const uint8_t** out_audio_payload_ptr,
    size_t* out_audio_payload_size)
{
    LOG_CPP_DEBUG("[SourceProc:%s] Entering check_format_and_reconfigure for packet from tag: %s", config_.instance_id.c_str(), packet.source_tag.c_str());
    
    // --- Use format directly from packet ---
    int target_ap_input_channels = packet.channels;
    int target_ap_input_samplerate = packet.sample_rate;
    int target_ap_input_bitdepth = packet.bit_depth;
    // Channel layout bytes (packet.chlayout1, packet.chlayout2) are available but not directly used by AudioProcessor constructor
    const uint8_t* audio_data_start = packet.audio_data.data(); // Payload is always 1152 bytes now
    size_t audio_data_len = packet.audio_data.size();

    // --- Validate Packet Format and Size ---
    if (audio_data_len != CHUNK_SIZE) {
         LOG_CPP_ERROR("[SourceProc:%s] Incorrect audio payload size. Expected %zu, got %zu", config_.instance_id.c_str(), static_cast<size_t>(CHUNK_SIZE), audio_data_len);
         return false;
    }
     if (target_ap_input_channels <= 0 || target_ap_input_channels > 8 ||
         (target_ap_input_bitdepth != 8 && target_ap_input_bitdepth != 16 && target_ap_input_bitdepth != 24 && target_ap_input_bitdepth != 32) ||
         target_ap_input_samplerate <= 0) {
         LOG_CPP_ERROR("[SourceProc:%s] Invalid format info in packet. SR=%d, BD=%d, CH=%d",
                       config_.instance_id.c_str(), target_ap_input_samplerate, target_ap_input_bitdepth, target_ap_input_channels);
         return false;
     }
     LOG_CPP_DEBUG("[SourceProc:%s] Packet Format: CH=%d SR=%d BD=%d",
                   config_.instance_id.c_str(), target_ap_input_channels, target_ap_input_samplerate, target_ap_input_bitdepth);


    // --- Check if Reconfiguration is Needed ---
    bool needs_reconfig = !audio_processor_ ||
                          m_current_ap_input_channels != target_ap_input_channels ||
                          m_current_ap_input_samplerate != target_ap_input_samplerate ||
                          m_current_ap_input_bitdepth != target_ap_input_bitdepth;
   
   LOG_CPP_DEBUG("[SourceProc:%s] Current AP Format: CH=%d SR=%d BD=%d",
                 config_.instance_id.c_str(), m_current_ap_input_channels, m_current_ap_input_samplerate, m_current_ap_input_bitdepth);
   LOG_CPP_DEBUG("[SourceProc:%s] Needs Reconfiguration Check: audio_processor_ null? %s, CH mismatch? %s, SR mismatch? %s, BD mismatch? %s",
                 config_.instance_id.c_str(),
                 (!audio_processor_ ? "Yes" : "No"),
                 (m_current_ap_input_channels != target_ap_input_channels ? "Yes" : "No"),
                 (m_current_ap_input_samplerate != target_ap_input_samplerate ? "Yes" : "No"),
                 (m_current_ap_input_bitdepth != target_ap_input_bitdepth ? "Yes" : "No"));
   LOG_CPP_DEBUG("[SourceProc:%s] Result of needs_reconfig: %s", config_.instance_id.c_str(), (needs_reconfig ? "true" : "false"));


   if (needs_reconfig) {
       LOG_CPP_DEBUG("[SourceProc:%s] Entering reconfiguration block...", config_.instance_id.c_str()); // Log entry into the block
       // Add logging here to show the change
       if (audio_processor_) { // Log only if it's a change, not initial creation
            LOG_CPP_WARNING("[SourceProc:%s] Audio format changed! Reconfiguring AudioProcessor. Old Format: CH=%d SR=%d BD=%d. New Format: CH=%d SR=%d BD=%d",
                            config_.instance_id.c_str(), m_current_ap_input_channels, m_current_ap_input_samplerate, m_current_ap_input_bitdepth,
                            target_ap_input_channels, target_ap_input_samplerate, target_ap_input_bitdepth);
       } else {
            LOG_CPP_INFO("[SourceProc:%s] Initializing AudioProcessor for the first time. Format: CH=%d SR=%d BD=%d",
                         config_.instance_id.c_str(), target_ap_input_channels, target_ap_input_samplerate, target_ap_input_bitdepth);
       }
       
       // Lock processor_config_mutex_ to protect all configuration and the processor itself
       std::lock_guard<std::mutex> lock(processor_config_mutex_);
       LOG_CPP_INFO("[SourceProc:%s] Reconfiguring AudioProcessor: Input CH=%d SR=%d BD=%d -> Output CH=%d SR=%d",
                    config_.instance_id.c_str(), target_ap_input_channels, target_ap_input_samplerate, target_ap_input_bitdepth,
                    config_.output_channels, config_.output_samplerate);
        try {
            audio_processor_ = std::make_unique<AudioProcessor>(
                target_ap_input_channels,
                config_.output_channels,    // Target output format from SIP config
                target_ap_input_bitdepth,
                target_ap_input_samplerate,
                config_.output_samplerate,  // Target output format from SIP config
                current_volume_,
                current_speaker_layouts_map_ // Pass the currently configured speaker layouts
            );
            // The following lines will be adjusted once AudioProcessor constructor takes the map.
            // For now, this is how it would be if AudioProcessor still took individual settings.
            // This will be superseded by passing the map to the constructor.
            // audio_processor_->setEqualizer(current_eq_.data()); 

            // --- Initialize AudioProcessor with current_speaker_layouts_map_ ---
            // This requires AudioProcessor constructor to be updated (Task 17.11)
            // For now, we'll assume the constructor takes it.
            // If not, we'd call audio_processor_->update_speaker_layouts_config() here.
            // The line above creating AudioProcessor will be modified in Task 17.11 to pass the map.
            // For this task, we ensure the map is ready.
            // The actual application of the map to the new AudioProcessor instance
            // will happen via its constructor or an immediate call to update_speaker_layouts_config.
            // The task description for 17.10 says:
            // "When a new AudioProcessor instance is created... it needs to be initialized with the current_speaker_layouts_map_."
            // This implies the constructor of AudioProcessor will take it.
            // So, the existing call to make_unique<AudioProcessor> will be modified in task 17.11.
            // Here, we just ensure current_speaker_layouts_map_ is available.
            // The old direct application of speaker_mix_matrix/auto_mode is removed.
            
            // Apply other settings like EQ after construction
            audio_processor_->setEqualizer(current_eq_.data());
            
            // The AudioProcessor itself will use its copy of speaker_layouts_map
            // to select the appropriate mix based on its inputChannels.
            // No direct call to calculateAndApplyAutoSpeakerMix or applyCustomSpeakerMix here.

            m_current_ap_input_channels = target_ap_input_channels;
            m_current_ap_input_samplerate = target_ap_input_samplerate;
            m_current_ap_input_bitdepth = target_ap_input_bitdepth;
            LOG_CPP_INFO("[SourceProc:%s] AudioProcessor reconfigured successfully.", config_.instance_id.c_str());
        } catch (const std::exception& e) {
            LOG_CPP_ERROR("[SourceProc:%s] Failed to reconfigure AudioProcessor: %s", config_.instance_id.c_str(), e.what());
            audio_processor_.reset(); // Ensure it's null on failure
            return false; // Reconfiguration failed
        }
    }

    *out_audio_payload_ptr = audio_data_start;
    *out_audio_payload_size = audio_data_len;
    return true;
}

// run() is executed by component_thread_. It now starts only input_thread_ and processes commands.
void SourceInputProcessor::run() {
     LOG_CPP_INFO("[SourceProc:%s] Component run() started.", config_.instance_id.c_str());

     // Launch input_thread_ (which now contains the main processing logic)
     try {
        input_thread_ = std::thread(&SourceInputProcessor::input_loop, this);
        LOG_CPP_INFO("[SourceProc:%s] Input thread launched by run().", config_.instance_id.c_str());
        // output_thread_ is removed
     } catch (const std::system_error& e) {
         LOG_CPP_ERROR("[SourceProc:%s] Failed to start input_thread_ from run(): %s", config_.instance_id.c_str(), e.what());
         stop_flag_ = true; // Signal stop if thread failed to launch
         // timeshift_condition_ is removed
         if(input_queue_) input_queue_->stop();
         if(command_queue_) command_queue_->stop();
         return; // Exit run() if input_thread_ failed
     }

     // Command processing loop
     LOG_CPP_INFO("[SourceProc:%s] Starting command processing loop.", config_.instance_id.c_str());
     while (!stop_flag_) {
         process_commands(); // Check for commands

         // Sleep briefly to prevent busy-waiting when no commands are pending
         std::this_thread::sleep_for(std::chrono::milliseconds(20)); // Or use command_queue_->wait_for_data() if available
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
     // output_thread_ joining is removed.

     LOG_CPP_INFO("[SourceProc:%s] Component run() exiting.", config_.instance_id.c_str());
}
