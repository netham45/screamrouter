#include "source_input_processor.h"
#include <iostream> // For logging
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
namespace screamrouter { namespace audio { using namespace utils; } }
using namespace screamrouter::audio;
using namespace screamrouter::utils;
 
// Simple logger helper (replace with a proper logger if available)
// Updated logger macros to use instance_id from config_
#define LOG(msg) std::cout << "[SourceProc:" << config_.instance_id << "] " << msg << std::endl
//#define LOG_ERROR(msg) std::cerr << "[SourceProc Error:" << config_.instance_id << "] " << msg << std::endl
//#define LOG_WARN(msg) std::cout << "[SourceProc Warn:" << config_.instance_id << "] " << msg << std::endl // Added WARN
//#define LOG_DEBUG(msg) std::cout << "[SourceProc Debug:" << config_.instance_id << "] " << msg << std::endl // For verbose logging

//#define LOG(msg) // Disable standard logs
#define LOG_ERROR(msg) // Disable error logs
#define LOG_WARN(msg) // Disable warn logs
#define LOG_DEBUG(msg) // Disable debug logs

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
    LOG("Initializing...");
    if (!input_queue_ || !output_queue_ || !command_queue_) {
        // Log before throwing
        LOG_ERROR("Initialization failed: Requires valid input, output, and command queues.");
        throw std::runtime_error("SourceInputProcessor requires valid input, output, and command queues.");
    }
    // Ensure EQ vector has the correct size if provided, otherwise initialize default
    if (current_eq_.size() != EQ_BANDS) {
        LOG("Warning: Initial EQ size mismatch (" + std::to_string(current_eq_.size()) + " vs " + std::to_string(EQ_BANDS) + "). Resetting to default (flat).");
        current_eq_.assign(EQ_BANDS, 1.0f);
        config_.initial_eq = current_eq_; // Update config_ member
    }

    // initialize_audio_processor(); // Removed
    audio_processor_ = nullptr; // Set audio_processor_ to nullptr initially
    LOG("Initialization complete.");
}

SourceInputProcessor::~SourceInputProcessor() {
    LOG("Destroying...");
    if (!stop_flag_) {
        LOG("Destructor called while still running. Stopping...");
        stop(); // Ensure stop logic is triggered if not already stopped
    }
    // Join input_thread_ here (output_thread_ is removed)
    if (input_thread_.joinable()) {
        LOG("Joining input thread in destructor...");
        try {
            input_thread_.join();
            LOG("Input thread joined.");
        } catch (const std::system_error& e) {
            LOG_ERROR("Error joining input thread in destructor: " + std::string(e.what()));
        }
    }
    // timeshift_condition_ is removed, so no notification cleanup needed.
    LOG("Destructor finished.");
}

// --- Getters ---

const std::string& SourceInputProcessor::get_source_tag() const {
    // This getter is needed by AudioManager to interact with RtpReceiver/SinkMixer
    // which might still rely on the original source tag (IP) until fully refactored.
    return config_.source_tag;
}

// --- Plugin Data Injection ---
    // Method void SourceInputProcessor::inject_plugin_packet(...) is removed.


// --- Initialization & Configuration ---

void SourceInputProcessor::set_speaker_layouts_config(const std::map<int, screamrouter::audio::CppSpeakerLayout>& layouts_map) { // Changed to audio namespace
    std::lock_guard<std::mutex> lock(speaker_layouts_mutex_); // Protect map access
    current_speaker_layouts_map_ = layouts_map;
    LOG_DEBUG("Received " + std::to_string(layouts_map.size()) + " speaker layouts.");

    std::lock_guard<std::mutex> ap_lock(audio_processor_mutex_); // Protect audio_processor_ access
    if (audio_processor_) {
        // AudioProcessor needs a method to accept this map (will be added in Task 17.11)
        audio_processor_->update_speaker_layouts_config(current_speaker_layouts_map_);
        LOG_DEBUG("Updated AudioProcessor with new speaker layouts.");
    }
}

// void SourceInputProcessor::initialize_audio_processor() { // Removed

void SourceInputProcessor::start() {
     if (is_running()) {
        LOG("Already running.");
        return;
    }
    LOG("Starting...");
    // Reset state specific to this component
    // timeshift_buffer_read_idx_ = 0; // Removed
    process_buffer_.clear();
    // Implementation for start: set flag, launch thread
    stop_flag_ = false; // Reset stop flag before launching threads
    try {
        // Only launch the main component thread here.
        // run() will launch the worker threads.
        component_thread_ = std::thread(&SourceInputProcessor::run, this);
        LOG("Component thread launched (will start workers).");
    } catch (const std::system_error& e) {
        LOG_ERROR("Failed to start component thread: " + std::string(e.what()));
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
        LOG("Already stopped or stopping.");
        return;
    }
    LOG("Stopping...");

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
            LOG("Component thread joined.");
        } catch (const std::system_error& e) {
            LOG_ERROR("Error joining component thread: " + std::string(e.what()));
        }
    } else {
         LOG("Component thread was not joinable in stop().");
    }
    // Joining of input/output threads happens in run() or destructor.
}


void SourceInputProcessor::process_commands() {
    ControlCommand cmd;
    // Use try_pop for non-blocking check. Loop while queue is valid and not stopped.
    while (command_queue_ && !command_queue_->is_stopped() && command_queue_->try_pop(cmd)) {
        LOG_DEBUG("Processing command: " + std::to_string(static_cast<int>(cmd.type)));
        bool needs_processor_settings_update = false; // Renamed for clarity
        // bool needs_timeshift_update = false; // Removed

        { // Scope for audio_processor_mutex_ lock
            std::lock_guard<std::mutex> lock(audio_processor_mutex_); // Protects current settings and audio_processor_ calls
            // timeshift_mutex_ is removed

            switch (cmd.type) {
                case CommandType::SET_VOLUME:
                    current_volume_ = cmd.float_value;
                    needs_processor_settings_update = true;
                    break;
                case CommandType::SET_EQ:
                    if (cmd.eq_values.size() == EQ_BANDS) {
                        current_eq_ = cmd.eq_values;
                        needs_processor_settings_update = true;
                    } else {
                        LOG_ERROR("Invalid EQ size in command: " + std::to_string(cmd.eq_values.size()));
                    }
                    break;
                case CommandType::SET_DELAY:
                    current_delay_ms_ = cmd.int_value;
                    // Notify AudioManager to update TimeshiftManager
                    // This requires a callback or similar mechanism to AudioManager, not implemented here.
                    LOG_DEBUG("SET_DELAY command processed. New delay: " + std::to_string(current_delay_ms_) + "ms. AudioManager should be notified.");
                    // needs_timeshift_update = true; // Removed
                    break;
                case CommandType::SET_TIMESHIFT:
                    current_timeshift_backshift_sec_config_ = cmd.float_value;
                    // Notify AudioManager to update TimeshiftManager
                    LOG_DEBUG("SET_TIMESHIFT command processed. New timeshift: " + std::to_string(current_timeshift_backshift_sec_config_) + "s. AudioManager should be notified.");
                    // needs_timeshift_update = true; // Removed
                    break;
                // --- New Case for SET_SPEAKER_MIX ---
                case CommandType::SET_SPEAKER_MIX:
                    // This command will now update a specific key in current_speaker_layouts_map_
                    // Assuming cmd struct is updated (Task 17.12) to have:
                    // cmd.input_channel_key (int)
                    // cmd.speaker_layout_for_key (screamrouter::audio::CppSpeakerLayout) // Changed to audio namespace
                    { // New scope for layout_lock
                        std::lock_guard<std::mutex> layout_lock(speaker_layouts_mutex_);
                        current_speaker_layouts_map_[cmd.input_channel_key] = cmd.speaker_layout_for_key;
                        LOG_DEBUG("SET_SPEAKER_MIX command processed for key: " + std::to_string(cmd.input_channel_key) +
                                  ". Auto mode: " + std::string(cmd.speaker_layout_for_key.auto_mode ? "true" : "false"));
                    } // layout_lock released
                    // Signal that audio_processor_ needs its entire map updated
                    // This will be handled by needs_processor_settings_update logic below,
                    // which will call audio_processor_->update_speaker_layouts_config()
                    needs_processor_settings_update = true; 
                    break;
                default:
                    LOG_ERROR("Unknown command type received.");
                    break;
            }
        } // audio_processor_mutex_ released here

        if (needs_processor_settings_update) {
             std::lock_guard<std::mutex> lock(audio_processor_mutex_);
             if (audio_processor_) {
                 LOG_DEBUG("Applying processor update for command: " + std::to_string(static_cast<int>(cmd.type)));
                 if (cmd.type == CommandType::SET_VOLUME) {
                     audio_processor_->setVolume(current_volume_);
                 } else if (cmd.type == CommandType::SET_EQ) {
                     if (current_eq_.size() == EQ_BANDS) {
                         audio_processor_->setEqualizer(current_eq_.data());
                     } else {
                          LOG_ERROR("EQ data size mismatch during apply. Skipping EQ update.");
                     }
                 } 
                 // For SET_SPEAKER_MIX, we update the entire map on the AudioProcessor
                 // This is because the command modified one entry in our local map,
                 // and AudioProcessor needs the full context.
                 // This part of the logic assumes AudioProcessor has update_speaker_layouts_config.
                 if (cmd.type == CommandType::SET_SPEAKER_MIX) { // Could also be part of a general update
                    std::lock_guard<std::mutex> layout_lock(speaker_layouts_mutex_); // Lock for reading current_speaker_layouts_map_
                    audio_processor_->update_speaker_layouts_config(current_speaker_layouts_map_);
                    LOG_DEBUG("Applied updated speaker_layouts_map to AudioProcessor due to SET_SPEAKER_MIX command.");
                 }
             } else {
                  LOG_WARN("Command received but AudioProcessor is null. Cannot apply processor update.");
             }
        }
        // timeshift_condition_.notify_one(); // Removed
    }
}

// handle_new_input_packet, update_timeshift_target_time, cleanup_timeshift_buffer, check_readiness_condition are removed.

void SourceInputProcessor::process_audio_chunk(const std::vector<uint8_t>& input_chunk_data) {
    if (!audio_processor_) {
        LOG_ERROR("AudioProcessor not initialized. Cannot process chunk.");
        return;
    }
    size_t input_bytes = input_chunk_data.size();
    LOG_DEBUG("ProcessAudio: Processing chunk. Input Size=" + std::to_string(input_bytes) + " bytes. Expected=" + std::to_string(CHUNK_SIZE) + " bytes.");
    if (input_bytes != CHUNK_SIZE) { // Use constant CHUNK_SIZE
         LOG_ERROR("process_audio_chunk called with incorrect data size: " + std::to_string(input_bytes) + ". Skipping processing.");
         return;
    }
    
    // Allocate a temporary output buffer large enough to hold the maximum possible output
    // from AudioProcessor::processAudio. Match the size of AudioProcessor's internal processed_buffer.
    // Size = CHUNK_SIZE * MAX_CHANNELS * 4 = 1152 * 8 * 4 = 36864 samples
    std::vector<int32_t> processor_output_buffer(CHUNK_SIZE * MAX_CHANNELS * 4); 

    int actual_samples_processed = 0; // Renamed variable for clarity
    { // Lock mutex for accessing AudioProcessor
        std::lock_guard<std::mutex> lock(audio_processor_mutex_);
        if (!audio_processor_) {
             LOG_ERROR("AudioProcessor is null during process_audio_chunk call.");
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
             LOG_ERROR("Failed to insert into process_buffer_: " + std::string(e.what()));
             // Handle allocation failure, maybe clear buffer or stop processing?
             process_buffer_.clear(); // Example: clear buffer to prevent further issues
             return;
        }
        LOG_DEBUG("ProcessAudio: Appended " + std::to_string(samples_to_insert) + " samples. process_buffer_ size=" + std::to_string(process_buffer_.size()) + " samples.");
    } else if (actual_samples_processed < 0) {
         // processAudio returned an error code (e.g., -1)
         LOG_ERROR("AudioProcessor::processAudio returned an error code: " + std::to_string(actual_samples_processed));
    } else {
         // processAudio returned 0 samples (e.g., no data processed or output buffer was null)
         LOG_DEBUG("ProcessAudio: AudioProcessor returned 0 samples.");
    }
}

void SourceInputProcessor::push_output_chunk_if_ready() {
    // Check if we have enough samples for a full output chunk
    size_t required_samples = OUTPUT_CHUNK_SAMPLES; // Should be 576 for 16-bit stereo sink target
    size_t current_buffer_size = process_buffer_.size();

    LOG_DEBUG("PushOutput: Checking buffer. Current=" + std::to_string(current_buffer_size) + " samples. Required=" + std::to_string(required_samples) + " samples.");

    while (output_queue_ && current_buffer_size >= required_samples) { // Check queue pointer
        ProcessedAudioChunk output_chunk;
        // Copy the required number of samples
         output_chunk.audio_data.assign(process_buffer_.begin(), process_buffer_.begin() + required_samples);
         size_t pushed_samples = output_chunk.audio_data.size();

         // Push to the output queue
         LOG_DEBUG("PushOutput: Pushing chunk with " + std::to_string(pushed_samples) + " samples (Expected=" + std::to_string(required_samples) + ") to Sink queue.");
         if (pushed_samples != required_samples) {
             LOG_ERROR("PushOutput: Mismatch between pushed samples (" + std::to_string(pushed_samples) + ") and required samples (" + std::to_string(required_samples) + ").");
         }
         output_queue_->push(std::move(output_chunk));

         // Remove the copied samples from the process buffer
        process_buffer_.erase(process_buffer_.begin(), process_buffer_.begin() + required_samples);
        current_buffer_size = process_buffer_.size(); // Update size after erasing

        LOG_DEBUG("PushOutput: Pushed chunk. Remaining process_buffer_ size=" + std::to_string(current_buffer_size) + " samples.");
    }
}

// --- New/Modified Thread Loops ---

void SourceInputProcessor::input_loop() {
    LOG("Input loop started.");
    TaggedAudioPacket new_packet;
    // Loop exits when pop returns false (queue stopped and empty) or stop_flag_ is set
    // This loop now receives packets already timed by TimeshiftManager.
    LOG("Input loop started (receives timed packets).");
    TaggedAudioPacket timed_packet;
    while (!stop_flag_ && input_queue_ && input_queue_->pop(timed_packet)) {
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
                std::vector<uint8_t> chunk_data_for_processing(audio_payload_ptr, audio_payload_ptr + audio_payload_size);
                process_audio_chunk(chunk_data_for_processing);
                push_output_chunk_if_ready();
            } else {
                LOG_ERROR("Audio payload invalid after check_format_and_reconfigure. Size: " + std::to_string(audio_payload_size));
            }
        } else if (!packet_ok_for_processing) {
            LOG_WARN("Packet discarded by input_loop due to format/size issues or no audio processor.");
        }
    }
    LOG("Input loop exiting. StopFlag=" + std::to_string(stop_flag_.load()));
}

// output_loop() is removed entirely.

bool SourceInputProcessor::check_format_and_reconfigure(
    const TaggedAudioPacket& packet,
    const uint8_t** out_audio_payload_ptr,
    size_t* out_audio_payload_size)
{
    LOG_DEBUG("Entering check_format_and_reconfigure for packet from tag: " + packet.source_tag);
    
    // --- Use format directly from packet ---
    int target_ap_input_channels = packet.channels;
    int target_ap_input_samplerate = packet.sample_rate;
    int target_ap_input_bitdepth = packet.bit_depth;
    // Channel layout bytes (packet.chlayout1, packet.chlayout2) are available but not directly used by AudioProcessor constructor
    const uint8_t* audio_data_start = packet.audio_data.data(); // Payload is always 1152 bytes now
    size_t audio_data_len = packet.audio_data.size();

    // --- Validate Packet Format and Size ---
    if (audio_data_len != CHUNK_SIZE) {
         LOG_ERROR("Incorrect audio payload size. Expected " + std::to_string(CHUNK_SIZE) + ", got " + std::to_string(audio_data_len));
         return false;
    }
     if (target_ap_input_channels <= 0 || target_ap_input_channels > 8 ||
         (target_ap_input_bitdepth != 8 && target_ap_input_bitdepth != 16 && target_ap_input_bitdepth != 24 && target_ap_input_bitdepth != 32) ||
         target_ap_input_samplerate <= 0) {
         LOG_ERROR("Invalid format info in packet. SR=" + std::to_string(target_ap_input_samplerate) +
                   ", BD=" + std::to_string(target_ap_input_bitdepth) + ", CH=" + std::to_string(target_ap_input_channels));
         return false;
     }
     LOG_DEBUG("Packet Format: CH=" + std::to_string(target_ap_input_channels) +
               " SR=" + std::to_string(target_ap_input_samplerate) + " BD=" + std::to_string(target_ap_input_bitdepth));


    // --- Check if Reconfiguration is Needed ---
    bool needs_reconfig = !audio_processor_ ||
                          m_current_ap_input_channels != target_ap_input_channels ||
                          m_current_ap_input_samplerate != target_ap_input_samplerate ||
                          m_current_ap_input_bitdepth != target_ap_input_bitdepth;
    
    LOG_DEBUG("Current AP Format: CH=" + std::to_string(m_current_ap_input_channels) + 
              " SR=" + std::to_string(m_current_ap_input_samplerate) + " BD=" + std::to_string(m_current_ap_input_bitdepth));
    LOG_DEBUG("Needs Reconfiguration Check: audio_processor_ null? " + std::string(!audio_processor_ ? "Yes" : "No") + 
              ", CH mismatch? " + std::string(m_current_ap_input_channels != target_ap_input_channels ? "Yes" : "No") +
              ", SR mismatch? " + std::string(m_current_ap_input_samplerate != target_ap_input_samplerate ? "Yes" : "No") +
              ", BD mismatch? " + std::string(m_current_ap_input_bitdepth != target_ap_input_bitdepth ? "Yes" : "No"));
    LOG_DEBUG("Result of needs_reconfig: " + std::string(needs_reconfig ? "true" : "false"));


    if (needs_reconfig) {
        LOG_DEBUG("Entering reconfiguration block..."); // Log entry into the block
        // Add logging here to show the change
        if (audio_processor_) { // Log only if it's a change, not initial creation
             LOG_WARN("Audio format changed! Reconfiguring AudioProcessor. Old Format: CH=" + std::to_string(m_current_ap_input_channels) +
                      " SR=" + std::to_string(m_current_ap_input_samplerate) + " BD=" + std::to_string(m_current_ap_input_bitdepth) +
                      ". New Format: CH=" + std::to_string(target_ap_input_channels) + " SR=" + std::to_string(target_ap_input_samplerate) +
                      " BD=" + std::to_string(target_ap_input_bitdepth));
        } else {
             LOG("Initializing AudioProcessor for the first time. Format: CH=" + std::to_string(target_ap_input_channels) +
                 " SR=" + std::to_string(target_ap_input_samplerate) + " BD=" + std::to_string(target_ap_input_bitdepth));
        }
        
        std::lock_guard<std::mutex> lock(audio_processor_mutex_);
        // Lock speaker_layouts_mutex_ before accessing current_speaker_layouts_map_
        std::lock_guard<std::mutex> layout_lock(speaker_layouts_mutex_);
        LOG("Reconfiguring AudioProcessor: Input CH=" + std::to_string(target_ap_input_channels) +
            " SR=" + std::to_string(target_ap_input_samplerate) +
            " BD=" + std::to_string(target_ap_input_bitdepth) +
            " -> Output CH=" + std::to_string(config_.output_channels) +
            " SR=" + std::to_string(config_.output_samplerate));
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
            LOG("AudioProcessor reconfigured successfully.");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to reconfigure AudioProcessor: " + std::string(e.what()));
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
     LOG("Component run() started.");

     // Launch input_thread_ (which now contains the main processing logic)
     try {
        input_thread_ = std::thread(&SourceInputProcessor::input_loop, this);
        LOG("Input thread launched by run().");
        // output_thread_ is removed
     } catch (const std::system_error& e) {
         LOG_ERROR("Failed to start input_thread_ from run(): " + std::string(e.what()));
         stop_flag_ = true; // Signal stop if thread failed to launch
         // timeshift_condition_ is removed
         if(input_queue_) input_queue_->stop();
         if(command_queue_) command_queue_->stop();
         return; // Exit run() if input_thread_ failed
     }

     // Command processing loop
     LOG("Starting command processing loop.");
     while (!stop_flag_) {
         process_commands(); // Check for commands

         // Sleep briefly to prevent busy-waiting when no commands are pending
         std::this_thread::sleep_for(std::chrono::milliseconds(20)); // Or use command_queue_->wait_for_data() if available
     }
     LOG("Command processing loop finished (stop signaled).");

     // --- Cleanup after stop_flag_ is set ---
     // Ensure input_thread_ is signaled (already done in stop()) and join it here.
     LOG("Joining input_thread_ in run()...");
     if (input_thread_.joinable()) {
         try {
             input_thread_.join();
             LOG("Input thread joined in run().");
         } catch (const std::system_error& e) {
             LOG_ERROR("Error joining input thread in run(): " + std::string(e.what()));
         }
     }
     // output_thread_ joining is removed.

     LOG("Component run() exiting.");
}
