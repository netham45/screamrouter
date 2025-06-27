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
#include "../../audio_types.h"

#include <map>
#include <mutex>
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
    /** @brief Validates the basic structure of a per-process Scream packet. */
    bool is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) override;
    
    /** @brief Processes a valid per-process Scream packet, extracting audio data and metadata. */
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
    /** @brief Returns the poll timeout for the receive loop. */
    int get_poll_timeout_ms() const override;

private:
    PerProcessScreamReceiverConfig config_;
    std::map<std::string, uint32_t> stream_timestamps_;
    std::mutex timestamps_mutex_;

    /**
     * @brief Validates the content of a per-process Scream packet.
     * @param buffer The packet data.
     * @param size The size of the packet data.
     * @param sender_ip The IP address of the sender.
     * @param out_packet The `TaggedAudioPacket` to populate.
     * @param out_composite_source_tag The composite source tag to be created.
     * @return true if the content is valid, false otherwise.
     */
    bool validate_per_process_scream_content(const uint8_t* buffer, int size, const std::string& sender_ip, TaggedAudioPacket& out_packet, std::string& out_composite_source_tag);
};

} // namespace audio
} // namespace screamrouter

#endif // PER_PROCESS_SCREAM_RECEIVER_H
