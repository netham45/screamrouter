#include "network_audio_receiver.h"
#include "../input_processor/timeshift_manager.h" // Ensure full definition is available
#include "../utils/thread_safe_queue.h" // For full definition of ThreadSafeQueue
#include "../utils/cpp_logger.h"       // For new C++ logger
#include <iostream>      // For logging (cpp_logger fallb_ack)
#include <vector>
#include <cstring>       // For memset
#include <stdexcept>     // For runtime_error
#include <system_error>  // For socket error checking
#include <algorithm>     // For std::find
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
    ClockManager* clock_manager,
    std::size_t chunk_size_bytes)
    : listen_port_(listen_port),
      socket_fd_(NAR_INVALID_SOCKET_VALUE),
      notification_queue_(notification_queue),
      timeshift_manager_(timeshift_manager),
      clock_manager_(clock_manager),
      chunk_size_bytes_(chunk_size_bytes),
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
    if (clock_manager_ && chunk_size_bytes_ == 0) {
        log_warning("ClockManager provided but chunk size is zero; clock scheduling will be disabled.");
        clock_manager_ = nullptr;
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
    clear_clock_managed_streams();
    reset_all_pcm_accumulators();
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
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), static_cast<int>(sizeof(reuse))) < 0) {
        log_error("Failed to set SO_REUSEADDR");
        close_socket();
        return false;
    }
    int buffer_size = 1152 * 10;
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&buffer_size), static_cast<int>(sizeof(buffer_size)));
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

    clear_clock_managed_streams();
    reset_all_pcm_accumulators();
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
        maybe_log_telemetry();
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
    if (clock_manager_ && chunk_size_bytes_ > 0) {
        if (enqueue_clock_managed_packet(std::move(packet))) {
            return;
        }
    }

    if (timeshift_manager_) {
        timeshift_manager_->add_packet(std::move(packet));
    } else {
        log_error("TimeshiftManager is null. Cannot add packet for source: " + packet.source_tag);
    }
}

void NetworkAudioReceiver::on_before_poll_wait() {
    service_clock_manager();
}

void NetworkAudioReceiver::on_after_poll_iteration() {
    service_clock_manager();
}

std::vector<std::string> NetworkAudioReceiver::get_seen_tags() {
    std::lock_guard<std::mutex> lock(seen_tags_mutex_);
    std::vector<std::string> tags;
    tags.swap(seen_tags_); // Return collected tags and clear for next poll
    return tags;
}

bool NetworkAudioReceiver::enqueue_clock_managed_packet(TaggedAudioPacket&& packet) {
    if (!clock_manager_ || chunk_size_bytes_ == 0) {
        return false;
    }

    std::shared_ptr<ClockStreamState> state;
    {
        std::lock_guard<std::mutex> lock(stream_state_mutex_);
        state = get_or_create_stream_state_locked(packet);
        if (!state) {
            return false;
        }

        constexpr std::size_t kDefaultMaxPendingPackets = 64;
        std::size_t max_pending_packets = kDefaultMaxPendingPackets;
        if (timeshift_manager_) {
            auto settings = timeshift_manager_->get_settings();
            if (settings) {
                const auto configured = settings->timeshift_tuning.max_clock_pending_packets;
                if (configured > 0) {
                    max_pending_packets = configured;
                }
            }
        }
        if (max_pending_packets > 0 && state->pending_packets.size() >= max_pending_packets) {
            state->pending_packets.pop_front();
            log_warning("Pending packet queue capped for source " + packet.source_tag +
                        " (limit=" + std::to_string(max_pending_packets) + ")");
        }

        state->pending_packets.push_back(std::move(packet));
        if (!state->pending_packets.back().ssrcs.empty()) {
            state->last_ssrcs = state->pending_packets.back().ssrcs;
        }
        state->current_playback_rate = state->pending_packets.back().playback_rate;
    }

    return true;
}

void NetworkAudioReceiver::service_clock_manager() {
    if (!clock_manager_ || chunk_size_bytes_ == 0) {
        return;
    }

    std::vector<std::pair<std::string, uint64_t>> pending_ticks;

    {
        std::lock_guard<std::mutex> lock(stream_state_mutex_);
        for (auto& [tag, state_ptr] : stream_states_) {
            if (!state_ptr) {
                continue;
            }

            auto& state = *state_ptr;
            if (!state.clock_handle.valid()) {
                continue;
            }

            auto condition = state.clock_handle.condition;
            if (!condition) {
                continue;
            }

            uint64_t sequence_snapshot = 0;
            {
                std::unique_lock<std::mutex> condition_lock(condition->mutex);
                sequence_snapshot = condition->sequence;
            }

            if (sequence_snapshot > state.clock_last_sequence) {
                uint64_t tick_count = sequence_snapshot - state.clock_last_sequence;
                state.clock_last_sequence = sequence_snapshot;
                pending_ticks.emplace_back(tag, tick_count);
            }
        }
    }

    for (const auto& [tag, tick_count] : pending_ticks) {
        for (uint64_t i = 0; i < tick_count; ++i) {
            if (stop_flag_) {
                return;
            }
            handle_clock_tick(tag);
        }
    }
}

std::vector<TaggedAudioPacket> NetworkAudioReceiver::append_pcm_payload(PcmAppendContext&& context) {
    std::vector<TaggedAudioPacket> completed_chunks;

    if (chunk_size_bytes_ == 0 || context.payload.empty()) {
        return completed_chunks;
    }

    auto samples_per_chunk = calculate_samples_per_chunk(context.channels, context.bit_depth);
    if (samples_per_chunk == 0) {
        log_warning("Unable to determine samples per chunk for accumulator key " + context.accumulator_key);
        return completed_chunks;
    }

    std::lock_guard<std::mutex> lock(pcm_accumulator_mutex_);
    auto& accumulator = pcm_accumulators_[context.accumulator_key];

    const bool format_initialized = accumulator.last_sample_rate != 0;
    const bool format_changed = format_initialized &&
        (accumulator.last_sample_rate != context.sample_rate ||
         accumulator.last_channels != context.channels ||
         accumulator.last_bit_depth != context.bit_depth ||
         accumulator.last_chlayout1 != context.chlayout1 ||
         accumulator.last_chlayout2 != context.chlayout2);

    if (format_changed) {
        accumulator.buffer.clear();
        accumulator.chunk_active = false;
        accumulator.first_packet_rtp_timestamp.reset();
    }

    accumulator.last_sample_rate = context.sample_rate;
    accumulator.last_channels = context.channels;
    accumulator.last_bit_depth = context.bit_depth;
    accumulator.last_chlayout1 = context.chlayout1;
    accumulator.last_chlayout2 = context.chlayout2;

    if (!accumulator.chunk_active) {
        accumulator.chunk_active = true;
        accumulator.first_packet_time = context.received_time;
        accumulator.first_packet_rtp_timestamp = context.rtp_timestamp;
    }

    accumulator.buffer.insert(accumulator.buffer.end(), context.payload.begin(), context.payload.end());

    while (accumulator.buffer.size() >= chunk_size_bytes_) {
        if (!accumulator.chunk_active) {
            accumulator.chunk_active = true;
            accumulator.first_packet_time = context.received_time;
            accumulator.first_packet_rtp_timestamp = context.rtp_timestamp;
        }

        TaggedAudioPacket packet;
        packet.source_tag = context.source_tag;
        packet.received_time = accumulator.first_packet_time;
        if (accumulator.first_packet_rtp_timestamp.has_value()) {
            packet.rtp_timestamp = accumulator.first_packet_rtp_timestamp;
            accumulator.first_packet_rtp_timestamp = accumulator.first_packet_rtp_timestamp.value() + samples_per_chunk;
        }

        packet.sample_rate = context.sample_rate;
        packet.channels = context.channels;
        packet.bit_depth = context.bit_depth;
        packet.chlayout1 = context.chlayout1;
        packet.chlayout2 = context.chlayout2;
        packet.ssrcs = context.ssrcs;

        packet.audio_data.assign(accumulator.buffer.begin(),
                                 accumulator.buffer.begin() + static_cast<std::ptrdiff_t>(chunk_size_bytes_));
        accumulator.buffer.erase(accumulator.buffer.begin(),
                                 accumulator.buffer.begin() + static_cast<std::ptrdiff_t>(chunk_size_bytes_));

        completed_chunks.push_back(std::move(packet));

        accumulator.chunk_active = false;
        accumulator.first_packet_rtp_timestamp.reset();
        accumulator.first_packet_time = {};
    }

    if (accumulator.buffer.empty()) {
        accumulator.chunk_active = false;
        accumulator.first_packet_rtp_timestamp.reset();
        accumulator.first_packet_time = {};
    }

    return completed_chunks;
}

void NetworkAudioReceiver::reset_pcm_accumulator(const std::string& accumulator_key) {
    std::lock_guard<std::mutex> lock(pcm_accumulator_mutex_);
    pcm_accumulators_.erase(accumulator_key);
}

void NetworkAudioReceiver::reset_all_pcm_accumulators() {
    std::lock_guard<std::mutex> lock(pcm_accumulator_mutex_);
    pcm_accumulators_.clear();
}

std::shared_ptr<NetworkAudioReceiver::ClockStreamState> NetworkAudioReceiver::get_or_create_stream_state_locked(const TaggedAudioPacket& packet) {
    if (!clock_manager_) {
        return nullptr;
    }

    auto it = stream_states_.find(packet.source_tag);
    bool created = false;

    if (it == stream_states_.end()) {
        auto state = std::make_shared<ClockStreamState>();
        state->source_tag = packet.source_tag;
        it = stream_states_.emplace(packet.source_tag, std::move(state)).first;
        created = true;
    }

    auto state = it->second;
    if (!state) {
        state = std::make_shared<ClockStreamState>();
        state->source_tag = packet.source_tag;
        it->second = state;
        created = true;
    }

    const bool format_changed = created ||
        state->sample_rate != packet.sample_rate ||
        state->channels != packet.channels ||
        state->bit_depth != packet.bit_depth ||
        state->chlayout1 != packet.chlayout1 ||
        state->chlayout2 != packet.chlayout2;

    if (format_changed) {
        if (state->clock_handle.valid()) {
            try {
                clock_manager_->unregister_clock_condition(state->clock_handle);
            } catch (const std::exception& ex) {
                log_error("Failed to unregister clock for " + state->source_tag + ": " + ex.what());
            }
            state->clock_handle = {};
            state->clock_last_sequence = 0;
        }

        state->sample_rate = packet.sample_rate;
        state->channels = packet.channels;
        state->bit_depth = packet.bit_depth;
        state->chlayout1 = packet.chlayout1;
        state->chlayout2 = packet.chlayout2;
        state->samples_per_chunk = calculate_samples_per_chunk(packet.channels, packet.bit_depth);
        state->next_rtp_timestamp = 0;
        state->pending_packets.clear();

        if (state->samples_per_chunk == 0) {
            log_error("Unsupported audio format for clock scheduling from " + packet.source_tag);
            stream_states_.erase(it);
            return nullptr;
        }

        try {
            state->clock_handle = clock_manager_->register_clock_condition(
                state->sample_rate, state->channels, state->bit_depth);
            if (!state->clock_handle.valid()) {
                throw std::runtime_error("ClockManager returned invalid condition handle");
            }
            if (auto condition = state->clock_handle.condition) {
                std::lock_guard<std::mutex> condition_lock(condition->mutex);
                state->clock_last_sequence = condition->sequence;
            } else {
                state->clock_last_sequence = 0;
            }
        } catch (const std::exception& ex) {
            log_error("Failed to register clock for " + state->source_tag + ": " + ex.what());
            stream_states_.erase(it);
            return nullptr;
        }
    }

    return state;
}

void NetworkAudioReceiver::clear_clock_managed_streams() {
    std::lock_guard<std::mutex> lock(stream_state_mutex_);

    if (clock_manager_) {
        for (auto& [tag, state] : stream_states_) {
            if (state && state->clock_handle.valid()) {
                try {
                    clock_manager_->unregister_clock_condition(state->clock_handle);
                } catch (const std::exception& ex) {
                    log_error("Failed to unregister clock for " + tag + ": " + ex.what());
                }
                state->clock_handle = {};
                state->clock_last_sequence = 0;
            }
        }
    }

    stream_states_.clear();
}

void NetworkAudioReceiver::handle_clock_tick(const std::string& source_tag) {
    TaggedAudioPacket packet;
    bool have_packet = false;

    {
        std::lock_guard<std::mutex> lock(stream_state_mutex_);
        auto it = stream_states_.find(source_tag);
        if (it == stream_states_.end() || !it->second) {
            return;
        }

        auto& state = *it->second;
        const auto now = std::chrono::steady_clock::now();

        if (!state.pending_packets.empty()) {
            packet = std::move(state.pending_packets.front());
            state.pending_packets.pop_front();
            packet.received_time = now;
            if (!packet.ssrcs.empty()) {
                state.last_ssrcs = packet.ssrcs;
            }

            if (state.samples_per_chunk > 0) {
                if (packet.rtp_timestamp.has_value()) {
                    state.next_rtp_timestamp = packet.rtp_timestamp.value();
                } else {
                    state.next_rtp_timestamp += state.samples_per_chunk;
                    packet.rtp_timestamp = state.next_rtp_timestamp;
                }
            } else {
                packet.rtp_timestamp.reset();
            }

            if (packet.playback_rate <= 0.0) {
                packet.playback_rate = state.current_playback_rate;
            }

            have_packet = true;
        } else {
            if (chunk_size_bytes_ == 0 || state.sample_rate == 0 || state.channels == 0) {
                return;
            }

            packet.source_tag = state.source_tag;
            packet.audio_data.assign(chunk_size_bytes_, 0);
            packet.received_time = now;
            packet.sample_rate = state.sample_rate;
            packet.channels = state.channels;
            packet.bit_depth = state.bit_depth;
            packet.chlayout1 = state.chlayout1;
            packet.chlayout2 = state.chlayout2;
            packet.ssrcs = state.last_ssrcs;

            if (state.samples_per_chunk > 0) {
                state.next_rtp_timestamp += state.samples_per_chunk;
                packet.rtp_timestamp = state.next_rtp_timestamp;
            } else {
                packet.rtp_timestamp.reset();
            }

            packet.playback_rate = state.current_playback_rate;

            have_packet = true;
        }
    }

    if (!have_packet) {
        return;
    }

    if (timeshift_manager_) {
        timeshift_manager_->add_packet(std::move(packet));
    } else {
        log_error("TimeshiftManager is null. Cannot add packet for source: " + packet.source_tag);
    }
}

uint32_t NetworkAudioReceiver::calculate_samples_per_chunk(int channels, int bit_depth) const {
    if (chunk_size_bytes_ == 0 || channels <= 0 || bit_depth <= 0 || (bit_depth % 8) != 0) {
        return 0;
    }

    const std::size_t bytes_per_frame = static_cast<std::size_t>(channels) * static_cast<std::size_t>(bit_depth / 8);
    if (bytes_per_frame == 0 || (chunk_size_bytes_ % bytes_per_frame) != 0) {
        return 0;
    }

    return static_cast<uint32_t>(chunk_size_bytes_ / bytes_per_frame);
}

void NetworkAudioReceiver::maybe_log_telemetry() {
    static constexpr auto kTelemetryInterval = std::chrono::seconds(30);

    const auto now = std::chrono::steady_clock::now();
    if (telemetry_last_log_time_.time_since_epoch().count() != 0 &&
        now - telemetry_last_log_time_ < kTelemetryInterval) {
        return;
    }

    telemetry_last_log_time_ = now;

    size_t stream_count = 0;
    size_t total_pending = 0;
    size_t max_pending = 0;
    double total_pending_ms = 0.0;
    double max_pending_ms = 0.0;
    {
        std::lock_guard<std::mutex> lock(stream_state_mutex_);
        for (const auto& [tag, state_ptr] : stream_states_) {
            (void)tag;
            if (!state_ptr) {
                continue;
            }
            stream_count++;
            const size_t pending = state_ptr->pending_packets.size();
            total_pending += pending;
            if (pending > max_pending) {
                max_pending = pending;
            }
            double chunk_ms = 0.0;
            if (state_ptr->sample_rate > 0 && state_ptr->samples_per_chunk > 0) {
                chunk_ms = (static_cast<double>(state_ptr->samples_per_chunk) * 1000.0) /
                           static_cast<double>(state_ptr->sample_rate);
            }
            double pending_ms = chunk_ms > 0.0 ? chunk_ms * static_cast<double>(pending) : 0.0;
            total_pending_ms += pending_ms;
            if (pending_ms > max_pending_ms) {
                max_pending_ms = pending_ms;
            }
            LOG_CPP_INFO(
                "%s [Telemetry][Stream %s] pending_chunks=%zu backlog_ms=%.3f playback_rate=%.3f",
                logger_prefix_.c_str(),
                tag.c_str(),
                pending,
                pending_ms,
                state_ptr->current_playback_rate);
        }
    }

    size_t accumulator_count = 0;
    size_t total_pcm_bytes = 0;
    size_t max_pcm_bytes = 0;
    double total_pcm_ms = 0.0;
    double max_pcm_ms = 0.0;
    {
        std::lock_guard<std::mutex> lock(pcm_accumulator_mutex_);
        accumulator_count = pcm_accumulators_.size();
        for (const auto& [key, accumulator] : pcm_accumulators_) {
            (void)key;
            const size_t bytes = accumulator.buffer.size();
            total_pcm_bytes += bytes;
            if (bytes > max_pcm_bytes) {
                max_pcm_bytes = bytes;
            }
            double pcm_ms = 0.0;
            if (accumulator.last_sample_rate > 0 && accumulator.last_channels > 0 && accumulator.last_bit_depth > 0 &&
                (accumulator.last_bit_depth % 8) == 0) {
                const std::size_t frame_bytes = static_cast<std::size_t>(accumulator.last_channels) *
                                                static_cast<std::size_t>(accumulator.last_bit_depth / 8);
                if (frame_bytes > 0) {
                    const double frames = static_cast<double>(bytes) / static_cast<double>(frame_bytes);
                    pcm_ms = (frames * 1000.0) / static_cast<double>(accumulator.last_sample_rate);
                }
            }
            total_pcm_ms += pcm_ms;
            if (pcm_ms > max_pcm_ms) {
                max_pcm_ms = pcm_ms;
            }
            LOG_CPP_INFO(
                "%s [Telemetry][Accumulator] key=%s bytes=%zu backlog_ms=%.3f",
                logger_prefix_.c_str(),
                key.c_str(),
                bytes,
                pcm_ms);
        }
    }

    LOG_CPP_INFO(
        "%s [Telemetry] streams=%zu pending_total=%zu (%.3f ms) pending_max=%zu (%.3f ms) accumulators=%zu pcm_total_bytes=%zu (%.3f ms) pcm_max_bytes=%zu (%.3f ms)",
        logger_prefix_.c_str(),
        stream_count,
        total_pending,
        total_pending_ms,
        max_pending,
        max_pending_ms,
        accumulator_count,
        total_pcm_bytes,
        total_pcm_ms,
        max_pcm_bytes,
        max_pcm_ms);
}
} // namespace audio
} // namespace screamrouter
