/**
 * @file sap_listener.h
 * @brief Defines the SapListener class for discovering RTP streams via SAP.
 * @details This file contains the definition of the `SapListener`, which listens for
 *          Session Announcement Protocol (SAP) packets on the network to dynamically
 *          discover new RTP audio streams and their properties.
 */
#ifndef SAP_LISTENER_H
#define SAP_LISTENER_H

#include <string>
#include <vector>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <memory>
#ifndef _WIN32
    #include <sys/epoll.h>
#endif

#include "../../utils/cpp_logger.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    #define NAR_INVALID_SOCKET_VALUE INVALID_SOCKET
#else // POSIX
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    using socket_t = int;
    #define NAR_INVALID_SOCKET_VALUE -1
#endif

namespace screamrouter {
namespace audio {

/**
 * @enum Endianness
 * @brief Specifies the endianness of an audio stream.
 */
enum class Endianness {
    LITTLE, ///< Little-endian byte order.
    BIG     ///< Big-endian byte order.
};

/**
 * @struct StreamProperties
 * @brief Holds the audio properties of a discovered stream.
 */
struct StreamProperties {
    int sample_rate;      ///< The sample rate in Hz.
    int channels;         ///< The number of audio channels.
    int bit_depth;        ///< The bit depth of the audio samples.
    Endianness endianness;///< The byte order of the audio samples.
};

/**
 * @class SapListener
 * @brief Listens for and parses SAP announcements to discover RTP streams.
 * @details This class runs a dedicated thread to listen on the standard SAP multicast
 *          address. When it receives a valid SAP packet, it parses the SDP payload
 *          to extract stream properties and invokes a callback to notify its owner
 *          (typically an `RtpReceiver`) of the new session.
 */
class SapListener {
public:
    /** @brief A callback function type to notify about a new session. */
    using SessionCallback = std::function<void(const std::string& ip, int port, const std::string& source_ip)>;

    /**
     * @brief Constructs a SapListener.
     * @param logger_prefix A prefix for log messages.
     * @param known_ips A list of pre-configured IP addresses to also listen on.
     */
    SapListener(std::string logger_prefix, const std::vector<std::string>& known_ips);
    /**
     * @brief Destructor.
     */
    ~SapListener();

    /** @brief Starts the listener thread. */
    void start();
    /** @brief Stops the listener thread. */
    void stop();
    /**
     * @brief Sets the callback function to be invoked when a new session is discovered.
     * @param callback The function to call.
     */
    void set_session_callback(SessionCallback callback);

    /**
     * @brief Gets the properties of a stream by its SSRC.
     * @param ssrc The SSRC of the stream.
     * @param properties A reference to a `StreamProperties` struct to be filled.
     * @return true if the SSRC was found, false otherwise.
     */
    bool get_stream_properties(uint32_t ssrc, StreamProperties& properties);
    /**
     * @brief Gets the properties of a stream by its IP address.
     * @param ip The IP address of the stream source.
     * @param properties A reference to a `StreamProperties` struct to be filled.
     * @return true if the IP was found, false otherwise.
     */
    bool get_stream_properties_by_ip(const std::string& ip, StreamProperties& properties);
    /**
     * @brief Gets a list of all SSRCs discovered via SAP.
     * @return A vector of SSRC values.
     */
    std::vector<uint32_t> get_known_ssrcs();

    std::string logger_prefix_;
    /** @brief The main loop for the listener thread. */
    void run();
    /** @brief Sets up the multicast listening sockets. */
    bool setup_sockets();
    /** @brief Closes all listening sockets. */
    void close_sockets();
    /**
     * @brief Parses a received SAP packet.
     * @param buffer The packet data.
     * @param size The size of the packet data.
     * @param source_ip The source IP of the packet.
     */
    void process_sap_packet(const char* buffer, int size, const std::string& source_ip);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::vector<socket_t> sockets_;
    #ifdef _WIN32
        fd_set master_read_fds_;
        socket_t max_fd_ = NAR_INVALID_SOCKET_VALUE;
    #else
        int epoll_fd_ = -1;
    #endif

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