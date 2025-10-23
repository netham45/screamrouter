#ifndef SCREAMROUTER_AUDIO_PULSE_PULSE_RECEIVER_H
#define SCREAMROUTER_AUDIO_PULSE_PULSE_RECEIVER_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "../../utils/audio_component.h"
#include "../../utils/thread_safe_queue.h"
#include "../../audio_types.h"
#include "../clock_manager.h"

namespace screamrouter {
namespace audio {

class TimeshiftManager;

namespace pulse {

struct PulseReceiverConfig {
    uint16_t tcp_listen_port = 0;            ///< 0 disables TCP listener
    bool require_auth_cookie = false;       ///< If true, clients must authenticate using cookie
#if !defined(_WIN32)
    std::string unix_socket_path;           ///< Absolute path for native protocol UNIX socket
    std::string auth_cookie_path;           ///< Optional cookie file path
    std::string socket_owner_user;          ///< Optional owner username for the UNIX socket
    std::string socket_owner_group;         ///< Optional group name for the UNIX socket
    int socket_permissions = 0660;          ///< File mode for the UNIX socket
#endif
};

/**
 * @brief Receives PulseAudio native-protocol streams and forwards PCM into ScreamRouter.
 */
class PulseAudioReceiver : public AudioComponent {
public:
    PulseAudioReceiver(PulseReceiverConfig config,
                       std::shared_ptr<NotificationQueue> notification_queue,
                       TimeshiftManager* timeshift_manager,
                       ClockManager* clock_manager,
                       std::string logger_prefix = "PulseAudioReceiver");

    ~PulseAudioReceiver() override;

    void start() override;
    void stop() override;

    std::vector<std::string> get_seen_tags();

    const PulseReceiverConfig& config() const { return config_; }

private:
    void run() override;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    PulseReceiverConfig config_;
};

} // namespace pulse
} // namespace audio
} // namespace screamrouter

#endif // SCREAMROUTER_AUDIO_PULSE_PULSE_RECEIVER_H
