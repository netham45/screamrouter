#include "audio_manager.h"
#include "raw_scream_receiver.h"
#include "per_process_scream_receiver.h" // Include the new header
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
#define LOG_AM(msg) //std::cout << "[AudioManager] " << msg << std::endl
#define LOG_ERROR_AM(msg) //std::cerr << "[AudioManager Error] " << msg << std::endl
#define LOG_WARN_AM(msg) //std::cout << "[AudioManager Warn] " << msg << std::endl // Define WARN

// Static counter for generating unique instance IDs
static std::atomic<uint64_t> instance_id_counter{0};
 
// Default duration for the TimeshiftManager's global buffer if not configured otherwise
// This constant is now superseded by the parameter in initialize(), but kept for reference or other potential uses.
const std::chrono::seconds DEFAULT_GLOBAL_TIMESHIFT_BUFFER_DURATION(300);

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

bool AudioManager::initialize(int rtp_listen_port, int global_timeshift_buffer_duration_sec) {
    LOG_AM("Initializing with rtp_listen_port: " + std::to_string(rtp_listen_port) +
           ", timeshift_buffer_duration: " + std::to_string(global_timeshift_buffer_duration_sec) + "s");
    std::lock_guard<std::mutex> lock(manager_mutex_); // Protect shared state during init

    if (running_) {
        LOG_AM("Already initialized.");
        return true;
    }

    // 1. Create and Start TimeshiftManager
    try {
        timeshift_manager_ = std::make_unique<TimeshiftManager>(std::chrono::seconds(global_timeshift_buffer_duration_sec));
        timeshift_manager_->start();
        LOG_AM("TimeshiftManager started with buffer duration: " + std::to_string(global_timeshift_buffer_duration_sec) + "s.");
    } catch (const std::exception& e) {
        LOG_ERROR_AM("Failed to initialize TimeshiftManager: " + std::string(e.what()));
        return false;
    }

    // 2. Create Notification Queue
    new_source_notification_queue_ = std::make_shared<NotificationQueue>();

    // 3. Create and Start RTP Receiver
    try {
        RtpReceiverConfig rtp_config;
        rtp_config.listen_port = rtp_listen_port;
        rtp_receiver_ = std::make_unique<RtpReceiver>(rtp_config, new_source_notification_queue_, timeshift_manager_.get());
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
        if(timeshift_manager_) timeshift_manager_->stop(); // Stop TimeshiftManager if subsequent steps fail
        timeshift_manager_.reset();
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

    // Stop TimeshiftManager first, as other components might depend on it or its state
    if (timeshift_manager_) {
        LOG_AM("Stopping TimeshiftManager...");
        timeshift_manager_->stop();
        timeshift_manager_.reset();
        LOG_AM("TimeshiftManager stopped.");
    }

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

    // Stop all PerProcessScreamReceivers
    LOG_AM("Stopping Per-Process Scream Receivers...");
    for (auto& pair : per_process_scream_receivers_) {
        if (pair.second) {
            pair.second->stop();
        }
    }
    per_process_scream_receivers_.clear();
    LOG_AM("Per-Process Scream Receivers stopped.");

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
        new_receiver = std::make_unique<RawScreamReceiver>(config, new_source_notification_queue_, timeshift_manager_.get());
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

bool AudioManager::add_per_process_scream_receiver(const PerProcessScreamReceiverConfig& config) {
    LOG_AM("Adding per-process scream receiver for port: " + std::to_string(config.listen_port));
    std::lock_guard<std::mutex> lock(manager_mutex_);

    if (!running_) {
        LOG_ERROR_AM("Cannot add per-process scream receiver, manager is not running.");
        return false;
    }

    if (per_process_scream_receivers_.count(config.listen_port)) {
        LOG_ERROR_AM("Per-process scream receiver for port " + std::to_string(config.listen_port) + " already exists.");
        return false;
    }

    std::unique_ptr<PerProcessScreamReceiver> new_receiver;
    try {
        new_receiver = std::make_unique<PerProcessScreamReceiver>(config, new_source_notification_queue_, timeshift_manager_.get());
        new_receiver->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
        if (!new_receiver->is_running()) {
            throw std::runtime_error("PerProcessScreamReceiver failed to start.");
        }
    } catch (const std::exception& e) {
        LOG_ERROR_AM("Failed to create or start PerProcessScreamReceiver for port " + std::to_string(config.listen_port) + ": " + std::string(e.what()));
        return false;
    }

    per_process_scream_receivers_[config.listen_port] = std::move(new_receiver);
    LOG_AM("Per-process scream receiver for port " + std::to_string(config.listen_port) + " added successfully.");
    return true;
}

bool AudioManager::remove_per_process_scream_receiver(int listen_port) {
    LOG_AM("Removing per-process scream receiver for port: " + std::to_string(listen_port));
    std::unique_ptr<PerProcessScreamReceiver> receiver_to_remove;

    { // Scope for lock
        std::lock_guard<std::mutex> lock(manager_mutex_);
        if (!running_) {
            LOG_ERROR_AM("Cannot remove per-process scream receiver, manager is not running.");
            return false;
        }

        auto it = per_process_scream_receivers_.find(listen_port);
        if (it == per_process_scream_receivers_.end()) {
            LOG_ERROR_AM("Per-process scream receiver for port " + std::to_string(listen_port) + " not found.");
            return false;
        }

        receiver_to_remove = std::move(it->second);
        per_process_scream_receivers_.erase(it);
    } // Lock released

    if (receiver_to_remove) {
        receiver_to_remove->stop();
    }

    LOG_AM("Per-process scream receiver for port " + std::to_string(listen_port) + " removed successfully.");
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
    // mixer_config.use_tcp = config.use_tcp; // Removed

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
    std::lock_guard<std::mutex> lock(manager_mutex_);

    if (!running_) {
        return ""; // Return empty string on failure
    }

    // Generate a unique instance ID
    std::string instance_id = generate_unique_instance_id(config.tag);
    LOG_AM("Generated unique instance ID: " + instance_id);

    // Validate EQ size from the input config
    SourceConfig validated_config = config; // Make a copy
    if (!validated_config.initial_eq.empty() && validated_config.initial_eq.size() != EQ_BANDS) {
        validated_config.initial_eq.assign(EQ_BANDS, 1.0f);
    } else if (validated_config.initial_eq.empty()) {
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
        proc_config.output_channels = 2; // Fallback
    } else {
        proc_config.output_channels = validated_config.target_output_channels;
    }

    const std::vector<int> valid_samplerates = {8000, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 192000};
    if (std::find(valid_samplerates.begin(), valid_samplerates.end(), validated_config.target_output_samplerate) == valid_samplerates.end()) {
        proc_config.output_samplerate = 48000; // Fallback
    } else {
        proc_config.output_samplerate = validated_config.target_output_samplerate;
    }
    
    proc_config.initial_volume = validated_config.initial_volume;
    proc_config.initial_eq = validated_config.initial_eq; 
    proc_config.initial_delay_ms = validated_config.initial_delay_ms;
    proc_config.initial_timeshift_sec = validated_config.initial_timeshift_sec; // Added from SourceConfig
    // proc_config.timeshift_buffer_duration_sec remains default or could be made configurable

    // Determine protocol type and target receiver port from validated config
    // These fields are primarily for the SourceInputProcessor's internal configuration
    // and for the remove_source logic if it needs to unregister from a specific receiver.
    // For add_output_queue, we now register with ALL receivers.
    if (validated_config.protocol_type_hint == 0) {
        proc_config.protocol_type = InputProtocolType::RTP_SCREAM_PAYLOAD;
    } else if (validated_config.protocol_type_hint == 1) {
        proc_config.protocol_type = InputProtocolType::RAW_SCREAM_PACKET;
    } else if (validated_config.protocol_type_hint == 2) {
        proc_config.protocol_type = InputProtocolType::PER_PROCESS_SCREAM_PACKET;
    } else {
        LOG_WARN_AM("Unknown protocol_type_hint: " + std::to_string(validated_config.protocol_type_hint) + ". Defaulting to RTP_SCREAM_PAYLOAD.");
        proc_config.protocol_type = InputProtocolType::RTP_SCREAM_PAYLOAD; // Default
    }
    proc_config.target_receiver_port = validated_config.target_receiver_port; // Copy target port

    std::string protocol_str = "UNKNOWN";
    if (proc_config.protocol_type == InputProtocolType::RTP_SCREAM_PAYLOAD) protocol_str = "RTP_SCREAM_PAYLOAD";
    else if (proc_config.protocol_type == InputProtocolType::RAW_SCREAM_PACKET) protocol_str = "RAW_SCREAM_PACKET";
    else if (proc_config.protocol_type == InputProtocolType::PER_PROCESS_SCREAM_PACKET) protocol_str = "PER_PROCESS_SCREAM_PACKET";

    LOG_AM("Source instance " + instance_id + " configured with protocol type: " + protocol_str +
           (proc_config.target_receiver_port != -1 ? ", Target Port: " + std::to_string(proc_config.target_receiver_port) : ""));


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

    // Register input queue with RtpReceiver - This logic is now superseded by TimeshiftManager
    // The SIP's input queue (rtp_queue) is given to TimeshiftManager.
    // Receivers send to TimeshiftManager, which then routes to the correct SIP's rtp_queue.
    // So, direct registration of SIP queues with individual receivers is no longer needed.
    // if (rtp_receiver_) {
    //     // std::mutex* proc_mutex = new_source->get_timeshift_mutex(); // Removed
    //     // std::condition_variable* proc_cv = new_source->get_timeshift_cv(); // Removed
    //      rtp_receiver_->add_output_queue(proc_config.source_tag, instance_id, rtp_queue); // Removed
    //  } else {
    //       LOG_ERROR_AM("RtpReceiver is null, cannot add output target for instance " + instance_id);
    // ... (cleanup logic for failure)
    //  }

    // --- Register SourceInputProcessor's queue with ALL active receivers ---
    // This entire block is no longer needed as TimeshiftManager handles packet distribution.
    // Receivers will send all packets to TimeshiftManager.
    /*
    int registration_count = 0;

    // 1. Register with RtpReceiver
    if (rtp_receiver_) {
        rtp_receiver_->add_output_queue(proc_config.source_tag, instance_id, rtp_queue);
        LOG_AM("  Registered instance " + instance_id + " with RtpReceiver.");
        registration_count++;
    } else {
        LOG_WARN_AM("  RtpReceiver is null. Cannot register instance " + instance_id + ".");
    }

    // 2. Register with all RawScreamReceivers
    if (raw_scream_receivers_.empty()) {
        LOG_AM("  No RawScreamReceivers active to register with.");
    }
    for (auto const& [port, receiver_ptr] : raw_scream_receivers_) {
        if (receiver_ptr) {
            receiver_ptr->add_output_queue(proc_config.source_tag, instance_id, rtp_queue);
            LOG_AM("  Registered instance " + instance_id + " with RawScreamReceiver on port " + std::to_string(port) + ".");
            registration_count++;
        }
    }

    // 3. Register with all PerProcessScreamReceivers
    if (per_process_scream_receivers_.empty()) {
        LOG_AM("  No PerProcessScreamReceivers active to register with.");
    }
    for (auto const& [port, receiver_ptr] : per_process_scream_receivers_) {
        if (receiver_ptr) {
            receiver_ptr->add_output_queue(proc_config.source_tag, instance_id, rtp_queue);
            LOG_AM("  Registered instance " + instance_id + " (tag: " + proc_config.source_tag + ") with PerProcessScreamReceiver on port " + std::to_string(port) + ".");
            registration_count++;
        }
    }

    if (registration_count == 0) {
         LOG_WARN_AM("Warning: Source instance " + instance_id + " (tag: " + proc_config.source_tag + ") was not registered with ANY active receivers. It will not receive packets.");
    }
    */
    // Ensure TimeshiftManager is available
    if (!timeshift_manager_) {
        LOG_ERROR_AM("TimeshiftManager is null. Cannot configure source instance " + instance_id);
        // Clean up already created resources for this failed source
        rtp_to_source_queues_.erase(instance_id);
        source_to_sink_queues_.erase(instance_id);
        command_queues_.erase(instance_id);
        if(new_source) new_source->stop(); // Stop the processor if it was started
        return ""; // Return empty string on failure
    }

    // Register with TimeshiftManager
    if (new_source) { // new_source should be valid if we reached here
        timeshift_manager_->register_processor(
            instance_id,
            proc_config.source_tag,
            rtp_queue, // This is the input queue for the SourceInputProcessor
            proc_config.initial_delay_ms,
            proc_config.initial_timeshift_sec
        );
        LOG_AM("  Registered instance " + instance_id + " with TimeshiftManager.");
    } else {
        LOG_ERROR_AM("TimeshiftManager or new_source is null. Cannot register with TimeshiftManager for instance " + instance_id);
        // Cleanup if registration fails
        rtp_to_source_queues_.erase(instance_id);
        source_to_sink_queues_.erase(instance_id);
        command_queues_.erase(instance_id);
        if(new_source) new_source->stop(); // Stop the processor if it was started
        return ""; // Failure
    }

    sources_[instance_id] = std::move(new_source);

    LOG_AM("Source instance " + instance_id + " (tag: " + config.tag + ") configured and started successfully.");
    return instance_id; // Return the unique ID
}

// Refactored remove_source to handle different receiver types
bool AudioManager::remove_source(const std::string& instance_id) {
    std::unique_ptr<SourceInputProcessor> source_to_remove;
    std::string source_tag_for_removal; 
    InputProtocolType proto_type = InputProtocolType::RTP_SCREAM_PAYLOAD; // Default
    int target_port = -1; // Default

    { // Scope for lock
        std::lock_guard<std::mutex> lock(manager_mutex_);
        if (!running_) {
             return false;
        }

        // Find the processor by instance_id
        auto it = sources_.find(instance_id);
        if (it == sources_.end()) {
            LOG_ERROR_AM("Source processor instance not found: " + instance_id);
            return false;
        }

        if (it->second) {
             const auto& proc_config = it->second->get_config();
             source_tag_for_removal = proc_config.source_tag;
             proto_type = proc_config.protocol_type;
             target_port = proc_config.target_receiver_port;
        } else {
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

        // --- Unregister from appropriate receivers ---
        if (!source_tag_for_removal.empty()) {
            // Unregister from TimeshiftManager
            if (timeshift_manager_) {
                timeshift_manager_->unregister_processor(instance_id, source_tag_for_removal);
                LOG_AM("Unregistered instance " + instance_id + " (tag: " + source_tag_for_removal + ") from TimeshiftManager.");
            } else {
                LOG_WARN_AM("TimeshiftManager is null during removal of instance " + instance_id + ". Cannot unregister.");
            }
            // Calls to specific receiver remove_output_queue are no longer needed.
        } else {
            LOG_ERROR_AM("Source tag for removal is empty for instance " + instance_id + ". Cannot unregister from TimeshiftManager.");
        }

        for (auto& sink_pair : sinks_) {
            if (sink_pair.second) {
                sink_pair.second->remove_input_queue(instance_id);
            }
        }
    } // Mutex lock released

    // Stop the processor outside the lock
    if (source_to_remove) {
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
    }
    LOG_AM("Notification processing thread finished.");
}

// handle_new_source is now just informational, processor creation is explicit via configure_source
void AudioManager::handle_new_source(const std::string& source_tag) {
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
    std::lock_guard<std::mutex> lock(manager_mutex_);

    if (!running_) {
        return false;
    }

    // Find the sink
    auto sink_it = sinks_.find(sink_id);
    if (sink_it == sinks_.end() || !sink_it->second) {
        LOG_ERROR_AM("Sink not found or invalid: " + sink_id);
        return false;
    }

    auto queue_it = source_to_sink_queues_.find(source_instance_id);
    if (queue_it == source_to_sink_queues_.end() || !queue_it->second) {
        LOG_ERROR_AM("Source output queue not found for instance ID: " + source_instance_id);
        return false;
    }

    auto source_it = sources_.find(source_instance_id);
     if (source_it == sources_.end() || !source_it->second) {
         LOG_ERROR_AM("Source processor instance not found for ID: " + source_instance_id);
         return false;
     }
     std::string source_tag = source_it->second->get_source_tag();
     // LOG_WARN_AM("Need getter for source_tag in SourceInputProcessor for connect_source_sink");


    sink_it->second->add_input_queue(source_instance_id, queue_it->second); // Use instance_id now
    LOG_AM("Connection successful: Source instance " + source_instance_id + " -> Sink " + sink_id);
    return true;
}

// Refactored disconnect_source_sink to use instance_id
bool AudioManager::disconnect_source_sink(const std::string& source_instance_id, const std::string& sink_id) {
    std::lock_guard<std::mutex> lock(manager_mutex_);

    if (!running_) {
        return false;
    }

    // Find the sink
    auto sink_it = sinks_.find(sink_id);
    if (sink_it == sinks_.end() || !sink_it->second) {
        LOG_ERROR_AM("Sink not found or invalid for disconnection: " + sink_id);
        return false;
    }

     auto source_it = sources_.find(source_instance_id);
     if (source_it == sources_.end() || !source_it->second) {
         // Source instance doesn't exist, maybe already removed. Consider this success?
         LOG_WARN_AM("Source processor instance not found for disconnection: " + source_instance_id + ". Assuming already disconnected.");
         return true; // Or return false if strict check needed
     }
     std::string source_tag = source_it->second->get_source_tag();
     // LOG_WARN_AM("Need getter for source_tag in SourceInputProcessor for disconnect_source_sink");


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
         return false;
     }
    ControlCommand cmd;
    cmd.type = CommandType::SET_EQ;
    cmd.eq_values = eq_values; // Copy vector
    return send_command_to_source(instance_id, cmd);
}

bool AudioManager::update_source_delay(const std::string& instance_id, int delay_ms) {
    // First, send command to SIP if it still needs to know its own delay for some reason
    // (though TimeshiftManager now controls actual timing)
    ControlCommand cmd;
    cmd.type = CommandType::SET_DELAY;
    cmd.int_value = delay_ms;
    bool cmd_sent = send_command_to_source(instance_id, cmd);

    // Then, update TimeshiftManager
    if (timeshift_manager_) {
        timeshift_manager_->update_processor_delay(instance_id, delay_ms);
        LOG_AM("Updated delay in TimeshiftManager for instance " + instance_id + " to " + std::to_string(delay_ms) + "ms.");
    } else {
        LOG_ERROR_AM("TimeshiftManager is null. Cannot update processor delay for instance " + instance_id);
        return false; // Indicate failure if TimeshiftManager update fails
    }
    return cmd_sent; // Return status of command to SIP (or true if only TSM matters)
}

bool AudioManager::update_source_timeshift(const std::string& instance_id, float timeshift_sec) {
    // First, send command to SIP if it still needs to know its own timeshift
    ControlCommand cmd;
    cmd.type = CommandType::SET_TIMESHIFT;
    cmd.float_value = timeshift_sec;
    bool cmd_sent = send_command_to_source(instance_id, cmd);

    // Then, update TimeshiftManager
    if (timeshift_manager_) {
        timeshift_manager_->update_processor_timeshift(instance_id, timeshift_sec);
        LOG_AM("Updated timeshift in TimeshiftManager for instance " + instance_id + " to " + std::to_string(timeshift_sec) + "s.");
    } else {
        LOG_ERROR_AM("TimeshiftManager is null. Cannot update processor timeshift for instance " + instance_id);
        return false; // Indicate failure
    }
    return cmd_sent; // Return status of command to SIP
}

// Updated method to set speaker layout for a specific key
bool AudioManager::update_source_speaker_layout_for_key(const std::string& instance_id, int input_channel_key, const screamrouter::audio::CppSpeakerLayout& layout) {
    ControlCommand cmd;
    cmd.type = CommandType::SET_SPEAKER_MIX; // This command type now handles per-key layout updates
    cmd.input_channel_key = input_channel_key;
    cmd.speaker_layout_for_key = layout; // CppSpeakerLayout is now part of ControlCommand

    LOG_AM("Sending SET_SPEAKER_MIX command to instance_id: " + instance_id + 
           " for key: " + std::to_string(input_channel_key) + 
           " (Auto: " + (layout.auto_mode ? "true" : "false") + ")");
    return send_command_to_source(instance_id, cmd);
}

// New method to update the entire speaker layouts map
bool AudioManager::update_source_speaker_layouts_map(const std::string& instance_id, const std::map<int, screamrouter::audio::CppSpeakerLayout>& layouts_map) {
    // This method is called by AudioEngineConfigApplier.
    // It should inform the SourceInputProcessor about the entire new map.
    // SourceInputProcessor will have a method like set_speaker_layouts_config.
    // We need to find the SourceInputProcessor and call that method.
    // This is NOT done via ControlCommand, but a direct method call if possible,
    // or a new CommandType if direct call is not feasible (e.g. threading).
    // For now, let's assume SourceInputProcessor has set_speaker_layouts_config
    // and AudioManager can call it.

    std::lock_guard<std::mutex> lock(manager_mutex_); // Protects sources_ map
    if (!running_) return false;

    auto source_it = sources_.find(instance_id);
    if (source_it != sources_.end() && source_it->second) {
        // Assuming SourceInputProcessor has a method:
        // void set_speaker_layouts_config(const std::map<int, screamrouter::audio::CppSpeakerLayout>& layouts_map);
        // This method was added to SourceInputProcessor in Task 17.10.1
        source_it->second->set_speaker_layouts_config(layouts_map);
        LOG_AM("Updated speaker_layouts_map directly on SourceInputProcessor instance: " + instance_id);
        return true;
    } else {
        LOG_ERROR_AM("SourceInputProcessor instance not found for speaker_layouts_map update: " + instance_id);
        return false;
    }
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

std::vector<uint8_t> AudioManager::get_mp3_data_by_ip(const std::string& ip_address) {
    std::lock_guard<std::mutex> lock(manager_mutex_); // Protect access to sink_configs_

    if (!running_) {
        return {}; // Return empty vector if not running
    }

    // Iterate through sink_configs_ to find a sink with the matching output_ip
    for (const auto& pair : sink_configs_) {
        const SinkConfig& config = pair.second;
        if (config.output_ip == ip_address) {
            auto queue_it = mp3_output_queues_.find(config.id); // Use config.id instead of undefined found_sink_id
            if (queue_it != mp3_output_queues_.end()) {
                std::shared_ptr<Mp3Queue> target_queue = queue_it->second;
                if (target_queue) {
                    // We are still holding manager_mutex_ here.
                    // The try_pop on ThreadSafeQueue is designed to be safe.
                    EncodedMP3Data mp3_data_item;
                    if (target_queue->try_pop(mp3_data_item)) {
                        return mp3_data_item.mp3_data;
                    }
                }
            }
            return {}; // Found IP, but no queue or queue empty
        }
    }

    // LOG_WARN_AM("No sink found with IP: " + ip_address); // Can be noisy
    return {}; // IP address not found among sink configurations
}


// --- External Control ---
// Removed AudioManager::set_sink_tcp_fd

bool AudioManager::write_plugin_packet(
    const std::string& source_instance_tag,
    const std::vector<uint8_t>& audio_payload,
    int channels,
    int sample_rate,
    int bit_depth,
    uint8_t chlayout1,
    uint8_t chlayout2)
{
    std::lock_guard<std::mutex> lock(manager_mutex_);

    if (!running_) {
        LOG_ERROR_AM("AudioManager not running. Cannot write plugin packet.");
        return false;
    }

    // Find the SourceInputProcessor by its original tag (passed as source_instance_tag parameter)
    SourceInputProcessor* target_processor_ptr = nullptr;
    std::string found_instance_id; // To store the instance_id if found

    // Iterate over the sources map to find a processor whose configured tag matches source_instance_tag
    for (const auto& pair : sources_) {
        if (pair.second) { // Check if the unique_ptr is valid
            const auto& proc_config = pair.second->get_config(); // Get the processor's configuration
            if (proc_config.source_tag == source_instance_tag) { // Compare with the provided tag (parameter)
                target_processor_ptr = pair.second.get(); // Get raw pointer to the processor
                found_instance_id = pair.first;           // Store its unique instance_id (map key)
                break;                                    // Found, exit loop
            }
        }
    }

    if (!target_processor_ptr) {
        LOG_ERROR_AM("SourceInputProcessor instance not found for tag: " + source_instance_tag);
        return false;
    }

    // Call the global injection method instead.
    // The 'source_instance_tag' passed to write_plugin_packet is the 'source_tag'
    // that TimeshiftManager will use for filtering.
    this->inject_plugin_packet_globally(
        source_instance_tag, // This is the original source_tag the plugin wants to inject for
        audio_payload,
        channels,
        sample_rate,
        bit_depth,
        chlayout1,
        chlayout2
    );

    // inject_plugin_packet_globally handles its own logging.
    return true; // Assume success if the call is made.
}

// --- Receiver Info API Implementations ---
std::vector<std::string> AudioManager::get_rtp_receiver_seen_tags() {
    std::lock_guard<std::mutex> lock(manager_mutex_); // Ensure thread safety if rtp_receiver_ could be modified
    if (rtp_receiver_) {
        return rtp_receiver_->get_seen_tags();
    }
    return {}; // Return empty vector if receiver doesn't exist
}

std::vector<std::string> AudioManager::get_raw_scream_receiver_seen_tags(int listen_port) {
    std::lock_guard<std::mutex> lock(manager_mutex_);
    auto it = raw_scream_receivers_.find(listen_port);
    if (it != raw_scream_receivers_.end() && it->second) {
        return it->second->get_seen_tags();
    }
    LOG_WARN_AM("RawScreamReceiver not found for port: " + std::to_string(listen_port) + " when calling get_raw_scream_receiver_seen_tags.");
    return {}; // Return empty vector if receiver not found
}

void AudioManager::inject_plugin_packet_globally(
    const std::string& source_tag,
    const std::vector<uint8_t>& audio_payload,
    int channels,
    int sample_rate,
    int bit_depth,
    uint8_t chlayout1,
    uint8_t chlayout2)
{
    // Scope for lock, though timeshift_manager_->add_packet should be thread-safe internally
    std::lock_guard<std::mutex> lock(manager_mutex_);

    if (!running_ || !timeshift_manager_) {
        LOG_WARN_AM("AudioManager not running or TimeshiftManager not available. Plugin packet ignored for source_tag: " + source_tag);
        return;
    }

    // INPUT_CHUNK_BYTES should be available from source_input_processor.h (included via audio_manager.h)
    // If not, it needs to be defined or included appropriately.
    // For now, assuming it's accessible. If it causes a build error, source_input_processor.h might need direct include here.
    // Let's assume it's defined in audio_types.h or source_input_processor.h which is included.
    // A quick check of source_input_processor.h shows: const size_t INPUT_CHUNK_BYTES = 1152;
    if (audio_payload.size() != INPUT_CHUNK_BYTES) {
         LOG_ERROR_AM("Plugin packet payload incorrect size for source_tag: " + source_tag +
                      ". Expected " + std::to_string(INPUT_CHUNK_BYTES) + ", got " + std::to_string(audio_payload.size()));
         return;
    }

    TaggedAudioPacket packet;
    packet.source_tag = source_tag;
    packet.received_time = std::chrono::steady_clock::now();
    packet.sample_rate = sample_rate;
    packet.bit_depth = bit_depth;
    packet.channels = channels;
    packet.chlayout1 = chlayout1;
    packet.chlayout2 = chlayout2;
    packet.audio_data = audio_payload; // Copies the data

    timeshift_manager_->add_packet(std::move(packet));
    LOG_AM("Plugin packet injected globally via TimeshiftManager for source_tag: " + source_tag);
}

std::vector<std::string> AudioManager::get_per_process_scream_receiver_seen_tags(int listen_port) {
    std::lock_guard<std::mutex> lock(manager_mutex_);
    auto it = per_process_scream_receivers_.find(listen_port);
    if (it != per_process_scream_receivers_.end() && it->second) {
        return it->second->get_seen_tags();
    }
    LOG_WARN_AM("PerProcessScreamReceiver not found for port: " + std::to_string(listen_port) + " when calling get_per_process_scream_receiver_seen_tags.");
    return {}; // Return empty vector if receiver not found
}
