#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#ifdef _WIN32
using socket_t = SOCKET;
const socket_t PLATFORM_INVALID_SOCKET = INVALID_SOCKET;
#define  platform_close_socket(sock) { if(sock != INVALID_SOCKET) closesocket(sock); }
#else
using socket_t = int;
const socket_t PLATFORM_INVALID_SOCKET = -1;
#define platform_close_socket(sock) { if(sock >= 0) ::close(sock); }
#endif

namespace screamrouter {
namespace audio {

/**
 * @class INetworkSender
 * @brief An interface for classes that send audio data over the network.
 */
class INetworkSender {
public:
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
     * @param csrcs A vector of CSRC identifiers to include in the RTP header.
     */
    virtual void send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) = 0;
};

} // namespace audio
} // namespace screamrouter