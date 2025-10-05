/**
 * @file protocol_types.h
 * @brief Defines types related to network protocols used in the audio engine.
 * @details This file contains enumerations and structures for configuring
 *          different network protocols like legacy Scream and RTP.
 */
#ifndef PROTOCOL_TYPES_H
#define PROTOCOL_TYPES_H

#include <cstdint> // For uint8_t

namespace screamrouter {
/**
 * @brief Namespace for configuration-related types used throughout the audio engine.
 */
namespace config {

/**
 * @enum ProtocolType
 * @brief Defines the network protocol to be used by a sender or receiver.
 */
enum class ProtocolType {
    LEGACY_SCREAM = 0,  ///< Use the original Scream protocol format.
    RTP = 1             ///< Use the Real-time Transport Protocol (RTP).
};

/**
 * @struct RTPConfigCpp
 * @brief Holds configuration parameters specific to the RTP protocol.
 */
struct RTPConfigCpp {
    /** @brief The UDP port to which RTP packets will be sent. */
    int destination_port = 0;
    /** @brief The UDP port where the corresponding RTP source is listening. */
    int source_listening_port = 0;
    /** @brief The RTP payload type identifier for raw PCM audio. */
    uint8_t payload_type_pcm = 96;
    /** @brief The RTP payload type identifier for MP3 audio. */
    uint8_t payload_type_mp3 = 14;
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