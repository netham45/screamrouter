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
#define LOG_ERROR_RSR(msg) std::cerr << "[RawScreamReceiver Error] " << msg << " (errno: " << GET_LAST_SOCK_ERROR_RAW << ")" << std::endl // Use macro
#define LOG_WARN_RSR(msg) std::cout << "[RawScreamReceiver Warn] " << msg << std::endl

RawScreamReceiver::RawScreamReceiver(
    RawScreamReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager) // Added timeshift_manager
    : config_(config),
      notification_queue_(notification_queue),
      timeshift_manager_(timeshift_manager), // Initialize timeshift_manager_
      socket_fd_(INVALID_SOCKET_VALUE) // Initialize with platform-specific invalid value
{
    if (!timeshift_manager_) {
        LOG_ERROR_RSR("TimeshiftManager pointer is null. RawScreamReceiver cannot function.");
        // Consider throwing an exception or setting an error state
    }
    #ifdef _WIN32
        // WSAStartup might have already been called by RtpReceiver if both exist.
        // A more robust solution would use a static counter or flag.
        // For now, assume it might need to be called here too, or handle potential errors.
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) { // Allow already started error
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

// add_output_queue and remove_output_queue are removed.

bool RawScreamReceiver::is_valid_raw_scream_packet(const uint8_t* buffer, int size) {
    // buffer is unused in this implementation, but kept for signature consistency
    (void)buffer; // Mark as unused to prevent compiler warnings
    return size == static_cast<int>(EXPECTED_RAW_PACKET_SIZE);
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
                // POSIX recvfrom returns int, buffer is void*
                int bytes_received = recvfrom(socket_fd_, receive_buffer.data(), receive_buffer.size(), 0,
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
                        // lock.~lock_guard(); // This explicit unlock is not strictly necessary before the next lock, but kept for consistency with original logic
                        
                        // Add to seen_tags_ if not already present
                        { // New scope for seen_tags_mutex_
                            std::lock_guard<std::mutex> seen_lock(seen_tags_mutex_);
                            if (std::find(seen_tags_.begin(), seen_tags_.end(), source_tag) == seen_tags_.end()) {
                                seen_tags_.push_back(source_tag);
                            }
                        } // seen_tags_mutex_ released here

                        // Original notification logic (ensure known_tags_mutex_ is released if not already)
                        // If lock.~lock_guard() was used above, this is fine. Otherwise, ensure it's released.
                        // For safety, let's re-evaluate the lock scope for notification.
                        // The original code unlocked known_tags_mutex_ before notification.
                        // To maintain that, we can unlock it here if it wasn't already.
                        // However, since we are already in its scope, we can just proceed.
                        // The critical part is that notification_queue_->push is not holding the lock.
                        // The original explicit unlock `lock.~lock_guard();` handles this.
                        // Let's ensure that behavior is preserved.
                        // The explicit unlock `lock.~lock_guard();` should be AFTER adding to seen_tags_ if we want to keep known_tags_mutex_ locked during that operation.
                        // Or, we can have separate locks.
                        // Revisiting: The original code unlocked known_tags_mutex_ *before* notification.
                        // Let's stick to that pattern.
                        // The known_source_tags_.insert(source_tag) is done.
                        // Now, before notification, release known_tags_mutex_
                        // lock.~lock_guard(); // This was the original position.
                        // The new logic for seen_tags_ should ideally be within the same known_tags_mutex_ scope if we want to ensure atomicity of adding to both.
                        // However, the request is just to add to seen_tags_ when a new source is detected.
                        // Let's refine:
                        // 1. Lock known_tags_mutex_
                        // 2. Check if new source
                        // 3. If new:
                        //    a. insert into known_source_tags_
                        //    b. Lock seen_tags_mutex_
                        //    c. Add to seen_tags_ if not present
                        //    d. Unlock seen_tags_mutex_
                        //    e. Unlock known_tags_mutex_ (original explicit unlock)
                        //    f. Push notification
                        // This order seems correct. The explicit unlock of known_tags_mutex_ was already there.
                        // The code above already implements this logic flow.
                        // The `lock.~lock_guard()` was for `known_tags_mutex_`.
                        // We need to ensure it's called after the `seen_tags_` modification.
                        // Let's adjust the placement of the explicit unlock.
                        // No, the original `lock.~lock_guard()` was for `known_tags_mutex_` and it was before the notification.
                        // The `seen_tags_` logic is now nested.
                        // The `known_tags_mutex_` is still locked when `seen_tags_mutex_` is acquired.
                        // This is fine (lock ordering is consistent if always known_tags -> seen_tags).
                        // The explicit unlock `lock.~lock_guard()` for `known_tags_mutex_` should happen *after* all modifications guarded by it.
                        // So, it should be after the inner scope for `seen_tags_mutex_`.
                        
                        // Corrected placement of explicit unlock for known_tags_mutex_
                        // This ensures it's unlocked before the notification, as in the original code.
                        // The lock object `lock` refers to `known_tags_mutex_`.
                        // The inner lock `seen_lock` refers to `seen_tags_mutex_`.
                        // The explicit unlock should be for `lock` (known_tags_mutex_).
                        // The original code had `lock.~lock_guard();` right before `LOG_RSR` and `notification_queue_->push`.
                        // Let's ensure that.
                        // The current structure is:
                        // lock(known_tags_mutex_)
                        // if new:
                        //   insert to known_source_tags_
                        //   lock(seen_tags_mutex_)
                        //   add to seen_tags_
                        //   unlock(seen_tags_mutex_)
                        //   lock.~lock_guard() // for known_tags_mutex_
                        //   LOG_RSR
                        //   notification_queue_->push
                        // This seems correct. The provided code already has `lock.~lock_guard();`
                        // Let's verify its position relative to the new block.
                        // Original:
                        // { std::lock_guard<std::mutex> lock(known_tags_mutex_);
                        //   if (known_source_tags_.find(source_tag) == known_source_tags_.end()) {
                        //     known_source_tags_.insert(source_tag);
                        //     lock.~lock_guard(); // UNLOCKS KNOWN_TAGS_MUTEX
                        //     LOG_RSR("New source detected: " + source_tag);
                        //     notification_queue_->push(NewSourceNotification{source_tag});
                        //   }
                        // }
                        // With new code:
                        // { std::lock_guard<std::mutex> lock(known_tags_mutex_);
                        //   if (known_source_tags_.find(source_tag) == known_source_tags_.end()) {
                        //     known_source_tags_.insert(source_tag);
                        //     { std::lock_guard<std::mutex> seen_lock(seen_tags_mutex_);
                        //       if (std::find(seen_tags_.begin(), seen_tags_.end(), source_tag) == seen_tags_.end()) {
                        //         seen_tags_.push_back(source_tag);
                        //       }
                        //     }
                        //     lock.~lock_guard(); // UNLOCKS KNOWN_TAGS_MUTEX
                        //     LOG_RSR("New source detected: " + source_tag);
                        //     notification_queue_->push(NewSourceNotification{source_tag});
                        //   }
                        // }
                        // This order is correct. The `lock.~lock_guard()` is for `known_tags_mutex_`.
                        // The `seen_tags_` modification happens while `known_tags_mutex_` is still locked.
                        // Then `known_tags_mutex_` is explicitly unlocked before notification.
                        // This matches the logic.
                        // The code block I wrote for the SEARCH part was:
                        // {
                        //     std::lock_guard<std::mutex> lock(known_tags_mutex_);
                        //     if (known_source_tags_.find(source_tag) == known_source_tags_.end()) {
                        //         known_source_tags_.insert(source_tag);
                        //         // Unlock before pushing to queue
                        //         lock.~lock_guard(); 
                        //         LOG_RSR("New source detected: " + source_tag);
                        //         notification_queue_->push(NewSourceNotification{source_tag});
                        //     }
                        // }
                        // This is what I need to replace.
                        // The replacement block correctly inserts the seen_tags logic before the explicit unlock.
                        lock.~lock_guard(); // This explicitly unlocks known_tags_mutex_
                        LOG_RSR("New source detected: " + source_tag);
                        notification_queue_->push(NewSourceNotification{source_tag});
                    }
                }

                TaggedAudioPacket packet;
                packet.source_tag = source_tag;
                packet.received_time = received_time;

                // --- Parse Header and Set Format ---
                const uint8_t* header = receive_buffer.data();
                bool is_44100_base = (header[0] >> 7) & 0x01;
                uint8_t samplerate_divisor = header[0] & 0x7F;
                if (samplerate_divisor == 0) samplerate_divisor = 1; 

                packet.sample_rate = (is_44100_base ? 44100 : 48000) / samplerate_divisor;
                packet.bit_depth = static_cast<int>(header[1]);
                packet.channels = static_cast<int>(header[2]);
                packet.chlayout1 = header[3]; 
                packet.chlayout2 = header[4];
                // --- Assign Payload Only ---
                // Ensure we only copy the 1152 bytes after the header
                packet.audio_data.assign(receive_buffer.data() + SCREAM_HEADER_SIZE_CONST,
                                               receive_buffer.data() + bytes_received); 

                // Basic validation after parsing
                if (packet.channels <= 0 || packet.channels > 8 ||
                    (packet.bit_depth != 8 && packet.bit_depth != 16 && packet.bit_depth != 24 && packet.bit_depth != 32) ||
                    packet.sample_rate <= 0 ||
                    packet.audio_data.size() != CHUNK_SIZE_CONST) { // Validate payload size
                    LOG_ERROR_RSR("Parsed invalid format or payload size from packet. SR=" + std::to_string(packet.sample_rate) +
                                  ", BD=" + std::to_string(packet.bit_depth) + ", CH=" + std::to_string(packet.channels) + 
                                  ", PayloadSize=" + std::to_string(packet.audio_data.size()));
                    // Skip pushing this packet if format is invalid
                    continue; 
                }

                if (timeshift_manager_) {
                    timeshift_manager_->add_packet(std::move(packet));
                } else {
                    LOG_ERROR_RSR("TimeshiftManager is null. Cannot add packet.");
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

std::vector<std::string> RawScreamReceiver::get_seen_tags() {
    std::lock_guard<std::mutex> lock(seen_tags_mutex_);
    return seen_tags_; // Return a copy
}
