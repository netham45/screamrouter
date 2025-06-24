#ifndef SAP_LISTENER_H
#define SAP_LISTENER_H

#include <string>
#include <vector>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <functional> // For std::function
#include "../../utils/cpp_logger.h"
#include <memory>
#include <sys/epoll.h> // For epoll

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
#else // POSIX
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    using socket_t = int;
#endif

namespace screamrouter {
namespace audio {

enum class Endianness {
    LITTLE,
    BIG
};

struct StreamProperties {
    int sample_rate;
    int channels;
    int bit_depth;
    Endianness endianness;
};

class SapListener {
public:
    using SessionCallback = std::function<void(const std::string& ip, int port)>;

    SapListener(std::string logger_prefix, const std::vector<std::string>& known_ips);
    ~SapListener();

    void start();
    void stop();
    void set_session_callback(SessionCallback callback);

    bool get_stream_properties(uint32_t ssrc, StreamProperties& properties);
    bool get_stream_properties_by_ip(const std::string& ip, StreamProperties& properties);
    std::vector<uint32_t> get_known_ssrcs();

    std::string logger_prefix_;
    void run();
    bool setup_sockets();
    void close_sockets();
    void process_sap_packet(const char* buffer, int size, const std::string& source_ip);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::vector<socket_t> sockets_;
    int epoll_fd_ = -1;

    std::mutex ssrc_map_mutex_;
    std::unordered_map<uint32_t, StreamProperties> ssrc_to_properties_;
    
    std::mutex ip_map_mutex_;
    std::unordered_map<std::string, StreamProperties> ip_to_properties_;

private:
    SessionCallback session_callback_;
    std::vector<std::string> known_ips_;
};

} // namespace audio
} // namespace screamrouter

#endif // SAP_LISTENER_H