/**
 * @file rtp_receiver.h
 * @brief Declares RTP receiver classes for PCM and Opus payload handling.
 */
#ifndef RTP_RECEIVER_H
#define RTP_RECEIVER_H

#include "../network_audio_receiver.h"
#include "../clock_manager.h"
#include "../../audio_types.h"
#include "sap_listener.h"
#include "rtp_reordering_buffer.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct OpusDecoder;

namespace screamrouter {
namespace audio {

class RtpPayloadReceiver {
public:
    virtual ~RtpPayloadReceiver() = default;

    virtual bool supports_payload_type(uint8_t payload_type) const = 0;
    virtual bool populate_append_context(
        const RtpPacketData& packet,
        const StreamProperties& properties,
        NetworkAudioReceiver::PcmAppendContext& context
    ) = 0;
    virtual void on_ssrc_state_cleared(uint32_t ssrc) { (void)ssrc; }
    virtual void on_all_ssrcs_cleared() {}
};

/**
 * @class RtpReceiverBase
 * @brief Provides shared socket, reordering, and SAP logic for RTP receivers.
 */
class RtpReceiverBase : public NetworkAudioReceiver {
public:
    RtpReceiverBase(
        RtpReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager,
        ClockManager* clock_manager
    );

    ~RtpReceiverBase() noexcept override;

    std::vector<SapAnnouncement> get_sap_announcements();

protected:
    void run() override;
    bool setup_socket() override;
    void close_socket() override;

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

    void register_payload_receiver(std::unique_ptr<RtpPayloadReceiver> receiver);

    bool supports_payload_type(uint8_t payload_type, uint32_t ssrc) const;
    bool resolve_stream_properties(
        uint32_t ssrc,
        const struct sockaddr_in& client_addr,
        uint8_t payload_type,
        StreamProperties& out_properties
    ) const;

    std::string get_source_key(const struct sockaddr_in& addr) const;
    std::string make_pcm_accumulator_key(uint32_t ssrc) const;
    void handle_ssrc_changed(uint32_t old_ssrc, uint32_t new_ssrc, const std::string& source_key);
    void open_dynamic_session(const std::string& ip, int port, const std::string& source_ip = "");

    void process_ready_packets(uint32_t ssrc, const struct sockaddr_in& client_addr);
    void process_ready_packets_internal(uint32_t ssrc, const struct sockaddr_in& client_addr, bool take_lock);

    void maybe_log_telemetry();

    struct SessionInfo {
        socket_t socket_fd;
        std::string destination_ip;
        int port;
        std::string source_ip;
    };

    RtpReceiverConfig config_;
    const std::size_t chunk_size_bytes_;
#ifdef _WIN32
    fd_set master_read_fds_;
    socket_t max_fd_;
#else
    int epoll_fd_;
#endif
    std::vector<socket_t> socket_fds_;
    std::mutex socket_fds_mutex_;

    std::map<std::string, uint32_t> source_to_last_ssrc_;
    std::mutex source_ssrc_mutex_;

    std::map<uint32_t, RtpReorderingBuffer> reordering_buffers_;
    std::mutex reordering_buffer_mutex_;

    std::unique_ptr<SapListener> sap_listener_;

    std::map<socket_t, SessionInfo> socket_sessions_;
    std::map<std::string, socket_t> unicast_source_to_socket_;

    std::vector<std::unique_ptr<RtpPayloadReceiver>> payload_receivers_;

    std::chrono::steady_clock::time_point telemetry_last_log_time_{};
};

class RtpPcmReceiver : public RtpPayloadReceiver {
public:
    bool supports_payload_type(uint8_t payload_type) const override;
    bool populate_append_context(
        const RtpPacketData& packet,
        const StreamProperties& properties,
        NetworkAudioReceiver::PcmAppendContext& context
    ) override;
};

class RtpOpusReceiver : public RtpPayloadReceiver {
public:
    RtpOpusReceiver();
    ~RtpOpusReceiver() noexcept override;

    bool supports_payload_type(uint8_t payload_type) const override;
    bool populate_append_context(
        const RtpPacketData& packet,
        const StreamProperties& properties,
        NetworkAudioReceiver::PcmAppendContext& context
    ) override;
    void on_ssrc_state_cleared(uint32_t ssrc) override;
    void on_all_ssrcs_cleared() override;

private:
    struct DecoderState {
        OpusDecoder* handle = nullptr;
        int sample_rate = 0;
        int channels = 0;
    };

    void destroy_decoder(uint32_t ssrc);
    void destroy_all_decoders();

    static int maximum_frame_samples(int sample_rate);

    std::unordered_map<uint32_t, DecoderState> decoder_states_;
    mutable std::mutex decoder_mutex_;
};

class RtpReceiver : public RtpReceiverBase {
public:
    RtpReceiver(
        RtpReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager,
        ClockManager* clock_manager
    );
};

} // namespace audio
} // namespace screamrouter

#endif // RTP_RECEIVER_H
