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
#include <deque>
#include <map>
#include <optional>

#include "../utils/audio_component.h"
#include "../utils/thread_safe_queue.h"
#include "../audio_types.h"
#include "clock_manager.h"

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
        ClockManager* clock_manager = nullptr,
        std::size_t chunk_size_bytes = 0
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

    /** @brief Allows derived classes to trigger clock servicing when using custom loops. */
    void service_clock_manager();

    /**
     * @brief Queues a packet for clock managed dispatch, falling back to direct dispatch on failure.
     * @return true if the packet was queued for scheduled dispatch, false if it was forwarded immediately.
     */
    bool enqueue_clock_managed_packet(TaggedAudioPacket&& packet);

    struct PcmAppendContext {
        std::string accumulator_key;   ///< Identifier for the accumulator (e.g. SSRC or composite tag)
        std::string source_tag;        ///< Final tag used for dispatching downstream
        std::vector<uint8_t> payload;  ///< PCM payload fragment to append
        int sample_rate = 0;
        int channels = 0;
        int bit_depth = 0;
        uint8_t chlayout1 = 0;
        uint8_t chlayout2 = 0;
        std::vector<uint32_t> ssrcs;   ///< SSRC/CSRC list associated with the fragment
        std::chrono::steady_clock::time_point received_time{};
        std::optional<uint32_t> rtp_timestamp;
    };

    /**
     * @brief Appends PCM data to an accumulator and returns any completed chunks.
     */
    std::vector<TaggedAudioPacket> append_pcm_payload(PcmAppendContext&& context);

    /** @brief Clears a specific PCM accumulator. */
    void reset_pcm_accumulator(const std::string& accumulator_key);

    /** @brief Clears all PCM accumulators. */
    void reset_all_pcm_accumulators();

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

    ClockManager* clock_manager_;
    std::size_t chunk_size_bytes_;

    std::set<std::string> known_source_tags_;
    std::mutex known_tags_mutex_;

    std::vector<std::string> seen_tags_;
    std::mutex seen_tags_mutex_;

    std::string logger_prefix_;

    struct ClockStreamState {
        std::string source_tag;
        int sample_rate = 0;
        int channels = 0;
        int bit_depth = 0;
        uint8_t chlayout1 = 0;
        uint8_t chlayout2 = 0;
        uint32_t samples_per_chunk = 0;
        uint32_t next_rtp_timestamp = 0;
        std::vector<uint32_t> last_ssrcs;
        ClockManager::ConditionHandle clock_handle;
        uint64_t clock_last_sequence = 0;
        std::deque<TaggedAudioPacket> pending_packets;
        double current_playback_rate = 1;
    };

    struct PcmAccumulatorState {
        std::vector<uint8_t> buffer;
        bool chunk_active = false;
        std::chrono::steady_clock::time_point first_packet_time{};
        std::optional<uint32_t> first_packet_rtp_timestamp;
        int last_sample_rate = 0;
        int last_channels = 0;
        int last_bit_depth = 0;
        uint8_t last_chlayout1 = 0;
        uint8_t last_chlayout2 = 0;
    };

    std::map<std::string, std::shared_ptr<ClockStreamState>> stream_states_;
    std::mutex stream_state_mutex_;

    std::map<std::string, PcmAccumulatorState> pcm_accumulators_;
    std::mutex pcm_accumulator_mutex_;

    std::shared_ptr<ClockStreamState> get_or_create_stream_state_locked(const TaggedAudioPacket& packet);
    void clear_clock_managed_streams();
    void handle_clock_tick(const std::string& source_tag);
    uint32_t calculate_samples_per_chunk(int channels, int bit_depth) const;
    void maybe_log_telemetry();
    std::chrono::steady_clock::time_point telemetry_last_log_time_{};

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
