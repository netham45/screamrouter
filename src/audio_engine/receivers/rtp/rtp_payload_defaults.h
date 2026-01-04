#ifndef RTP_PAYLOAD_DEFAULTS_H
#define RTP_PAYLOAD_DEFAULTS_H

#include "sap_listener/sap_types.h"

#include <cstdint>

namespace screamrouter {
namespace audio {

struct PayloadTypeDefault {
    uint8_t payload_type;
    StreamCodec codec;
    int sample_rate;
    int channels;
    int bit_depth;
    Endianness endianness;
};

inline const PayloadTypeDefault* find_payload_default(uint8_t payload_type) {
    static constexpr PayloadTypeDefault kDefaults[] = {
        {111, StreamCodec::OPUS, 48000, 2, 16, Endianness::LITTLE},
        {0,   StreamCodec::PCMU, 8000,  1, 8,  Endianness::BIG},
        {8,   StreamCodec::PCMA, 8000,  1, 8,  Endianness::BIG},
        {10,  StreamCodec::PCM,  44100, 1, 16, Endianness::BIG},
        {11,  StreamCodec::PCM,  44100, 2, 16, Endianness::BIG},
        {127, StreamCodec::PCM,  48000, 2, 16, Endianness::BIG}
    };

    for (const auto& def : kDefaults) {
        if (def.payload_type == payload_type) {
            return &def;
        }
    }
    return nullptr;
}

inline void apply_payload_default_to_properties(
    const PayloadTypeDefault& def,
    int listen_port,
    StreamProperties& props) {
    props.payload_type = def.payload_type;
    props.codec = def.codec;
    props.sample_rate = def.sample_rate;
    props.channels = def.channels;
    props.bit_depth = def.bit_depth;
    props.endianness = def.endianness;
    props.port = listen_port;

    if (def.codec == StreamCodec::OPUS) {
        props.opus_streams = 0;
        props.opus_coupled_streams = 0;
        props.opus_mapping_family = 0;
        props.opus_channel_mapping.clear();
    }
}

inline bool populate_stream_properties_from_payload(
    uint8_t payload_type,
    uint8_t canonical_payload_type,
    int listen_port,
    StreamProperties& props) {
    const PayloadTypeDefault* match = find_payload_default(payload_type);
    if (!match) {
        match = find_payload_default(canonical_payload_type);
    }
    if (!match) {
        return false;
    }
    apply_payload_default_to_properties(*match, listen_port, props);
    return true;
}

} // namespace audio
} // namespace screamrouter

#endif // RTP_PAYLOAD_DEFAULTS_H
