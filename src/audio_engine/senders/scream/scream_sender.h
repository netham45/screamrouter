/**
 * @file scream_sender.h
 * @brief Defines the ScreamSender class for sending audio data using the Scream protocol.
 * @details This file contains the definition of the `ScreamSender` class, which
 *          implements the `INetworkSender` interface for the raw Scream audio protocol.
 */
#pragma once

#include "../i_network_sender.h"
#include "../../output_mixer/sink_audio_mixer.h"
#include <vector>
#include <array>

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
 * @class ScreamSender
 * @brief An implementation of `INetworkSender` for the raw Scream protocol.
 * @details This class handles sending audio payloads over UDP using the Scream
 *          protocol, which involves prepending a 5-byte header to the raw PCM data.
 */
class ScreamSender : public INetworkSender {
public:
    /**
     * @brief Constructs a ScreamSender.
     * @param config The configuration for the sink this sender is associated with.
     */
    explicit ScreamSender(const SinkMixerConfig& config);
    /**
     * @brief Destructor.
     */
    ~ScreamSender() noexcept override;

    /** @brief Sets up the UDP socket for sending. */
    bool setup() override;
    /** @brief Closes the UDP socket. */
    void close() override;
    /**
     * @brief Sends an audio payload as a Scream packet.
     * @param payload_data Pointer to the raw audio data.
     * @param payload_size The size of the audio data in bytes.
     * @param csrcs Contributing source identifiers (ignored by this sender).
     */
    void send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) override;

private:
    /**
     * @brief Builds the 5-byte Scream protocol header based on the current configuration.
     */
    void build_scream_header();

    static constexpr std::size_t kScreamPayloadBytes = 1152;

    SinkMixerConfig config_;
    socket_t udp_socket_fd_;
    struct sockaddr_in udp_dest_addr_;
    std::array<uint8_t, 5> scream_header_;
    std::vector<uint8_t> packetizer_buffer_;

    bool is_silence(const uint8_t* payload_data, size_t payload_size) const;
    void send_scream_packet(const uint8_t* payload_slice, size_t slice_size);
};

} // namespace audio
} // namespace screamrouter
