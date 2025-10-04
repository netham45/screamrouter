#include "rtp_reordering_buffer.h"
#include "../../utils/cpp_logger.h" // For logging
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

    // Prevent buffer from growing indefinitely
    if (m_buffer.size() >= m_max_size) {
        LOG_CPP_WARNING(("[RtpReorderingBuffer] Buffer full (size: " + std::to_string(m_buffer.size()) + "). Discarding oldest packet (Seq: " + std::to_string(m_buffer.begin()->first) + ") to make space for new packet (Seq: " + std::to_string(packet.sequence_number) + ").").c_str());
        m_buffer.erase(m_buffer.begin());
    }

    m_buffer[packet.sequence_number] = std::move(packet);
}

std::vector<RtpPacketData> RtpReorderingBuffer::get_ready_packets() {
    std::vector<RtpPacketData> ready_packets;
    if (!m_is_initialized) {
        return ready_packets;
    }

    // NO WAITING - Process packets immediately in order or skip missing ones
    for (auto it = m_buffer.begin(); it != m_buffer.end(); ) {
        if (it->first == m_next_expected_seq) {
            // This is the packet we expect, add it to the list
            ready_packets.push_back(std::move(it->second));
            it = m_buffer.erase(it);
            m_next_expected_seq++; // Increment for the next expected packet
        } else if (is_sequence_greater(it->first, m_next_expected_seq)) {
            // We have a gap - packet(s) are missing
            // Don't wait at all - immediately skip to the available packet
            uint16_t num_missing = (uint16_t)(it->first - m_next_expected_seq);
            
            LOG_CPP_WARNING(("[RtpReorderingBuffer] Skipping " + std::to_string(num_missing) +
                           " missing packet(s) from seq " + std::to_string(m_next_expected_seq) +
                           " to " + std::to_string((uint16_t)(it->first - 1)) +
                           ". Immediately advancing to available packet: " + std::to_string(it->first)).c_str());
            
            // Skip all missing packets and jump to the available one
            m_next_expected_seq = it->first;
            // Continue to process the now-current packet
            continue;
        } else {
            // This packet is older than what we expect (late arrival)
            // Discard it since we've already moved past this sequence number
            LOG_CPP_DEBUG(("[RtpReorderingBuffer] Discarding late packet with seq " +
                         std::to_string(it->first) + " (expecting " +
                         std::to_string(m_next_expected_seq) + " or higher)").c_str());
            it = m_buffer.erase(it);
        }
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