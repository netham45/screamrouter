/**
 * @file i_network_sender.h
 * @brief Defines the INetworkSender interface for network audio sending components.
 * @details This file contains the abstract base class `INetworkSender`, which defines
 *          the common interface for all classes that send audio data over the network,
 *          such as `ScreamSender`, `RtpSender`, and `WebRtcSender`.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
using socket_t = SOCKET;
const socket_t PLATFORM_INVALID_SOCKET = INVALID_SOCKET;
#define  platform_close_socket(sock) { if(sock != INVALID_SOCKET) closesocket(sock); }
#else
#include <unistd.h>
using socket_t = int;
const socket_t PLATFORM_INVALID_SOCKET = -1;
#define platform_close_socket(sock) { if(sock >= 0) ::close(sock); }
#endif

namespace screamrouter {
namespace audio {

/**
 * @class INetworkSender
 * @brief An interface for classes that send audio data over the network.
 * @details This pure virtual class defines the contract for all network sender
 *          implementations. It ensures that they provide methods for setup,
 *          teardown, and sending payload data.
 */
class INetworkSender {
public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~INetworkSender() = default;

    /**
     * @brief Sets up the network sender, including creating sockets.
     * @return true if setup is successful, false otherwise.
     */
    virtual bool setup() = 0;

    /**
     * @brief Closes network connections and releases resources.
     */
    virtual void close() = 0;

    /**
     * @brief Sends an audio payload over the network.
     * @param payload_data Pointer to the payload data.
     * @param payload_size Size of the payload data in bytes.
     * @param csrcs A vector of CSRC identifiers to include (e.g., in an RTP header).
     */
    virtual void send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) = 0;
};

} // namespace audio
} // namespace screamrouter