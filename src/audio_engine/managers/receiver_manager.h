/**
 * @file receiver_manager.h
 * @brief Defines the ReceiverManager class for managing various audio receivers.
 * @details This class is responsible for the lifecycle of different types of network
 *          receivers (RTP, Raw Scream, etc.), initializing them, and providing a
 *          unified interface to access information from them.
 */
#ifndef RECEIVER_MANAGER_H
#define RECEIVER_MANAGER_H

#include "../receivers/rtp/rtp_receiver.h"
#include "../receivers/clock_manager.h"
#include "../receivers/scream/raw_scream_receiver.h"
#include "../receivers/scream/per_process_scream_receiver.h"
#if !defined(_WIN32)
#include "../receivers/pulse/pulse_receiver.h"
#endif
#include "../receivers/system/alsa_capture_receiver.h"
#include "../receivers/system/screamrouter_fifo_receiver.h"
#include "../receivers/system/wasapi_capture_receiver.h"
#include "../system_audio/system_audio_tags.h"
#include "../utils/thread_safe_queue.h"
#include "../input_processor/timeshift_manager.h"
#include "../audio_types.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace screamrouter {
namespace audio {

/**
 * @class ReceiverManager
 * @brief Manages the creation, lifecycle, and data access for all audio receivers.
 * @details This class abstracts the handling of multiple receiver types. It holds instances
 *          of different receivers and provides methods to initialize, start, stop, and
 *          query them.
 */
class ReceiverManager {
public:
    /**
     * @brief Constructs a ReceiverManager.
     * @param manager_mutex A reference to the main AudioManager mutex for thread safety.
     * @param timeshift_manager A pointer to the TimeshiftManager to which receivers will send packets.
     */
    ReceiverManager(std::recursive_mutex& manager_mutex, TimeshiftManager* timeshift_manager);
    /**
     * @brief Destructor.
     */
    ~ReceiverManager();

    /**
     * @brief Initializes all configured receivers.
     * @param rtp_listen_port The port for the main RTP receiver.
     * @param notification_queue The queue for sending notifications about new sources.
     * @return true on success, false otherwise.
     */
    bool initialize_receivers(int rtp_listen_port, std::shared_ptr<NotificationQueue> notification_queue);
    /**
     * @brief Starts all initialized receivers.
     */
    void start_receivers();
    /**
     * @brief Stops all running receivers.
     */
    void stop_receivers();
    /**
     * @brief Cleans up and destroys all receiver instances.
     */
    void cleanup_receivers();

    /**
     * @brief Gets the list of source tags seen by the main RTP receiver.
     * @return A vector of source tag strings.
     */
    std::vector<std::string> get_rtp_receiver_seen_tags();
    /**
     * @brief Gets the list of SAP announcements detected by the main RTP receiver.
     * @return A vector of SAP announcements with stream metadata.
     */
    std::vector<SapAnnouncement> get_rtp_sap_announcements();
    /**
     * @brief Gets the list of source tags seen by a specific Raw Scream receiver.
     * @param listen_port The port of the receiver to query.
     * @return A vector of source tag strings.
     */
    std::vector<std::string> get_raw_scream_receiver_seen_tags(int listen_port);
    /**
     * @brief Gets the list of source tags seen by a specific Per-Process Scream receiver.
     * @param listen_port The port of the receiver to query.
     * @return A vector of source tag strings.
     */
    std::vector<std::string> get_per_process_scream_receiver_seen_tags(int listen_port);
#if !defined(_WIN32)
    std::vector<std::string> get_pulse_receiver_seen_tags();
#endif

    std::optional<std::string> resolve_stream_tag(const std::string& tag);

    std::vector<std::string> list_stream_tags_for_wildcard(const std::string& wildcard_tag);

    void set_stream_tag_callbacks(
        std::function<void(const std::string&, const std::string&)> on_resolved,
        std::function<void(const std::string&)> on_removed);

    /**
     * @brief Ensures an ALSA capture receiver is active for the requested device tag.
     * @param tag ALSA capture tag (ac:<card>.<device>).
     * @param params Capture configuration parameters describing the desired stream.
     * @return true if the receiver exists or was started successfully.
     */
    bool ensure_capture_receiver(const std::string& tag, const CaptureParams& params);

    /**
     * @brief Releases a reference to a capture receiver.
     * @param tag ALSA capture tag previously passed to ensure_capture_receiver().
     */
    void release_capture_receiver(const std::string& tag);

    /**
     * @brief Set the format probe duration for RTP receivers.
     * @param duration_ms Probe duration in milliseconds.
     */
    void set_format_probe_duration_ms(double duration_ms);

    /**
     * @brief Set the minimum bytes required for format probe.
     * @param min_bytes Minimum bytes before detection.
     */
    void set_format_probe_min_bytes(size_t min_bytes);

    /**
     * @brief Logs the current status of receivers for debugging.
     */
    void log_status();

private:
    std::recursive_mutex& m_manager_mutex;
    TimeshiftManager* m_timeshift_manager;
    std::unique_ptr<ClockManager> m_clock_manager;

    std::unique_ptr<RtpReceiver> m_rtp_receiver;
    std::map<int, std::unique_ptr<RawScreamReceiver>> m_raw_scream_receivers;
    std::map<int, std::unique_ptr<PerProcessScreamReceiver>> m_per_process_scream_receivers;
#if !defined(_WIN32)
    std::unique_ptr<pulse::PulseAudioReceiver> m_pulse_receiver;
#endif
    std::unordered_map<std::string, std::unique_ptr<NetworkAudioReceiver>> capture_receivers_;
    std::unordered_map<std::string, size_t> capture_receiver_usage_;
    std::shared_ptr<NotificationQueue> m_notification_queue;

    std::function<void(const std::string&, const std::string&)> stream_tag_resolved_cb_;
    std::function<void(const std::string&)> stream_tag_removed_cb_;
};

} // namespace audio
} // namespace screamrouter

#endif // RECEIVER_MANAGER_H
