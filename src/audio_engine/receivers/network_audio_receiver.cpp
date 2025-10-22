#include "network_audio_receiver.h"
#include "../input_processor/timeshift_manager.h" // Ensure full definition is available
#include "../utils/thread_safe_queue.h" // For full definition of ThreadSafeQueue
#include "../utils/cpp_logger.h"       // For new C++ logger
#include <iostream>      // For logging (cpp_logger fallback)
#include <vector>
#include <cstring>       // For memset
#include <stdexcept>     // For runtime_error
#include <system_error>  // For socket error checking
#include <algorithm>     // For std::find

namespace screamrouter {
namespace audio {

#ifdef _WIN32
    std::atomic<int> NetworkAudioReceiver::winsock_user_count_(0);
#endif

NetworkAudioReceiver::NetworkAudioReceiver(
    uint16_t listen_port,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager,
    std::string logger_prefix)
    : listen_port_(listen_port),
      socket_fd_(NAR_INVALID_SOCKET_VALUE),
      notification_queue_(notification_queue),
      timeshift_manager_(timeshift_manager),
      logger_prefix_(std::move(logger_prefix)) {

#ifdef _WIN32
    increment_winsock_users();
#endif
    if (!notification_queue_) {
        throw std::runtime_error(logger_prefix_ + " requires a valid notification queue.");
    }
    if (!timeshift_manager_) {
        // This is a critical dependency, consider throwing or logging a severe error
        log_error("TimeshiftManager pointer is null. Receiver will not function correctly.");
        // throw std::runtime_error(logger_prefix_ + " requires a valid TimeshiftManager.");
    }
    log_message("Initialized with port " + std::to_string(listen_port_));
}

NetworkAudioReceiver::~NetworkAudioReceiver() noexcept {
    if (!stop_flag_) {
        log_warning("Destructor called while still running. Forcing stop.");
        stop(); // Ensure stop is called
    }
#ifdef _WIN32
    decrement_winsock_users();
#endif
    log_message("Destroyed.");
}

#ifdef _WIN32
void NetworkAudioReceiver::increment_winsock_users() {
    if (winsock_user_count_.fetch_add(1) == 0) {
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            // This is a critical failure, cannot proceed with socket operations
            // Decrement count as startup failed
            winsock_user_count_.fetch_sub(1);
            throw std::runtime_error("WSAStartup failed: " + std::to_string(iResult));
        }
    }
}

void NetworkAudioReceiver::decrement_winsock_users() {
    if (winsock_user_count_.fetch_sub(1) == 1) {
        WSACleanup();
    }
}
#endif

void NetworkAudioReceiver::log_message(const std::string& msg) {
    LOG_CPP_INFO("%s %s", logger_prefix_.c_str(), msg.c_str());
}

void NetworkAudioReceiver::log_error(const std::string& msg) {
    LOG_CPP_ERROR("%s Error: %s (errno: %d)", logger_prefix_.c_str(), msg.c_str(), NAR_GET_LAST_SOCK_ERROR);
}

void NetworkAudioReceiver::log_warning(const std::string& msg) {
    LOG_CPP_WARNING("%s Warn: %s", logger_prefix_.c_str(), msg.c_str());
}

bool NetworkAudioReceiver::setup_socket() {
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd_ == NAR_INVALID_SOCKET_VALUE) {
        log_error("Failed to create socket");
        return false;
    }

#ifdef _WIN32
    char reuse = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        log_error("Failed to set SO_REUSEADDR");
        close_socket();
        return false;
    }
    int buffer_size = 1152 * 10;
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
#else // POSIX
    int reuse = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        log_error("Failed to set SO_REUSEADDR");
        close_socket();
        return false;
    }
    int buffer_size = 1152 * 10;
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
#endif

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(listen_port_);

    if (bind(socket_fd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        log_error("Failed to bind socket to port " + std::to_string(listen_port_));
        close_socket();
        return false;
    }

    log_message("Socket created and bound successfully to port " + std::to_string(listen_port_));
    return true;
}

void NetworkAudioReceiver::close_socket() {
    if (socket_fd_ != NAR_INVALID_SOCKET_VALUE) {
        log_message("Closing socket");
#ifdef _WIN32
        closesocket(socket_fd_);
#else
        close(socket_fd_);
#endif
        socket_fd_ = NAR_INVALID_SOCKET_VALUE;
    }
}

void NetworkAudioReceiver::start() {
    if (is_running()) {
        log_warning("Already running.");
        return;
    }
    log_message("Starting...");
    stop_flag_ = false;

    if (!setup_socket()) {
        log_error("Failed to setup socket. Cannot start receiver thread.");
        return; // Or throw
    }

    try {
        component_thread_ = std::thread([this]() {
            this->run();
        });
        log_message("Receiver thread started.");
    } catch (const std::system_error& e) {
        log_error("Failed to start thread: " + std::string(e.what()));
        close_socket(); // Clean up socket if thread failed to start
        throw; // Rethrow or handle error
    }
}

void NetworkAudioReceiver::stop() {
    if (stop_flag_) {
        log_warning("Already stopped or stopping.");
        return;
    }
    log_message("Stopping... (socket_fd=" + std::to_string(socket_fd_) + ", thread_joinable=" + (component_thread_.joinable() ? std::string("1") : std::string("0")) + ")");
    stop_flag_ = true;

    // Closing the socket can help interrupt blocking recvfrom/poll calls
    // This needs to be done carefully if the socket is used by another thread (run loop)
    // It's generally safe if poll/recvfrom handle errors like EBADF or specific shutdown signals.
    close_socket(); // Close socket to unblock poll/recvfrom
    log_message("Socket closed (fd now=" + std::to_string(socket_fd_) + ")");

    if (component_thread_.joinable()) {
        try {
            component_thread_.join();
            log_message("Receiver thread joined.");
        } catch (const std::system_error& e) {
            log_error("Error joining thread: " + std::string(e.what()));
        }
    } else {
        log_warning("Thread was not joinable (might not have started or already stopped).");
    }
}

void NetworkAudioReceiver::run() {
    log_message("Receiver thread entering run loop.");
    std::vector<uint8_t> receive_buffer(get_receive_buffer_size());
    struct sockaddr_in client_addr;
#ifdef _WIN32
    int client_addr_len = sizeof(client_addr);
#else
    socklen_t client_addr_len = sizeof(client_addr);
#endif

    struct pollfd fds[1];
    fds[0].fd = socket_fd_;
    fds[0].events = POLLIN; // Check for data to read

    int poll_timeout = get_poll_timeout_ms();

    while (!stop_flag_) {
        if (socket_fd_ == NAR_INVALID_SOCKET_VALUE) { // Socket might have been closed by stop()
            log_warning("Socket is invalid, exiting run loop.");
            break;
        }
        fds[0].fd = socket_fd_; // Ensure fd is current, though it shouldn't change unless re-setup
        fds[0].events = POLLIN;
        fds[0].revents = 0;

        int poll_ret = NAR_POLL(fds, 1, poll_timeout);

        if (stop_flag_) break; // Check immediately after poll returns

        if (poll_ret < 0) {
#ifndef _WIN32 // EINTR is POSIX specific
            if (NAR_GET_LAST_SOCK_ERROR == EINTR) {
                continue; // Interrupted by signal, just retry
            }
#endif
            // If socket was closed by stop(), poll might return error.
            // Check stop_flag_ again to avoid logging error during shutdown.
            if (!stop_flag_ && socket_fd_ != NAR_INVALID_SOCKET_VALUE) {
                 log_error("poll() failed");
            }
            // Avoid busy-looping on persistent error if not stopping
            if (!stop_flag_) {
                 std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        if (poll_ret == 0) {
            // Timeout - no data received, loop again to check stop_flag_
            continue;
        }

        if (fds[0].revents & POLLIN) {
            // Check socket validity again before recvfrom, as stop() might have closed it
            if (socket_fd_ == NAR_INVALID_SOCKET_VALUE) {
                log_warning("Socket became invalid before recvfrom, exiting run loop.");
                break;
            }
#ifdef _WIN32
            int bytes_received = recvfrom(socket_fd_, reinterpret_cast<char*>(receive_buffer.data()), static_cast<int>(receive_buffer.size()), 0,
                                              (struct sockaddr *)&client_addr, &client_addr_len);
#else
            int bytes_received = recvfrom(socket_fd_, receive_buffer.data(), receive_buffer.size(), 0,
                                              (struct sockaddr *)&client_addr, &client_addr_len);
#endif

            if (bytes_received < 0) {
                if (!stop_flag_ && socket_fd_ != NAR_INVALID_SOCKET_VALUE) {
                    log_error("recvfrom() failed");
                }
                continue;
            }

            if (is_valid_packet_structure(receive_buffer.data(), bytes_received, client_addr)) {
                TaggedAudioPacket packet;
                std::string source_tag;
                auto received_time = std::chrono::steady_clock::now();

                if (process_and_validate_payload(receive_buffer.data(), bytes_received, client_addr, received_time, packet, source_tag)) {
                    // Check if source is new and always track it for discovery polling
                    bool is_new_source = false;
                    {
                        std::lock_guard<std::mutex> lock(known_tags_mutex_);
                        auto insert_result = known_source_tags_.insert(source_tag);
                        is_new_source = insert_result.second;
                    }

                    {
                        std::lock_guard<std::mutex> seen_lock(seen_tags_mutex_);
                        if (std::find(seen_tags_.begin(), seen_tags_.end(), source_tag) == seen_tags_.end()) {
                            seen_tags_.push_back(source_tag);
                        }
                    }

                    if (is_new_source) {
                        log_message("New source detected: " + source_tag);
                        if (notification_queue_) {
                             notification_queue_->push(DeviceDiscoveryNotification{source_tag, DeviceDirection::CAPTURE, true});
                        } else {
                            log_warning("Notification queue is null, cannot notify for new source: " + source_tag);
                        }
                    }

                    // Let derived classes decide how to dispatch validated packets.
                    dispatch_ready_packet(std::move(packet));
                } else {
                    // process_and_validate_payload should log specific reasons for failure
                    // log_warning("Failed to process/validate payload from " + std::string(inet_ntoa(client_addr.sin_addr)));
                }
            } else {
                // is_valid_packet_structure might log, or we can log generically here
                // log_warning("Received invalid packet structure from " + std::string(inet_ntoa(client_addr.sin_addr)));
            }
        } else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
             // Socket error occurred
             if (!stop_flag_ && socket_fd_ != NAR_INVALID_SOCKET_VALUE) {
                log_error("Socket error detected by poll()");
             }
             break; // Exit loop on socket error
        }
    } // End while loop

    log_message("Receiver thread exiting run loop.");
}

void NetworkAudioReceiver::dispatch_ready_packet(TaggedAudioPacket&& packet) {
    if (timeshift_manager_) {
        timeshift_manager_->add_packet(std::move(packet));
    } else {
        log_error("TimeshiftManager is null. Cannot add packet for source: " + packet.source_tag);
    }
}

std::vector<std::string> NetworkAudioReceiver::get_seen_tags() {
    std::lock_guard<std::mutex> lock(seen_tags_mutex_);
    std::vector<std::string> tags;
    tags.swap(seen_tags_); // Return collected tags and clear for next poll
    return tags;
}
} // namespace audio
} // namespace screamrouter
