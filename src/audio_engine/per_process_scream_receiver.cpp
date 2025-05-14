#include "per_process_scream_receiver.h"
#include <iostream>      // For logging/debugging
#include <vector>
#include <cstring>       // For memcpy, memset, strncpy
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
const size_t PROGRAM_TAG_SIZE_CONST = 30;
const size_t SCREAM_HEADER_SIZE_CONST_PPSR = 5; // Renamed to avoid conflict
const size_t CHUNK_SIZE_CONST_PPSR = 1152;      // Renamed
const size_t EXPECTED_PER_PROCESS_PACKET_SIZE = PROGRAM_TAG_SIZE_CONST + SCREAM_HEADER_SIZE_CONST_PPSR + CHUNK_SIZE_CONST_PPSR; // 30 + 5 + 1152 = 1187
const size_t PER_PROCESS_RECEIVE_BUFFER_SIZE = 2048; // Should be larger than EXPECTED_PER_PROCESS_PACKET_SIZE
const int PER_PROCESS_POLL_TIMEOUT_MS = 100;   // Check for stop flag every 100ms

// Simple logger helper
//#define LOG_PPSR(msg) std::cout << "[PerProcessScreamReceiver] " << msg << std::endl
//#define LOG_ERROR_PPSR(msg) std::cerr << "[PerProcessScreamReceiver Error] " << msg << " (errno: " << GET_LAST_SOCK_ERROR_PPSR << ")" << std::endl
//#define LOG_WARN_PPSR(msg) std::cout << "[PerProcessScreamReceiver Warn] " << msg << std::endl
#define LOG_PPSR(msg)
#define LOG_ERROR_PPSR(msg)
#define LOG_WARN_PPSR(msg)

PerProcessScreamReceiver::PerProcessScreamReceiver(
    PerProcessScreamReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager) // Added timeshift_manager
    : config_(config),
      notification_queue_(notification_queue),
      timeshift_manager_(timeshift_manager), // Initialize timeshift_manager_
      socket_fd_(INVALID_SOCKET_VALUE_PPSR)
{
    if (!timeshift_manager_) {
        LOG_ERROR_PPSR("TimeshiftManager pointer is null. PerProcessScreamReceiver cannot function.");
        // Consider throwing an exception or setting an error state
    }
    #ifdef _WIN32
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            LOG_ERROR_PPSR("WSAStartup failed: " + std::to_string(iResult));
            throw std::runtime_error("WSAStartup failed.");
        }
    #endif
    if (!notification_queue_) {
        throw std::runtime_error("PerProcessScreamReceiver requires a valid notification queue.");
    }
    LOG_PPSR("Initialized with port " + std::to_string(config_.listen_port));
}

PerProcessScreamReceiver::~PerProcessScreamReceiver() noexcept {
    if (!stop_flag_) {
        LOG_WARN_PPSR("Destructor called while still running. Forcing stop.");
        stop();
    }
    if (component_thread_.joinable()) {
        LOG_WARN_PPSR("Warning: Joining thread in destructor, stop() might not have been called properly.");
        try {
            component_thread_.join();
        } catch (const std::system_error& e) {
            LOG_ERROR_PPSR("Error joining thread in destructor: " + std::string(e.what()));
        }
    }
    close_socket();
    LOG_PPSR("Destroyed.");

    #ifdef _WIN32
        // WSACleanup(); // Managed globally
    #endif
}

bool PerProcessScreamReceiver::setup_socket() {
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ == INVALID_SOCKET_VALUE_PPSR) {
        LOG_ERROR_PPSR("Failed to create socket");
        return false;
    }

    #ifdef _WIN32
        char reuse = 1;
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            LOG_ERROR_PPSR("Failed to set SO_REUSEADDR");
            close_socket();
            return false;
        }
    #else // POSIX
        int reuse = 1;
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            LOG_ERROR_PPSR("Failed to set SO_REUSEADDR");
            close_socket();
            return false;
        }
    #endif

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(config_.listen_port);

    if (bind(socket_fd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR_PPSR("Failed to bind socket to port " + std::to_string(config_.listen_port));
        close_socket();
        return false;
    }

    LOG_PPSR("Socket created and bound successfully to port " + std::to_string(config_.listen_port));
    return true;
}

void PerProcessScreamReceiver::close_socket() {
    if (socket_fd_ != INVALID_SOCKET_VALUE_PPSR) {
        LOG_PPSR("Closing socket");
        #ifdef _WIN32
            closesocket(socket_fd_);
        #else
            close(socket_fd_);
        #endif
        socket_fd_ = INVALID_SOCKET_VALUE_PPSR;
    }
}

void PerProcessScreamReceiver::start() {
    if (is_running()) {
        LOG_PPSR("Already running.");
        return;
    }
    LOG_PPSR("Starting...");
    stop_flag_ = false;

    if (!setup_socket()) {
        LOG_ERROR_PPSR("Failed to setup socket. Cannot start receiver thread.");
        return;
    }

    try {
        component_thread_ = std::thread(&PerProcessScreamReceiver::run, this);
        LOG_PPSR("Receiver thread started.");
    } catch (const std::system_error& e) {
        LOG_ERROR_PPSR("Failed to start thread: " + std::string(e.what()));
        close_socket();
        throw;
    }
}

void PerProcessScreamReceiver::stop() {
    if (stop_flag_) {
        LOG_PPSR("Already stopped or stopping.");
        return;
    }
    LOG_PPSR("Stopping...");
    stop_flag_ = true;

    close_socket(); // Interrupt blocking recvfrom/poll

    if (component_thread_.joinable()) {
        try {
            component_thread_.join();
            LOG_PPSR("Receiver thread joined.");
        } catch (const std::system_error& e) {
            LOG_ERROR_PPSR("Error joining thread: " + std::string(e.what()));
        }
    } else {
        LOG_PPSR("Thread was not joinable.");
    }
}

// add_output_queue and remove_output_queue are removed.

bool PerProcessScreamReceiver::is_valid_per_process_scream_packet(const uint8_t* buffer, int size) {
    (void)buffer; // Mark as unused if not checking content here
    return size == static_cast<int>(EXPECTED_PER_PROCESS_PACKET_SIZE);
}

void PerProcessScreamReceiver::run() {
    LOG_PPSR("Receiver thread entering run loop.");
    std::vector<uint8_t> receive_buffer(PER_PROCESS_RECEIVE_BUFFER_SIZE);
    struct sockaddr_in client_addr;
    #ifdef _WIN32
        int client_addr_len = sizeof(client_addr);
    #else
        socklen_t client_addr_len = sizeof(client_addr);
    #endif

    struct pollfd fds[1];
    fds[0].fd = socket_fd_;
    fds[0].events = POLLIN;

    while (!stop_flag_) {
        #ifdef _WIN32
            int poll_ret = WSAPoll(fds, 1, PER_PROCESS_POLL_TIMEOUT_MS);
        #else
            int poll_ret = poll(fds, 1, PER_PROCESS_POLL_TIMEOUT_MS);
        #endif

        if (poll_ret < 0) {
            #ifndef _WIN32
                if (errno == EINTR) continue;
            #endif
            if (!stop_flag_) LOG_ERROR_PPSR("poll() failed");
            if (!stop_flag_) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (poll_ret == 0) continue; // Timeout

        if (fds[0].revents & POLLIN) {
            #ifdef _WIN32
                int bytes_received = recvfrom(socket_fd_, reinterpret_cast<char*>(receive_buffer.data()), static_cast<int>(receive_buffer.size()), 0,
                                                  (struct sockaddr *)&client_addr, &client_addr_len);
            #else
                int bytes_received = recvfrom(socket_fd_, receive_buffer.data(), receive_buffer.size(), 0,
                                                  (struct sockaddr *)&client_addr, &client_addr_len);
            #endif

            if (bytes_received < 0) {
                if (!stop_flag_) LOG_ERROR_PPSR("recvfrom() failed");
                continue;
            }

            std::string sender_ip = inet_ntoa(client_addr.sin_addr);
            LOG_PPSR("Received " + std::to_string(bytes_received) + " bytes from " + sender_ip + ":" + std::to_string(ntohs(client_addr.sin_port)) + " on port " + std::to_string(config_.listen_port));

            if (is_valid_per_process_scream_packet(receive_buffer.data(), bytes_received)) {
                // Extract Program Tag
                char program_tag_cstr[PROGRAM_TAG_SIZE_CONST + 1];
                strncpy(program_tag_cstr, reinterpret_cast<const char*>(receive_buffer.data()), PROGRAM_TAG_SIZE_CONST);
                program_tag_cstr[PROGRAM_TAG_SIZE_CONST] = '\0'; // Ensure null termination
                std::string program_tag(program_tag_cstr);
                // Trim trailing spaces from program_tag if any, as per c_utils logic
                program_tag.erase(program_tag.find_last_not_of(" \n\r\t")+1);

                // Format sender_ip to be 15 chars, space-padded
                std::string fixed_sender_ip(15, ' '); // Max IPv4 length is 15 (xxx.xxx.xxx.xxx)
                if (sender_ip.length() <= 15) {
                    fixed_sender_ip.replace(0, sender_ip.length(), sender_ip);
                } else {
                    // This case should ideally not be hit with valid IPv4 addresses
                    fixed_sender_ip.replace(0, 15, sender_ip.substr(0, 15));
                }

                std::string composite_source_tag = fixed_sender_ip + program_tag;
                auto received_time = std::chrono::steady_clock::now();

                {
                    std::lock_guard<std::mutex> lock(known_tags_mutex_);
                    if (known_source_tags_.find(composite_source_tag) == known_source_tags_.end()) {
                        known_source_tags_.insert(composite_source_tag);
                        
                        // Add to seen_tags_ if not already present
                        { // New scope for seen_tags_mutex_
                            std::lock_guard<std::mutex> seen_lock(seen_tags_mutex_);
                            if (std::find(seen_tags_.begin(), seen_tags_.end(), composite_source_tag) == seen_tags_.end()) {
                                seen_tags_.push_back(composite_source_tag);
                            }
                        } // seen_tags_mutex_ released here
                        
                        // Unlock before pushing to queue
                        lock.~lock_guard();
                        LOG_PPSR("New source detected: " + composite_source_tag);
                        notification_queue_->push(NewSourceNotification{composite_source_tag});
                    }
                }

                TaggedAudioPacket packet;
                packet.source_tag = composite_source_tag; // Use composite tag
                packet.received_time = received_time;

                // --- Parse Header and Set Format ---
                // Header starts after the program tag
                const uint8_t* header = receive_buffer.data() + PROGRAM_TAG_SIZE_CONST;
                bool is_44100_base = (header[0] >> 7) & 0x01;
                uint8_t samplerate_divisor = header[0] & 0x7F;
                if (samplerate_divisor == 0) samplerate_divisor = 1;

                packet.sample_rate = (is_44100_base ? 44100 : 48000) / samplerate_divisor;
                packet.bit_depth = static_cast<int>(header[1]);
                packet.channels = static_cast<int>(header[2]);
                packet.chlayout1 = header[3];
                packet.chlayout2 = header[4];

                LOG_PPSR("Parsed Header for " + composite_source_tag + ": SR=" + std::to_string(packet.sample_rate) +
                         ", BD=" + std::to_string(packet.bit_depth) + ", CH=" + std::to_string(packet.channels) +
                         ", Layout=" + std::to_string(packet.chlayout1) + "/" + std::to_string(packet.chlayout2));

                // --- Assign Payload Only ---
                // Audio data starts after program tag and header
                const uint8_t* audio_payload_start = receive_buffer.data() + PROGRAM_TAG_SIZE_CONST + SCREAM_HEADER_SIZE_CONST_PPSR;
                packet.audio_data.assign(audio_payload_start, audio_payload_start + CHUNK_SIZE_CONST_PPSR);

                // Basic validation after parsing
                if (packet.channels <= 0 || packet.channels > 8 ||
                    (packet.bit_depth != 8 && packet.bit_depth != 16 && packet.bit_depth != 24 && packet.bit_depth != 32) ||
                    packet.sample_rate <= 0 ||
                    packet.audio_data.size() != CHUNK_SIZE_CONST_PPSR) {
                    LOG_ERROR_PPSR("Parsed invalid format or payload size from packet. SR=" + std::to_string(packet.sample_rate) +
                                  ", BD=" + std::to_string(packet.bit_depth) + ", CH=" + std::to_string(packet.channels) +
                                  ", PayloadSize=" + std::to_string(packet.audio_data.size()));
                    continue;
                }

                if (timeshift_manager_) {
                    timeshift_manager_->add_packet(std::move(packet));
                } else {
                    LOG_ERROR_PPSR("TimeshiftManager is null. Cannot add packet.");
                }
            } else {
                 std::string sender_ip_for_log = inet_ntoa(client_addr.sin_addr); // Already available as sender_ip
                 LOG_WARN_PPSR("Received invalid or unexpected size packet (" + std::to_string(bytes_received) + " bytes) from " + sender_ip + ". Expected " + std::to_string(EXPECTED_PER_PROCESS_PACKET_SIZE) + " bytes.");
            }
        } else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
             LOG_ERROR_PPSR("Socket error detected by poll()");
             break;
        }
    }
    LOG_PPSR("Receiver thread exiting run loop.");
}

std::vector<std::string> PerProcessScreamReceiver::get_seen_tags() {
    std::lock_guard<std::mutex> lock(seen_tags_mutex_);
    return seen_tags_; // Return a copy
}
