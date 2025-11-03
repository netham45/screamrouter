#pragma once

#include <map>
#include <vector>
#include <cstdint>
#include <optional>
#include <chrono>
#include <vector>

/**
 * @brief A structure to hold the essential data of a received RTP packet
 *        for buffering and reordering.
 */
struct RtpPacketData {
    uint16_t sequence_number;
    uint32_t rtp_timestamp;
    std::chrono::steady_clock::time_point received_time;
    std::vector<uint8_t> payload;
    uint32_t ssrc;
    std::vector<uint32_t> csrcs;
    uint8_t payload_type = 0;
};

/**
 * @brief A buffer to handle out-of-order RTP packets and manage jitter.
 *
 * This class stores incoming RTP packets, sorts them by sequence number,
 * and releases them in the correct order. It will wait for a configurable
 * duration for missing packets before declaring them lost and proceeding.
 */
class RtpReorderingBuffer {
public:
    /**
     * @brief Constructs the reordering buffer.
     * @param max_delay The maximum time to wait for a missing packet before skipping it.
     * @param max_size The maximum number of packets to store to prevent buffer bloat.
     */
    explicit RtpReorderingBuffer(
        std::chrono::milliseconds max_delay = std::chrono::milliseconds(50),
        size_t max_size = 128
    );

    /**
     * @brief Adds a packet to the buffer.
     * @param packet The packet data to add.
     */
    void add_packet(RtpPacketData&& packet);

    /**
     * @brief Retrieves all packets that are now ready to be processed in sequence.
     * @return A vector of packets in correct sequence number order.
     */
    std::vector<RtpPacketData> get_ready_packets();

    /**
     * @brief Resets the buffer's state, clearing all stored packets.
     *        Should be called on SSRC change or stream reset.
     */
    void reset();

    /**
     * @brief Gets the current number of packets stored in the buffer.
     */
    size_t size() const;

private:
    // A map is used to automatically store packets sorted by sequence number.
    std::map<uint16_t, RtpPacketData> m_buffer;

    uint16_t m_next_expected_seq;
    bool m_is_initialized;

    const std::chrono::milliseconds m_max_delay;
    const size_t m_max_size;

    // Helper to correctly compare 16-bit sequence numbers with wraparound.
    static bool is_sequence_greater(uint16_t seq1, uint16_t seq2);
};
