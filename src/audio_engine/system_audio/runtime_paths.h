#pragma once

#include <cstdlib>
#include <string>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace screamrouter {
namespace audio {
namespace system_audio {

inline std::string resolve_runtime_base_dir() {
    const char* xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime && xdg_runtime[0] != '\0') {
        std::string base(xdg_runtime);
        while (!base.empty() && base.back() == '/') {
            base.pop_back();
        }
        return base;
    }

#if defined(__linux__)
    std::string fallback = "/run/user/" + std::to_string(static_cast<unsigned int>(getuid()));
    return fallback;
#else
    return {};
#endif
}

inline std::string screamrouter_runtime_dir() {
    std::string base = resolve_runtime_base_dir();
    if (base.empty()) {
        return "/var/run/screamrouter";
    }
    return base + "/screamrouter";
}

inline bool is_screamrouter_fifo_path(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    const std::string runtime_dir = screamrouter_runtime_dir();
    const std::string runtime_prefix = runtime_dir + "/";
    if (path.rfind(runtime_prefix, 0) == 0) {
        return true;
    }
    // Backward compatibility for legacy installations.
    return path.rfind("/var/run/screamrouter/", 0) == 0;
}

} // namespace system_audio
} // namespace audio
} // namespace screamrouter
