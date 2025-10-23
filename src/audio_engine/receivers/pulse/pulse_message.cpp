#include "pulse_message.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <cstring>

namespace screamrouter {
namespace audio {
namespace pulse {

std::vector<uint8_t> EncodeMessage(const Message& message) {
    std::vector<uint8_t> frame;
    frame.resize(kDescriptorBytes + message.payload.size());

    const MessageDescriptor& d = message.descriptor;
    uint32_t descriptor[5] = {
        htonl(d.length),
        htonl(d.channel),
        htonl(d.offset_hi),
        htonl(d.offset_lo),
        htonl(d.flags)
    };
    std::memcpy(frame.data(), descriptor, kDescriptorBytes);
    if (!message.payload.empty()) {
        std::memcpy(frame.data() + kDescriptorBytes, message.payload.data(), message.payload.size());
    }
    return frame;
}

size_t DecodeMessage(const uint8_t* buffer, size_t buffer_size, Message& out_message) {
    if (!buffer) {
        return 0;
    }
    if (buffer_size < kDescriptorBytes) {
        return 0; // Need more data for header.
    }

    MessageDescriptor descriptor;
    uint32_t raw[5];
    std::memcpy(raw, buffer, kDescriptorBytes);
    descriptor.length = ntohl(raw[0]);
    descriptor.channel = ntohl(raw[1]);
    descriptor.offset_hi = ntohl(raw[2]);
    descriptor.offset_lo = ntohl(raw[3]);
    descriptor.flags = ntohl(raw[4]);

    const size_t total_needed = kDescriptorBytes + descriptor.length;
    if (buffer_size < total_needed) {
        return 0; // Wait for full payload.
    }

    out_message.descriptor = descriptor;
    out_message.payload.assign(buffer + kDescriptorBytes, buffer + total_needed);
    return total_needed;
}

} // namespace pulse
} // namespace audio
} // namespace screamrouter
