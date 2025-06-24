// src/audio_engine/protocol_types.h
#ifndef PROTOCOL_TYPES_H
#define PROTOCOL_TYPES_H

#include <cstdint> // For uint8_t

namespace screamrouter {
/**
 * @brief Namespace for configuration-related types used throughout the audio engine.
 */
namespace config {

/**
 * @brief Defines the network protocol to be used by a sender or receiver.
 */
enum class ProtocolType {
    LEGACY_SCREAM = 0,  // Use the original Scream protocol format.
    RTP = 1             // Use the Real-time Transport Protocol (RTP).
};

/**
 * @brief Holds configuration parameters specific to the RTP protocol.
 */
struct RTPConfigCpp {
    int destination_port = 0;       // The UDP port to which RTP packets will be sent.
    int source_listening_port = 0;  // The UDP port where the corresponding RTP source is listening.
    uint8_t payload_type_pcm = 96;  // The RTP payload type identifier for raw PCM audio.
    uint8_t payload_type_mp3 = 14;  // The RTP payload type identifier for MP3 audio.
};

/**
 * @brief Non-member equality operator for comparing two RTPConfigCpp objects.
 * @param lhs The left-hand side object.
 * @param rhs The right-hand side object.
 * @return True if all members of the objects are equal, false otherwise.
 */
inline bool operator==(const RTPConfigCpp& lhs, const RTPConfigCpp& rhs) {
    return lhs.destination_port == rhs.destination_port &&
           lhs.source_listening_port == rhs.source_listening_port &&
           lhs.payload_type_pcm == rhs.payload_type_pcm &&
           lhs.payload_type_mp3 == rhs.payload_type_mp3;
}

} // namespace config
} // namespace screamrouter

#endif // PROTOCOL_TYPES_H