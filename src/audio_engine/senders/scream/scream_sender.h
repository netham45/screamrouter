#pragma once

#include "../i_network_sender.h"
#include "../../output_mixer/sink_audio_mixer.h" // For SinkMixerConfig
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
#include <unistd.h> // For close
#endif

namespace screamrouter {
namespace audio {

class ScreamSender : public INetworkSender {
public:
    explicit ScreamSender(const SinkMixerConfig& config);
    ~ScreamSender() noexcept override;

    bool setup() override;
    void close() override;
    void send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) override;

private:
    void build_scream_header();

    SinkMixerConfig config_;
    socket_t udp_socket_fd_;
    struct sockaddr_in udp_dest_addr_;
    std::array<uint8_t, 5> scream_header_;
};

} // namespace audio
} // namespace screamrouter