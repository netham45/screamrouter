#include "rtp_reordering_buffer.h"
#include "../../utils/cpp_logger.h" // For logging
#include <limits>
#include <utility>
#include <iostream>

namespace {
constexpr uint16_t kLargeGapResetThreshold = 192;
constexpr auto kLargeGapLogInterval = std::chrono::seconds(2);
}

RtpReorderingBuffer::RtpReorderingBuffer(std::chrono::milliseconds max_delay, size_t max_size)
    : m_next_expected_seq(0),
      m_is_initialized(false),
      m_max_delay(max_delay),
      m_max_size(max_size) {}

void RtpReorderingBuffer::add_packet(RtpPacketData&& packet) {
    if (!m_is_initialized) {
        m_is_initialized = true;
        m_next_expected_seq = packet.sequence_number;
        LOG_CPP_DEBUG(("[RtpReorderingBuffer] Initialized. First packet sequence: " + std::to_string(packet.sequence_number)).c_str());
    }

    // Detect out-of-order arrivals (ahead of the next expected sequence).
    if (m_is_initialized &&
        packet.sequence_number != m_next_expected_seq &&
        is_sequence_greater(packet.sequence_number, m_next_expected_seq)) {
        const uint16_t seq_gap = static_cast<uint16_t>(packet.sequence_number - m_next_expected_seq);
        const auto now = std::chrono::steady_clock::now();

        if (seq_gap >= kLargeGapResetThreshold && m_buffer.empty()) {
            if (last_large_gap_log_.time_since_epoch().count() == 0 ||
                now - last_large_gap_log_ >= kLargeGapLogInterval) {
                LOG_CPP_WARNING(("[RtpReorderingBuffer] Large forward jump (gap=" +
                                 std::to_string(seq_gap) +
                                 ") detected. Resetting expectation to seq " +
                                 std::to_string(packet.sequence_number) + ".").c_str());
                last_large_gap_log_ = now;
            }
            m_next_expected_seq = packet.sequence_number;
        } else {
            if (last_out_of_order_log_.time_since_epoch().count() == 0 ||
                now - last_out_of_order_log_ >= std::chrono::milliseconds(200)) {
                LOG_CPP_WARNING(("[RtpReorderingBuffer] Out-of-order packet arrived. Expected seq " +
                                 std::to_string(m_next_expected_seq) + " but received " +
                                 std::to_string(packet.sequence_number) + " (gap=" +
                                 std::to_string(seq_gap) + ", buffered=" +
                                 std::to_string(m_buffer.size()) + ").").c_str());
                last_out_of_order_log_ = now;
            }
        }
    }

    // Discard packets that are too old (already processed)
    if (!is_sequence_greater(packet.sequence_number, m_next_expected_seq) &&
        packet.sequence_number != m_next_expected_seq) {
        LOG_CPP_DEBUG(("[RtpReorderingBuffer] Discarding late packet. Sequence: " +
                     std::to_string(packet.sequence_number) + ", Already at: " +
                     std::to_string(m_next_expected_seq)).c_str());
        return;
    }

    // Check for duplicate packets
    if (m_buffer.count(packet.sequence_number)) {
        LOG_CPP_DEBUG(("[RtpReorderingBuffer] Discarding duplicate packet. Sequence: " + std::to_string(packet.sequence_number)).c_str());
        return;
    }

    const uint16_t new_delta = static_cast<uint16_t>(packet.sequence_number - m_next_expected_seq);

    // Prevent buffer from growing indefinitely
    if (m_buffer.size() >= m_max_size) {
        auto drop_it = m_buffer.end();
        uint16_t farthest_distance = 0;

        for (auto it = m_buffer.begin(); it != m_buffer.end(); ++it) {
            const uint16_t delta = static_cast<uint16_t>(it->first - m_next_expected_seq);

            // Prefer discarding packets that are already stale (behind the expected sequence).
            if (delta >= 32768) {
                drop_it = it;
                break;
            }

            if (drop_it == m_buffer.end() || delta > farthest_distance) {
                drop_it = it;
                farthest_distance = delta;
            }
        }

        if (drop_it != m_buffer.end() && new_delta < 32768 && new_delta > farthest_distance) {
            LOG_CPP_WARNING(("[RtpReorderingBuffer] Buffer full (size: " + std::to_string(m_buffer.size()) +
                            "). Dropping incoming packet Seq: " + std::to_string(packet.sequence_number) +
                            " (distance " + std::to_string(new_delta) + ") as it is farther than buffered packets.").c_str());
            return;
        }

        if (drop_it != m_buffer.end()) {
            LOG_CPP_WARNING(("[RtpReorderingBuffer] Buffer full (size: " + std::to_string(m_buffer.size()) +
                            "). Discarding packet Seq: " + std::to_string(drop_it->first) +
                            " to make space for new packet Seq: " + std::to_string(packet.sequence_number) + ".").c_str());
            m_buffer.erase(drop_it);
        }
    }

    m_buffer[packet.sequence_number] = std::move(packet);
}

std::vector<RtpPacketData> RtpReorderingBuffer::get_ready_packets() {
    std::vector<RtpPacketData> ready_packets;
    if (!m_is_initialized) {
        return ready_packets;
    }

    const auto now = std::chrono::steady_clock::now();

    while (true) {
        auto exact_it = m_buffer.find(m_next_expected_seq);
        if (exact_it != m_buffer.end()) {
            RtpPacketData packet = std::move(exact_it->second);
            m_last_released_packet = packet; // Save copy for history
            ready_packets.push_back(std::move(packet));
            m_buffer.erase(exact_it);
            m_next_expected_seq++;
            continue;
        }

        // Drop packets that are already behind the expected sequence number.
        bool removed_late_packet = false;
        for (auto it = m_buffer.begin(); it != m_buffer.end();) {
            const uint16_t delta = static_cast<uint16_t>(it->first - m_next_expected_seq);
            if (delta >= 32768) {
                LOG_CPP_DEBUG(("[RtpReorderingBuffer] Discarding late packet with seq " +
                               std::to_string(it->first) + " (expecting " +
                               std::to_string(m_next_expected_seq) + " or higher)").c_str());
                it = m_buffer.erase(it);
                removed_late_packet = true;
            } else {
                ++it;
            }
        }
        if (removed_late_packet) {
            continue;
        }

        if (m_buffer.empty()) {
            break;
        }

        auto candidate_it = m_buffer.end();
        uint16_t best_distance = std::numeric_limits<uint16_t>::max();
        for (auto it = m_buffer.begin(); it != m_buffer.end(); ++it) {
            const uint16_t delta = static_cast<uint16_t>(it->first - m_next_expected_seq);
            if (delta < 32768 && delta < best_distance) {
                candidate_it = it;
                best_distance = delta;
            }
        }

        if (candidate_it == m_buffer.end()) {
            break;
        }

        const auto wait_time = now - candidate_it->second.received_time;
        if (wait_time >= m_max_delay) {
            const uint16_t skipped = best_distance;
            if (skipped > 0) {
                std::cout << "DEBUG: Skipped > 0. has_last=" << m_last_released_packet.has_value() << std::endl;
                if (m_last_released_packet.has_value()) {
                     std::cout << "DEBUG: Calling can_interpolate. Packet size: " << m_last_released_packet->payload.size() << std::endl;
                }
                if (m_last_released_packet.has_value() && can_interpolate(*m_last_released_packet, candidate_it->second)) {
                    std::cout << "DEBUG: Interpolating..." << std::endl;
                    LOG_CPP_WARNING(("[RtpReorderingBuffer] Timed out. Interpolating " +
                                         std::to_string(skipped) + " packet(s) from seq " +
                                         std::to_string(m_next_expected_seq) + " using Crossfade.").c_str());

                    uint32_t start_ts = m_last_released_packet->rtp_timestamp;
                    uint32_t end_ts = candidate_it->second.rtp_timestamp;
                    
                    // std::cout << "DEBUG: TS " << start_ts << " -> " << end_ts << std::endl;
                    
                    int64_t ts_diff = static_cast<int64_t>(end_ts) - static_cast<int64_t>(start_ts);
                    if (end_ts < start_ts) {
                         // Wrapped.
                         ts_diff = (static_cast<int64_t>(end_ts) + 4294967296LL) - static_cast<int64_t>(start_ts);
                    }
                    
                    double ts_increment = static_cast<double>(ts_diff) / static_cast<double>(skipped + 1);
                    double total_steps = static_cast<double>(skipped + 1);

                    for (uint16_t i = 0; i < skipped; ++i) {
                        uint16_t seq = static_cast<uint16_t>(m_next_expected_seq + i);
                        
                        RtpPacketData filler;
                        filler.sequence_number = seq;
                        filler.payload_type = m_last_released_packet->payload_type; 
                        filler.ssrc = m_last_released_packet->ssrc;
                        filler.csrcs = m_last_released_packet->csrcs;
                        filler.received_time = now;
                        filler.ingress_from_loopback = m_last_released_packet->ingress_from_loopback;
                        
                        double offset = ts_increment * (i + 1);
                        filler.rtp_timestamp = static_cast<uint32_t>(static_cast<double>(start_ts) + offset);
                        
                        float alpha_start = static_cast<float>(i) / static_cast<float>(total_steps);
                        float alpha_end = static_cast<float>(i + 1) / static_cast<float>(total_steps);
                        
                        filler.payload = generate_interpolated_payload(
                            m_last_released_packet->payload, 
                            candidate_it->second.payload, 
                            alpha_start, 
                            alpha_end
                        );
                        
                        ready_packets.push_back(std::move(filler));
                    }
                } else {
                    LOG_CPP_WARNING(("[RtpReorderingBuffer] Timed out waiting for " +
                                     std::to_string(skipped) + " packet(s) starting at seq " +
                                     std::to_string(m_next_expected_seq) + ". Advancing to seq " +
                                     std::to_string(candidate_it->first) + ".").c_str());
                }
            }
            m_next_expected_seq = candidate_it->first;
            continue;
        }

        break;
    }

    return ready_packets;
}

void RtpReorderingBuffer::reset() {
    LOG_CPP_INFO("[RtpReorderingBuffer] Resetting buffer state.");
    m_buffer.clear();
    m_is_initialized = false;
    m_next_expected_seq = 0;
    m_last_released_packet.reset();
}

size_t RtpReorderingBuffer::size() const {
    return m_buffer.size();
}

// Provide the definition for the static member function.
bool RtpReorderingBuffer::is_sequence_greater(uint16_t seq1, uint16_t seq2) {
    return (seq1 != seq2) && (static_cast<uint16_t>(seq1 - seq2) < 32768);
}

void RtpReorderingBuffer::set_properties(const screamrouter::audio::StreamProperties& props) {
    m_properties = props;
}

std::optional<uint8_t> RtpReorderingBuffer::get_head_payload_type() const {
    if (m_buffer.empty()) {
        return std::nullopt;
    }
    return m_buffer.begin()->second.payload_type;
}

bool RtpReorderingBuffer::can_interpolate(const RtpPacketData& old_pkt, const RtpPacketData& new_pkt) const {
    if (m_properties.codec != screamrouter::audio::StreamCodec::PCM) {
        return false;
    }
    if (m_properties.bit_depth != 8 && 
        m_properties.bit_depth != 16 && 
        m_properties.bit_depth != 24 && 
        m_properties.bit_depth != 32) {
        return false;
    }
    if (old_pkt.payload.size() != new_pkt.payload.size()) {
        return false;
    }
    if (old_pkt.payload.empty()) {
        return false;
    }
    const int bytes_per_sample = m_properties.bit_depth / 8;
    const int block_align = m_properties.channels * bytes_per_sample;
    if (block_align > 0 && (old_pkt.payload.size() % block_align != 0)) {
        return false;
    }
    return true;
}

int32_t RtpReorderingBuffer::read_sample(const uint8_t* ptr, int bit_depth, screamrouter::audio::Endianness endianness) {
    if (bit_depth == 16) {
        int16_t val;
        if (endianness == screamrouter::audio::Endianness::BIG) {
            val = static_cast<int16_t>((ptr[0] << 8) | ptr[1]);
        } else {
            val = static_cast<int16_t>((ptr[1] << 8) | ptr[0]);
        }
        return val;
    } else if (bit_depth == 24) {
        int32_t val;
        if (endianness == screamrouter::audio::Endianness::BIG) {
            uint32_t uval = (ptr[0] << 16) | (ptr[1] << 8) | ptr[2];
            if (uval & 0x800000) {
                uval |= 0xFF000000;
            }
            val = static_cast<int32_t>(uval);
        } else {
            uint32_t uval = (ptr[2] << 16) | (ptr[1] << 8) | ptr[0];
            if (uval & 0x800000) {
                uval |= 0xFF000000;
            }
            val = static_cast<int32_t>(uval);
        }
        return val;
    } else if (bit_depth == 32) {
        int32_t val;
        if (endianness == screamrouter::audio::Endianness::BIG) {
             uint32_t uval = (static_cast<uint32_t>(ptr[0]) << 24) |
                             (static_cast<uint32_t>(ptr[1]) << 16) |
                             (static_cast<uint32_t>(ptr[2]) << 8) |
                             static_cast<uint32_t>(ptr[3]);
             val = static_cast<int32_t>(uval);
        } else {
             uint32_t uval = (static_cast<uint32_t>(ptr[3]) << 24) |
                             (static_cast<uint32_t>(ptr[2]) << 16) |
                             (static_cast<uint32_t>(ptr[1]) << 8) |
                             static_cast<uint32_t>(ptr[0]);
             val = static_cast<int32_t>(uval);
        }
        return val;
    } else if (bit_depth == 8) {
        return static_cast<int8_t>(*ptr);
    }
    return 0;
}

void RtpReorderingBuffer::write_sample(uint8_t* ptr, int32_t sample, int bit_depth, screamrouter::audio::Endianness endianness) {
    if (bit_depth == 16) {
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        if (endianness == screamrouter::audio::Endianness::BIG) {
            ptr[0] = (sample >> 8) & 0xFF;
            ptr[1] = sample & 0xFF;
        } else {
            ptr[0] = sample & 0xFF;
            ptr[1] = (sample >> 8) & 0xFF;
        }
    } else if (bit_depth == 24) {
        if (sample > 8388607) sample = 8388607;
        if (sample < -8388608) sample = -8388608;
        uint32_t uval = static_cast<uint32_t>(sample) & 0xFFFFFF;
        if (endianness == screamrouter::audio::Endianness::BIG) {
            ptr[0] = (uval >> 16) & 0xFF;
            ptr[1] = (uval >> 8) & 0xFF;
            ptr[2] = uval & 0xFF;
        } else {
            ptr[0] = uval & 0xFF;
            ptr[1] = (uval >> 8) & 0xFF;
            ptr[2] = (uval >> 16) & 0xFF;
        }
    } else if (bit_depth == 32) {
        uint32_t uval = static_cast<uint32_t>(sample);
        if (endianness == screamrouter::audio::Endianness::BIG) {
            ptr[0] = (uval >> 24) & 0xFF;
            ptr[1] = (uval >> 16) & 0xFF;
            ptr[2] = (uval >> 8) & 0xFF;
            ptr[3] = uval & 0xFF;
        } else {
            ptr[0] = uval & 0xFF;
            ptr[1] = (uval >> 8) & 0xFF;
            ptr[2] = (uval >> 16) & 0xFF;
            ptr[3] = (uval >> 24) & 0xFF;
        }
    } else if (bit_depth == 8) {
        if (sample > 127) sample = 127;
        if (sample < -128) sample = -128;
        *ptr = static_cast<uint8_t>(sample);
    }
}

std::vector<uint8_t> RtpReorderingBuffer::generate_interpolated_payload(
    const std::vector<uint8_t>& old_data, 
    const std::vector<uint8_t>& new_data, 
    float alpha_start, 
    float alpha_end) const {
    
    std::vector<uint8_t> result = old_data; 
    
    const int bytes_per_sample = m_properties.bit_depth / 8;
    if (bytes_per_sample == 0) return result; 

    // Number of samples
    const float alpha_step = (alpha_end - alpha_start) / static_cast<float>(old_data.size() / bytes_per_sample);
    float current_alpha = alpha_start;

    for (size_t i = 0; i < old_data.size(); i += bytes_per_sample) {
        int32_t val_old = read_sample(&old_data[i], m_properties.bit_depth, m_properties.endianness);
        int32_t val_new = read_sample(&new_data[i], m_properties.bit_depth, m_properties.endianness);
        
        double d_old = static_cast<double>(val_old);
        double d_new = static_cast<double>(val_new);
        
        double mixed = d_old * (1.0f - current_alpha) + d_new * current_alpha;
        
        write_sample(&result[i], static_cast<int32_t>(mixed), m_properties.bit_depth, m_properties.endianness);
        
        current_alpha += alpha_step;
    }
    
    return result;
}
