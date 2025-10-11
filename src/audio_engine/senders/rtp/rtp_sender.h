/**
 * @file rtp_sender.h
 * @brief Defines the RtpSender class for sending audio data using RTP.
 * @details This file contains the definition of the `RtpSender` class, which
 *          implements the `INetworkSender` interface for the Real-time Transport Protocol.
 *          It also handles sending SAP announcements for stream discovery.
 */
#pragma once

#include "../i_network_sender.h"
#include "../../output_mixer/sink_audio_mixer.h"
#include "../../timing/timestamp_mapper.h"
#include <rtc/rtp.hpp>
#include <cstdint>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace screamrouter {
namespace audio {

/**
 * @class RtpSender
 * @brief An implementation of `INetworkSender` for the RTP protocol.
 * @details This class encapsulates the logic for sending audio payloads as RTP packets.
 *          It manages its own UDP socket and RTP session state (SSRC, sequence number, timestamp).
 *          It also runs a separate thread to periodically send Session Announcement Protocol (SAP)
 *          packets to make the stream discoverable by `SapListener` instances on the network.
 */
class RtpSender : public INetworkSender {
public:
    /**
     * @brief Constructs an RtpSender.
     * @param config The configuration for the sink this sender is associated with.
     * @param timestamp_mapper Optional shared pointer to TimestampMapper for synchronized timestamps.
     */
    explicit RtpSender(const SinkMixerConfig& config,
                      std::shared_ptr<screamrouter::audio_engine::TimestampMapper> timestamp_mapper = nullptr);
    /**
     * @brief Destructor.
     */
    ~RtpSender() noexcept override;

    /** @brief Sets up the UDP socket and starts the SAP announcement thread. */
    bool setup() override;
    /** @brief Closes the sockets and stops the SAP announcement thread. */
    void close() override;
    /**
     * @brief Sends an audio payload as an RTP packet.
     * @param payload_data Pointer to the raw audio data.
     * @param payload_size The size of the audio data in bytes.
     * @param csrcs A vector of CSRC identifiers to include in the RTP header.
     */
    void send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) override;

private:
    SinkMixerConfig config_;
    socket_t udp_socket_fd_;
    struct sockaddr_in udp_dest_addr_;

    uint32_t ssrc_;
    uint16_t sequence_number_;
    uint32_t rtp_timestamp_;
    
    // Phase 5: Timestamp synchronization
    std::shared_ptr<screamrouter::audio_engine::TimestampMapper> timestamp_mapper_;

    // SAP announcement members
    socket_t sap_socket_fd_;
    std::vector<struct sockaddr_in> sap_dest_addrs_;
    std::thread sap_thread_;
    std::atomic<bool> sap_thread_running_;

    // RTCP socket infrastructure
    socket_t rtcp_socket_fd_;                              ///< Socket for RTCP communication
    struct sockaddr_in rtcp_dest_addr_;                    ///< Destination address for RTCP packets

    // RTCP thread management
    std::thread rtcp_thread_;                              ///< Thread for handling RTCP operations
    std::atomic<bool> rtcp_thread_running_;                ///< Flag to control RTCP thread lifecycle

    // RTCP statistics tracking
    std::atomic<uint32_t> packet_count_;                   ///< Number of RTP packets sent
    std::atomic<uint32_t> octet_count_;                    ///< Number of payload octets sent

    // Time synchronization variables
    std::chrono::system_clock::time_point stream_start_time_;     ///< When the stream started (system_clock)
    std::chrono::steady_clock::time_point stream_start_time_ref_; ///< When the stream started (reference clock) - Phase 5
    uint32_t stream_start_rtp_timestamp_;                         ///< Initial RTP timestamp at stream start
    int time_sync_delay_ms_;                                      ///< Delay to add to wall clock time (ms)

    /**
     * @brief The main loop for the SAP announcement thread.
     */
    void sap_announcement_loop();

    /**
     * @brief The main loop for the RTCP thread.
     * @details Handles sending periodic Sender Reports and processing incoming RTCP packets.
     */
    void rtcp_thread_loop();

    /**
     * @brief Sends an RTCP Sender Report packet.
     * @details Contains NTP timestamp for time synchronization and stream statistics.
     */
    void send_rtcp_sr();

    /**
     * @brief Calculates the current NTP timestamp with optional delay.
     * @return 64-bit NTP timestamp (seconds in upper 32 bits, fraction in lower 32 bits).
     */
    uint64_t get_ntp_timestamp_with_delay();
    
    /**
     * @brief Gets synchronized NTP timestamp including measured pipeline latency.
     * @return 64-bit NTP timestamp adjusted for measured latency.
     */
    uint64_t get_synchronized_ntp_timestamp();

    /**
     * @brief Maps an NTP timestamp to the corresponding RTP timestamp.
     * @param ntp_timestamp The NTP timestamp to convert.
     * @return The corresponding RTP timestamp.
     */
    uint32_t calculate_rtp_timestamp_for_ntp(uint64_t ntp_timestamp);

    /**
     * @brief Processes incoming RTCP packets.
     * @param data Pointer to the received RTCP data.
     * @param size Size of the received data.
     * @param sender_addr Address of the sender.
     */
    void process_incoming_rtcp(const uint8_t* data, size_t size,
                               const struct sockaddr_in& sender_addr);

    /**
     * @brief Processes an RTCP Receiver Report.
     * @param rr Pointer to the Receiver Report packet.
     * @param sender_addr Address of the sender.
     */
    void process_rtcp_rr(const void* rr, const struct sockaddr_in& sender_addr);

    /**
     * @brief Processes an RTCP Source Description packet.
     * @param sdes Pointer to the SDES packet.
     * @param sender_addr Address of the sender.
     */
    void process_rtcp_sdes(const void* sdes, const struct sockaddr_in& sender_addr);

    /**
     * @brief Processes an RTCP BYE packet.
     * @param bye Pointer to the BYE packet.
     * @param sender_addr Address of the sender.
     */
    void process_rtcp_bye(const void* bye, const struct sockaddr_in& sender_addr);

    /**
     * @brief Sends an RTCP packet.
     * @param packet Pointer to the RTCP packet data.
     * @param size Size of the packet.
     */
    void send_rtcp_packet(const void* packet, size_t size);
};

} // namespace audio
} // namespace screamrouter