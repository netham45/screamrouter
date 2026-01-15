#ifndef SCREAMROUTER_AUDIO_SAP_TYPES_H
#define SCREAMROUTER_AUDIO_SAP_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace screamrouter {
namespace audio {

enum class Endianness {
    LITTLE,
    BIG
};

enum class StreamCodec {
    PCM,
    PCMU,
    PCMA,
    OPUS,
    UNKNOWN
};

struct StreamProperties {
    int sample_rate = 0;
    int channels = 0;
    int bit_depth = 0;
    Endianness endianness = Endianness::BIG;
    int port = 0;
    int payload_type = -1;
    StreamCodec codec = StreamCodec::UNKNOWN;
    int opus_streams = 0;
    int opus_coupled_streams = 0;
    int opus_mapping_family = 0;
    std::vector<uint8_t> opus_channel_mapping;
};

struct SapAnnouncement {
    std::string stream_ip;
    std::string announcer_ip;
    int port = 0;
    StreamProperties properties;
    std::string stream_guid;
    std::string target_sink;
    std::string target_host;
    std::string session_name;
};

} // namespace audio
} // namespace screamrouter

#endif // SCREAMROUTER_AUDIO_SAP_TYPES_H
