#include "rtp_receiver.h"

#include <memory>

namespace screamrouter {
namespace audio {

RtpReceiver::RtpReceiver(
    RtpReceiverConfig config,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager)
    : RtpReceiverBase(std::move(config), std::move(notification_queue), timeshift_manager) {
    register_payload_receiver(std::make_unique<RtpPcmReceiver>());
    register_payload_receiver(std::make_unique<RtpPcmuReceiver>());
    register_payload_receiver(std::make_unique<RtpOpusReceiver>());
}

} // namespace audio
} // namespace screamrouter
