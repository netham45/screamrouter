#pragma once

#include <string>
#include <cstring>

namespace screamrouter {
namespace audio {
namespace system_audio {

// Common tag prefixes for system audio devices.
constexpr const char* kAlsaCapturePrefix = "ac:";
constexpr const char* kAlsaPlaybackPrefix = "ap:";
constexpr const char* kWasapiCapturePrefix = "wc:";
constexpr const char* kWasapiPlaybackPrefix = "wp:";
constexpr const char* kWasapiLoopbackPrefix = "ws:";

// Default tags for convenience routing.
constexpr const char* kWasapiDefaultCaptureTag = "wc:default";
constexpr const char* kWasapiDefaultPlaybackTag = "wp:default";
constexpr const char* kWasapiDefaultLoopbackTag = "ws:default";

inline bool tag_has_prefix(const std::string& tag, const char* prefix) {
    return tag.rfind(prefix, 0) == 0;
}

inline bool is_capture_tag(const std::string& tag) {
    return tag_has_prefix(tag, kAlsaCapturePrefix) ||
           tag_has_prefix(tag, kWasapiCapturePrefix) ||
           tag_has_prefix(tag, kWasapiLoopbackPrefix);
}

inline bool is_playback_tag(const std::string& tag) {
    return tag_has_prefix(tag, kAlsaPlaybackPrefix) ||
           tag_has_prefix(tag, kWasapiPlaybackPrefix);
}

inline bool is_loopback_tag(const std::string& tag) {
    return tag_has_prefix(tag, kWasapiLoopbackPrefix);
}

} // namespace system_audio
} // namespace audio
} // namespace screamrouter
