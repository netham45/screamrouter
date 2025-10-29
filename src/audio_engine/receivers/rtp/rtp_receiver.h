/**
 * @file rtp_receiver.h
 * @brief Defines the RtpReceiver class for handling RTP audio streams.
 * @details This file contains the definition of the `RtpReceiver` class, which is a
 *          specialization of `NetworkAudioReceiver` for the Real-time Transport Protocol (RTP).
 *          It uses `libdatachannel` for RTP packet handling and also includes a `SapListener`
 *          to discover streams via the Session Announcement Protocol (SAP).
 */
#ifndef RTP_RECEIVER_H
#define RTP_RECEIVER_H

#include "../network_audio_receiver.h"
#include "../clock_manager.h"
#include "../../audio_types.h"
#include <rtc/rtp.hpp>
#include "sap_listener.h"
#include "rtp_reordering_buffer.h"
#include <mutex>
#include <memory>
#include <vector>
#include <map>
#include <cstdint>
#ifndef _WIN32
    #include <sys/epoll.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

namespace screamrouter {
namespace audio {

/**
 * @class RtpReceiver
 * @brief A network receiver specialized for handling RTP audio streams.
 * @details This class inherits from `NetworkAudioReceiver` and implements the logic
 *          for receiving and processing RTP packets. It uses an epoll-based loop to
 *          manage multiple sockets for both RTP data and SAP announcements.
 */
class RtpReceiver : public NetworkAudioReceiver {
public:
    /**
     * @brief Constructs an RtpReceiver.
     * @param config The configuration for the RTP receiver.
     * @param notification_queue A queue for sending notifications about new sources.
     * @param timeshift_manager A pointer to the `TimeshiftManager` for packet buffering.
     */
    RtpReceiver(
        RtpReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager,
        ClockManager* clock_manager
    );

    /**
     * @brief Destructor.
     */
    ~RtpReceiver() noexcept override;

    // Deleted copy and move constructors/assignments to prevent unintended copies.
    RtpReceiver(const RtpReceiver&) = delete;
    RtpReceiver& operator=(const RtpReceiver&) = delete;
    RtpReceiver(RtpReceiver&&) = delete;
    RtpReceiver& operator=(RtpReceiver&&) = delete;

    /**
     * @brief Retrieves the currently known SAP announcements processed by this receiver.
     * @return A vector of SAP announcements containing stream metadata.
     */
    std::vector<SapAnnouncement> get_sap_announcements();

protected:
    /** @brief The main processing loop using epoll to handle multiple sockets. */
    void run() override;
    /** @brief Sets up the initial listening socket and epoll instance. */
    bool setup_socket() override;
    /** @brief Closes all managed sockets and the epoll instance. */
    void close_socket() override;

    /** @brief Validates the basic structure of an RTP packet. */
    bool is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) override;
    
    /** @brief Processes a valid RTP packet, extracting audio data and metadata. */
    bool process_and_validate_payload(
        const uint8_t* buffer,
        int size,
        const struct sockaddr_in& client_addr,
        std::chrono::steady_clock::time_point received_time,
        TaggedAudioPacket& out_packet,
        std::string& out_source_tag
    ) override;
    
    /** @brief Returns the recommended receive buffer size. */
    size_t get_receive_buffer_size() const override;
    /** @brief Returns the poll timeout for the epoll loop. */
    int get_poll_timeout_ms() const override;

private:
    RtpReceiverConfig config_;
    const std::size_t chunk_size_bytes_;
    #ifdef _WIN32
        fd_set master_read_fds_;  // Master set for select()
        socket_t max_fd_;          // Highest socket fd for select()
    #else
        int epoll_fd_;             // Linux epoll descriptor
    #endif
    std::vector<socket_t> socket_fds_;
    std::mutex socket_fds_mutex_;
 
    /**
     * @brief Opens a new socket to receive a dynamic RTP session announced via SAP.
     * @param ip The IP address of the session.
     * @param port The port of the session.
     * @param source_ip The source IP for unicast streams (empty for multicast).
     */
    void open_dynamic_session(const std::string& ip, int port, const std::string& source_ip = "");
    /**
     * @brief Handles changes in the SSRC of an RTP stream.
     * @param old_ssrc The old SSRC.
     * @param new_ssrc The new SSRC.
     * @param source_key The source identifier (IP:port).
     */
    void handle_ssrc_changed(uint32_t old_ssrc, uint32_t new_ssrc, const std::string& source_key);
    /** @brief Processes packets that are ready from the reordering buffer. */
    void process_ready_packets(uint32_t ssrc, const struct sockaddr_in& client_addr);
    /** @brief Internal version of process_ready_packets that can optionally skip locking. */
    void process_ready_packets_internal(uint32_t ssrc, const struct sockaddr_in& client_addr, bool take_lock);
    
    /**
     * @brief Generates a unique key for identifying a source.
     * @param addr The socket address of the source.
     * @return A string in the format "IP:port".
     */
    std::string get_source_key(const struct sockaddr_in& addr) const;
    std::string make_pcm_accumulator_key(uint32_t ssrc) const;

    // Per-source SSRC tracking to handle multiple independent RTP streams
    std::map<std::string, uint32_t> source_to_last_ssrc_;  // Map: "IP:port" -> last known SSRC
    std::mutex source_ssrc_mutex_;  // Protects source_to_last_ssrc_

    // Jitter and reordering handling
    std::map<uint32_t, RtpReorderingBuffer> reordering_buffers_;
    std::mutex reordering_buffer_mutex_;

    std::unique_ptr<SapListener> sap_listener_;

    // Track unicast sessions by source IP -> socket mapping
    struct SessionInfo {
        socket_t socket_fd;
        std::string destination_ip;
        int port;
        std::string source_ip; // Empty for multicast, specific IP for unicast
    };
    std::map<socket_t, SessionInfo> socket_sessions_; // Maps socket FD to session info
    std::map<std::string, socket_t> unicast_source_to_socket_; // Maps "source_ip:dest_ip:port" to socket FD

    void maybe_log_telemetry();
    std::chrono::steady_clock::time_point telemetry_last_log_time_{};
};

} // namespace audio
} // namespace screamrouter

#endif // RTP_RECEIVER_H
