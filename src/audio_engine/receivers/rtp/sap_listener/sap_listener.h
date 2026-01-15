#ifndef SCREAMROUTER_AUDIO_SAP_LISTENER_H
#define SCREAMROUTER_AUDIO_SAP_LISTENER_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sap_types.h"

#ifndef _WIN32
    #include <sys/epoll.h>
#endif

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    #define NAR_INVALID_SOCKET_VALUE INVALID_SOCKET
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    using socket_t = int;
    #define NAR_INVALID_SOCKET_VALUE -1
#endif

namespace screamrouter {
namespace audio {

class SapDirectory;

class SapListener {
public:
    using SessionCallback = std::function<void(const std::string& ip, int port, const std::string& source_ip)>;

    SapListener(std::string logger_prefix, const std::vector<std::string>& known_ips);
    ~SapListener();

    void start();
    void stop();

    void set_session_callback(SessionCallback callback);

    bool get_stream_properties(uint32_t ssrc, StreamProperties& properties);
    bool get_stream_properties_by_ip(const std::string& ip, int port, StreamProperties& properties);

    std::vector<SapAnnouncement> get_announcements();
    bool get_stream_identity(const std::string& ip,
                             int port,
                             std::string& guid,
                             std::string& session_name,
                             std::string& stream_ip_out,
                             int& stream_port_out);
    bool get_stream_identity_by_ssrc(uint32_t ssrc,
                                     std::string& guid,
                                     std::string& session_name,
                                     std::string& stream_ip_out,
                                     int& stream_port_out);

private:
    void run();
    bool setup_sockets();
    void close_sockets();
    void process_sap_packet(const char* buffer, int size, const std::string& source_ip);

    std::string logger_prefix_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::vector<socket_t> sockets_;
#ifdef _WIN32
    fd_set master_read_fds_;
    socket_t max_fd_ = NAR_INVALID_SOCKET_VALUE;
#else
    int epoll_fd_ = -1;
#endif

    SessionCallback session_callback_;
    std::vector<std::string> known_ips_;
    std::unique_ptr<SapDirectory> directory_;
};

} // namespace audio
} // namespace screamrouter

#endif // SCREAMROUTER_AUDIO_SAP_LISTENER_H
