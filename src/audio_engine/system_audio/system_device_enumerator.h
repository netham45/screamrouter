#pragma once

#include "../audio_types.h"

#include <memory>

namespace screamrouter {
namespace audio {
namespace system_audio {

/**
 * @class SystemDeviceEnumerator
 * @brief Platform-agnostic interface for monitoring system audio endpoints.
 */
class SystemDeviceEnumerator {
public:
    using Registry = SystemDeviceRegistry;

    virtual ~SystemDeviceEnumerator() = default;

    /**
     * @brief Starts monitoring system audio devices.
     */
    virtual void start() = 0;

    /**
     * @brief Stops monitoring and releases resources.
     */
    virtual void stop() = 0;

    /**
     * @brief Returns a snapshot of currently known devices.
     */
    virtual Registry get_registry_snapshot() const = 0;
};

} // namespace system_audio
} // namespace audio
} // namespace screamrouter

