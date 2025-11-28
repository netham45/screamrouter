/**
 * @file rtcp_controller.h
 * @brief Defines the RtcpController class for managing RTCP for multiple RTP streams.
 * @details This file contains the RtcpController class which manages sending RTCP
 *          Sender Report (SR) packets for all active streams to provide synchronization
 *          information (NTP timestamps).
 */
#pragma once

#include "rtp_sender_core.h"
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

namespace screamrouter {
namespace audio {

/**
 * @class RtcpController
 * @brief Manages RTCP for multiple RTP streams.
 * @details This class handles sending periodic RTCP Sender Reports for
 *          multiple RTP streams to provide synchronization information.
 *          This is a simplified implementation focusing on SR packets.
 */
class RtcpController {
public:
    /**
     * @brief Constructs an RtcpController.
     * @param time_sync_delay_ms Time synchronization delay in milliseconds.
     */
    explicit RtcpController(int time_sync_delay_ms = 0);
    
    /**
     * @brief Destructor.
     */
    ~RtcpController() noexcept;
    
    /**
     * @struct StreamInfo
     * @brief Information about a single RTP stream for RTCP management.
     */
    struct StreamInfo {
        std::string stream_id;
        std::string dest_ip;
        uint16_t rtcp_port;  // RTP port + 1
        uint32_t ssrc;
        RtpSenderCore* sender;  // Non-owning pointer
    };
    
    /**
     * @brief Adds a stream to be managed by this controller.
     * @param info Information about the stream to add.
     */
    void add_stream(const StreamInfo& info);
    
    /**
     * @brief Starts the RTCP thread for sending periodic reports.
     * @return true if started successfully, false otherwise.
     */
    bool start();
    
    /**
     * @brief Stops the RTCP thread.
     */
    void stop();
    
    /**
     * @brief Checks if the RTCP controller is running.
     * @return true if running, false otherwise.
     */
    bool is_running() const { return rtcp_thread_running_.load(); }

private:
    /**
     * @struct ManagedStream
     * @brief Internal representation of a managed stream.
     */
    struct ManagedStream {
        StreamInfo info;
        socket_t rtcp_socket;
        struct sockaddr_in rtcp_dest_addr;
        std::chrono::system_clock::time_point stream_start_time;
        uint32_t stream_start_rtp_timestamp;
    };
    
    std::vector<ManagedStream> streams_;
    std::mutex streams_mutex_;
    
    std::thread rtcp_thread_;
    std::atomic<bool> rtcp_thread_running_;
    int time_sync_delay_ms_;
    
    /**
     * @brief The main RTCP thread loop.
     */
    void rtcp_thread_loop();
    
    /**
     * @brief Sends RTCP SR packets for all managed streams.
     */
    void send_sr_packets();
    
    /**
     * @brief Sends an RTCP SR packet for a specific stream.
     * @param stream The stream to send the SR for.
     */
    void send_rtcp_sr(const ManagedStream& stream);
    
    /**
     * @brief Gets the current NTP timestamp with optional delay.
     * @return 64-bit NTP timestamp.
     */
    uint64_t get_ntp_timestamp_with_delay();
    
    /**
     * @brief Sets up an RTCP socket for a stream.
     * @param stream The stream to set up the socket for.
     * @return true if successful, false otherwise.
     */
    bool setup_rtcp_socket(ManagedStream& stream);
    
    /**
     * @brief Closes an RTCP socket.
     * @param stream The stream whose socket to close.
     */
    void close_rtcp_socket(ManagedStream& stream);
};

} // namespace audio
} // namespace screamrouter
