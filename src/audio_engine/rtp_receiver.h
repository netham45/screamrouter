#ifndef RTP_RECEIVER_H
#define RTP_RECEIVER_H

#include "network_audio_receiver.h" // Base class
#include "audio_types.h"            // For RtpReceiverConfig, TaggedAudioPacket etc.
                                    // std::string, std::vector, std::shared_ptr, std::chrono etc.
                                    // are included via network_audio_receiver.h

namespace screamrouter {
namespace audio {

// Forward declare if needed, though RtpReceiverConfig should be complete from audio_types.h
// struct RtpReceiverConfig; (already in audio_types.h)
// class TimeshiftManager; (already handled by network_audio_receiver.h)
// using NotificationQueue = ::screamrouter::utils::ThreadSafeQueue<NewSourceNotification>; (already an alias in network_audio_receiver.h)

class RtpReceiver : public NetworkAudioReceiver {
public:
    RtpReceiver(
        RtpReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager
    );

    ~RtpReceiver() noexcept override;

    // Public methods like start(), stop(), get_seen_tags() are inherited.

protected:
    // Implementations for NetworkAudioReceiver's pure virtual methods
    bool is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) override;
    
    bool process_and_validate_payload(
        const uint8_t* buffer,
        int size,
        const struct sockaddr_in& client_addr,
        std::chrono::steady_clock::time_point received_time,
        TaggedAudioPacket& out_packet,
        std::string& out_source_tag
    ) override;
    
    size_t get_receive_buffer_size() const override;
    int get_poll_timeout_ms() const override;

private:
    RtpReceiverConfig config_; // Specific configuration for RtpReceiver

    // Helper method specific to RTP processing
    bool is_valid_rtp_header_payload(const uint8_t* buffer, int size); // Renamed for clarity
};

} // namespace audio
} // namespace screamrouter

#endif // RTP_RECEIVER_H
