#include "rtp_reordering_buffer.h"
#include "../../utils/cpp_logger.h" // For logging
#include <limits>
#include <utility>

// The is_sequence_greater function is now a static member of the class.
// The anonymous namespace is no longer needed.

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
        const auto now = std::chrono::steady_clock::now();
        const uint16_t seq_gap = static_cast<uint16_t>(packet.sequence_number - m_next_expected_seq);
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
            ready_packets.push_back(std::move(exact_it->second));
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
                LOG_CPP_WARNING(("[RtpReorderingBuffer] Timed out waiting for " +
                                 std::to_string(skipped) + " packet(s) starting at seq " +
                                 std::to_string(m_next_expected_seq) + ". Advancing to seq " +
                                 std::to_string(candidate_it->first) + ".").c_str());
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
}

size_t RtpReorderingBuffer::size() const {
    return m_buffer.size();
}

// Provide the definition for the static member function.
bool RtpReorderingBuffer::is_sequence_greater(uint16_t seq1, uint16_t seq2) {
    return (seq1 != seq2) && (static_cast<uint16_t>(seq1 - seq2) < 32768);
}
