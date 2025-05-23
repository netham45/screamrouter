#include "rtp_receiver.h"
#include <iostream> // For logging/debugging
#include <vector>
#include <cstring> // For memcpy, memset
#include <chrono>
#include <stdexcept> // For runtime_error
#include <system_error> // For socket error checking
#include <utility> // For std::move
#include <algorithm> // For std::find, std::remove

// Use namespaces for clarity
namespace screamrouter { namespace audio { using namespace utils; } }
using namespace screamrouter::audio;
using namespace screamrouter::utils;


// Define constants based on original code and RTP standard
// CHUNK_SIZE was 1152 in original code, likely the expected PCM data size per packet
const size_t EXPECTED_CHUNK_SIZE = 1152;
const size_t RTP_HEADER_SIZE = 12;
const size_t EXPECTED_PAYLOAD_SIZE = RTP_HEADER_SIZE + EXPECTED_CHUNK_SIZE;
const uint8_t SCREAM_PAYLOAD_TYPE = 127;

// Define a reasonable buffer size for recvfrom
const size_t RECEIVE_BUFFER_SIZE = 2048; // Should be larger than EXPECTED_PAYLOAD_SIZE

// Poll timeout in milliseconds
const int POLL_TIMEOUT_MS = 100; // Check for stop flag every 100ms

long number = 0;

// Simple logger helper (replace with a proper logger if available)
#define LOG(msg) std::cout << "[RtpReceiver] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[RtpReceiver Error] " << msg << " (errno: " << GET_LAST_SOCK_ERROR << ")" << std::endl // Use macro
#define LOG_WARN(msg) std::cout << "[RtpReceiver Warn] " << msg << std::endl // Define WARN

RtpReceiver::RtpReceiver(
    RtpReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager) // Added timeshift_manager
    : config_(config),
      notification_queue_(notification_queue),
      timeshift_manager_(timeshift_manager), // Initialize timeshift_manager_
      socket_fd_(INVALID_SOCKET_VALUE) // Initialize with platform-specific invalid value
{
    if (!timeshift_manager_) {
        LOG_ERROR("TimeshiftManager pointer is null. RtpReceiver cannot function.");
        // Consider throwing an exception or setting an error state
        // For now, just log and continue, but it will likely crash if used.
        // throw std::runtime_error("TimeshiftManager is null in RtpReceiver constructor.");
    }
    #ifdef _WIN32
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            LOG_ERROR("WSAStartup failed: " + std::to_string(iResult));
            throw std::runtime_error("WSAStartup failed.");
        }
    #endif
    if (!notification_queue_) {
        throw std::runtime_error("RtpReceiver requires a valid notification queue.");
    }
    LOG("Initialized with port " + std::to_string(config_.listen_port));
}

RtpReceiver::~RtpReceiver() {
    // Ensure stop is called, even if not explicitly done by AudioManager
    // This helps prevent dangling threads if shutdown order is wrong.
    if (!stop_flag_) {
        stop();
    }
    // Join should happen in stop(), but double-check just in case stop wasn't called correctly
    if (component_thread_.joinable()) {
        LOG("Warning: Joining thread in destructor, stop() might not have been called properly.");
        try {
             component_thread_.join();
        } catch (const std::system_error& e) {
             LOG_ERROR("Error joining thread in destructor: " + std::string(e.what()));
        }
    }
    close_socket(); // Ensure socket is closed using macro

    #ifdef _WIN32
        WSACleanup();
    #endif
}

bool RtpReceiver::setup_socket() {
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0); // socket() is generally cross-platform
    if (socket_fd_ == INVALID_SOCKET_VALUE) { // Check against platform-specific invalid value
        LOG_ERROR("Failed to create socket");
        return false;
    }

    // Set socket options (e.g., reuse address) - SO_REUSEADDR might behave differently on Windows
    #ifdef _WIN32
        // On Windows, SO_REUSEADDR allows binding to a port in TIME_WAIT state,
        // but SO_EXCLUSIVEADDRUSE might be needed to prevent other sockets from binding.
        // For simplicity, we'll keep SO_REUSEADDR for now.
        char reuse = 1; // Use char for Windows setsockopt bool
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            LOG_ERROR("Failed to set SO_REUSEADDR");
            close_socket(); // Use macro
            return false;
        }
    #else // POSIX
        int reuse = 1;
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            LOG_ERROR("Failed to set SO_REUSEADDR");
            close_socket(); // Use macro
            return false;
        }
    #endif

    // Prepare address structure
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to any interface
    server_addr.sin_port = htons(config_.listen_port);

    // Bind the socket
    if (bind(socket_fd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("Failed to bind socket to port " + std::to_string(config_.listen_port));
        close_socket(); // Use macro
        return false;
    }

    LOG("Socket created and bound successfully to port " + std::to_string(config_.listen_port));
    return true;
}

void RtpReceiver::close_socket() {
    if (socket_fd_ != INVALID_SOCKET_VALUE) { // Check against platform-specific invalid value
        LOG("Closing socket"); // Simplified log message
        // Use the macro directly, assuming it resolves correctly based on header definition
        #ifdef _WIN32
            closesocket(socket_fd_);
        #else
            close(socket_fd_);
        #endif
        socket_fd_ = INVALID_SOCKET_VALUE; // Set to platform-specific invalid value
    }
}

void RtpReceiver::start() {
    if (is_running()) {
        LOG("Already running.");
        return;
    }
    LOG("Starting...");
    stop_flag_ = false; // Reset stop flag

    if (!setup_socket()) {
        LOG_ERROR("Failed to setup socket. Cannot start receiver thread.");
        // Optionally throw an exception or handle the error appropriately
        return;
    }

    // Launch the thread
    try {
        component_thread_ = std::thread(&RtpReceiver::run, this);
        LOG("Receiver thread started.");
    } catch (const std::system_error& e) {
        LOG_ERROR("Failed to start thread: " + std::string(e.what()));
        close_socket(); // Clean up socket if thread failed to start
        // Rethrow or handle error
        throw;
    }
}

void RtpReceiver::stop() {
    if (stop_flag_) {
        LOG("Already stopped or stopping.");
        return;
    }
    LOG("Stopping...");
    stop_flag_ = true; // Signal the run loop to exit

    // Closing the socket can help interrupt blocking recvfrom/poll calls
    close_socket();

    // Wait for the thread to finish
    if (component_thread_.joinable()) {
        try {
            component_thread_.join();
            LOG("Receiver thread joined.");
        } catch (const std::system_error& e) {
            LOG_ERROR("Error joining thread: " + std::string(e.what()));
        }
    } else {
        LOG("Thread was not joinable (might not have started or already stopped).");
    }
}

// add_output_queue and remove_output_queue are removed.

bool RtpReceiver::is_valid_rtp_payload(const uint8_t* buffer, int size) {
    // Basic size check
    if (size < RTP_HEADER_SIZE) {
        return false;
    }
    // Check payload type (byte 1, lower 7 bits)
    uint8_t payloadType = buffer[1] & 0x7F;
    return payloadType == SCREAM_PAYLOAD_TYPE;
}

void RtpReceiver::run() {
    LOG("Receiver thread entering run loop.");
    std::vector<uint8_t> receive_buffer(RECEIVE_BUFFER_SIZE);
    struct sockaddr_in client_addr;
    #ifdef _WIN32
        int client_addr_len = sizeof(client_addr); // Windows uses int for socklen_t equivalent
    #else
        socklen_t client_addr_len = sizeof(client_addr);
    #endif


    struct pollfd fds[1];
    fds[0].fd = socket_fd_;
    fds[0].events = POLLIN; // Check for data to read

    while (!stop_flag_) {
        // Use poll for non-blocking check with timeout
        #ifdef _WIN32
            int poll_ret = WSAPoll(fds, 1, POLL_TIMEOUT_MS);
        #else
            int poll_ret = poll(fds, 1, POLL_TIMEOUT_MS);
        #endif


        if (poll_ret < 0) {
            // Error in poll (ignore EINTR, handle others)
            #ifndef _WIN32 // EINTR is POSIX specific
                if (errno == EINTR) {
                    continue; // Interrupted by signal, just retry
                }
            #endif
            if (!stop_flag_) { // Don't log error if we are stopping anyway
                 LOG_ERROR("poll() failed");
            }
            if (!stop_flag_) { // Avoid busy-looping on error if not stopping
                 std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue; // Check stop_flag_ again
        }

        if (poll_ret == 0) {
            // Timeout - no data received, loop again to check stop_flag_
            continue;
        }

        // Check if data is available on the socket
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
                // Error receiving data (ignore if caused by stop())
                if (!stop_flag_) { // Don't log error if we are stopping anyway
                    LOG_ERROR("recvfrom() failed");
                }
                continue; // Check stop_flag_ again
            }

            // Process received packet
            if (static_cast<size_t>(bytes_received) == EXPECTED_PAYLOAD_SIZE && is_valid_rtp_payload(receive_buffer.data(), bytes_received)) {
                std::string source_tag = inet_ntoa(client_addr.sin_addr); // inet_ntoa is generally available but deprecated, consider inet_ntop
                auto received_time = std::chrono::steady_clock::now();

                // Check if source is new
                { // Scope for known_tags_mutex_ lock
                    std::lock_guard<std::mutex> lock(known_tags_mutex_);
                    if (known_source_tags_.find(source_tag) == known_source_tags_.end()) {
                        known_source_tags_.insert(source_tag);

                        // Add to seen_tags_ if not already present
                        { // New scope for seen_tags_mutex_
                            std::lock_guard<std::mutex> seen_lock(seen_tags_mutex_);
                            if (std::find(seen_tags_.begin(), seen_tags_.end(), source_tag) == seen_tags_.end()) {
                                seen_tags_.push_back(source_tag);
                            }
                        } // seen_tags_mutex_ released here
                        
                        // Unlock before pushing to queue to avoid holding lock while potentially blocking
                        lock.~lock_guard(); // Explicitly unlock before pushing
                        LOG("New source detected: " + source_tag);
                        notification_queue_->push(NewSourceNotification{source_tag});
                    }
                } // known_tags_mutex_ released here

                // Create the TaggedAudioPacket
                TaggedAudioPacket packet;
                packet.source_tag = source_tag;
                packet.received_time = received_time;
                // --- Set Default Format for RTP ---
                packet.channels = 2; 
                packet.sample_rate = 48000;
                packet.bit_depth = 16;
                packet.chlayout1 = 0x03; // Stereo L/R default
                packet.chlayout2 = 0x00;
                // --- Assign Payload ---
                packet.audio_data.assign(receive_buffer.data() + RTP_HEADER_SIZE,
                                               receive_buffer.data() + bytes_received);

                // Send packet to TimeshiftManager
                if (timeshift_manager_) {
                    timeshift_manager_->add_packet(std::move(packet));
                } else {
                    LOG_ERROR("TimeshiftManager is null. Cannot add packet.");
                }
            } else {
                 // Get sender IP again for the log message
                 std::string sender_ip_for_log = inet_ntoa(client_addr.sin_addr);
                 LOG("Received invalid or unexpected size packet (" + std::to_string(bytes_received) + " bytes) from " + sender_ip_for_log);
            }
        } else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
             // Socket error occurred
             LOG_ERROR("Socket error detected by poll()");
             // Consider breaking the loop or attempting recovery depending on the error
             break; // Exit loop on socket error
        }
    } // End while loop

    LOG("Receiver thread exiting run loop.");
}

std::vector<std::string> RtpReceiver::get_seen_tags() {
    std::lock_guard<std::mutex> lock(seen_tags_mutex_);
    return seen_tags_; // Return a copy
}
