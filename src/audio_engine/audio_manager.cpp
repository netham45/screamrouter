#include "audio_manager.h"
#include "raw_scream_receiver.h" // Include the new header
#include <iostream> // For logging
#include <stdexcept>
#include <utility> // For std::move
#include <system_error> // For thread errors
#include <atomic>       // For std::atomic_uint64_t
#include <sstream>      // For std::stringstream
#include <algorithm>    // For std::find

// Use namespaces for clarity
namespace screamrouter { namespace audio { using namespace utils; } }
using namespace screamrouter::audio;
using namespace screamrouter::utils;

// Simple logger helper (replace with a proper logger if available)
#define LOG_AM(msg) std::cout << "[AudioManager] " << msg << std::endl
#define LOG_ERROR_AM(msg) std::cerr << "[AudioManager Error] " << msg << std::endl
#define LOG_WARN_AM(msg) std::cout << "[AudioManager Warn] " << msg << std::endl // Define WARN

// Static counter for generating unique instance IDs
static std::atomic<uint64_t> instance_id_counter{0};

AudioManager::AudioManager() : running_(false) {
    LOG_AM("Created.");
}

// Helper function to generate unique IDs
std::string AudioManager::generate_unique_instance_id(const std::string& base_tag) {
    uint64_t id_num = instance_id_counter.fetch_add(1);
    std::stringstream ss;
    if (!base_tag.empty()) {
        // Basic sanitization: replace characters not suitable for IDs if needed
        // For now, just append. Consider more robust sanitization if tags can be complex.
        ss << base_tag << "-";
    }
    ss << "instance-" << id_num;
    return ss.str();
}

AudioManager::~AudioManager() {
    LOG_AM("Destroying...");
    if (running_) {
        shutdown(); // Ensure shutdown is called if not done explicitly
    }
    // Ensure notification thread is joined if it was started
    if (notification_thread_.joinable()) {
         LOG_ERROR_AM("Notification thread still joinable in destructor! Shutdown might have failed.");
         // Attempt to signal stop again just in case
         if (new_source_notification_queue_) new_source_notification_queue_->stop();
         try {
             notification_thread_.join();
         } catch (const std::system_error& e) {
             LOG_ERROR_AM("Error joining notification thread in destructor: " + std::string(e.what()));
         }
    }
    LOG_AM("Destroyed.");
}

bool AudioManager::initialize(int rtp_listen_port) {
    LOG_AM("Initializing...");
    std::lock_guard<std::mutex> lock(manager_mutex_); // Protect shared state during init

    if (running_) {
        LOG_AM("Already initialized.");
        return true;
    }

    // 1. Create Notification Queue
    new_source_notification_queue_ = std::make_shared<NotificationQueue>();

    // 2. Create and Start RTP Receiver
    try {
        RtpReceiverConfig rtp_config;
        rtp_config.listen_port = rtp_listen_port;
        rtp_receiver_ = std::make_unique<RtpReceiver>(rtp_config, new_source_notification_queue_);
        rtp_receiver_->start(); // This internally creates the socket and thread
        // Check if start was successful (basic check, might need more robust mechanism)
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Increased sleep
        if (!rtp_receiver_->is_running()) {
             throw std::runtime_error("RtpReceiver failed to start or socket setup failed.");
        }
    } catch (const std::exception& e) {
        LOG_ERROR_AM("Failed to initialize RtpReceiver: " + std::string(e.what()));
        rtp_receiver_.reset(); // Clean up partially created receiver
        new_source_notification_queue_.reset();
        return false;
    }

    // 3. Start Notification Processing Thread
    try {
        notification_thread_ = std::thread(&AudioManager::process_notifications, this);
    } catch (const std::system_error& e) {
        LOG_ERROR_AM("Failed to start notification thread: " + std::string(e.what()));
        if(rtp_receiver_) rtp_receiver_->stop(); // Stop the receiver if notification thread failed
        rtp_receiver_.reset();
        new_source_notification_queue_.reset();
        return false;
    }

    running_ = true;
    LOG_AM("Initialization successful.");
    return true;
}

void AudioManager::shutdown() {
    LOG_AM("Shutting down...");
    { // Scope for lock
        std::lock_guard<std::mutex> lock(manager_mutex_);
        if (!running_) {
            LOG_AM("Already shut down.");
            return;
        }
        running_ = false; // Signal all loops to stop

        // Stop notification queue first to prevent processing new sources during shutdown
        if (new_source_notification_queue_) {
            new_source_notification_queue_->stop();
        }
    } // Release lock before joining threads

    // Join notification thread
    if (notification_thread_.joinable()) {
        LOG_AM("Joining notification thread...");
        try {
             notification_thread_.join();
             LOG_AM("Notification thread joined.");
        } catch (const std::system_error& e) {
             LOG_ERROR_AM("Error joining notification thread: " + std::string(e.what()));
        }
    }

    // Now acquire lock again to stop components safely
    std::lock_guard<std::mutex> lock(manager_mutex_);

    // Stop RTP Receiver
    if (rtp_receiver_) {
        LOG_AM("Stopping RTP Receiver...");
        rtp_receiver_->stop();
        rtp_receiver_.reset(); // Destroy after stopping
        LOG_AM("RTP Receiver stopped.");
    }

    // Stop all Source Processors (using instance_id map)
    LOG_AM("Stopping Source Processors...");
    for (auto& pair : sources_) {
        if (pair.second) {
            pair.second->stop();
        }
    }
    sources_.clear(); // Destroy after stopping all
    // Clear associated queues (using instance_id maps)
    rtp_to_source_queues_.clear();
    source_to_sink_queues_.clear();
    command_queues_.clear();
    LOG_AM("Source Processors stopped.");


    // Stop all Sink Mixers
    LOG_AM("Stopping Sink Mixers...");
    for (auto& pair : sinks_) {
        if (pair.second) {
            pair.second->stop();
        }
    }
    sinks_.clear(); // Destroy after stopping all
    mp3_output_queues_.clear(); // Clear MP3 queues
    sink_configs_.clear();
    LOG_AM("Sink Mixers stopped.");

    // Removed source_configs_.clear();

    // Clear notification queue pointer
    new_source_notification_queue_.reset();

    // Stop all RawScreamReceivers
    LOG_AM("Stopping Raw Scream Receivers...");
    for (auto& pair : raw_scream_receivers_) {
        if (pair.second) {
            pair.second->stop();
        }
    }
    raw_scream_receivers_.clear();
    LOG_AM("Raw Scream Receivers stopped.");

    LOG_AM("Shutdown complete.");
}

bool AudioManager::add_raw_scream_receiver(const RawScreamReceiverConfig& config) {
    LOG_AM("Adding raw scream receiver for port: " + std::to_string(config.listen_port));
    std::lock_guard<std::mutex> lock(manager_mutex_);

    if (!running_) {
        LOG_ERROR_AM("Cannot add raw scream receiver, manager is not running.");
        return false;
    }

    if (raw_scream_receivers_.count(config.listen_port)) {
        LOG_ERROR_AM("Raw scream receiver for port " + std::to_string(config.listen_port) + " already exists.");
        return false;
    }

    std::unique_ptr<RawScreamReceiver> new_receiver;
    try {
        new_receiver = std::make_unique<RawScreamReceiver>(config, new_source_notification_queue_);
        new_receiver->start();
        // Basic check, might need more robust mechanism or longer sleep
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
        if (!new_receiver->is_running()) {
            throw std::runtime_error("RawScreamReceiver failed to start.");
        }
    } catch (const std::exception& e) {
        LOG_ERROR_AM("Failed to create or start RawScreamReceiver for port " + std::to_string(config.listen_port) + ": " + std::string(e.what()));
        // new_receiver is already a unique_ptr, will clean up if it was allocated before throw
        return false;
    }

    raw_scream_receivers_[config.listen_port] = std::move(new_receiver);
    LOG_AM("Raw scream receiver for port " + std::to_string(config.listen_port) + " added successfully.");
    return true;
}

bool AudioManager::remove_raw_scream_receiver(int listen_port) {
    LOG_AM("Removing raw scream receiver for port: " + std::to_string(listen_port));
    std::unique_ptr<RawScreamReceiver> receiver_to_remove;

    { // Scope for lock
        std::lock_guard<std::mutex> lock(manager_mutex_);
        if (!running_) {
            LOG_ERROR_AM("Cannot remove raw scream receiver, manager is not running.");
            return false;
        }

        auto it = raw_scream_receivers_.find(listen_port);
        if (it == raw_scream_receivers_.end()) {
            LOG_ERROR_AM("Raw scream receiver for port " + std::to_string(listen_port) + " not found.");
            return false;
        }

        receiver_to_remove = std::move(it->second);
        raw_scream_receivers_.erase(it);
    } // Lock released

    if (receiver_to_remove) {
        receiver_to_remove->stop();
    }

    LOG_AM("Raw scream receiver for port " + std::to_string(listen_port) + " removed successfully.");
    return true;
}

bool AudioManager::add_sink(const SinkConfig& config) {
    LOG_AM("Adding sink: " + config.id);
    std::lock_guard<std::mutex> lock(manager_mutex_);

    if (!running_) {
        LOG_ERROR_AM("Cannot add sink, manager is not running.");
        return false;
    }

    if (sinks_.count(config.id)) {
        LOG_ERROR_AM("Sink ID already exists: " + config.id);
        return false;
    }

    // 1. Create MP3 Queue unconditionally
    auto mp3_queue = std::make_shared<Mp3Queue>();
    mp3_output_queues_[config.id] = mp3_queue;
    LOG_AM("MP3 output queue created for sink: " + config.id);

    // 2. Create SinkMixerConfig
    SinkMixerConfig mixer_config;
    mixer_config.sink_id = config.id;
    mixer_config.output_ip = config.output_ip;
    mixer_config.output_port = config.output_port;
    mixer_config.output_bitdepth = config.bitdepth;
    mixer_config.output_samplerate = config.samplerate;
    mixer_config.output_channels = config.channels;
    mixer_config.output_chlayout1 = config.chlayout1;
    mixer_config.output_chlayout2 = config.chlayout2;
    mixer_config.use_tcp = config.use_tcp;

    // 3. Create and Start SinkAudioMixer
    std::unique_ptr<SinkAudioMixer> new_sink;
    try {
        new_sink = std::make_unique<SinkAudioMixer>(mixer_config, mp3_queue);
        new_sink->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!new_sink->is_running()) {
             throw std::runtime_error("SinkAudioMixer failed to start.");
        }
    } catch (const std::exception& e) {
        LOG_ERROR_AM("Failed to create or start SinkAudioMixer for " + config.id + ": " + std::string(e.what()));
        mp3_output_queues_.erase(config.id); // Clean up MP3 queue if mixer failed
        return false;
    }

    // 4. Store the new sink and its config (No automatic connection)
    sinks_[config.id] = std::move(new_sink);
    sink_configs_[config.id] = config;

    LOG_AM("Sink " + config.id + " added successfully.");
    return true;
}

bool AudioManager::remove_sink(const std::string& sink_id) {
    LOG_AM("Removing sink: " + sink_id);
    std::unique_ptr<SinkAudioMixer> sink_to_remove;

    { // Scope for lock
        std::lock_guard<std::mutex> lock(manager_mutex_);
        if (!running_) {
             LOG_ERROR_AM("Cannot remove sink, manager is not running.");
             return false;
        }

        auto it = sinks_.find(sink_id);
        if (it == sinks_.end()) {
            LOG_ERROR_AM("Sink not found: " + sink_id);
            return false;
        }

        sink_to_remove = std::move(it->second);
        sinks_.erase(it);
        sink_configs_.erase(sink_id);
        mp3_output_queues_.erase(sink_id);
    } // Lock released

    if (sink_to_remove) {
        sink_to_remove->stop();
    }

    LOG_AM("Sink " + sink_id + " removed successfully.");
    return true;
}

// Refactored configure_source to use instance_id
std::string AudioManager::configure_source(const SourceConfig& config) {
    LOG_AM("Configuring source instance with tag: " + config.tag);
    std::lock_guard<std::mutex> lock(manager_mutex_);

    if (!running_) {
        LOG_ERROR_AM("Cannot configure source, manager is not running.");
        return ""; // Return empty string on failure
    }

    // Generate a unique instance ID
    std::string instance_id = generate_unique_instance_id(config.tag);
    LOG_AM("Generated unique instance ID: " + instance_id);

    // Validate EQ size from the input config
    SourceConfig validated_config = config; // Make a copy
    if (!validated_config.initial_eq.empty() && validated_config.initial_eq.size() != EQ_BANDS) {
        LOG_ERROR_AM("Invalid initial EQ size for source tag " + validated_config.tag + ". Expected " + std::to_string(EQ_BANDS) + ", got " + std::to_string(validated_config.initial_eq.size()) + ". Resetting to flat.");
        validated_config.initial_eq.assign(EQ_BANDS, 1.0f);
    } else if (validated_config.initial_eq.empty()) {
         LOG_AM("No initial EQ provided for source tag " + validated_config.tag + ". Setting to flat.");
         validated_config.initial_eq.assign(EQ_BANDS, 1.0f);
    }

    // Create necessary queues for this specific instance
    auto rtp_queue = std::make_shared<PacketQueue>();
    auto sink_queue = std::make_shared<ChunkQueue>();
    auto cmd_queue = std::make_shared<CommandQueue>();

    // Store queues using the unique instance_id
    rtp_to_source_queues_[instance_id] = rtp_queue;
    source_to_sink_queues_[instance_id] = sink_queue;
    command_queues_[instance_id] = cmd_queue;

    // Create SourceProcessorConfig, including the instance_id and original tag
    SourceProcessorConfig proc_config; // Its constructor now initializes initial_eq
    proc_config.instance_id = instance_id; // Store the unique ID
    proc_config.source_tag = validated_config.tag; // Store the original tag (IP/name)

    // Validate and use the target output format specified in the input SourceConfig
    if (validated_config.target_output_channels <= 0 || validated_config.target_output_channels > 8) { // Max 8 channels for example
        LOG_ERROR_AM("Invalid target_output_channels (" + std::to_string(validated_config.target_output_channels) + ") for source tag " + validated_config.tag + ". Defaulting to 2.");
        proc_config.output_channels = 2; // Fallback
    } else {
        proc_config.output_channels = validated_config.target_output_channels;
    }

    const std::vector<int> valid_samplerates = {8000, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 192000};
    if (std::find(valid_samplerates.begin(), valid_samplerates.end(), validated_config.target_output_samplerate) == valid_samplerates.end()) {
        LOG_ERROR_AM("Invalid target_output_samplerate (" + std::to_string(validated_config.target_output_samplerate) + ") for source tag " + validated_config.tag + ". Defaulting to 48000.");
        proc_config.output_samplerate = 48000; // Fallback
    } else {
        proc_config.output_samplerate = validated_config.target_output_samplerate;
    }
    
    proc_config.initial_volume = validated_config.initial_volume;
    proc_config.initial_eq = validated_config.initial_eq; 
    proc_config.initial_delay_ms = validated_config.initial_delay_ms;
    // proc_config.timeshift_buffer_duration_sec remains default or could be made configurable

    // Determine protocol type and target receiver port from validated config
    InputProtocolType proto_type = (validated_config.protocol_type_hint == 1) ?
                                       InputProtocolType::RAW_SCREAM_PACKET :
                                       InputProtocolType::RTP_SCREAM_PAYLOAD;
    proc_config.protocol_type = proto_type;
    proc_config.target_receiver_port = validated_config.target_receiver_port; // Copy target port

    LOG_AM("Source instance " + instance_id + " configured with protocol type: " + (proto_type == InputProtocolType::RAW_SCREAM_PACKET ? "RAW_SCREAM" : "RTP_PAYLOAD") + 
           (proto_type == InputProtocolType::RAW_SCREAM_PACKET ? ", Target Port: " + std::to_string(proc_config.target_receiver_port) : ""));


    // Create and Start SourceInputProcessor
    std::unique_ptr<SourceInputProcessor> new_source;
    try {
        new_source = std::make_unique<SourceInputProcessor>(proc_config, rtp_queue, sink_queue, cmd_queue);
        new_source->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Allow time for thread start/potential failure
        if (!new_source->is_running()) {
             throw std::runtime_error("SourceInputProcessor failed to start.");
        }
    } catch (const std::exception& e) {
        LOG_ERROR_AM("Failed to create or start SourceInputProcessor for instance " + instance_id + " (tag: " + config.tag + "): " + std::string(e.what()));
        // Clean up queues created for this failed instance
        rtp_to_source_queues_.erase(instance_id);
        source_to_sink_queues_.erase(instance_id);
        command_queues_.erase(instance_id);
        return ""; // Return empty string on failure
    }

    // Register input queue with RtpReceiver
    // ** IMPORTANT: RtpReceiver needs modification to handle mapping source_tag (IP) to potentially multiple instance queues. **
    if (rtp_receiver_) {
        std::mutex* proc_mutex = new_source->get_timeshift_mutex();
        std::condition_variable* proc_cv = new_source->get_timeshift_cv();
         // Call the updated add_output_queue method with instance_id
         rtp_receiver_->add_output_queue(proc_config.source_tag, instance_id, rtp_queue);

     } else {
          LOG_ERROR_AM("RtpReceiver is null, cannot add output target for instance " + instance_id);
         new_source->stop(); // Stop the newly created source processor
         // Clean up queues...
         rtp_to_source_queues_.erase(instance_id);
         source_to_sink_queues_.erase(instance_id);
         command_queues_.erase(instance_id);
         // Clean up queues...
         rtp_to_source_queues_.erase(instance_id);
         source_to_sink_queues_.erase(instance_id);
         command_queues_.erase(instance_id);
         return ""; // Return empty string on failure
     }

    // --- Register with ALL active receivers ---
    // Removed proc_mutex and proc_cv retrieval as they are no longer passed
    int registration_count = 0;

    // 1. Register with RtpReceiver
    if (rtp_receiver_) {
        rtp_receiver_->add_output_queue(proc_config.source_tag, instance_id, rtp_queue); // Corrected: Removed mutex/cv args
        LOG_AM("Registered instance " + instance_id + " with RtpReceiver.");
        registration_count++;
    } else {
        LOG_WARN_AM("RtpReceiver is null, cannot register instance " + instance_id);
    }

    // 2. Register with all RawScreamReceivers
    if (raw_scream_receivers_.empty()) {
         LOG_WARN_AM("No RawScreamReceivers active, cannot register instance " + instance_id);
    } else {
        for (auto const& [port, receiver_ptr] : raw_scream_receivers_) {
            if (receiver_ptr) {
                receiver_ptr->add_output_queue(proc_config.source_tag, instance_id, rtp_queue); // Removed mutex/cv args
                LOG_AM("Registered instance " + instance_id + " with RawScreamReceiver on port " + std::to_string(port));
                registration_count++;
            } else {
                 LOG_ERROR_AM("Found null RawScreamReceiver pointer for port " + std::to_string(port));
            }
        }
    }
    
    // We might consider failing if registration_count is 0, but for now, let's allow it
    // as receivers might be added later dynamically. The SourceInputProcessor won't get
    // any data until registered.
    if (registration_count == 0) {
         LOG_WARN_AM("Source instance " + instance_id + " was not registered with ANY active receivers.");
    }

     // Store the new source processor using its unique instance_id
     sources_[instance_id] = std::move(new_source);

    LOG_AM("Source instance " + instance_id + " (tag: " + config.tag + ") configured and started successfully.");
    return instance_id; // Return the unique ID
}

// Refactored remove_source to handle different receiver types
bool AudioManager::remove_source(const std::string& instance_id) {
    LOG_AM("Removing source instance: " + instance_id);
    std::unique_ptr<SourceInputProcessor> source_to_remove;
    std::string source_tag_for_removal; 
    InputProtocolType proto_type = InputProtocolType::RTP_SCREAM_PAYLOAD; // Default
    int target_port = -1; // Default

    { // Scope for lock
        std::lock_guard<std::mutex> lock(manager_mutex_);
        if (!running_) {
             LOG_ERROR_AM("Cannot remove source instance, manager is not running.");
             return false;
        }

        // Find the processor by instance_id
        auto it = sources_.find(instance_id);
        if (it == sources_.end()) {
            LOG_ERROR_AM("Source processor instance not found: " + instance_id);
            return false;
        }

        // Get the source tag before moving the processor
        if (it->second) {
             // TODO: Add a getter for source_tag in SourceInputProcessor if needed, or retrieve from config if stored
             // Get config details needed for removal
             const auto& proc_config = it->second->get_config();
             source_tag_for_removal = proc_config.source_tag;
             proto_type = proc_config.protocol_type;
             target_port = proc_config.target_receiver_port;
        } else {
             LOG_ERROR_AM("Found null source processor pointer for instance: " + instance_id);
             sources_.erase(it); // Remove the null entry
             return false; // Indicate failure
        }


        source_to_remove = std::move(it->second); // Move ownership for later stopping
        sources_.erase(it); // Remove from map

        // Clean up associated queues using instance_id
        auto rtp_queue_it = rtp_to_source_queues_.find(instance_id);
        std::shared_ptr<PacketQueue> rtp_queue_ptr = (rtp_queue_it != rtp_to_source_queues_.end()) ? rtp_queue_it->second : nullptr;
        rtp_to_source_queues_.erase(instance_id); 
        source_to_sink_queues_.erase(instance_id);
        command_queues_.erase(instance_id);
        LOG_AM("Removed queues for source instance: " + instance_id);

        // --- Unregister from ALL receivers ---
        
        // 1. Unregister from RtpReceiver
        if (rtp_receiver_) {
            if (!source_tag_for_removal.empty()) {
                rtp_receiver_->remove_output_queue(source_tag_for_removal, instance_id);
                LOG_AM("Unregistered instance " + instance_id + " from RtpReceiver.");
            } else {
                LOG_ERROR_AM("Cannot remove instance " + instance_id + " from RtpReceiver without its source_tag.");
            }
        } else {
             LOG_WARN_AM("RtpReceiver is null during removal of instance " + instance_id);
        }

        // 2. Unregister from all RawScreamReceivers
        if (!source_tag_for_removal.empty()) {
             if (raw_scream_receivers_.empty()) {
                 LOG_WARN_AM("No RawScreamReceivers active during removal of instance " + instance_id);
             } else {
                 for (auto const& [port, receiver_ptr] : raw_scream_receivers_) {
                     if (receiver_ptr) {
                         receiver_ptr->remove_output_queue(source_tag_for_removal, instance_id);
                         LOG_AM("Unregistered instance " + instance_id + " from RawScreamReceiver on port " + std::to_string(port));
                     } else {
                          LOG_ERROR_AM("Found null RawScreamReceiver pointer for port " + std::to_string(port) + " during removal.");
                     }
                 }
             }
        } else {
             LOG_ERROR_AM("Cannot remove instance " + instance_id + " from RawScreamReceivers without its source_tag.");
        }

        // Tell all sinks to remove this source's input queue
        // ** IMPORTANT: SinkAudioMixer needs modification. **
        // SinkAudioMixer::remove_input_queue currently uses source_tag.
        // It should ideally use instance_id, or we need the source_tag here.
        LOG_AM("Disconnecting source instance " + instance_id + " from existing sinks...");
        for (auto& sink_pair : sinks_) {
            if (sink_pair.second) {
                sink_pair.second->remove_input_queue(instance_id); // Use instance_id now
            }
        }
    } // Mutex lock released

    // Stop the processor outside the lock
    if (source_to_remove) {
        LOG_AM("Stopping source processor instance: " + instance_id);
        source_to_remove->stop();
        LOG_AM("Source processor instance " + instance_id + " stopped and removed.");
        return true; // Successfully found and stopped
    }

    return false; // Instance was not found
}

// process_notifications remains largely the same, but handle_new_source changes role
void AudioManager::process_notifications() {
    LOG_AM("Notification processing thread started.");
    while (running_) {
        NewSourceNotification notification;
        // Blocking pop - waits until data available or queue is stopped
        bool success = new_source_notification_queue_->pop(notification);

        if (!success) { // pop returns false if queue was stopped
            if (running_) { // Log error only if we weren't expecting to stop
                 LOG_ERROR_AM("Notification queue pop failed unexpectedly.");
            }
            break; // Exit loop if queue stopped or failed
        }

        // Check running flag again after potentially blocking pop
        if (!running_) break;

        // TODO: Implement proper packet fan-out based on source_tag if RtpReceiver pushes packets here.
        // For now, this thread might just log the notification.
        LOG_AM("Received notification for source tag: " + notification.source_tag + ". No action taken (processor creation is explicit).");
    }
    LOG_AM("Notification processing thread finished.");
}

// handle_new_source is now just informational, processor creation is explicit via configure_source
void AudioManager::handle_new_source(const std::string& source_tag) {
    // This function is called when the RtpReceiver detects a packet from a source IP (tag).
    // It no longer creates processors automatically.
    // It could potentially be used to trigger logic if packets arrive for an *unknown* tag,
    // but currently, it does nothing significant.
    std::lock_guard<std::mutex> lock(manager_mutex_);
    if (!running_) return;
    LOG_AM("handle_new_source: Received packet notification for tag: " + source_tag + ". (Informational only)");
}

// Refactored send_command_to_source to use instance_id
bool AudioManager::send_command_to_source(const std::string& instance_id, const ControlCommand& command) {
    std::shared_ptr<CommandQueue> target_queue;
    { // Scope for lock
        std::lock_guard<std::mutex> lock(manager_mutex_);
        if (!running_) return false;

        auto it = command_queues_.find(instance_id);
        if (it == command_queues_.end()) {
            LOG_ERROR_AM("Command queue not found for source instance: " + instance_id);
            return false;
        }
        target_queue = it->second;
    } // Lock released

    if (target_queue) {
        target_queue->push(command);
        return true;
    }
    return false;
}

// Refactored connect_source_sink to use instance_id
bool AudioManager::connect_source_sink(const std::string& source_instance_id, const std::string& sink_id) {
    LOG_AM("Connecting source instance " + source_instance_id + " to sink " + sink_id);
    std::lock_guard<std::mutex> lock(manager_mutex_);

    if (!running_) {
        LOG_ERROR_AM("Cannot connect source/sink, manager is not running.");
        return false;
    }

    // Find the sink
    auto sink_it = sinks_.find(sink_id);
    if (sink_it == sinks_.end() || !sink_it->second) {
        LOG_ERROR_AM("Sink not found or invalid: " + sink_id);
        return false;
    }

    // Find the source's output queue using its instance_id
    auto queue_it = source_to_sink_queues_.find(source_instance_id);
    if (queue_it == source_to_sink_queues_.end() || !queue_it->second) {
        LOG_ERROR_AM("Source output queue not found for instance ID: " + source_instance_id);
        return false;
    }

    // Find the source processor to get its original tag
    auto source_it = sources_.find(source_instance_id);
     if (source_it == sources_.end() || !source_it->second) {
         LOG_ERROR_AM("Source processor instance not found for ID: " + source_instance_id);
         return false;
     }
     std::string source_tag = source_it->second->get_source_tag();
     // LOG_WARN_AM("Need getter for source_tag in SourceInputProcessor for connect_source_sink");


    // Connect the source's output queue to the sink's input using instance_id
    sink_it->second->add_input_queue(source_instance_id, queue_it->second); // Use instance_id now
    LOG_AM("Connection successful: Source instance " + source_instance_id + " -> Sink " + sink_id);
    return true;
}

// Refactored disconnect_source_sink to use instance_id
bool AudioManager::disconnect_source_sink(const std::string& source_instance_id, const std::string& sink_id) {
    LOG_AM("Disconnecting source instance " + source_instance_id + " from sink " + sink_id);
    std::lock_guard<std::mutex> lock(manager_mutex_);

    if (!running_) {
        LOG_ERROR_AM("Cannot disconnect source/sink, manager is not running.");
        return false;
    }

    // Find the sink
    auto sink_it = sinks_.find(sink_id);
    if (sink_it == sinks_.end() || !sink_it->second) {
        LOG_ERROR_AM("Sink not found or invalid for disconnection: " + sink_id);
        return false;
    }

     // Find the source processor to get its original tag
     auto source_it = sources_.find(source_instance_id);
     if (source_it == sources_.end() || !source_it->second) {
         // Source instance doesn't exist, maybe already removed. Consider this success?
         LOG_WARN_AM("Source processor instance not found for disconnection: " + source_instance_id + ". Assuming already disconnected.");
         return true; // Or return false if strict check needed
     }
     std::string source_tag = source_it->second->get_source_tag();
     // LOG_WARN_AM("Need getter for source_tag in SourceInputProcessor for disconnect_source_sink");


    // Tell the sink to remove the input queue associated with the source instance_id
    sink_it->second->remove_input_queue(source_instance_id); // Use instance_id now
    LOG_AM("Disconnection successful: Source instance " + source_instance_id + " -x Sink " + sink_id);
    return true;
}


// --- Control API Implementations ---

// Refactored control methods to use instance_id
bool AudioManager::update_source_volume(const std::string& instance_id, float volume) {
    ControlCommand cmd;
    cmd.type = CommandType::SET_VOLUME;
    cmd.float_value = volume;
    return send_command_to_source(instance_id, cmd);
}

bool AudioManager::update_source_equalizer(const std::string& instance_id, const std::vector<float>& eq_values) {
     if (eq_values.size() != EQ_BANDS) {
         LOG_ERROR_AM("update_source_equalizer: Invalid EQ size (" + std::to_string(eq_values.size()) + ") for source instance " + instance_id);
         return false;
     }
    ControlCommand cmd;
    cmd.type = CommandType::SET_EQ;
    cmd.eq_values = eq_values; // Copy vector
    return send_command_to_source(instance_id, cmd);
}

bool AudioManager::update_source_delay(const std::string& instance_id, int delay_ms) {
    ControlCommand cmd;
    cmd.type = CommandType::SET_DELAY;
    cmd.int_value = delay_ms;
    return send_command_to_source(instance_id, cmd);
}

bool AudioManager::update_source_timeshift(const std::string& instance_id, float timeshift_sec) {
    ControlCommand cmd;
    cmd.type = CommandType::SET_TIMESHIFT;
    cmd.float_value = timeshift_sec;
    return send_command_to_source(instance_id, cmd);
}

// --- Data Retrieval API Implementation ---

std::vector<uint8_t> AudioManager::get_mp3_data(const std::string& sink_id) {
    std::shared_ptr<Mp3Queue> target_queue;
    { // Scope for lock
        std::lock_guard<std::mutex> lock(manager_mutex_);
         if (!running_) return {}; // Return empty vector if not running

        auto it = mp3_output_queues_.find(sink_id);
        if (it == mp3_output_queues_.end()) {
            // LOG_ERROR_AM("MP3 queue not found for sink: " + sink_id); // Can be noisy
            return {}; // Return empty vector
        }
        target_queue = it->second;
    } // Lock released

    if (target_queue) {
        EncodedMP3Data mp3_data;
        if (target_queue->try_pop(mp3_data)) {
            return mp3_data.mp3_data; // Return the vector (move happens implicitly)
        }
    }
    return {}; // Return empty vector if queue is null or empty
}

// --- External Control ---

bool AudioManager::set_sink_tcp_fd(const std::string& sink_id, int fd) {
     std::lock_guard<std::mutex> lock(manager_mutex_);
     if (!running_) return false;

     auto it = sinks_.find(sink_id);
     if (it == sinks_.end()) {
         LOG_ERROR_AM("Sink not found for setting TCP FD: " + sink_id);
         return false;
     }
     if (it->second) {
         it->second->set_tcp_fd(fd);
         return true;
     }
     return false; // Should not happen if found in map
}
