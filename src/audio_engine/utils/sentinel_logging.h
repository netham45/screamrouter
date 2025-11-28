#ifndef SENTINEL_LOGGING_H
#define SENTINEL_LOGGING_H

#include <chrono>
#include <string>
#include "../audio_types.h"
#include "cpp_logger.h"

namespace screamrouter {
namespace audio {
namespace utils {

inline long long steady_ms(const std::chrono::steady_clock::time_point& tp) {
    if (tp.time_since_epoch().count() == 0) {
        return -1;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

inline void log_sentinel(const char* stage,
                         const TaggedAudioPacket& packet,
                         const std::string& context = std::string()) {
    if (!packet.is_sentinel) {
        return;
    }
    const char* stage_label = stage ? stage : "unknown";
    const auto received_ms = steady_ms(packet.received_time);
    const char* tag = packet.source_tag.empty() ? "<unknown>" : packet.source_tag.c_str();

    if (packet.rtp_timestamp.has_value()) {
        LOG_CPP_WARNING("[Sentinel:%s]%s source=%s rtp_ts=%u received_ms=%lld",
                        stage_label,
                        context.empty() ? "" : context.c_str(),
                        tag,
                        *packet.rtp_timestamp,
                        static_cast<long long>(received_ms));
    } else {
        LOG_CPP_WARNING("[Sentinel:%s]%s source=%s received_ms=%lld",
                        stage_label,
                        context.empty() ? "" : context.c_str(),
                        tag,
                        static_cast<long long>(received_ms));
    }
}

inline void log_sentinel(const char* stage,
                         const ProcessedAudioChunk& chunk,
                         const std::string& context = std::string()) {
    if (!chunk.is_sentinel) {
        return;
    }
    const char* stage_label = stage ? stage : "unknown";
    const auto received_ms = steady_ms(
        (chunk.origin_time.time_since_epoch().count() != 0) ? chunk.origin_time : chunk.produced_time);

    LOG_CPP_WARNING("[Sentinel:%s]%s received_ms=%lld",
                    stage_label,
                    context.empty() ? "" : context.c_str(),
                    static_cast<long long>(received_ms));
}

} // namespace utils
} // namespace audio
} // namespace screamrouter

#endif // SENTINEL_LOGGING_H
