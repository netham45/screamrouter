#ifndef RTP_RECEIVER_H
#define RTP_RECEIVER_H

#include "../network_audio_receiver.h" // Base class
#include "../../audio_types.h"            // For RtpReceiverConfig, TaggedAudioPacket etc.
#include <rtc/rtp.hpp>              // libdatachannel RTP header
#include "sap_listener.h"
#include <mutex>                    // For std::mutex
#include <memory>
#include <cstdint>                  // For uint32_t
#include <sys/epoll.h>              // For epoll_create1, epoll_ctl, epoll_wait
 
 // POSIX socket includes (some might be in network_audio_receiver.h but good to be explicit)
 #include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>


namespace screamrouter {
namespace audio {

class RtpReceiver : public NetworkAudioReceiver {
public:
    RtpReceiver(
        RtpReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager
    );

    ~RtpReceiver() noexcept override;

    // Deleted copy and move constructors/assignments
    RtpReceiver(const RtpReceiver&) = delete;
    RtpReceiver& operator=(const RtpReceiver&) = delete;
    RtpReceiver(RtpReceiver&&) = delete;
    RtpReceiver& operator=(RtpReceiver&&) = delete;

    // Global oRTP init/deinit are removed

protected:
    // Override base class methods
    void run() override; // Core receiving loop using oRTP
    bool setup_socket() override; // oRTP session setup replaces manual socket creation
    void close_socket() override; // oRTP session destruction handles socket closing

    // These methods become less relevant or dummied out with oRTP handling packet validation
    bool is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) override;
    
    bool process_and_validate_payload(
        const uint8_t* buffer,
        int size,
        const struct sockaddr_in& client_addr,
        std::chrono::steady_clock::time_point received_time,
        TaggedAudioPacket& out_packet,
        std::string& out_source_tag
    ) override;
    
    // These might still be used by base class if run() calls parts of base logic, or can be dummied.
    // For oRTP, buffer size is managed internally by oRTP.
    size_t get_receive_buffer_size() const override; 
    // Poll timeout is handled by rtp_session_recvm_with_ts blocking nature.
    int get_poll_timeout_ms() const override; 

private:
    RtpReceiverConfig config_;
    // session_ and profile_ (oRTP specific) are removed.
    // socket_fd_ from NetworkAudioReceiver is no longer used; we use a vector of fds.
    int epoll_fd_;
    std::vector<socket_t> socket_fds_;
    std::mutex socket_fds_mutex_;
 
      // Static members for global oRTP initialization are removed.
 
    void open_dynamic_session(const std::string& ip, int port);
    // oRTP SSRC changed callback is removed.
    // handle_ssrc_changed will be called directly.
    void handle_ssrc_changed(uint32_t old_ssrc, uint32_t new_ssrc);

    uint32_t last_known_ssrc_;
    bool ssrc_initialized_;

    // --- New members for PCM buffering ---
    std::vector<uint8_t> pcm_accumulator_;
    std::chrono::steady_clock::time_point last_rtp_packet_timestamp_;
    // --- End new members ---
    std::unique_ptr<SapListener> sap_listener_;

    // Helper method specific to RTP processing (might be removed or adapted)
    // bool is_valid_rtp_header_payload(const uint8_t* buffer, int size); // Likely obsolete
};

} // namespace audio
} // namespace screamrouter

#endif // RTP_RECEIVER_H
