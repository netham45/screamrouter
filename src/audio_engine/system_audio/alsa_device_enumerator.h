#ifndef SCREAMROUTER_AUDIO_SYSTEM_AUDIO_ALSA_DEVICE_ENUMERATOR_H
#define SCREAMROUTER_AUDIO_SYSTEM_AUDIO_ALSA_DEVICE_ENUMERATOR_H

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "../audio_types.h"
#include "system_device_enumerator.h"

namespace screamrouter {
namespace audio {
namespace system_audio {

/**
 * @class AlsaDeviceEnumerator
 * @brief Enumerates ALSA PCM devices and monitors them for hotplug events.
 * @details Scans all ALSA cards/devices to build a registry of capture/playback
 *          endpoints. Subscribes to ALSA control events so newly added or removed
 *          devices trigger discovery notifications for the audio manager.
 */
class AlsaDeviceEnumerator : public SystemDeviceEnumerator {
public:
    using Registry = SystemDeviceRegistry;

    /**
     * @brief Constructs an enumerator.
     * @param notification_queue Queue that receives device discovery notifications.
     */
    explicit AlsaDeviceEnumerator(std::shared_ptr<NotificationQueue> notification_queue);

    /**
     * @brief Destructor. Ensures monitoring thread is stopped.
     */
    ~AlsaDeviceEnumerator();

    /**
     * @brief Starts the monitoring thread (no-op on non-Linux platforms).
     */
    void start() override;

    /**
     * @brief Stops the monitoring thread and clears internal state.
     */
    void stop() override;

    /**
     * @brief Gets a snapshot of the current device registry.
     * @return Copy of the cached registry keyed by device tags.
     */
    Registry get_registry_snapshot() const override;

private:
#if defined(__linux__)
    void monitor_loop();
    void enumerate_devices();
    struct ControlHandle {
        int card_index = -1;
        void* handle = nullptr; // stored as void to avoid leaking ALSA headers in header file
    };
    std::vector<ControlHandle> open_control_handles();
    void close_control_handles(std::vector<ControlHandle>& handles);
    void setup_fifo_watch();
    void teardown_fifo_watch();
    bool drain_fifo_watch_events();
    void append_screamrouter_runtime_devices(Registry& registry);
    std::optional<SystemDeviceInfo> parse_screamrouter_fifo_entry(const std::string& filename) const;
    int inotify_fd_ = -1;
    int inotify_watch_fd_ = -1;
    // Self-pipe to wake poll() on shutdown
    int wake_pipe_[2] = {-1, -1};
#endif

    std::shared_ptr<NotificationQueue> notification_queue_;
    mutable std::mutex registry_mutex_;
    Registry registry_;
    std::thread monitor_thread_;
    std::atomic<bool> running_{false};
};

} // namespace system_audio
} // namespace audio
} // namespace screamrouter

#endif // SCREAMROUTER_AUDIO_SYSTEM_AUDIO_ALSA_DEVICE_ENUMERATOR_H
