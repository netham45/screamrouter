#ifndef NETWORK_AUDIO_RECEIVER_H
#define NETWORK_AUDIO_RECEIVER_H

// Standard library includes first
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>

// Project headers
#include "../utils/audio_component.h"
#include "../utils/thread_safe_queue.h" // Ensure this is included at the top level, after std libs
#include "../audio_types.h" // For NewSourceNotification, TaggedAudioPacket, TimeshiftManager

// Platform-specific socket includes
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
    using socket_t = SOCKET;
    #define NAR_INVALID_SOCKET_VALUE INVALID_SOCKET
    #define NAR_GET_LAST_SOCK_ERROR WSAGetLastError()
    #define NAR_POLL WSAPoll
#else // POSIX
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h> // For close
    #include <poll.h>   // For poll
    #include <errno.h>
    using socket_t = int;
    #define NAR_INVALID_SOCKET_VALUE -1
    #define NAR_GET_LAST_SOCK_ERROR errno
    #define NAR_POLL poll
#endif

namespace screamrouter {
namespace audio {

// Forward declaration
class TimeshiftManager; // Forward declare

// Note: Including thread_safe_queue.h which defines the ThreadSafeQueue in the screamrouter::audio::utils namespace
using NotificationQueue = screamrouter::audio::utils::ThreadSafeQueue<NewSourceNotification>;
// TaggedAudioPacket is defined in audio_types.h

class NetworkAudioReceiver : public AudioComponent {
public:
    NetworkAudioReceiver(
        uint16_t listen_port,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager,
        std::string logger_prefix
    );
    virtual ~NetworkAudioReceiver() noexcept;

    // --- AudioComponent Interface ---
    void start() override;
    void stop() override;

    std::vector<std::string> get_seen_tags();

protected:
    // --- AudioComponent Interface ---
    void run() override; // The main thread loop

    // --- Pure Virtual Methods for Derived Classes ---
    /**
     * @brief Performs basic structural validation of the received packet.
     * E.g., checks size for Raw/PerProcess, or minimum size for RTP header.
     * @param buffer Pointer to the start of the received UDP payload.
     * @param size Size of the received payload.
     * @param client_addr The address of the client that sent the packet.
     * @return true if the packet structure is initially valid, false otherwise.
     */
    virtual bool is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) = 0;

    /**
     * @brief Processes the payload of a structurally valid packet.
     * This method should parse the packet type (RTP, Raw Scream, Per-Process Scream),
     * extract the source_tag, populate the out_packet with audio data and format.
     * @param buffer Pointer to the start of the received UDP payload.
     * @param size Size of the received payload.
     * @param client_addr The address of the client that sent the packet.
     * @param received_time The time the packet was received.
     * @param out_packet Reference to a TaggedAudioPacket to be populated.
     * @param out_source_tag Reference to a string to store the extracted source tag.
     * @return true if the payload is successfully parsed and validated, false otherwise.
     */
    virtual bool process_and_validate_payload(
        const uint8_t* buffer,
        int size,
        const struct sockaddr_in& client_addr,
        std::chrono::steady_clock::time_point received_time,
        TaggedAudioPacket& out_packet,
        std::string& out_source_tag
    ) = 0;

    /**
     * @brief Gets the recommended size for the receive buffer.
     * @return The size of the receive buffer.
     */
    virtual size_t get_receive_buffer_size() const = 0;

    /**
     * @brief Gets the timeout value for the poll() call in milliseconds.
     * @return The poll timeout in milliseconds.
     */
    virtual int get_poll_timeout_ms() const = 0;


    // --- Common Helper Methods ---
    virtual bool setup_socket();
    virtual void close_socket();
    void log_message(const std::string& msg);
    void log_error(const std::string& msg);
    void log_warning(const std::string& msg);

    // --- Common Data Members ---
    uint16_t listen_port_;
    socket_t socket_fd_;
    std::shared_ptr<NotificationQueue> notification_queue_;
    TimeshiftManager* timeshift_manager_;

    std::set<std::string> known_source_tags_;
    std::mutex known_tags_mutex_;

    std::vector<std::string> seen_tags_;
    std::mutex seen_tags_mutex_;

    std::string logger_prefix_;

private:
    // --- Winsock Initialization Management (Windows specific) ---
    #ifdef _WIN32
        static std::atomic<int> winsock_user_count_;
        static void increment_winsock_users();
        static void decrement_winsock_users();
    #endif
};

} // namespace audio
} // namespace screamrouter

#endif // NETWORK_AUDIO_RECEIVER_H
