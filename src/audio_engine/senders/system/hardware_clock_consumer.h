#pragma once

#include <cstdint>

#include "../../receivers/clock_manager.h"

namespace screamrouter {
namespace audio {

/**
 * @brief Optional interface for output backends that can pace the mixer using a hardware clock.
 */
class IHardwareClockConsumer {
public:
    virtual ~IHardwareClockConsumer() = default;

    /**
     * @brief Begin using the playback device clock to signal mix ticks.
     * @param clock_manager Pointer owned by the caller; valid until stop_hardware_clock is invoked.
     * @param handle Handle obtained from ClockManager::register_external_clock_condition.
     * @param frames_per_tick Number of PCM frames that correspond to a single mix tick.
     * @return true on success; false to request falling back to the software clock.
     */
    virtual bool start_hardware_clock(ClockManager* clock_manager,
                                      const ClockManager::ConditionHandle& handle,
                                      std::uint32_t frames_per_tick) = 0;

    /**
     * @brief Stop pacing with the hardware clock and release any associated resources.
     */
    virtual void stop_hardware_clock() = 0;
};

} // namespace audio
} // namespace screamrouter
