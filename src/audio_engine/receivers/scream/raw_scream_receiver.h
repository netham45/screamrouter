#ifndef RAW_SCREAM_RECEIVER_H
#define RAW_SCREAM_RECEIVER_H

#include "../network_audio_receiver.h" // Base class
#include "../../audio_types.h"            // For RawScreamReceiverConfig etc.

#include <map>
#include <mutex>
#include <cstdint>

namespace screamrouter {
namespace audio {

class RawScreamReceiver : public NetworkAudioReceiver {
public:
    RawScreamReceiver(
        RawScreamReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager,
        std::string logger_prefix
    );

    ~RawScreamReceiver() noexcept override;

    int get_listen_port() const { return config_.listen_port; }

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
    RawScreamReceiverConfig config_;
    std::map<std::string, uint32_t> stream_timestamps_;
    std::mutex timestamps_mutex_;

    // Helper method specific to Raw Scream processing
    // This method will now primarily focus on the content validation after basic structure is checked.
    bool validate_raw_scream_content(const uint8_t* buffer, int size, TaggedAudioPacket& out_packet);
};

} // namespace audio
} // namespace screamrouter

#endif // RAW_SCREAM_RECEIVER_H
