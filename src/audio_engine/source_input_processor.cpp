#include "source_input_processor.h"
#include <iostream> // For logging
#include <stdexcept>
#include <cstring> // For memcpy
#include <algorithm> // For std::min, std::max
#include <cmath>     // For std::chrono durations
#include <thread>    // For sleep_for

// Use namespaces for clarity
namespace screamrouter { namespace audio { using namespace utils; } }
using namespace screamrouter::audio;
using namespace screamrouter::utils;
 
// Simple logger helper (replace with a proper logger if available)
// Updated logger macros to use instance_id from config_
#define LOG(msg) std::cout << "[SourceProc:" << config_.instance_id << "] " << msg << std::endl
//#define LOG_ERROR(msg) std::cerr << "[SourceProc Error:" << config_.instance_id << "] " << msg << std::endl
//#define LOG_DEBUG(msg) std::cout << "[SourceProc Debug:" << config_.instance_id << "] " << msg << std::endl // For verbose logging

//#define LOG(msg) // Disable standard logs
#define LOG_ERROR(msg) // Disable error logs
#define LOG_DEBUG(msg) // Disable debug logs

// Define how often to cleanup the timeshift buffer (e.g., every second)
const std::chrono::milliseconds TIMESIFT_CLEANUP_INTERVAL(1000);


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
      current_timeshift_backshift_sec_(0.0f) // Start with no timeshift backshift
{
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

    initialize_audio_processor();
    LOG("Initialization complete.");
}

SourceInputProcessor::~SourceInputProcessor() {
    LOG("Destroying...");
    if (!stop_flag_) {
        LOG("Destructor called while still running. Stopping...");
        stop(); // Ensure stop logic is triggered if not already stopped
    }
     // Join threads here
     if (input_thread_.joinable()) {
        LOG("Joining input thread in destructor...");
        try {
            input_thread_.join();
            LOG("Input thread joined.");
        } catch (const std::system_error& e) {
             LOG_ERROR("Error joining input thread in destructor: " + std::string(e.what()));
        }
    }
     if (output_thread_.joinable()) {
        LOG("Joining output thread in destructor...");
         try {
            output_thread_.join();
            LOG("Output thread joined.");
        } catch (const std::system_error& e) {
             LOG_ERROR("Error joining output thread in destructor: " + std::string(e.what()));
        }
    }
     LOG("Destructor finished.");
}

// --- Getters ---

const std::string& SourceInputProcessor::get_source_tag() const {
    // This getter is needed by AudioManager to interact with RtpReceiver/SinkMixer
    // which might still rely on the original source tag (IP) until fully refactored.
    return config_.source_tag;
}

// --- Initialization ---

void SourceInputProcessor::initialize_audio_processor() {
    LOG("Initializing AudioProcessor...");
    std::lock_guard<std::mutex> lock(audio_processor_mutex_);
    try {
        // Assuming input format is fixed for now (e.g., 16-bit, 2ch, 48kHz)
        // This might need to become dynamic based on RTP payload or config later
        audio_processor_ = std::make_unique<AudioProcessor>(
            DEFAULT_INPUT_CHANNELS,
            config_.output_channels,
            DEFAULT_INPUT_BITDEPTH,
            DEFAULT_INPUT_SAMPLERATE,
            config_.output_samplerate, // Use config_ member
            current_volume_
        );
        // Set initial EQ
        audio_processor_->setEqualizer(current_eq_.data());
        LOG("AudioProcessor created.");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize AudioProcessor: " + std::string(e.what()));
        // Handle error appropriately, maybe rethrow or set a failed state
        throw; // Rethrow to signal failure to AudioManager
    }
}

void SourceInputProcessor::start() {
     if (is_running()) {
        LOG("Already running.");
        return;
    }
    LOG("Starting...");
    // Reset state specific to this component
    timeshift_buffer_read_idx_ = 0;
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
        // Notify potentially waiting threads even if launch failed partially
        timeshift_condition_.notify_all();
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
    timeshift_condition_.notify_all(); // Wake up output loop if waiting
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
        bool needs_processor_update = false;
        bool needs_timeshift_update = false;

        { // Scope for mutex lock
            std::lock_guard<std::mutex> lock(audio_processor_mutex_); // Protects current settings and audio_processor_ calls
            std::lock_guard<std::mutex> ts_lock(timeshift_mutex_); // Protects timeshift settings

            switch (cmd.type) {
                case CommandType::SET_VOLUME:
                    current_volume_ = cmd.float_value;
                    needs_processor_update = true;
                    break;
                case CommandType::SET_EQ:
                    if (cmd.eq_values.size() == EQ_BANDS) {
                        current_eq_ = cmd.eq_values;
                        needs_processor_update = true;
                    } else {
                        LOG_ERROR("Invalid EQ size in command: " + std::to_string(cmd.eq_values.size()));
                    }
                    break;
                case CommandType::SET_DELAY:
                    current_delay_ms_ = cmd.int_value;
                    needs_timeshift_update = true;
                    break;
                case CommandType::SET_TIMESHIFT:
                    current_timeshift_backshift_sec_ = cmd.float_value;
                    needs_timeshift_update = true;
                    break;
                default:
                    LOG_ERROR("Unknown command type received.");
                    break;
            }
        } // Mutexes released here

        // Apply updates outside the lock if possible
        if (needs_processor_update && audio_processor_) {
             std::lock_guard<std::mutex> lock(audio_processor_mutex_);
             if (cmd.type == CommandType::SET_VOLUME) audio_processor_->setVolume(current_volume_);
             if (cmd.type == CommandType::SET_EQ) audio_processor_->setEqualizer(current_eq_.data());
        }
        if (needs_timeshift_update) {
            // Wake up output loop in case it was waiting based on old settings
            timeshift_condition_.notify_one();
        }
    }
}

void SourceInputProcessor::handle_new_input_packet(TaggedAudioPacket& packet) {
    size_t received_bytes = packet.audio_data.size();
    LOG_DEBUG("InputLoop: Received packet from tag " + packet.source_tag + ". Size=" + std::to_string(received_bytes) + " bytes. Expected=" + std::to_string(INPUT_CHUNK_BYTES) + " bytes.");

    // Ensure packet data has the expected size before adding
    if (received_bytes != INPUT_CHUNK_BYTES) {
        LOG_ERROR("Received packet with unexpected data size: " + std::to_string(received_bytes) + ". Discarding.");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(timeshift_mutex_);
        // Add to the end of the deque
        timeshift_buffer_.push_back(std::move(packet)); // Move packet into buffer
    } // Mutex released

    // Notify the output_loop that new data might make it ready
    timeshift_condition_.notify_one();
}

// update_timeshift_target_time is REMOVED


void SourceInputProcessor::cleanup_timeshift_buffer() {
    // Assumes timeshift_mutex_ is locked
    if (timeshift_buffer_.empty()) {
        return;
    }
    // Calculate the oldest acceptable timestamp based on buffer duration
    auto now = std::chrono::steady_clock::now();
    auto max_age = std::chrono::seconds(config_.timeshift_buffer_duration_sec);
    auto oldest_allowed_time = now - max_age;

    // Remove packets older than the allowed time, BUT ensure we don't remove the packet currently being read
    size_t remove_count = 0;
    while (remove_count < timeshift_buffer_read_idx_ && // Only check packets before the read index
           !timeshift_buffer_.empty() &&
           timeshift_buffer_.front().received_time < oldest_allowed_time)
    {
        timeshift_buffer_.pop_front();
        remove_count++;
    }

    // Adjust the read index since we removed elements from the front
    if (remove_count > 0) {
        if (timeshift_buffer_read_idx_ >= remove_count) {
             timeshift_buffer_read_idx_ -= remove_count;
        } else {
             // Should not happen if logic is correct, but reset defensively
             LOG_ERROR("Timeshift buffer read index inconsistency during cleanup.");
             timeshift_buffer_read_idx_ = 0;
        }
       LOG_DEBUG("Cleaned up " + std::to_string(remove_count) + " old packets.");
    }
}


// get_next_input_chunk is REMOVED

void SourceInputProcessor::process_audio_chunk(const std::vector<uint8_t>& input_chunk_data) {
    if (!audio_processor_) {
        LOG_ERROR("AudioProcessor not initialized. Cannot process chunk.");
        return;
    }
    size_t input_bytes = input_chunk_data.size();
    LOG_DEBUG("ProcessAudio: Processing chunk. Input Size=" + std::to_string(input_bytes) + " bytes. Expected=" + std::to_string(INPUT_CHUNK_BYTES) + " bytes.");
    if (input_bytes != INPUT_CHUNK_BYTES) {
         LOG_ERROR("process_audio_chunk called with incorrect data size: " + std::to_string(input_bytes) + ". Skipping processing.");
         return;
    }
    // Calculate expected output samples based on input bytes, input format, and output channels
    // Assuming 16-bit stereo input -> 1152 / (16/8) = 576 samples input
    // Output samples = input samples * output_channels / input_channels (if no resampling)
    // For 2ch->2ch, expect 576 output samples.
    size_t expected_output_samples = (INPUT_CHUNK_BYTES / (DEFAULT_INPUT_BITDEPTH / 8)) * config_.output_channels / DEFAULT_INPUT_CHANNELS;
    // Allocate a reasonably large buffer, AudioProcessor should handle its own output size.
    // Let's allocate based on OUTPUT_CHUNK_SAMPLES which should match expected_output_samples in the common case.
    std::vector<int32_t> processor_output_buffer(expected_output_samples * 2); // Provide some extra space

    int processed_samples = 0;
    { // Lock mutex for accessing AudioProcessor
        std::lock_guard<std::mutex> lock(audio_processor_mutex_);
        processed_samples = audio_processor_->processAudio(input_chunk_data.data(), processor_output_buffer.data());
    }

    if (processed_samples > 0) {
        // Append processed samples to the internal process_buffer_
        process_buffer_.insert(process_buffer_.end(),
                               processor_output_buffer.begin(),
                               processor_output_buffer.begin() + processed_samples);
        LOG_DEBUG("ProcessAudio: Appended " + std::to_string(processed_samples) + " samples. process_buffer_ size=" + std::to_string(process_buffer_.size()) + " samples.");
    } else if (processed_samples < 0) {
         LOG_ERROR("AudioProcessor::processAudio returned an error code: " + std::to_string(processed_samples));
    } else {
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
    while (!stop_flag_ && input_queue_ && input_queue_->pop(new_packet)) { // Check queue pointer
         // No need to check stop_flag_ again here as it's in the loop condition
         handle_new_input_packet(new_packet); // Adds to buffer and notifies output loop
    }
    LOG("Input loop exiting. StopFlag=" + std::to_string(stop_flag_.load()));
}

void SourceInputProcessor::output_loop() {
    LOG("Output loop started.");
    std::vector<uint8_t> current_input_chunk_data;
    current_input_chunk_data.reserve(INPUT_CHUNK_BYTES);
    auto last_cleanup_time = std::chrono::steady_clock::now();
    auto loop_start_time = std::chrono::steady_clock::now(); // Time each loop iteration

    // Define a timeout for the condition variable wait
    // Adjust this value based on expected network conditions and desired responsiveness
    const std::chrono::milliseconds wait_timeout(100); // e.g., 100ms timeout

    while (!stop_flag_) {
        bool data_retrieved = false;
        loop_start_time = std::chrono::steady_clock::now(); // Reset timer at start of loop
        std::chrono::microseconds retrieve_duration(0); // Initialize here

        { // Scope for timeshift mutex lock
            std::unique_lock<std::mutex> lock(timeshift_mutex_);

            LOG_DEBUG("OutputLoop: Waiting. BufSize=" << timeshift_buffer_.size() << ", ReadIdx=" << timeshift_buffer_read_idx_);

            // Wait using wait_for with a timeout
            auto predicate = [&] {
                // Check stop flag first inside predicate
                if (stop_flag_) return true;

                auto check_start = std::chrono::steady_clock::now();
                bool ready = check_readiness_condition();
                auto check_end = std::chrono::steady_clock::now();
                auto check_duration = std::chrono::duration_cast<std::chrono::microseconds>(check_end - check_start);
                // Log readiness check inside the loop if debugging heavily
                // LOG_DEBUG("OutputLoop: Wait predicate check took " << check_duration.count() << "us. Ready=" << ready);
                return ready; // Return true if ready (or if stop_flag_ was set)
            };

            // wait_for returns false if timeout occurred before condition met
            if (!timeshift_condition_.wait_for(lock, wait_timeout, predicate)) {
                // Timeout occurred! Condition was not met within wait_timeout.
                if (stop_flag_) {
                    LOG_DEBUG("OutputLoop: Stop flag set during timeout wait, breaking.");
                    break; // Exit outer loop if stopped
                }

                // If not stopped, timeout means data wasn't ready in time.
                // What to do?
                // Option 1: Log and continue loop (will wait again). Good if temporary glitch.
                // Option 2: Try to skip ahead? Risky, might lose sync.
                // Option 3: Generate silence? Requires more logic.
                // Let's log and continue for now.
                LOG_DEBUG("OutputLoop: Wait timed out after " << wait_timeout.count() << "ms. No data ready.");
                // We might still want to cleanup the buffer periodically even on timeouts
                  auto now = std::chrono::steady_clock::now();
                  if (now - last_cleanup_time > TIMESIFT_CLEANUP_INTERVAL) {
                     cleanup_timeshift_buffer();
                     last_cleanup_time = now;
                 }
                continue; // Go back to the start of the while loop to wait again
            }

            // If wait succeeded (didn't time out and predicate is true) OR stop_flag_ became true:
            LOG_DEBUG("OutputLoop: Woke up/Wait satisfied. StopFlag=" << stop_flag_);

            // If stopped while waiting or after waking up, exit loop
            if (stop_flag_) {
                LOG_DEBUG("OutputLoop: Stop flag set, breaking wait loop.");
                break;
            }

            // ---- If we got here, the predicate (check_readiness_condition) was true ----
            auto retrieve_start = std::chrono::steady_clock::now();
            // Data should be ready because the predicate was met
            if (timeshift_buffer_read_idx_ < timeshift_buffer_.size()) {
                 LOG_DEBUG("OutputLoop: Data ready. Retrieving packet at index " << timeshift_buffer_read_idx_);
                 current_input_chunk_data = timeshift_buffer_[timeshift_buffer_read_idx_].audio_data;
                 // Note: Copying data here. Could be optimized with move if TaggedAudioPacket is not needed after this.
                 timeshift_buffer_read_idx_++;
                 data_retrieved = true;
                 LOG_DEBUG("OutputLoop: Read index advanced to " << timeshift_buffer_read_idx_);
            } else {
                 // This case should ideally not happen if wait condition and predicate are correct
                 LOG_ERROR("Output loop woke up ready, but read index (" << timeshift_buffer_read_idx_ << ") is out of bounds for buffer size (" << timeshift_buffer_.size() << ").");
                 // Reset read index defensively? Or just continue? Let's continue for now.
                 continue; // Continue loop to re-evaluate state
            }
            auto retrieve_end = std::chrono::steady_clock::now();
            retrieve_duration = std::chrono::duration_cast<std::chrono::microseconds>(retrieve_end - retrieve_start);


             // Periodically cleanup the timeshift buffer (while lock is held)
             auto now = std::chrono::steady_clock::now();
             if (now - last_cleanup_time > TIMESIFT_CLEANUP_INTERVAL) {
                 cleanup_timeshift_buffer();
                 last_cleanup_time = now;
             }
             
             // --- Release lock BEFORE processing ---
             lock.unlock(); 
             LOG_DEBUG("OutputLoop: Mutex unlocked before processing.");
             // ------------------------------------

        } // Mutex lock scope ends (if not unlocked earlier)

        auto wait_end = std::chrono::steady_clock::now();
        auto wait_duration = std::chrono::duration_cast<std::chrono::milliseconds>(wait_end - loop_start_time);
        LOG_DEBUG("OutputLoop: Wait/Check/Retrieve phase finished in " << wait_duration.count() << "ms. Retrieve took " << retrieve_duration.count() << "us. DataRetrieved=" << data_retrieved);


        // Process the retrieved data (now definitely outside the timeshift lock)
        if (data_retrieved) {
            auto process_start = std::chrono::steady_clock::now();
            LOG_DEBUG("OutputLoop: Processing retrieved chunk (lock released).");
            process_audio_chunk(current_input_chunk_data);
            push_output_chunk_if_ready();
            LOG_DEBUG("OutputLoop: Finished processing chunk.");
            auto process_end = std::chrono::steady_clock::now();
            auto process_duration = std::chrono::duration_cast<std::chrono::microseconds>(process_end - process_start);
            LOG_DEBUG("OutputLoop: Processing & Push took " << process_duration.count() << "us.");
        } else {
             // This branch might be reached if stopped during wait or after a timeout where we 'continue'd
             LOG_DEBUG("OutputLoop: No data retrieved this iteration (timeout or stopped?).");
        }
    } // end while (!stop_flag_)

    LOG("Output loop exiting.");
}

// Make sure the check_readiness_condition remains the same as it correctly
// implements the time-based logic for this single-source processor.
bool SourceInputProcessor::check_readiness_condition() {
    // Assumes timeshift_mutex_ is already locked by the caller (the wait predicate)
    if (timeshift_buffer_read_idx_ >= timeshift_buffer_.size()) {
        // LOG_DEBUG("CheckReady: Read index out of bounds (" << timeshift_buffer_read_idx_ << " >= " << timeshift_buffer_.size() << ")");
        return false; // Cannot be ready if index is out of bounds or buffer empty relative to index
    }

    // Calculate the target play time for the next packet
    auto packet_received_time = timeshift_buffer_.at(timeshift_buffer_read_idx_).received_time;
    auto delay_duration = std::chrono::milliseconds(current_delay_ms_);
    auto backshift_duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                  std::chrono::duration<double>(current_timeshift_backshift_sec_));
    auto scheduled_play_time = packet_received_time + delay_duration - backshift_duration;

    // Get current time
    auto now = std::chrono::steady_clock::now();
    auto time_until_play = std::chrono::duration_cast<std::chrono::milliseconds>(scheduled_play_time - now);

    // Log detailed timing information (only if needed, can be verbose)
    // LOG_DEBUG("CheckReady: Idx=" << timeshift_buffer_read_idx_
    //             << ", BufSize=" << timeshift_buffer_.size()
    //             << ", Delay=" << current_delay_ms_ << "ms"
    //             << ", Backshift=" << current_timeshift_backshift_sec_ << "s"
    //             << ", ScheduledIn=" << time_until_play.count() << "ms");

    bool ready = scheduled_play_time <= now;

    // LOG_DEBUG("CheckReady: Result = " << ready);
    return ready;
}

// run() is executed by component_thread_. It starts worker threads and processes commands.
void SourceInputProcessor::run() {
     LOG("Component run() started.");

     // Launch worker threads from within the main component thread's run method
     try {
        input_thread_ = std::thread(&SourceInputProcessor::input_loop, this);
        LOG("Input thread launched by run().");
        output_thread_ = std::thread(&SourceInputProcessor::output_loop, this);
        LOG("Output thread launched by run().");
     } catch (const std::system_error& e) {
         LOG_ERROR("Failed to start worker threads from run(): " + std::string(e.what()));
         stop_flag_ = true; // Signal stop if threads failed to launch
         // Notify potentially waiting threads even if launch failed partially
         timeshift_condition_.notify_all();
         if(input_queue_) input_queue_->stop();
         if(command_queue_) command_queue_->stop();
         return; // Exit run() if workers failed
     }

     // Command processing loop
     LOG("Starting command processing loop.");
     while (!stop_flag_) {
         process_commands(); // Check for commands

         // Sleep briefly to prevent busy-waiting when no commands are pending
         std::this_thread::sleep_for(std::chrono::milliseconds(20));
     }
     LOG("Command processing loop finished (stop signaled).");

     // --- Cleanup after stop_flag_ is set ---
     // Ensure worker threads are signaled (already done in stop()) and join them here.
     LOG("Joining worker threads in run()...");
     if (input_thread_.joinable()) {
         try {
             input_thread_.join();
             LOG("Input thread joined in run().");
         } catch (const std::system_error& e) {
             LOG_ERROR("Error joining input thread in run(): " + std::string(e.what()));
         }
     }
     if (output_thread_.joinable()) {
         try {
             output_thread_.join();
             LOG("Output thread joined in run().");
         } catch (const std::system_error& e) {
             LOG_ERROR("Error joining output thread in run(): " + std::string(e.what()));
         }
     }

     LOG("Component run() exiting.");
}
