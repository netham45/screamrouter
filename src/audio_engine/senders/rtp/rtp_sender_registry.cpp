#include "rtp_sender_registry.h"

namespace screamrouter {
namespace audio {

RtpSenderRegistry& RtpSenderRegistry::get_instance() {
    static RtpSenderRegistry instance;
    return instance;
}

void RtpSenderRegistry::add_ssrc(uint32_t ssrc) {
    std::lock_guard<std::mutex> lock(mutex_);
    local_ssrcs_.insert(ssrc);
}

void RtpSenderRegistry::remove_ssrc(uint32_t ssrc) {
    std::lock_guard<std::mutex> lock(mutex_);
    local_ssrcs_.erase(ssrc);
}

bool RtpSenderRegistry::is_local_ssrc(uint32_t ssrc) {
    std::lock_guard<std::mutex> lock(mutex_);
    return local_ssrcs_.count(ssrc) > 0;
}

} // namespace audio
} // namespace screamrouter