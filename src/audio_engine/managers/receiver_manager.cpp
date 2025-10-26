#include "receiver_manager.h"
#include "../utils/cpp_logger.h"
#include <exception>
#include <chrono>

namespace screamrouter {
namespace audio {

ReceiverManager::ReceiverManager(std::recursive_mutex& manager_mutex, TimeshiftManager* timeshift_manager)
    : m_manager_mutex(manager_mutex), m_timeshift_manager(timeshift_manager) {
    try {
        m_clock_manager = std::make_unique<ClockManager>();
    } catch (const std::exception& ex) {
        LOG_CPP_ERROR("Failed to create ClockManager: %s", ex.what());
        throw;
    }
    LOG_CPP_INFO("ReceiverManager created.");
}

ReceiverManager::~ReceiverManager() {
    LOG_CPP_INFO("ReceiverManager destroyed.");
}

bool ReceiverManager::initialize_receivers(int rtp_listen_port, std::shared_ptr<NotificationQueue> notification_queue) {
    try {
        m_notification_queue = notification_queue;
        RtpReceiverConfig rtp_config;
        rtp_config.listen_port = rtp_listen_port;
        m_rtp_receiver = std::make_unique<RtpReceiver>(rtp_config, notification_queue, m_timeshift_manager, m_clock_manager.get());

        RawScreamReceiverConfig raw_config_1;
        raw_config_1.listen_port = 4010;
        m_raw_scream_receivers[4010] = std::make_unique<RawScreamReceiver>(raw_config_1, notification_queue, m_timeshift_manager, m_clock_manager.get(), "RawScreamReceiver-4010");

        RawScreamReceiverConfig raw_config_2;
        raw_config_2.listen_port = 16401;
        m_raw_scream_receivers[16401] = std::make_unique<RawScreamReceiver>(raw_config_2, notification_queue, m_timeshift_manager, m_clock_manager.get(), "RawScreamReceiver-16401");

        PerProcessScreamReceiverConfig per_process_config;
        per_process_config.listen_port = 16402;
        m_per_process_scream_receivers[16402] = std::make_unique<PerProcessScreamReceiver>(per_process_config, notification_queue, m_timeshift_manager, m_clock_manager.get(), "PerProcessScreamReceiver-16402");

#if !defined(_WIN32)
        pulse::PulseReceiverConfig pulse_config;
        pulse_config.tcp_listen_port = 4713;
        pulse_config.unix_socket_path = std::string(getenv("XDG_RUNTIME_DIR")) + std::string("/pulse");
        pulse_config.require_auth_cookie = false;
        m_pulse_receiver = std::make_unique<pulse::PulseAudioReceiver>(pulse_config, notification_queue, m_timeshift_manager, m_clock_manager.get(), "PulseAudioReceiver");
        if (stream_tag_resolved_cb_ || stream_tag_removed_cb_) {
            m_pulse_receiver->set_stream_tag_callbacks(stream_tag_resolved_cb_, stream_tag_removed_cb_);
        }
#endif

    } catch (const std::exception& e) {
        LOG_CPP_ERROR("Failed during receiver creation in initialize: %s", e.what());
        return false;
    }
    return true;
}

void ReceiverManager::start_receivers() {
    if (m_rtp_receiver) {
        m_rtp_receiver->start();
    }
    for (auto const& [port, receiver] : m_raw_scream_receivers) {
        receiver->start();
        LOG_CPP_INFO("RawScreamReceiver started on port %d.", port);
    }
    for (auto const& [port, receiver] : m_per_process_scream_receivers) {
        receiver->start();
        LOG_CPP_INFO("PerProcessScreamReceiver started on port %d.", port);
    }
#if !defined(_WIN32)
    if (m_pulse_receiver) {
        m_pulse_receiver->start();
        LOG_CPP_INFO("PulseAudioReceiver started.");
    }
#endif
}

void ReceiverManager::stop_receivers() {
    if (m_rtp_receiver) {
        m_rtp_receiver->stop();
    }
    for (auto const& [port, receiver] : m_raw_scream_receivers) {
        receiver->stop();
    }
    for (auto const& [port, receiver] : m_per_process_scream_receivers) {
        receiver->stop();
    }
#if !defined(_WIN32)
    if (m_pulse_receiver) {
        m_pulse_receiver->stop();
    }
#endif
    for (auto& [tag, receiver] : capture_receivers_) {
        if (receiver) {
            receiver->stop();
        }
    }
}

void ReceiverManager::cleanup_receivers() {
    m_rtp_receiver.reset();
    m_raw_scream_receivers.clear();
    m_per_process_scream_receivers.clear();
#if !defined(_WIN32)
    m_pulse_receiver.reset();
#endif
    capture_receivers_.clear();
    capture_receiver_usage_.clear();
}

std::vector<std::string> ReceiverManager::get_rtp_receiver_seen_tags() {
    if (m_rtp_receiver) {
        return m_rtp_receiver->get_seen_tags();
    }
    return {};
}

std::vector<SapAnnouncement> ReceiverManager::get_rtp_sap_announcements() {
    if (m_rtp_receiver) {
        return m_rtp_receiver->get_sap_announcements();
    }
    return {};
}

std::vector<std::string> ReceiverManager::get_raw_scream_receiver_seen_tags(int listen_port) {
    auto it = m_raw_scream_receivers.find(listen_port);
    if (it != m_raw_scream_receivers.end() && it->second) {
        return it->second->get_seen_tags();
    }
    LOG_CPP_WARNING("RawScreamReceiver not found for port: %d when calling get_raw_scream_receiver_seen_tags.", listen_port);
    return {};
}

std::vector<std::string> ReceiverManager::get_per_process_scream_receiver_seen_tags(int listen_port) {
    auto it = m_per_process_scream_receivers.find(listen_port);
    if (it != m_per_process_scream_receivers.end() && it->second) {
        return it->second->get_seen_tags();
    }
    LOG_CPP_WARNING("PerProcessScreamReceiver not found for port: %d when calling get_per_process_scream_receiver_seen_tags.", listen_port);
    return {};
}

#if !defined(_WIN32)
std::vector<std::string> ReceiverManager::get_pulse_receiver_seen_tags() {
    if (m_pulse_receiver) {
        return m_pulse_receiver->get_seen_tags();
    }
    return {};
}
#endif

std::optional<std::string> ReceiverManager::resolve_stream_tag(const std::string& tag) {
#if !defined(_WIN32)
    if (m_pulse_receiver) {
        LOG_CPP_DEBUG("[ReceiverManager] resolve_stream_tag('%s') -> querying Pulse", tag.c_str());
        auto resolved = m_pulse_receiver->resolve_stream_tag(tag);
        if (resolved) {
            LOG_CPP_INFO("[ReceiverManager] resolve_stream_tag('%s') => '%s'", tag.c_str(), resolved->c_str());
            return resolved;
        }
    }
#endif
    LOG_CPP_DEBUG("[ReceiverManager] resolve_stream_tag('%s') => <none>", tag.c_str());
    (void)tag;
    return std::nullopt;
}

std::vector<std::string> ReceiverManager::list_stream_tags_for_wildcard(const std::string& wildcard_tag) {
#if !defined(_WIN32)
    if (m_pulse_receiver) {
        return m_pulse_receiver->list_stream_tags_for_wildcard(wildcard_tag);
    }
#else
    (void)wildcard_tag;
#endif
    return {};
}

void ReceiverManager::set_stream_tag_callbacks(
    std::function<void(const std::string&, const std::string&)> on_resolved,
    std::function<void(const std::string&)> on_removed) {
    stream_tag_resolved_cb_ = std::move(on_resolved);
    stream_tag_removed_cb_ = std::move(on_removed);
#if !defined(_WIN32)
    if (m_pulse_receiver) {
        m_pulse_receiver->set_stream_tag_callbacks(stream_tag_resolved_cb_, stream_tag_removed_cb_);
    }
#else
    (void)stream_tag_resolved_cb_;
    (void)stream_tag_removed_cb_;
#endif
}

bool ReceiverManager::ensure_capture_receiver(const std::string& tag, const CaptureParams& params) {
    const auto t0 = std::chrono::steady_clock::now();
    std::scoped_lock lock(m_manager_mutex);

    auto usage_it = capture_receiver_usage_.find(tag);
    if (usage_it != capture_receiver_usage_.end()) {
        usage_it->second++;
        auto receiver_it = capture_receivers_.find(tag);
        if (receiver_it != capture_receivers_.end() && receiver_it->second && !receiver_it->second->is_running()) {
            receiver_it->second->start();
            if (!receiver_it->second->is_running()) {
                LOG_CPP_ERROR("ReceiverManager failed to restart capture receiver %s.", tag.c_str());
                return false;
            }
        }
        LOG_CPP_INFO("ReceiverManager ensured existing capture receiver %s (ref_count=%zu).",
                     tag.c_str(), usage_it->second);
        const auto t1 = std::chrono::steady_clock::now();
        LOG_CPP_INFO("[ReceiverManager] ensure_capture(existing) %s (ref=%zu) in %lld ms",
                     tag.c_str(), usage_it->second,
                     (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        return true;
    }

    if (!m_notification_queue) {
        LOG_CPP_ERROR("ReceiverManager cannot create capture receiver %s without notification queue.", tag.c_str());
        return false;
    }

    std::unique_ptr<NetworkAudioReceiver> receiver;

#if SCREAMROUTER_FIFO_CAPTURE_AVAILABLE
    if (!receiver && system_audio::tag_has_prefix(tag, system_audio::kScreamrouterCapturePrefix)) {
        receiver = std::make_unique<ScreamrouterFifoReceiver>(tag, params, m_notification_queue, m_timeshift_manager);
    }
#endif

#if SCREAMROUTER_ALSA_CAPTURE_AVAILABLE
    if (!receiver && system_audio::tag_has_prefix(tag, system_audio::kAlsaCapturePrefix)) {
        receiver = std::make_unique<AlsaCaptureReceiver>(tag, params, m_notification_queue, m_timeshift_manager);
    }
#endif

#if SCREAMROUTER_WASAPI_CAPTURE_AVAILABLE
    if (!receiver && (system_audio::tag_has_prefix(tag, system_audio::kWasapiCapturePrefix) ||
                      system_audio::tag_has_prefix(tag, system_audio::kWasapiLoopbackPrefix))) {
        receiver = std::make_unique<system_audio::WasapiCaptureReceiver>(tag, params, m_notification_queue, m_timeshift_manager);
    }
#endif

    if (!receiver) {
        LOG_CPP_WARNING("ReceiverManager has no capture backend for tag %s.", tag.c_str());
        return false;
    }

    const auto t_s0 = std::chrono::steady_clock::now();
    receiver->start();
    const auto t_s1 = std::chrono::steady_clock::now();
    if (!receiver->is_running()) {
        LOG_CPP_ERROR("ReceiverManager created capture receiver %s but it failed to start.", tag.c_str());
        return false;
    }

    capture_receiver_usage_[tag] = 1;
    capture_receivers_[tag] = std::move(receiver);
    LOG_CPP_INFO("ReceiverManager started capture receiver %s (start=%lld ms total=%lld ms).",
                 tag.c_str(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_s1 - t_s0).count(),
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_s1 - t0).count());
    return true;
}

void ReceiverManager::release_capture_receiver(const std::string& tag) {
    std::scoped_lock lock(m_manager_mutex);

    auto usage_it = capture_receiver_usage_.find(tag);
    if (usage_it == capture_receiver_usage_.end()) {
        LOG_CPP_WARNING("ReceiverManager release requested for unknown capture receiver %s.", tag.c_str());
        return;
    }

    if (usage_it->second > 0) {
        usage_it->second--;
    }

    if (usage_it->second == 0) {
        auto receiver_it = capture_receivers_.find(tag);
        if (receiver_it != capture_receivers_.end() && receiver_it->second) {
            receiver_it->second->stop();
            capture_receivers_.erase(receiver_it);
        }
        capture_receiver_usage_.erase(usage_it);
        LOG_CPP_INFO("ReceiverManager released capture receiver %s.", tag.c_str());
    } else {
        LOG_CPP_INFO("ReceiverManager decremented capture receiver %s (ref_count=%zu).",
                     tag.c_str(), usage_it->second);
    }
}

void ReceiverManager::log_status() {
    // RTP
    if (m_rtp_receiver) {
        LOG_CPP_INFO("[ReceiverManager] RTP receiver running=%d", m_rtp_receiver->is_running() ? 1 : 0);
    } else {
        LOG_CPP_INFO("[ReceiverManager] RTP receiver: none");
    }

    // Raw scream receivers
    LOG_CPP_INFO("[ReceiverManager] Raw scream receivers: %zu", m_raw_scream_receivers.size());
    for (auto const& [port, recv] : m_raw_scream_receivers) {
        LOG_CPP_INFO("  - RawScream port %d running=%d", port, recv && recv->is_running() ? 1 : 0);
    }

    // Per-process scream receivers
    LOG_CPP_INFO("[ReceiverManager] Per-process scream receivers: %zu", m_per_process_scream_receivers.size());
    for (auto const& [port, recv] : m_per_process_scream_receivers) {
        LOG_CPP_INFO("  - PerProcessScream port %d running=%d", port, recv && recv->is_running() ? 1 : 0);
    }

#if !defined(_WIN32)
    // Pulse receiver
    if (m_pulse_receiver) {
        LOG_CPP_INFO("[ReceiverManager] PulseAudio receiver running=%d", m_pulse_receiver->is_running() ? 1 : 0);
    } else {
        LOG_CPP_INFO("[ReceiverManager] PulseAudio receiver: none");
    }
#endif
    // Capture receivers
    LOG_CPP_INFO("[ReceiverManager] Capture receivers: %zu", capture_receivers_.size());
    for (auto const& [tag, recv] : capture_receivers_) {
        LOG_CPP_INFO("  - Capture %s running=%d", tag.c_str(), recv && recv->is_running() ? 1 : 0);
    }
}

} // namespace audio
} // namespace screamrouter
