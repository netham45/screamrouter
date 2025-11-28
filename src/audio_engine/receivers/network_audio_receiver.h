/**
 * @file network_audio_receiver.h
 * @brief Defines the base class for all network audio receivers.
 * @details This file contains the abstract base class `NetworkAudioReceiver`, which provides
 *          the core functionality for receiving audio packets from the network. It handles
 *          socket setup, the main receive loop, and management of seen source tags.
 *          Derived classes must implement the protocol-specific logic for packet validation
 *          and proces1g.
 */
#ifndef NETWORK_AUDIO_RECEIVER_H
#define NETWORK_AUDIO_RECEIVER_H

#include <string>
#include <vector>
#include <set>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <map>
#include <optional>
#include <unordered_map>

#include "../utils/audio_component.h"
#include "../utils/thread_safe_queue.h"
#include "../utils/byte_ring_buffer.h"
#include "../audio_types.h"
#include "../configuration/audio_engine_settings.h"

// Platform-specific socket includes and type definitions
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
    #include <unistd.h>
    #include <poll.h>
    #include <errno.h>
    using socket_t = int;
    #define NAR_INVALID_SOCKET_VALUE -1
    #define NAR_GET_LAST_SOCK_ERROR errno
    #define NAR_POLL poll
#endif

namespace screamrouter {
namespace audio {

class TimeshiftManager;

using NotificationQueue = screamrouter::audio::utils::ThreadSafeQueue<DeviceDiscoveryNotification>;

/**
 * @class NetworkAudioReceiver
 * @brief An abstract base class for components that receive audio from the network.
 * @details This class implements the `AudioComponent` interface and provides a common
 *          framework for receiving UDP packets. It manages a socket, runs a receive
 *          loop in a dedicated thread, and forwards received packets to a `TimeshiftManager`.
 *          Derived classes must implement the protocol-specific parsing and validation logic.
 */
class NetworkAudioReceiver : public AudioComponent {
public:
    /**
     * @brief Constructs a NetworkAudioReceiver.
     * @param listen_port The UDP port to listen on.
     * @param notification_queue A queue for sending notifications about new sources.
     * @param timeshift_manager A pointer to the `TimeshiftManager` to which packets will be sent.
     * @param logger_prefix A prefix string for log messages.
     */
    NetworkAudioReceiver(
        uint16_t listen_port,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager,
        std::string logger_prefix,
        std::size_t base_frames_per_chunk_mono16 = kDefaultBaseFramesPerChunkMono16
    );
    /**
     * @brief Virtual destructor.
     */
    virtual ~NetworkAudioReceiver() noexcept;

    /** @brief Starts the receiver's processing thread. */
    void start() override;
    /** @brief Stops the receiver's processing thread. */
    void stop() override;

    /**
     * @brief Gets the set of source tags observed since the last call and clears the cache.
     * @return A vector of strings, each representing a source tag.
     */
    std::vector<std::string> get_seen_tags();

protected:
    /** @brief The main processing loop for the receiver thread. */
    void run() override;

    /** @brief Hook invoked immediately before the thread blocks in poll/select. */
    virtual void on_before_poll_wait();

    /** @brief Hook invoked once at the end of each poll iteration (including timeouts or errors). */
    virtual void on_after_poll_iteration();

    // --- Pure Virtual Methods for Derived Classes ---
    /**
     * @brief Performs basic structural validation of a received packet.
     * @param buffer Pointer to the start of the received UDP payload.
     * @param size Size of the received payload.
     * @param client_addr The address of the client that sent the packet.
     * @return true if the packet structure is initially valid, false otherwise.
     */
    virtual bool is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) = 0;

    /**
     * @brief Processes the payload of a structurally valid packet.
     * @param buffer Pointer to the start of the received UDP payload.
     * @param size Size of the received payload.
     * @param client_addr The address of the client that sent the packet.
     * @param received_time The time the packet was received.
     * @param out_packet Reference to a `TaggedAudioPacket` to be populated.
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
     * @brief Called when a packet has been validated and is ready for dispatch.
     * @param packet The packet to dispatch.
     * @note Default implementation forwards directly to TimeshiftManager.
     */
    virtual void dispatch_ready_packet(TaggedAudioPacket&& packet);

    bool register_source_tag(const std::string& tag);

    /**
     * @brief Gets the recommended size for the receive buffer.
     * @return The size of the receive buffer in bytes.
     */
    virtual size_t get_receive_buffer_size() const = 0;

    /**
     * @brief Gets the timeout value for the `poll()` call.
     * @return The poll timeout in milliseconds.
     */
    virtual int get_poll_timeout_ms() const = 0;


    // --- Common Helper Methods ---
    /** @brief Sets up the UDP socket for listening. */
    virtual bool setup_socket();
    /** @brief Closes the UDP socket. */
    virtual void close_socket();
    void log_message(const std::string& msg);
    void log_error(const std::string& msg);
    void log_warning(const std::string& msg);

    // --- Common Data Members ---
    uint16_t listen_port_;
    socket_t socket_fd_;
    std::shared_ptr<NotificationQueue> notification_queue_;
    TimeshiftManager* timeshift_manager_;

    const std::size_t base_frames_per_chunk_mono16_;
    const std::size_t default_chunk_size_bytes_;

    std::set<std::string> known_source_tags_;
    std::mutex known_tags_mutex_;

    std::vector<std::string> seen_tags_;
    std::mutex seen_tags_mutex_;

    std::string logger_prefix_;

    struct SourceAccumulator {
        ::screamrouter::audio::utils::ByteRingBuffer buffer;
        std::chrono::steady_clock::time_point first_received{};
        std::chrono::steady_clock::time_point last_delivery{};
        bool has_last_delivery = false;
        std::optional<uint32_t> base_rtp_timestamp;
        uint64_t frame_cursor = 0;
        int channels = 0;
        int sample_rate = 0;
        int bit_depth = 0;
        uint8_t chlayout1 = 0;
        uint8_t chlayout2 = 0;
        std::size_t chunk_bytes = 0;
        std::size_t bytes_per_frame = 0;
        std::vector<uint32_t> ssrcs;
    };

    std::unordered_map<std::string, SourceAccumulator> accumulators_;
    std::mutex accumulator_mutex_;

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
