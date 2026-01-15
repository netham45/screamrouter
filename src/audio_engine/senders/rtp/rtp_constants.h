#pragma once

#include <cstddef>

namespace screamrouter {
namespace audio {

// Default maximum RTP payload size to stay within typical MTU limits.
constexpr std::size_t kDefaultRtpPayloadMtu = 1152;

} // namespace audio
} // namespace screamrouter

