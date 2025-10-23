/**
 * @file raw_scream_receiver.h
 * @brief Defines the RawScreamReceiver class for handling raw Scream protocol audio streams.
 * @details This class is a specialization of `NetworkAudioReceiver` for the standard
 *          Scream protocol, which consists of a 5-byte header followed by PCM audio data.
 */
#ifndef RAW_SCREAM_RECEIVER_H
#define RAW_SCREAM_RECEIVER_H

#include "../network_audio_receiver.h"
#include "../clock_manager.h"
#include "../../audio_types.h"

#include <cstdint>

namespace screamrouter {
namespace audio {

/**
 * @class RawScreamReceiver
 * @brief A network receiver for the raw Scream audio protocol.
 * @details This class inherits from `NetworkAudioReceiver` and implements the specific
 *          logic for parsing and validating standard Scream packets. The source tag
 *          for these streams is the sender's IP address.
 */
class RawScreamReceiver : public NetworkAudioReceiver {
public:
    /**
     * @brief Constructs a RawScreamReceiver.
     * @param config The configuration for this receiver.
     * @param notification_queue A queue for sending notifications about new sources.
     * @param timeshift_manager A pointer to the `TimeshiftManager` for packet buffering.
     * @param logger_prefix A prefix for log messages.
     */
    RawScreamReceiver(
        RawScreamReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager,
        ClockManager* clock_manager,
        std::string logger_prefix
    );

    /**
     * @brief Destructor.
     */
    ~RawScreamReceiver() noexcept override;

    /**
     * @brief Gets the listening port for this receiver.
     * @return The UDP listen port.
     */
    int get_listen_port() const { return config_.listen_port; }

protected:
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

    bool validate_raw_scream_content(const uint8_t* buffer, int size, TaggedAudioPacket& out_packet);
};

} // namespace audio
} // namespace screamrouter

#endif // RAW_SCREAM_RECEIVER_H
