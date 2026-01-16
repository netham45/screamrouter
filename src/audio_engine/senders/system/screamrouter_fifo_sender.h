#pragma once

#include "../i_network_sender.h"
#include "../../audio_types.h"
#include "../../system_audio/runtime_device_advertiser.h"

#include <mutex>
#include <memory>
#include <string>

#if defined(__linux__)
#define SCREAMROUTER_FIFO_SENDER_AVAILABLE 1
#else
#define SCREAMROUTER_FIFO_SENDER_AVAILABLE 0
#endif

namespace screamrouter {
namespace audio {

class ScreamrouterFifoSender : public INetworkSender {
public:
    explicit ScreamrouterFifoSender(const SinkMixerConfig& config);
    ~ScreamrouterFifoSender() override;

    bool setup() override;
    void close() override;
    void send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) override;

private:
#if SCREAMROUTER_FIFO_SENDER_AVAILABLE
    bool open_fifo_locked();
    void close_fifo_locked();
#endif

    SinkMixerConfig config_;
    std::string fifo_path_;
#if SCREAMROUTER_FIFO_SENDER_AVAILABLE
    int fifo_fd_ = -1;
    std::mutex state_mutex_;
    std::unique_ptr<system_audio::RuntimeDeviceAdvertiser> runtime_advertiser_;
#endif
};

} // namespace audio
} // namespace screamrouter
