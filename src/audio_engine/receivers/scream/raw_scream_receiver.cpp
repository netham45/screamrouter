#include "raw_scream_receiver.h"
#include "../../input_processor/timeshift_manager.h" // For TimeshiftManager operations
#include <iostream>             // For std::cout, std::cerr (used by base logger)
#include <vector>
#include <cstring>              // For memcpy, memset
#include <stdexcept>            // For runtime_error
#include <utility>              // For std::move
#include <algorithm>            // For std::find

// Platform-specific includes for inet_ntoa, struct sockaddr_in etc. are in network_audio_receiver.h

namespace screamrouter {
namespace audio {

// Define constants
const size_t RAW_SCREAM_HEADER_SIZE = 5;
const size_t RAW_CHUNK_SIZE = 1152;
const size_t EXPECTED_RAW_PACKET_SIZE = RAW_SCREAM_HEADER_SIZE + RAW_CHUNK_SIZE; // 5 + 1152 = 1157
const size_t RAW_RECEIVE_BUFFER_SIZE_CONFIG = 2048; // Should be larger than EXPECTED_RAW_PACKET_SIZE
const int RAW_POLL_TIMEOUT_MS_CONFIG = 5;   // Check for stop flag every 5ms to service clock ticks promptly

RawScreamReceiver::RawScreamReceiver(
    RawScreamReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager,
    ClockManager* clock_manager,
    std::string logger_prefix)
    : NetworkAudioReceiver(config.listen_port, notification_queue, timeshift_manager, logger_prefix),
      config_(config),
      clock_manager_(clock_manager) {
    if (!clock_manager_) {
        throw std::runtime_error("RawScreamReceiver requires a valid ClockManager instance");
    }
    // Base class constructor handles WSAStartup, null checks for queue/manager, and initial logging.
}

RawScreamReceiver::~RawScreamReceiver() noexcept {
    clear_all_streams();
    // Base class destructor handles stopping, joining thread, closing socket, and WSACleanup.
}

// --- NetworkAudioReceiver Pure Virtual Method Implementations ---

bool RawScreamReceiver::is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) {
    (void)buffer; // Buffer content not checked here, only size
    (void)client_addr; // Client address not used for this basic check
    
    if (static_cast<size_t>(size) != EXPECTED_RAW_PACKET_SIZE) {
        // Logging this here might be too verbose if many non-matching packets arrive.
        // The base class's run loop can log a generic "invalid packet structure" if this returns false.
        // Or, process_and_validate_payload can log if it also finds this to be an issue.
        // For now, keep it silent here, relying on higher-level logging.
        return false;
    }
    return true;
}

bool RawScreamReceiver::validate_raw_scream_content(const uint8_t* buffer, int size, TaggedAudioPacket& out_packet) {
    // Assumes buffer is not null and size is EXPECTED_RAW_PACKET_SIZE based on is_valid_packet_structure
    if (static_cast<size_t>(size) != EXPECTED_RAW_PACKET_SIZE) { // Defensive check
        log_warning("validate_raw_scream_content called with unexpected size: " + std::to_string(size));
        return false;
    }

    const uint8_t* header = buffer;
    bool is_44100_base = (header[0] >> 7) & 0x01;
    uint8_t samplerate_divisor = header[0] & 0x7F;
    if (samplerate_divisor == 0) samplerate_divisor = 1; // As per original logic

    out_packet.sample_rate = (is_44100_base ? 44100 : 48000) / samplerate_divisor;
    out_packet.bit_depth = static_cast<int>(header[1]);
    out_packet.channels = static_cast<int>(header[2]);
    out_packet.chlayout1 = header[3];
    out_packet.chlayout2 = header[4];

    // Basic validation of parsed format
    if (out_packet.channels <= 0 || out_packet.channels > 64 || // Max channels reasonable limit
        (out_packet.bit_depth != 8 && out_packet.bit_depth != 16 && out_packet.bit_depth != 24 && out_packet.bit_depth != 32) ||
        out_packet.sample_rate <= 0) {
        log_warning("Parsed invalid audio format from Raw Scream packet. SR=" + std::to_string(out_packet.sample_rate) +
                      ", BD=" + std::to_string(out_packet.bit_depth) + ", CH=" + std::to_string(out_packet.channels));
        return false;
    }

    // Assign Payload Only
    out_packet.audio_data.assign(buffer + RAW_SCREAM_HEADER_SIZE,
                                 buffer + RAW_SCREAM_HEADER_SIZE + RAW_CHUNK_SIZE);
    
    if (out_packet.audio_data.size() != RAW_CHUNK_SIZE) { // Should always be true if size == EXPECTED_RAW_PACKET_SIZE
        log_error("Internal error: audio data size mismatch. Expected " + std::to_string(RAW_CHUNK_SIZE) +
                  ", got " + std::to_string(out_packet.audio_data.size()));
        return false;
    }
    
    return true;
}


bool RawScreamReceiver::process_and_validate_payload(
    const uint8_t* buffer,
    int size,
    const struct sockaddr_in& client_addr,
    std::chrono::steady_clock::time_point received_time,
    TaggedAudioPacket& out_packet,
    std::string& out_source_tag) {

    // is_valid_packet_structure (size check) should have already been called by the base class.
    // If we are here, size should be EXPECTED_RAW_PACKET_SIZE.

    // Extract source tag (IP address for Raw Scream)
    char sender_ip_cstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), sender_ip_cstr, INET_ADDRSTRLEN);
    out_source_tag = std::string(sender_ip_cstr);

    // Populate common parts of the TaggedAudioPacket
    out_packet.source_tag = out_source_tag;
    out_packet.received_time = received_time;

    // Parse header, set format, and assign payload
    if (!validate_raw_scream_content(buffer, size, out_packet)) {
        log_warning("Invalid Raw Scream packet content from " + out_source_tag + 
                    ". Size: " + std::to_string(size) + " bytes.");
        return false;
    }
    
    return true;
}

uint32_t RawScreamReceiver::calculate_samples_per_chunk(int channels, int bit_depth) {
    if (channels <= 0 || bit_depth <= 0 || (bit_depth % 8) != 0) {
        return 0;
    }

    const std::size_t bytes_per_frame = static_cast<std::size_t>(channels) * static_cast<std::size_t>(bit_depth / 8);
    if (bytes_per_frame == 0 || (RAW_CHUNK_SIZE % bytes_per_frame) != 0) {
        return 0;
    }

    return static_cast<uint32_t>(RAW_CHUNK_SIZE / bytes_per_frame);
}

std::shared_ptr<RawScreamReceiver::StreamState> RawScreamReceiver::get_or_create_stream_state(const TaggedAudioPacket& packet) {
    auto it = stream_states_.find(packet.source_tag);

    if (it == stream_states_.end()) {
        auto state = std::make_shared<StreamState>();
        state->source_tag = packet.source_tag;
        state->sample_rate = packet.sample_rate;
        state->channels = packet.channels;
        state->bit_depth = packet.bit_depth;
        state->chlayout1 = packet.chlayout1;
        state->chlayout2 = packet.chlayout2;
        state->samples_per_chunk = calculate_samples_per_chunk(packet.channels, packet.bit_depth);
        state->next_rtp_timestamp = 0;

        if (state->samples_per_chunk == 0) {
            log_error("Unsupported audio format for scheduled delivery from " + packet.source_tag);
            return nullptr;
        }

        auto inserted = stream_states_.emplace(packet.source_tag, state);
        it = inserted.first;

        if (clock_manager_) {
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

    auto state = it->second;
    if (!state) {
        state = std::make_shared<StreamState>();
        state->source_tag = packet.source_tag;
        it->second = state;
    }

    bool format_changed = (state->sample_rate != packet.sample_rate) ||
                          (state->channels != packet.channels) ||
                          (state->bit_depth != packet.bit_depth);

    if (format_changed) {
        if (clock_manager_ && state->clock_handle.valid()) {
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
        state->samples_per_chunk = calculate_samples_per_chunk(packet.channels, packet.bit_depth);
        state->next_rtp_timestamp = 0;
        state->pending_packets.clear();

        if (state->samples_per_chunk == 0) {
            log_error("Unsupported audio format for scheduled delivery from " + packet.source_tag);
            stream_states_.erase(it);
            return nullptr;
        }

        if (clock_manager_) {
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
    }

    state->chlayout1 = packet.chlayout1;
    state->chlayout2 = packet.chlayout2;

    return state;
}

void RawScreamReceiver::dispatch_ready_packet(TaggedAudioPacket&& packet) {
    if (!clock_manager_) {
        NetworkAudioReceiver::dispatch_ready_packet(std::move(packet));
        return;
    }

    bool fallback = false;
    {
        std::lock_guard<std::mutex> lock(stream_state_mutex_);
        auto state = get_or_create_stream_state(packet);
        if (!state) {
            fallback = true;
        } else {
            state->pending_packets.push_back(std::move(packet));
        }
    }

    if (fallback) {
        NetworkAudioReceiver::dispatch_ready_packet(std::move(packet));
    }
}

void RawScreamReceiver::dispatch_clock_ticks() {
    if (stop_flag_) {
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

void RawScreamReceiver::handle_clock_tick(const std::string& source_tag) {
    TaggedAudioPacket packet;

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
        } else {
            packet.source_tag = state.source_tag;
            packet.audio_data.assign(RAW_CHUNK_SIZE, 0);
            packet.received_time = now;
            packet.sample_rate = state.sample_rate;
            packet.channels = state.channels;
            packet.bit_depth = state.bit_depth;
            packet.chlayout1 = state.chlayout1;
            packet.chlayout2 = state.chlayout2;
        }

        if (state.samples_per_chunk > 0) {
            state.next_rtp_timestamp += state.samples_per_chunk;
            packet.rtp_timestamp = state.next_rtp_timestamp;
        } else {
            packet.rtp_timestamp.reset();
        }
    }

    NetworkAudioReceiver::dispatch_ready_packet(std::move(packet));
}

void RawScreamReceiver::clear_all_streams() {
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

size_t RawScreamReceiver::get_receive_buffer_size() const {
    return RAW_RECEIVE_BUFFER_SIZE_CONFIG;
}

int RawScreamReceiver::get_poll_timeout_ms() const {
    return RAW_POLL_TIMEOUT_MS_CONFIG;
}

void RawScreamReceiver::on_before_poll_wait() {
    dispatch_clock_ticks();
}

void RawScreamReceiver::on_after_poll_iteration() {
    dispatch_clock_ticks();
}

} // namespace audio
} // namespace screamrouter
