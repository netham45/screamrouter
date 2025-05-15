#ifndef PER_PROCESS_SCREAM_RECEIVER_H
#define PER_PROCESS_SCREAM_RECEIVER_H

#include "network_audio_receiver.h" // Base class
#include "audio_types.h"            // For PerProcessScreamReceiverConfig etc.

namespace screamrouter {
namespace audio {

class PerProcessScreamReceiver : public NetworkAudioReceiver {
public:
    PerProcessScreamReceiver(
        PerProcessScreamReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager
    );

    ~PerProcessScreamReceiver() noexcept override;

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
    PerProcessScreamReceiverConfig config_;

    // Helper method specific to PerProcess Scream processing
    bool validate_per_process_scream_content(const uint8_t* buffer, int size, const std::string& sender_ip, TaggedAudioPacket& out_packet, std::string& out_composite_source_tag);
};

} // namespace audio
} // namespace screamrouter

#endif // PER_PROCESS_SCREAM_RECEIVER_H
