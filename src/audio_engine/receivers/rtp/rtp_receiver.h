/**
 * @file rtp_receiver.h
 * @brief Convenience header exposing the RTP receiver base and payload handlers.
 */
#ifndef RTP_RECEIVER_H
#define RTP_RECEIVER_H

#include "rtp_receiver_base.h"
#include "rtp_pcm_receiver.h"
#include "rtp_pcmu_receiver.h"
#include "rtp_pcma_receiver.h"
#include "rtp_opus_receiver.h"

namespace screamrouter {
namespace audio {

class RtpReceiver : public RtpReceiverBase {
public:
    RtpReceiver(
        RtpReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager);
};

} // namespace audio
} // namespace screamrouter

#endif // RTP_RECEIVER_H
