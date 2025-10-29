#pragma once

#include "../network_audio_receiver.h"

#include <string>
#include <vector>

#include "../../utils/byte_ring_buffer.h"

#if defined(__linux__)
#define SCREAMROUTER_FIFO_CAPTURE_AVAILABLE 1
#else
#define SCREAMROUTER_FIFO_CAPTURE_AVAILABLE 0
#endif

namespace screamrouter {
namespace audio {

class ScreamrouterFifoReceiver : public NetworkAudioReceiver {
public:
    ScreamrouterFifoReceiver(
        std::string device_tag,
        CaptureParams capture_params,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager);

    ~ScreamrouterFifoReceiver() noexcept override;

protected:
    bool setup_socket() override;
    void close_socket() override;
    void run() override;

    bool is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) override;
    bool process_and_validate_payload(const uint8_t* buffer,
                                      int size,
                                      const struct sockaddr_in& client_addr,
                                      std::chrono::steady_clock::time_point received_time,
                                      TaggedAudioPacket& out_packet,
                                      std::string& out_source_tag) override;
    size_t get_receive_buffer_size() const override;
    int get_poll_timeout_ms() const override;

private:
#if SCREAMROUTER_FIFO_CAPTURE_AVAILABLE
    bool open_fifo();
    void close_fifo();
    void dispatch_chunk(std::vector<uint8_t>&& chunk_data);
#endif

    std::string device_tag_;
    CaptureParams capture_params_;

#if SCREAMROUTER_FIFO_CAPTURE_AVAILABLE
    std::string fifo_path_;
    int fifo_fd_ = -1;
    unsigned int channels_ = 2;
    unsigned int sample_rate_ = 48000;
    unsigned int bit_depth_ = 16;
    size_t bytes_per_frame_ = 0;
    size_t chunk_bytes_ = 0;
    uint32_t running_timestamp_ = 0;
    std::vector<uint8_t> read_buffer_;
    ::screamrouter::audio::utils::ByteRingBuffer chunk_buffer_;
#endif
};

} // namespace audio
} // namespace screamrouter
