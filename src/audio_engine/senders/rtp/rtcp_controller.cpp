/**
 * @file rtcp_controller.cpp
 * @brief Implementation of the RtcpController class.
 */
#include "rtcp_controller.h"
#include "../../utils/cpp_logger.h"
#include <cstring>
#include <chrono>

#ifndef _WIN32
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
#endif

namespace screamrouter {
namespace audio {

RtcpController::RtcpController(int time_sync_delay_ms)
    : rtcp_thread_running_(false),
      time_sync_delay_ms_(time_sync_delay_ms) {
    LOG_CPP_INFO("[RtcpController] Initialized with time_sync_delay_ms=%d", time_sync_delay_ms_);
}

RtcpController::~RtcpController() noexcept {
    stop();
}

void RtcpController::add_stream(const StreamInfo& info) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    ManagedStream stream;
    stream.info = info;
    stream.rtcp_socket = PLATFORM_INVALID_SOCKET;
    stream.stream_start_time = std::chrono::system_clock::now();
    stream.stream_start_rtp_timestamp = 0; // Will be updated from actual RTP stream
    
    if (setup_rtcp_socket(stream)) {
        streams_.push_back(stream);
        LOG_CPP_INFO("[RtcpController] Added stream %s (SSRC=0x%08X) for %s:%d",
                    info.stream_id.c_str(), info.ssrc, 
                    info.dest_ip.c_str(), info.rtcp_port);
    } else {
        LOG_CPP_ERROR("[RtcpController] Failed to setup RTCP socket for stream %s",
                     info.stream_id.c_str());
    }
}

void RtcpController::remove_stream(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = std::remove_if(streams_.begin(), streams_.end(),
                            [&stream_id](const ManagedStream& s) {
                                return s.info.stream_id == stream_id;
                            });
    
    for (auto i = it; i != streams_.end(); ++i) {
        close_rtcp_socket(*i);
        LOG_CPP_INFO("[RtcpController] Removed stream %s", i->info.stream_id.c_str());
    }
    
    streams_.erase(it, streams_.end());
}

bool RtcpController::start() {
    if (rtcp_thread_running_) {
        LOG_CPP_WARNING("[RtcpController] Already running");
        return true;
    }
    
    LOG_CPP_INFO("[RtcpController] Starting RTCP thread");
    rtcp_thread_running_ = true;
    rtcp_thread_ = std::thread(&RtcpController::rtcp_thread_loop, this);
    
    return true;
}

void RtcpController::stop() {
    if (!rtcp_thread_running_) {
        return;
    }
    
    LOG_CPP_INFO("[RtcpController] Stopping RTCP thread");
    rtcp_thread_running_ = false;
    
    if (rtcp_thread_.joinable()) {
        rtcp_thread_.join();
    }
    
    // Close all RTCP sockets
    std::lock_guard<std::mutex> lock(streams_mutex_);
    for (auto& stream : streams_) {
        close_rtcp_socket(stream);
    }
    streams_.clear();
    
    LOG_CPP_INFO("[RtcpController] RTCP thread stopped");
}

void RtcpController::rtcp_thread_loop() {
    LOG_CPP_INFO("[RtcpController] RTCP thread started");
    
    auto last_sr_time = std::chrono::steady_clock::now();
    const auto sr_interval = std::chrono::seconds(5); // Send SR every 5 seconds
    
    while (rtcp_thread_running_) {
        auto now = std::chrono::steady_clock::now();
        
        // Send periodic Sender Reports
        if (now - last_sr_time >= sr_interval) {
            send_sr_packets();
            last_sr_time = now;
        }
        
        // Small sleep to prevent busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG_CPP_INFO("[RtcpController] RTCP thread exiting");
}

void RtcpController::send_sr_packets() {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    if (streams_.empty()) {
        return;
    }
    
    LOG_CPP_DEBUG("[RtcpController] Sending SR packets for %zu streams", streams_.size());
    
    for (const auto& stream : streams_) {
        if (stream.rtcp_socket != PLATFORM_INVALID_SOCKET) {
            send_rtcp_sr(stream);
        }
    }
}

void RtcpController::send_rtcp_sr(const ManagedStream& stream) {
    // RTCP Sender Report packet structure
    struct rtcp_sr {
        uint8_t version_p_rc;       // Version (2 bits), Padding (1 bit), Reception report count (5 bits)
        uint8_t packet_type;         // Packet type (SR = 200)
        uint16_t length;             // Length in 32-bit words minus one
        uint32_t ssrc;               // Synchronization source identifier
        uint32_t ntp_timestamp_msw;  // NTP timestamp, most significant word
        uint32_t ntp_timestamp_lsw;  // NTP timestamp, least significant word
        uint32_t rtp_timestamp;      // RTP timestamp
        uint32_t packet_count;       // Sender's packet count
        uint32_t octet_count;        // Sender's octet count
    };
    
    struct rtcp_sr sr;
    memset(&sr, 0, sizeof(sr));
    
    // Fill RTCP header
    sr.version_p_rc = 0x80;  // Version = 2, Padding = 0, RC = 0
    sr.packet_type = 200;     // SR packet type
    sr.length = htons(6);     // Length = 6 (7 words - 1)
    
    // Fill sender info
    sr.ssrc = htonl(stream.info.ssrc);
    
    // Get NTP timestamp with delay
    uint64_t ntp_ts = get_ntp_timestamp_with_delay();
    uint32_t ntp_seconds = (ntp_ts >> 32) & 0xFFFFFFFF;
    uint32_t ntp_fraction = ntp_ts & 0xFFFFFFFF;
    sr.ntp_timestamp_msw = htonl(ntp_seconds);
    sr.ntp_timestamp_lsw = htonl(ntp_fraction);
    
    uint32_t packet_count_value = 0;
    uint64_t octet_count_value = 0;
    uint32_t rtp_timestamp = stream.stream_start_rtp_timestamp;

    // Get current RTP timestamp and statistics from the sender
    if (stream.info.sender) {
        stream.info.sender->get_statistics(packet_count_value, octet_count_value);
        
        // For RTP timestamp, we'd need to calculate based on elapsed time
        // This is simplified - in production, you'd track the actual RTP timestamps
        auto elapsed = std::chrono::system_clock::now() - stream.stream_start_time;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        rtp_timestamp = stream.stream_start_rtp_timestamp + static_cast<uint32_t>(elapsed_ms * 48); // 48kHz sample rate
        
        sr.rtp_timestamp = htonl(rtp_timestamp);
        sr.packet_count = htonl(packet_count_value);
        sr.octet_count = htonl(static_cast<uint32_t>(octet_count_value));
    } else {
        sr.rtp_timestamp = htonl(rtp_timestamp);
    }
    
    // Send the RTCP packet
#ifdef _WIN32
    int sent_bytes = sendto(stream.rtcp_socket,
                           reinterpret_cast<const char*>(&sr),
                           sizeof(sr),
                           0,
                           (struct sockaddr*)&stream.rtcp_dest_addr,
                           sizeof(stream.rtcp_dest_addr));
#else
    int sent_bytes = sendto(stream.rtcp_socket,
                           &sr,
                           sizeof(sr),
                           0,
                           (struct sockaddr*)&stream.rtcp_dest_addr,
                           sizeof(stream.rtcp_dest_addr));
#endif
    
    if (sent_bytes < 0) {
        LOG_CPP_ERROR("[RtcpController] Failed to send RTCP SR for stream %s",
                     stream.info.stream_id.c_str());
    } else if (sent_bytes != static_cast<int>(sizeof(sr))) {
        LOG_CPP_WARNING("[RtcpController] Partial RTCP SR send for stream %s: %d/%zu bytes",
                        stream.info.stream_id.c_str(), sent_bytes, sizeof(sr));
    } else {
        LOG_CPP_INFO(
            "[RtcpController] Sent RTCP SR (%d bytes) stream=%s SSRC=0x%08X -> %s:%d | NTP=0x%016llX RTP=%u packets=%u octets=%llu",
            sent_bytes,
            stream.info.stream_id.c_str(),
            stream.info.ssrc,
            stream.info.dest_ip.c_str(),
            stream.info.rtcp_port,
            static_cast<unsigned long long>(ntp_ts),
            rtp_timestamp,
            packet_count_value,
            static_cast<unsigned long long>(octet_count_value));
    }
}

uint64_t RtcpController::get_ntp_timestamp_with_delay() {
    // Get current time
    auto now = std::chrono::system_clock::now();
    
    // Add delay if configured
    if (time_sync_delay_ms_ != 0) {
        now += std::chrono::milliseconds(time_sync_delay_ms_);
    }
    
    // Convert to microseconds since Unix epoch
    uint64_t unix_time_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()
        ).count()
    );
    
    // NTP epoch is January 1, 1900, Unix epoch is January 1, 1970
    // The difference is 70 years (2208988800 seconds)
    const uint64_t NTP_UNIX_EPOCH_DIFF = 2208988800ULL;
    
    // Convert microseconds to seconds and fraction
    uint64_t seconds = (unix_time_us / 1000000) + NTP_UNIX_EPOCH_DIFF;
    uint64_t microseconds = unix_time_us % 1000000;
    
    // Convert fraction to NTP format (32-bit fraction of a second)
    uint64_t fraction = (microseconds * 4294967296ULL) / 1000000ULL;
    
    // Combine seconds (upper 32 bits) and fraction (lower 32 bits)
    uint64_t ntp_timestamp = (seconds << 32) | (fraction & 0xFFFFFFFF);
    
    return ntp_timestamp;
}

bool RtcpController::setup_rtcp_socket(ManagedStream& stream) {
    stream.rtcp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (stream.rtcp_socket == PLATFORM_INVALID_SOCKET) {
        LOG_CPP_ERROR("[RtcpController] Failed to create RTCP socket for stream %s",
                     stream.info.stream_id.c_str());
        return false;
    }
    
#ifndef _WIN32
    // Set socket priority for low latency on Linux
    int priority = 6; // AC_VO
    if (setsockopt(stream.rtcp_socket, SOL_SOCKET, SO_PRIORITY, 
                  &priority, sizeof(priority)) < 0) {
        LOG_CPP_WARNING("[RtcpController] Failed to set socket priority for stream %s",
                       stream.info.stream_id.c_str());
    }
#endif
    
    // Set up destination address
    memset(&stream.rtcp_dest_addr, 0, sizeof(stream.rtcp_dest_addr));
    stream.rtcp_dest_addr.sin_family = AF_INET;
    stream.rtcp_dest_addr.sin_port = htons(stream.info.rtcp_port);
    
    if (inet_pton(AF_INET, stream.info.dest_ip.c_str(), 
                 &stream.rtcp_dest_addr.sin_addr) <= 0) {
        LOG_CPP_ERROR("[RtcpController] Invalid IP address for stream %s: %s",
                     stream.info.stream_id.c_str(), stream.info.dest_ip.c_str());
        platform_close_socket(stream.rtcp_socket);
        stream.rtcp_socket = PLATFORM_INVALID_SOCKET;
        return false;
    }
    
    LOG_CPP_DEBUG("[RtcpController] Setup RTCP socket for stream %s -> %s:%d",
                 stream.info.stream_id.c_str(), 
                 stream.info.dest_ip.c_str(),
                 stream.info.rtcp_port);
    
    return true;
}

void RtcpController::close_rtcp_socket(ManagedStream& stream) {
    if (stream.rtcp_socket != PLATFORM_INVALID_SOCKET) {
        platform_close_socket(stream.rtcp_socket);
        stream.rtcp_socket = PLATFORM_INVALID_SOCKET;
        LOG_CPP_DEBUG("[RtcpController] Closed RTCP socket for stream %s",
                     stream.info.stream_id.c_str());
    }
}

} // namespace audio
} // namespace screamrouter
