/**
 * @file audio_component.h
 * @brief Defines the AudioComponent abstract base class for threaded components.
 * @details This file provides a common interface for all major, long-running
 *          components in the audio engine, such as receivers and mixers. It standardizes
 *          the lifecycle management (start, stop) and provides a basic threading model.
 */
#ifndef AUDIO_COMPONENT_H
#define AUDIO_COMPONENT_H

#include <thread>
#include <atomic>
#include <string>

namespace screamrouter {
namespace audio {

/**
 * @class AudioComponent
 * @brief Abstract base class for core audio processing components.
 * @details Provides a common interface for lifecycle management (start, stop)
 *          and thread control. Any class that runs its own processing loop
 *          in a separate thread should inherit from this class.
 */
class AudioComponent {
public:
    /**
     * @brief Virtual destructor. Essential for proper cleanup of derived classes.
     */
    virtual ~AudioComponent() = default;

    // Prevent copying and moving to avoid slicing and ownership issues.
    AudioComponent(const AudioComponent&) = delete;
    AudioComponent& operator=(const AudioComponent&) = delete;
    AudioComponent(AudioComponent&&) = delete;
    AudioComponent& operator=(AudioComponent&&) = delete;

    /**
     * @brief Starts the component's processing thread.
     * @details Implementations should set `stop_flag_` to false and launch
     *          `component_thread_` executing the `run()` method.
     */
    virtual void start() = 0;

    /**
     * @brief Signals the component's processing thread to stop and then joins it.
     * @details Implementations should set `stop_flag_` to true, notify any
     *          condition variables in `run()`, and join `component_thread_`.
     */
    virtual void stop() = 0;

    /**
     * @brief Checks if the component's thread is currently running.
     * @return true if the component is considered active, false otherwise.
     */
    bool is_running() const {
        return component_thread_.joinable() && !stop_flag_;
    }

protected:
    /**
     * @brief Protected constructor for use by derived classes only.
     */
    AudioComponent() : stop_flag_(false) {}

    /**
     * @brief The main processing loop to be executed by the component's thread.
     * @details Implementations should contain the core logic of the component
     *          and periodically check `stop_flag_` to allow for graceful termination.
     */
    virtual void run() = 0;

    /** @brief The thread object for the component's processing loop. */
    std::thread component_thread_;
    /** @brief An atomic flag to signal the processing thread to stop. */
    std::atomic<bool> stop_flag_;
};

} // namespace audio
} // namespace screamrouter

#endif // AUDIO_COMPONENT_H
