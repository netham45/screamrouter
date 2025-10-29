/**
 * @file rtp_sender_core.h
 * @brief Core RTP functionality extracted from RtpSender for reuse.
 * @details This file contains the RtpSenderCore class which provides the core
 *          RTP packet sending functionality without the INetworkSender interface,
 *          making it reusable by both RtpSender and MultiDeviceRtpSender.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define PLATFORM_INVALID_SOCKET INVALID_SOCKET
#define platform_close_socket closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define PLATFORM_INVALID_SOCKET -1
#define platform_close_socket(sock) { if(sock >= 0) ::close(sock); }
#endif

namespace screamrouter {
namespace audio {

/**
 * @class RtpSenderCore
 * @brief Core RTP functionality without the INetworkSender interface.
 * @details This class encapsulates the core logic for sending RTP packets,
 *          managing RTP session state (SSRC, sequence number, timestamp),
 *          and handling the UDP socket. It's designed to be reusable by
 *          both single and multi-device RTP senders.
 */
class RtpSenderCore {
public:
    /**
     * @brief Constructs an RtpSenderCore.
     * @param ssrc The synchronization source identifier for this RTP stream.
     */
    explicit RtpSenderCore(uint32_t ssrc);
    
    /**
     * @brief Destructor.
     */
    ~RtpSenderCore() noexcept;

    /**
     * @brief Sets up the UDP socket and configures the destination.
     * @param dest_ip The destination IP address.
     * @param dest_port The destination port.
     * @param is_multicast Whether the destination is a multicast address.
     * @return true if setup succeeded, false otherwise.
     */
    bool setup(const std::string& dest_ip, uint16_t dest_port, bool is_multicast_addr = false);

    /**
     * @brief Closes the UDP socket.
     */
    void close();

    /**
     * @brief Sends an RTP packet with the given payload.
     * @param payload_data Pointer to the raw audio data.
     * @param payload_size The size of the audio data in bytes.
     * @param timestamp The RTP timestamp for this packet.
     * @param csrcs A vector of CSRC identifiers to include in the RTP header.
     * @return true if the packet was sent successfully, false otherwise.
     */
    bool send_rtp_packet(const uint8_t* payload_data, size_t payload_size,
                        uint32_t timestamp, const std::vector<uint32_t>& csrcs);

    /**
     * @brief Gets the current sequence number.
     * @return The current RTP sequence number.
     */
    uint16_t get_sequence_number() const { return sequence_number_; }

    /**
     * @brief Increments and returns the next sequence number.
     * @return The next RTP sequence number.
     */
    uint16_t get_next_sequence_number() { return ++sequence_number_; }

    /**
     * @brief Gets the SSRC for this RTP stream.
     * @return The SSRC identifier.
     */
    uint32_t get_ssrc() const { return ssrc_; }

    /**
     * @brief Gets packet statistics.
     * @param packet_count Output parameter for the number of packets sent.
     * @param octet_count Output parameter for the number of octets sent.
     */
    void get_statistics(uint32_t& packet_count, uint64_t& octet_count) const {
        packet_count = packet_count_.load();
        octet_count = octet_count_.load();
    }

    /**
     * @brief Checks if the socket is valid and ready.
     * @return true if the socket is valid, false otherwise.
     */
    bool is_ready() const { return udp_socket_fd_ != PLATFORM_INVALID_SOCKET; }

private:
    socket_t udp_socket_fd_;
    struct sockaddr_in udp_dest_addr_;
    
    uint32_t ssrc_;
    std::atomic<uint16_t> sequence_number_;
    
    // Statistics tracking
    std::atomic<uint32_t> packet_count_;
    std::atomic<uint64_t> octet_count_;
    
    std::string dest_ip_;
    uint16_t dest_port_;
};

} // namespace audio
} // namespace screamrouter