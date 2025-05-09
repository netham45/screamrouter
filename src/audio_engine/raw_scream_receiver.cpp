#include "raw_scream_receiver.h"
#include <iostream>      // For logging/debugging
#include <vector>
#include <cstring>       // For memcpy, memset
#include <chrono>
#include <stdexcept>     // For runtime_error
#include <system_error>  // For socket error checking
#include <utility>       // For std::move
#include <algorithm>     // For std::find, std::remove

// Use namespaces for clarity
namespace screamrouter { namespace audio { using namespace utils; } }
using namespace screamrouter::audio;
using namespace screamrouter::utils;

// Define constants
const size_t SCREAM_HEADER_SIZE_CONST = 5; // Renamed to avoid conflict if CHUNK_SIZE is also 5 somewhere
const size_t CHUNK_SIZE_CONST = 1152; // Renamed
const size_t EXPECTED_RAW_PACKET_SIZE = SCREAM_HEADER_SIZE_CONST + CHUNK_SIZE_CONST; // 5 + 1152 = 1157
const size_t RAW_RECEIVE_BUFFER_SIZE = 2048; // Should be larger than EXPECTED_RAW_PACKET_SIZE
const int RAW_POLL_TIMEOUT_MS = 100;   // Check for stop flag every 100ms

// Simple logger helper (replace with a proper logger if available)
#define LOG_RSR(msg) std::cout << "[RawScreamReceiver] " << msg << std::endl
#define LOG_ERROR_RSR(msg) std::cerr << "[RawScreamReceiver Error] " << msg << " (errno: " << GET_LAST_SOCK_ERROR << ")" << std::endl // Use macro
#define LOG_WARN_RSR(msg) std::cout << "[RawScreamReceiver Warn] " << msg << std::endl

RawScreamReceiver::RawScreamReceiver(
    RawScreamReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue)
    : config_(config),
      notification_queue_(notification_queue),
      socket_fd_(INVALID_SOCKET_VALUE) // Initialize with platform-specific invalid value
{
    #ifdef _WIN32
        // WSAStartup might have already been called by RtpReceiver if both exist.
        // A more robust solution would use a static counter or flag.
        // For now, assume it might need to be called here too, or handle potential errors.
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0 && iResult != WSAEALREADYSTARTED) { // Allow already started error
            LOG_ERROR_RSR("WSAStartup failed: " + std::to_string(iResult));
            throw std::runtime_error("WSAStartup failed.");
        }
    #endif
    if (!notification_queue_) {
        throw std::runtime_error("RawScreamReceiver requires a valid notification queue.");
    }
    LOG_RSR("Initialized with port " + std::to_string(config_.listen_port));
}

RawScreamReceiver::~RawScreamReceiver() {
    if (!stop_flag_) {
        // Ensure stop is called, even if not explicitly done by AudioManager
        LOG_WARN_RSR("Destructor called while still running. Forcing stop.");
        stop();
    }
    // Join should happen in stop(), but double-check
    if (component_thread_.joinable()) {
        LOG_WARN_RSR("Warning: Joining thread in destructor, stop() might not have been called properly.");
        try {
            component_thread_.join();
        } catch (const std::system_error& e) {
            LOG_ERROR_RSR("Error joining thread in destructor: " + std::string(e.what()));
        }
    }
    close_socket(); // Ensure socket is closed using macro
    LOG_RSR("Destroyed.");

    #ifdef _WIN32
        // WSACleanup might be called prematurely if other components still need Winsock.
        // A static counter approach is better for managing WSAStartup/Cleanup.
        // For now, call it, but be aware of potential issues in multi-instance scenarios.
        // WSACleanup(); // Commenting out cleanup here, should be managed globally
    #endif
}

bool RawScreamReceiver::setup_socket() {
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0); // socket() is generally cross-platform
    if (socket_fd_ == INVALID_SOCKET_VALUE) { // Check against platform-specific invalid value
        LOG_ERROR_RSR("Failed to create socket");
        return false;
    }

    // Set socket options (e.g., reuse address)
    #ifdef _WIN32
        char reuse = 1; // Use char for Windows setsockopt bool
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            LOG_ERROR_RSR("Failed to set SO_REUSEADDR");
            close_socket(); // Use macro
            return false;
        }
    #else // POSIX
        int reuse = 1;
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            LOG_ERROR_RSR("Failed to set SO_REUSEADDR");
            close_socket(); // Use macro
            return false;
        }
    #endif

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(config_.listen_port);

    if (bind(socket_fd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR_RSR("Failed to bind socket to port " + std::to_string(config_.listen_port));
        close_socket(); // Use macro
        return false;
    }

    LOG_RSR("Socket created and bound successfully to port " + std::to_string(config_.listen_port));
    return true;
}

void RawScreamReceiver::close_socket() {
    if (socket_fd_ != INVALID_SOCKET_VALUE) { // Check against platform-specific invalid value
        LOG_RSR("Closing socket"); // Simplified log message
        // Use the macro directly, assuming it resolves correctly based on header definition
        #ifdef _WIN32
            closesocket(socket_fd_);
        #else
            close(socket_fd_);
        #endif
        socket_fd_ = INVALID_SOCKET_VALUE; // Set to platform-specific invalid value
    }
}

void RawScreamReceiver::start() {
    if (is_running()) {
        LOG_RSR("Already running.");
        return;
    }
    LOG_RSR("Starting...");
    stop_flag_ = false;

    if (!setup_socket()) {
        LOG_ERROR_RSR("Failed to setup socket. Cannot start receiver thread.");
        // Consider throwing an exception or setting an error state
        return;
    }

    try {
        component_thread_ = std::thread(&RawScreamReceiver::run, this);
        LOG_RSR("Receiver thread started.");
    } catch (const std::system_error& e) {
        LOG_ERROR_RSR("Failed to start thread: " + std::string(e.what()));
        close_socket();
        throw; // Rethrow or handle error
    }
}

void RawScreamReceiver::stop() {
    if (stop_flag_) {
        LOG_RSR("Already stopped or stopping.");
        return;
    }
    LOG_RSR("Stopping...");
    stop_flag_ = true;

    close_socket(); // Interrupt blocking recvfrom/poll

    if (component_thread_.joinable()) {
        try {
            component_thread_.join();
            LOG_RSR("Receiver thread joined.");
        } catch (const std::system_error& e) {
            LOG_ERROR_RSR("Error joining thread: " + std::string(e.what()));
        }
    } else {
        LOG_RSR("Thread was not joinable (might not have started or already stopped).");
    }
}

void RawScreamReceiver::add_output_queue(
    const std::string& source_tag,
    const std::string& instance_id,
    std::shared_ptr<PacketQueue> queue)
    // Removed mutex and cv parameters
{
    std::lock_guard<std::mutex> lock(targets_mutex_);
    if (queue) { // Check only queue
        output_targets_[source_tag][instance_id] = SourceOutputTarget{queue}; // Store only queue
        LOG_RSR("Added/Updated output target for source_tag: " + source_tag + ", instance_id: " + instance_id);
    } else {
        LOG_ERROR_RSR("Attempted to add output target with null queue for source_tag: " + source_tag + ", instance_id: " + instance_id);
    }
}

void RawScreamReceiver::remove_output_queue(const std::string& source_tag, const std::string& instance_id) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    auto tag_it = output_targets_.find(source_tag);
    if (tag_it != output_targets_.end()) {
        auto& instance_map = tag_it->second;
        auto instance_it = instance_map.find(instance_id);

        if (instance_it != instance_map.end()) {
            instance_map.erase(instance_it);
            LOG_RSR("Removed output target for source_tag: " + source_tag + ", instance_id: " + instance_id);
            if (instance_map.empty()) {
                output_targets_.erase(tag_it);
                LOG_RSR("Removed source tag entry as no targets remain: " + source_tag);
            }
        } else {
            LOG_WARN_RSR("Attempted to remove target for instance_id " + instance_id + " under source_tag " + source_tag + ", but instance_id was not found.");
        }
    } else {
        LOG_WARN_RSR("Attempted to remove target for non-existent source tag: " + source_tag);
    }
}

bool RawScreamReceiver::is_valid_raw_scream_packet(const uint8_t* buffer, ssize_t size) {
    // buffer is unused in this implementation, but kept for signature consistency
    (void)buffer; // Mark as unused to prevent compiler warnings
    return size == static_cast<ssize_t>(EXPECTED_RAW_PACKET_SIZE);
}

void RawScreamReceiver::run() {
    LOG_RSR("Receiver thread entering run loop.");
    std::vector<uint8_t> receive_buffer(RAW_RECEIVE_BUFFER_SIZE);
    struct sockaddr_in client_addr;
    #ifdef _WIN32
        int client_addr_len = sizeof(client_addr); // Windows uses int for socklen_t equivalent
    #else
        socklen_t client_addr_len = sizeof(client_addr);
    #endif


    struct pollfd fds[1];
    fds[0].fd = socket_fd_;
    fds[0].events = POLLIN;

    while (!stop_flag_) {
        #ifdef _WIN32
            int poll_ret = WSAPoll(fds, 1, RAW_POLL_TIMEOUT_MS);
        #else
            int poll_ret = poll(fds, 1, RAW_POLL_TIMEOUT_MS);
        #endif


        if (poll_ret < 0) {
            #ifndef _WIN32 // EINTR is POSIX specific
                if (errno == EINTR) {
                    continue; // Interrupted by signal, just retry
                }
            #endif
            if (!stop_flag_) {
                 LOG_ERROR_RSR("poll() failed");
            }
            if (!stop_flag_) {
                 std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        if (poll_ret == 0) {
            continue; // Timeout
        }

        if (fds[0].revents & POLLIN) {
            #ifdef _WIN32
                // Windows recvfrom returns int, buffer is char*
                int bytes_received = recvfrom(socket_fd_, reinterpret_cast<char*>(receive_buffer.data()), static_cast<int>(receive_buffer.size()), 0,
                                                  (struct sockaddr *)&client_addr, &client_addr_len);
            #else
                // POSIX recvfrom returns ssize_t, buffer is void*
                ssize_t bytes_received = recvfrom(socket_fd_, receive_buffer.data(), receive_buffer.size(), 0,
                                                  (struct sockaddr *)&client_addr, &client_addr_len);
            #endif


            if (bytes_received < 0) {
                if (!stop_flag_) {
                    LOG_ERROR_RSR("recvfrom() failed");
                }
                continue;
            }

            // Log packet reception *before* validation
            std::string sender_ip = inet_ntoa(client_addr.sin_addr);
//            LOG_RSR("Received " + std::to_string(bytes_received) + " bytes from " + sender_ip + " on port " + std::to_string(config_.listen_port));


            if (is_valid_raw_scream_packet(receive_buffer.data(), bytes_received)) {
                std::string source_tag = sender_ip; // Use the already converted IP
                auto received_time = std::chrono::steady_clock::now();

                {
                    std::lock_guard<std::mutex> lock(known_tags_mutex_);
                    if (known_source_tags_.find(source_tag) == known_source_tags_.end()) {
                        known_source_tags_.insert(source_tag);
                        // Unlock before pushing to queue
                        lock.~lock_guard(); 
                        LOG_RSR("New source detected: " + source_tag);
                        notification_queue_->push(NewSourceNotification{source_tag});
                    }
                }

                std::map<std::string, SourceOutputTarget> instance_targets_copy;
                {
                    std::lock_guard<std::mutex> lock(targets_mutex_);
                    auto tag_it = output_targets_.find(source_tag);
                    if (tag_it != output_targets_.end()) {
                        instance_targets_copy = tag_it->second;
                    }
                }

                if (!instance_targets_copy.empty()) {
                    TaggedAudioPacket base_packet;
                    base_packet.source_tag = source_tag;
                    base_packet.received_time = received_time;

                    // --- Parse Header and Set Format ---
                    const uint8_t* header = receive_buffer.data();
                    bool is_44100_base = (header[0] >> 7) & 0x01;
                    uint8_t samplerate_divisor = header[0] & 0x7F;
                    if (samplerate_divisor == 0) samplerate_divisor = 1; 

                    base_packet.sample_rate = (is_44100_base ? 44100 : 48000) / samplerate_divisor;
                    base_packet.bit_depth = static_cast<int>(header[1]);
                    base_packet.channels = static_cast<int>(header[2]);
                    base_packet.chlayout1 = header[3]; 
                    base_packet.chlayout2 = header[4];
                    // --- Assign Payload Only ---
                    // Ensure we only copy the 1152 bytes after the header
                    base_packet.audio_data.assign(receive_buffer.data() + SCREAM_HEADER_SIZE_CONST,
                                                   receive_buffer.data() + bytes_received); 

                    // Basic validation after parsing
                    if (base_packet.channels <= 0 || base_packet.channels > 8 ||
                        (base_packet.bit_depth != 8 && base_packet.bit_depth != 16 && base_packet.bit_depth != 24 && base_packet.bit_depth != 32) ||
                        base_packet.sample_rate <= 0 ||
                        base_packet.audio_data.size() != CHUNK_SIZE_CONST) { // Validate payload size
                        LOG_ERROR_RSR("Parsed invalid format or payload size from packet. SR=" + std::to_string(base_packet.sample_rate) +
                                      ", BD=" + std::to_string(base_packet.bit_depth) + ", CH=" + std::to_string(base_packet.channels) + 
                                      ", PayloadSize=" + std::to_string(base_packet.audio_data.size()));
                        // Skip pushing this packet if format is invalid
                        continue; 
                    }


                    for (const auto& [instance_id, target] : instance_targets_copy) {
                        if (target.queue) {
                            TaggedAudioPacket packet_copy = base_packet;
                            target.queue->push(std::move(packet_copy));
                            // Removed CV notification logic
                        } else {
                             LOG_ERROR_RSR("Found null queue pointer for instance: " + instance_id);
                        }
                    }
                } else {
                    // LOG_RSR("No output targets registered for source_tag: " + source_tag); // Can be noisy
                }
            } else {
                 // Get sender IP again for the log message
                 std::string sender_ip_for_log = inet_ntoa(client_addr.sin_addr);
                 LOG_WARN_RSR("Received invalid or unexpected size packet (" + std::to_string(bytes_received) + " bytes) from " + sender_ip_for_log + ". Expected " + std::to_string(EXPECTED_RAW_PACKET_SIZE) + " bytes.");
            }
        } else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
             LOG_ERROR_RSR("Socket error detected by poll()");
             break;
        }
    }
    LOG_RSR("Receiver thread exiting run loop.");
}
