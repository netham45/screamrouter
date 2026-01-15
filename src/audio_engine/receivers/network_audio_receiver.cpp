#include "network_audio_receiver.h"
#include "../input_processor/timeshift_manager.h" // Ensure full definition is available
#include "../utils/thread_safe_queue.h" // For full definition of ThreadSafeQueue
#include "../utils/cpp_logger.h"
#include "../utils/thread_priority.h"
#include "../utils/sentinel_logging.h"
#include <iostream>      // For logging (cpp_logger fallb_ack)
#include <vector>
#include <cstring>       // For memset
#include <stdexcept>     // For runtime_error
#include <system_error>  // For socket error checking
#include <algorithm>     // For std::find
#include <limits>
#include <cstddef>
#include <cmath>

namespace screamrouter {
namespace audio {

#ifdef _WIN32
    std::atomic<int> NetworkAudioReceiver::winsock_user_count_(0);


#endif

NetworkAudioReceiver::NetworkAudioReceiver(
    uint16_t listen_port,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager,
    std::string logger_prefix,
    std::size_t base_frames_per_chunk_mono16)
    : listen_port_(listen_port),
      socket_fd_(NAR_INVALID_SOCKET_VALUE),
      notification_queue_(notification_queue),
      timeshift_manager_(timeshift_manager),
      base_frames_per_chunk_mono16_(sanitize_base_frames_per_chunk(base_frames_per_chunk_mono16)),
      default_chunk_size_bytes_(compute_chunk_size_bytes_for_format(base_frames_per_chunk_mono16_, 2, 16)),
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

    const auto desired_buffer_bytes = default_chunk_size_bytes_ * 100;
    const int buffer_size = desired_buffer_bytes > static_cast<std::size_t>(std::numeric_limits<int>::max())
                                ? std::numeric_limits<int>::max()
                                : static_cast<int>(desired_buffer_bytes);
#ifdef _WIN32
    char reuse = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), static_cast<int>(sizeof(reuse))) < 0) {
        log_error("Failed to set SO_REUSEADDR");
        close_socket();
        return false;
    }
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&buffer_size), static_cast<int>(sizeof(buffer_size)));
#else // POSIX
    int reuse = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        log_error("Failed to set SO_REUSEADDR");
        close_socket();
        return false;
    }
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
    const std::string thread_name = "[NetworkAudioReceiver:" + logger_prefix_ + "]";
    utils::set_current_thread_realtime_priority(thread_name.c_str());
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

        on_before_poll_wait();

        int poll_ret = NAR_POLL(fds, 1, poll_timeout);

        if (stop_flag_) {
            on_after_poll_iteration();
            break; // Check immediately after poll returns
        }

        if (poll_ret < 0) {
#ifndef _WIN32 // EINTR is POSIX specific
            if (NAR_GET_LAST_SOCK_ERROR == EINTR) {
                on_after_poll_iteration();
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
            on_after_poll_iteration();
            continue;
        }

        if (poll_ret == 0) {
            // Timeout - no data received, loop again to check stop_flag_
            on_after_poll_iteration();
            continue;
        }

        if (fds[0].revents & POLLIN) {
            // Check socket validity again before recvfrom, as stop() might have closed it
            if (socket_fd_ == NAR_INVALID_SOCKET_VALUE) {
                log_warning("Socket became invalid before recvfrom, exiting run loop.");
                on_after_poll_iteration();
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
                on_after_poll_iteration();
                continue;
            }

            if (is_valid_packet_structure(receive_buffer.data(), bytes_received, client_addr)) {
                TaggedAudioPacket packet;
                std::string source_tag;
                auto received_time = std::chrono::steady_clock::now();

                bool valid_payload = process_and_validate_payload(receive_buffer.data(),
                                                                   bytes_received,
                                                                   client_addr,
                                                                   received_time,
                                                                   packet,
                                                                   source_tag);

                if (!source_tag.empty() && register_source_tag(source_tag)) {
                    log_message("New source detected: " + source_tag);
                }

                if (valid_payload) {
                    dispatch_ready_packet(std::move(packet));
                } else {
                    // process_and_validate_payload should log specific reasons for failure
                    // log_warning("Failed to process/validate payload from " + std::string(inet_ntoa(client_addr.sin_addr)));
                }
            } else {
                // is_valid_packet_structure might log, or we can log generically here
                // log_warning("Received invalid packet structure from " + std::string(inet_ntoa(client_addr.sin_addr)));
            }
            on_after_poll_iteration();
        } else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
             // Socket error occurred
             if (!stop_flag_ && socket_fd_ != NAR_INVALID_SOCKET_VALUE) {
                log_error("Socket error detected by poll()");
             }
             on_after_poll_iteration();
             break; // Exit loop on socket error
        } else {
            on_after_poll_iteration();
        }
    } // End while loop

    log_message("Receiver thread exiting run loop.");
}

void NetworkAudioReceiver::dispatch_ready_packet(TaggedAudioPacket&& packet) {
    if (!timeshift_manager_) {
        log_error("TimeshiftManager is null. Cannot add packet for source: " + packet.source_tag);
        return;
    }

    if (packet.audio_data.empty()) {
        return;
    }
    utils::log_sentinel("receiver_dispatch_entry", packet);
    if (packet.ingress_from_loopback && packet.rtp_sequence_number.has_value()) {
        LOG_CPP_INFO("[NetworkAudioReceiver] Loopback seq=%u entering accumulator (source=%s)",
                     packet.rtp_sequence_number.value(),
                     packet.source_tag.c_str());
    }

    const uint32_t bytes_per_sample = packet.bit_depth > 0 ? static_cast<uint32_t>(std::max(packet.bit_depth / 8, 1)) : 0;
    const std::size_t bytes_per_frame = (bytes_per_sample > 0 && packet.channels > 0)
        ? static_cast<std::size_t>(packet.channels) * bytes_per_sample
        : 0;

    std::size_t chunk_bytes = default_chunk_size_bytes_;
    if (bytes_per_frame > 0) {
        const std::size_t remainder = chunk_bytes % bytes_per_frame;
        if (remainder != 0) {
            chunk_bytes += (bytes_per_frame - remainder);
        }
    }
    if (chunk_bytes == 0) {
        chunk_bytes = default_chunk_size_bytes_;
    }

    std::vector<TaggedAudioPacket> ready_chunks;
    {
        std::lock_guard<std::mutex> lock(accumulator_mutex_);
        auto& acc = accumulators_[packet.source_tag];
        const bool format_changed = acc.channels != packet.channels ||
                                    acc.sample_rate != packet.sample_rate ||
                                    acc.bit_depth != packet.bit_depth ||
                                    acc.chlayout1 != packet.chlayout1 ||
                                    acc.chlayout2 != packet.chlayout2;
        if (format_changed || acc.chunk_bytes != chunk_bytes) {
            acc.buffer.clear();
            acc.base_rtp_timestamp.reset();
            acc.frame_cursor = 0;
            acc.ssrcs.clear();
            acc.contributions.clear();
        }

        acc.channels = packet.channels;
        acc.sample_rate = packet.sample_rate;
        acc.bit_depth = packet.bit_depth;
        acc.chlayout1 = packet.chlayout1;
        acc.chlayout2 = packet.chlayout2;
        acc.chunk_bytes = chunk_bytes;
        acc.bytes_per_frame = bytes_per_frame;

        if (acc.buffer.capacity() < acc.chunk_bytes * 2 && acc.chunk_bytes > 0) {
            acc.buffer.reserve(acc.chunk_bytes * 2);
        }

        if (!packet.ssrcs.empty()) {
            acc.ssrcs = packet.ssrcs;
        }
        if (!acc.base_rtp_timestamp && packet.rtp_timestamp.has_value()) {
            acc.base_rtp_timestamp = packet.rtp_timestamp;
            acc.frame_cursor = 0;
        }

        acc.buffer.write(packet.audio_data.data(), packet.audio_data.size());
        if (!packet.audio_data.empty()) {
            SourceAccumulator::ContributionInfo info;
            info.bytes = packet.audio_data.size();
            info.arrival = packet.received_time;
            acc.contributions.push_back(info);
        }

        while (acc.chunk_bytes > 0 && acc.buffer.size() >= acc.chunk_bytes) {
            std::vector<uint8_t> chunk(acc.chunk_bytes);
            const std::size_t popped = acc.buffer.pop(chunk.data(), acc.chunk_bytes);
            if (popped == 0) {
                break;
            }
            chunk.resize(popped);

            TaggedAudioPacket out;
            out.source_tag = packet.source_tag;
            out.audio_data = std::move(chunk);
            out.channels = acc.channels;
            out.sample_rate = acc.sample_rate;
            out.bit_depth = acc.bit_depth;
            out.chlayout1 = acc.chlayout1;
            out.chlayout2 = acc.chlayout2;
            out.ssrcs = acc.ssrcs.empty() ? packet.ssrcs : acc.ssrcs;
            out.ingress_from_loopback = packet.ingress_from_loopback;
            out.rtp_sequence_number = packet.rtp_sequence_number;
            out.is_sentinel = packet.is_sentinel;

            const std::size_t chunk_frames = (acc.bytes_per_frame > 0) ? (popped / acc.bytes_per_frame) : 0;

            auto derive_chunk_arrival = [&](std::size_t bytes_consumed,
                                            std::chrono::steady_clock::time_point fallback) {
                auto arrival = fallback;
                std::size_t remaining = bytes_consumed;
                while (remaining > 0 && !acc.contributions.empty()) {
                    auto& contrib = acc.contributions.front();
                    if (contrib.bytes <= remaining) {
                        arrival = contrib.arrival;
                        remaining -= contrib.bytes;
                        acc.contributions.pop_front();
                    } else {
                        arrival = contrib.arrival;
                        contrib.bytes -= remaining;
                        remaining = 0;
                    }
                }
                return arrival;
            };

            out.received_time = derive_chunk_arrival(popped, packet.received_time);

            if (acc.base_rtp_timestamp.has_value()) {
                const uint64_t ts64 = static_cast<uint64_t>(*acc.base_rtp_timestamp) + acc.frame_cursor;
                out.rtp_timestamp = static_cast<uint32_t>(ts64 & 0xFFFFFFFFu);
            } else if (packet.rtp_timestamp.has_value()) {
                out.rtp_timestamp = packet.rtp_timestamp;
            }

            if (chunk_frames > 0) {
                acc.frame_cursor += chunk_frames;
            }

            ready_chunks.push_back(std::move(out));
            utils::log_sentinel("receiver_chunk_ready", ready_chunks.back());
        }

        if (acc.buffer.size() == 0) {
            acc.contributions.clear();
        }
    }

    for (auto& ready_packet : ready_chunks) {
        if (ready_packet.ingress_from_loopback && ready_packet.rtp_sequence_number.has_value()) {
            LOG_CPP_INFO("[NetworkAudioReceiver] Loopback seq=%u enqueued to TimeshiftManager (chunk_bytes=%zu, source=%s)",
                         ready_packet.rtp_sequence_number.value(),
                         ready_packet.audio_data.size(),
                         ready_packet.source_tag.c_str());
        }
        timeshift_manager_->add_packet(std::move(ready_packet));
    }
}

void NetworkAudioReceiver::on_before_poll_wait() {
}

void NetworkAudioReceiver::on_after_poll_iteration() {
}

std::vector<std::string> NetworkAudioReceiver::get_seen_tags() {
    std::lock_guard<std::mutex> lock(seen_tags_mutex_);
    std::vector<std::string> tags;
    tags.swap(seen_tags_); // Return collected tags and clear for next poll
    return tags;
}

bool NetworkAudioReceiver::register_source_tag(const std::string& tag) {
    if (tag.empty()) {
        return false;
    }

    bool is_new_source = false;
    {
        std::lock_guard<std::mutex> lock(known_tags_mutex_);
        auto insert_result = known_source_tags_.insert(tag);
        is_new_source = insert_result.second;
    }

    {
        std::lock_guard<std::mutex> seen_lock(seen_tags_mutex_);
        if (std::find(seen_tags_.begin(), seen_tags_.end(), tag) == seen_tags_.end()) {
            seen_tags_.push_back(tag);
        }
    }

    if (is_new_source && notification_queue_) {
        notification_queue_->push(DeviceDiscoveryNotification{tag, DeviceDirection::CAPTURE, true});
    }

    return is_new_source;
}

} // namespace audio
} // namespace screamrouter
