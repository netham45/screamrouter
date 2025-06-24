#ifndef RTP_SENDER_REGISTRY_H
#define RTP_SENDER_REGISTRY_H

#include <cstdint>
#include <mutex>
#include <set>

namespace screamrouter {
namespace audio {

class RtpSenderRegistry {
public:
    static RtpSenderRegistry& get_instance();

    void add_ssrc(uint32_t ssrc);
    void remove_ssrc(uint32_t ssrc);
    bool is_local_ssrc(uint32_t ssrc);

private:
    RtpSenderRegistry() = default;
    ~RtpSenderRegistry() = default;
    RtpSenderRegistry(const RtpSenderRegistry&) = delete;
    RtpSenderRegistry& operator=(const RtpSenderRegistry&) = delete;

    std::mutex mutex_;
    std::set<uint32_t> local_ssrcs_;
};

} // namespace audio
} // namespace screamrouter

#endif // RTP_SENDER_REGISTRY_H