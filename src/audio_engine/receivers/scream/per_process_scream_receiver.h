/**
 * @file per_process_scream_receiver.h
 * @brief Defines the PerProcessScreamReceiver class for handling per-process Scream audio streams.
 * @details This class is a specialization of `NetworkAudioReceiver` for the per-process
 *          variant of the Scream protocol, which includes a program tag in each packet
 *          to identify the source application.
 */
#ifndef PER_PROCESS_SCREAM_RECEIVER_H
#define PER_PROCESS_SCREAM_RECEIVER_H

#include "../network_audio_receiver.h"
#include "../clock_manager.h"
#include "../../audio_types.h"

#include <cstdint>
#include <string>

namespace screamrouter {
namespace audio {

/**
 * @class PerProcessScreamReceiver
 * @brief A network receiver for the per-process Scream audio protocol.
 * @details This class inherits from `NetworkAudioReceiver` and implements the specific
 *          logic for parsing and validating Scream packets that include a leading
 *          program tag. The source tag for these streams is a composite of the
 *          sender's IP address and the program tag.
 */
class PerProcessScreamReceiver : public NetworkAudioReceiver {
public:
    /**
     * @brief Constructs a PerProcessScreamReceiver.
     * @param config The configuration for this receiver.
     * @param notification_queue A queue for sending notifications about new sources.
     * @param timeshift_manager A pointer to the `TimeshiftManager` for packet buffering.
     * @param logger_prefix A prefix for log messages.
     */
    PerProcessScreamReceiver(
        PerProcessScreamReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager,
        ClockManager* clock_manager,
        std::string logger_prefix
    );

    /**
     * @brief Destructor.
     */
    ~PerProcessScreamReceiver() noexcept override;

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
    PerProcessScreamReceiverConfig config_;

    bool validate_per_process_scream_content(const uint8_t* buffer, int size, const std::string& sender_ip, TaggedAudioPacket& out_packet, std::string& out_composite_source_tag);
};

} // namespace audio
} // namespace screamrouter

#endif // PER_PROCESS_SCREAM_RECEIVER_H
