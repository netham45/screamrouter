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

#include <map>
#include <deque>
#include <memory>
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
    void dispatch_ready_packet(TaggedAudioPacket&& packet) override;

    size_t get_receive_buffer_size() const override;
    int get_poll_timeout_ms() const override;

private:
    struct StreamState {
        std::string source_tag;
        int sample_rate = 0;
        int channels = 0;
        int bit_depth = 0;
        uint8_t chlayout1 = 0;
        uint8_t chlayout2 = 0;
        uint32_t samples_per_chunk = 0;
        uint32_t next_rtp_timestamp = 0;
        ClockManager::ConditionHandle clock_handle;
        uint64_t clock_last_sequence = 0;
        std::deque<TaggedAudioPacket> pending_packets;
    };

    static uint32_t calculate_samples_per_chunk(int channels, int bit_depth);
    void handle_clock_tick(const std::string& source_tag);
    std::shared_ptr<StreamState> get_or_create_stream_state(const TaggedAudioPacket& packet);
    void clear_all_streams();
    void dispatch_clock_ticks();

    PerProcessScreamReceiverConfig config_;
    ClockManager* clock_manager_;
    std::mutex stream_state_mutex_;
    std::map<std::string, std::shared_ptr<StreamState>> stream_states_;

    bool validate_per_process_scream_content(const uint8_t* buffer, int size, const std::string& sender_ip, TaggedAudioPacket& out_packet, std::string& out_composite_source_tag);

    void on_before_poll_wait() override;
    void on_after_poll_iteration() override;
};

} // namespace audio
} // namespace screamrouter

#endif // PER_PROCESS_SCREAM_RECEIVER_H
