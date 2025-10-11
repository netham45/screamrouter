#include "receiver_manager.h"
#include "../utils/cpp_logger.h"

namespace screamrouter {
namespace audio {

ReceiverManager::ReceiverManager(std::recursive_mutex& manager_mutex, TimeshiftManager* timeshift_manager)
    : m_manager_mutex(manager_mutex), m_timeshift_manager(timeshift_manager) {
    LOG_CPP_INFO("ReceiverManager created.");
}

ReceiverManager::~ReceiverManager() {
    LOG_CPP_INFO("ReceiverManager destroyed.");
}

bool ReceiverManager::initialize_receivers(int rtp_listen_port, std::shared_ptr<NotificationQueue> notification_queue) {
    try {
        RtpReceiverConfig rtp_config;
        rtp_config.listen_port = rtp_listen_port;
        m_rtp_receiver = std::make_unique<RtpReceiver>(rtp_config, notification_queue, m_timeshift_manager);

        RawScreamReceiverConfig raw_config_1;
        raw_config_1.listen_port = 4010;
        m_raw_scream_receivers[4010] = std::make_unique<RawScreamReceiver>(raw_config_1, notification_queue, m_timeshift_manager, "RawScreamReceiver-4010");

        RawScreamReceiverConfig raw_config_2;
        raw_config_2.listen_port = 16401;
        m_raw_scream_receivers[16401] = std::make_unique<RawScreamReceiver>(raw_config_2, notification_queue, m_timeshift_manager, "RawScreamReceiver-16401");

        PerProcessScreamReceiverConfig per_process_config;
        per_process_config.listen_port = 16402;
        m_per_process_scream_receivers[16402] = std::make_unique<PerProcessScreamReceiver>(per_process_config, notification_queue, m_timeshift_manager, "PerProcessScreamReceiver-16402");

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
}

void ReceiverManager::cleanup_receivers() {
    m_rtp_receiver.reset();
    m_raw_scream_receivers.clear();
    m_per_process_scream_receivers.clear();
}

std::vector<std::string> ReceiverManager::get_rtp_receiver_seen_tags() {
    if (m_rtp_receiver) {
        return m_rtp_receiver->get_seen_tags();
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

} // namespace audio
} // namespace screamrouter