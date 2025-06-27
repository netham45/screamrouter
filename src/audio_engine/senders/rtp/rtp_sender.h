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
#include <rtc/rtp.hpp>
#include <cstdint>
#include <thread>
#include <atomic>

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
     */
    explicit RtpSender(const SinkMixerConfig& config);
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

    socket_t sap_socket_fd_;
    std::vector<struct sockaddr_in> sap_dest_addrs_;
    std::thread sap_thread_;
    std::atomic<bool> sap_thread_running_;

    /**
     * @brief The main loop for the SAP announcement thread.
     */
    void sap_announcement_loop();
};

} // namespace audio
} // namespace screamrouter