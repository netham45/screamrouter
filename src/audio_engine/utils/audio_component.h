#ifndef AUDIO_COMPONENT_H
#define AUDIO_COMPONENT_H

#include <thread>
#include <atomic>
#include <string> // For potential ID/name later

namespace screamrouter {
namespace audio {

/**
 * @brief Abstract base class for core audio processing components.
 *        Provides a common interface for lifecycle management (start, stop)
 *        and thread control.
 */
class AudioComponent {
public:
    /**
     * @brief Virtual destructor is crucial for base classes with virtual methods.
     */
    virtual ~AudioComponent() = default;

    // Prevent copying/moving to avoid slicing and ownership issues
    AudioComponent(const AudioComponent&) = delete;
    AudioComponent& operator=(const AudioComponent&) = delete;
    AudioComponent(AudioComponent&&) = delete;
    AudioComponent& operator=(AudioComponent&&) = delete;

    /**
     * @brief Starts the component's processing thread.
     *        Implementations should set stop_flag_ to false and launch
     *        component_thread_ executing the run() method.
     */
    virtual void start() = 0;

    /**
     * @brief Signals the component's processing thread to stop and joins it.
     *        Implementations should set stop_flag_ to true, potentially notify
     *        any condition variables in run(), and join component_thread_.
     */
    virtual void stop() = 0;

    /**
     * @brief Checks if the component's thread is currently running (i.e., started and not stopped).
     * @return true if the component is considered active, false otherwise.
     */
    bool is_running() const {
        // Check if thread is joinable (implies it was started and hasn't been joined yet)
        // and if the stop flag hasn't been set. This is an approximation.
        // A more robust check might involve another atomic flag set in start/stop.
        return component_thread_.joinable() && !stop_flag_;
    }

protected:
    // Protected constructor for use by derived classes only
    AudioComponent() : stop_flag_(false) {}

    /**
     * @brief The main processing loop executed by the component's thread.
     *        Implementations should contain the core logic and periodically
     *        check stop_flag_ to allow for graceful termination.
     */
    virtual void run() = 0;

    std::thread component_thread_;
    std::atomic<bool> stop_flag_;

    // Potential future additions:
    // std::string component_id_;
};

} // namespace audio
} // namespace screamrouter

#endif // AUDIO_COMPONENT_H
