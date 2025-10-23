#ifndef SCREAMROUTER_AUDIO_PULSE_PULSE_MESSAGE_H
#define SCREAMROUTER_AUDIO_PULSE_PULSE_MESSAGE_H

#include <cstdint>
#include <cstddef>
#include <vector>

#include "pulse_protocol.h"

namespace screamrouter {
namespace audio {
namespace pulse {

struct Message {
    MessageDescriptor descriptor;
    std::vector<uint8_t> payload;
    std::vector<int> fds;
};

// Encode descriptor + payload into wire-format frame.
std::vector<uint8_t> EncodeMessage(const Message& message);

// Attempt to decode a complete frame from the supplied buffer.
// Returns number of bytes consumed on success, or 0 if more data is needed.
size_t DecodeMessage(const uint8_t* buffer, size_t buffer_size, Message& out_message);

constexpr size_t kDescriptorBytes = sizeof(uint32_t) * 5;

} // namespace pulse
} // namespace audio
} // namespace screamrouter

#endif // SCREAMROUTER_AUDIO_PULSE_PULSE_MESSAGE_H
