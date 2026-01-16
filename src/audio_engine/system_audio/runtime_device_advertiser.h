#pragma once

#include "../audio_types.h"

#include <string>

namespace screamrouter {
namespace audio {
namespace system_audio {

/**
 * @brief Helper that allows local sinks to publish their device metadata
 *        into the screamrouter runtime directory for discovery.
 */
class RuntimeDeviceAdvertiser {
public:
    RuntimeDeviceAdvertiser() = default;
    ~RuntimeDeviceAdvertiser();

    /**
     * @brief Publish or refresh the on-disk manifest for the provided info.
     * @param info Device metadata to expose to the ALSA enumerator.
     */
    void publish(const SystemDeviceInfo& info);

    /** @brief Remove any published manifest. */
    void withdraw();

private:
    std::string manifest_path_;
    std::string tag_;
};

} // namespace system_audio
} // namespace audio
} // namespace screamrouter
