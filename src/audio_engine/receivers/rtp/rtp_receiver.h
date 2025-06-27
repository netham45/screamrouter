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
#include "../../audio_types.h"
#include <rtc/rtp.hpp>
#include "sap_listener.h"
#include <mutex>
#include <memory>
#include <cstdint>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

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
        TimeshiftManager* timeshift_manager
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
    int epoll_fd_;
    std::vector<socket_t> socket_fds_;
    std::mutex socket_fds_mutex_;
 
    /**
     * @brief Opens a new socket to receive a dynamic RTP session announced via SAP.
     * @param ip The IP address of the session.
     * @param port The port of the session.
     */
    void open_dynamic_session(const std::string& ip, int port);
    /**
     * @brief Handles changes in the SSRC of an RTP stream.
     * @param old_ssrc The old SSRC.
     * @param new_ssrc The new SSRC.
     */
    void handle_ssrc_changed(uint32_t old_ssrc, uint32_t new_ssrc);

    uint32_t last_known_ssrc_;
    bool ssrc_initialized_;

    std::vector<uint8_t> pcm_accumulator_;
    
    // Timing information for the chunk currently being accumulated
    bool is_accumulating_chunk_;
    std::chrono::steady_clock::time_point chunk_first_packet_received_time_;
    uint32_t chunk_first_packet_rtp_timestamp_;
    uint32_t last_rtp_timestamp_ = 0;
    uint32_t last_chunk_remainder_samples_ = 0;

    std::unique_ptr<SapListener> sap_listener_;
};

} // namespace audio
} // namespace screamrouter

#endif // RTP_RECEIVER_H
