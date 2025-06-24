#pragma once

#include "../i_network_sender.h"
#include "../../output_mixer/sink_audio_mixer.h" // For SinkMixerConfig
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
#include <unistd.h> // For close
#endif

namespace screamrouter {
namespace audio {

class RtpSender : public INetworkSender {
public:
    explicit RtpSender(const SinkMixerConfig& config);
    ~RtpSender() noexcept override;

    bool setup() override;
    void close() override;
    void send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) override;

private:
    SinkMixerConfig config_;
    socket_t udp_socket_fd_;
    struct sockaddr_in udp_dest_addr_;

    uint32_t ssrc_;
    uint16_t sequence_number_;
    uint32_t rtp_timestamp_;

    // SAP Announcement members
    socket_t sap_socket_fd_;
    std::vector<struct sockaddr_in> sap_dest_addrs_;
    std::thread sap_thread_;
    std::atomic<bool> sap_thread_running_;

    void sap_announcement_loop();
};

} // namespace audio
} // namespace screamrouter