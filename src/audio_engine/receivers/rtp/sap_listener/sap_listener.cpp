#include "sap_listener.h"

#include "../../../senders/rtp/rtp_sender_registry.h"
#include "../../../utils/cpp_logger.h"
#include "sap_directory.h"
#include "sap_parser.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>

#ifndef _WIN32
    #include <sys/select.h>
#endif

namespace screamrouter {
namespace audio {

namespace {

constexpr int kSapPort = 9875;
const std::vector<std::string> kMulticastGroups = {"224.2.127.254", "224.0.0.56"};

} // namespace

SapListener::SapListener(std::string logger_prefix, const std::vector<std::string>& known_ips)
    : logger_prefix_(std::move(logger_prefix)),
      known_ips_(known_ips),
      directory_(std::make_unique<SapDirectory>()) {
#ifdef _WIN32
    FD_ZERO(&master_read_fds_);
    max_fd_ = NAR_INVALID_SOCKET_VALUE;
#endif
}

SapListener::~SapListener() {
    stop();
}

void SapListener::start() {
    if (running_) {
        return;
    }
    LOG_CPP_INFO("%s Starting SAP listener.", logger_prefix_.c_str());
    running_ = true;
    if (!setup_sockets()) {
        running_ = false;
        return;
    }
    thread_ = std::thread(&SapListener::run, this);
}

void SapListener::stop() {
    if (!running_) {
        return;
    }
    LOG_CPP_INFO("%s Stopping SAP listener.", logger_prefix_.c_str());
    running_ = false;
    close_sockets();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SapListener::set_session_callback(SessionCallback callback) {
    session_callback_ = std::move(callback);
}

bool SapListener::get_stream_properties(uint32_t ssrc, StreamProperties& properties) {
    if (!directory_) {
        return false;
    }
    return directory_->get_properties_for_ssrc(ssrc, properties);
}

bool SapListener::get_stream_properties_by_ip(const std::string& ip, int port, StreamProperties& properties) {
    if (!directory_) {
        return false;
    }
    return directory_->get_properties_for_ip(ip, port, properties);
}

std::vector<SapAnnouncement> SapListener::get_announcements() {
    if (!directory_) {
        return {};
    }
    return directory_->all_announcements();
}

bool SapListener::get_stream_identity(const std::string& ip,
                                      int port,
                                      std::string& guid,
                                      std::string& session_name,
                                      std::string& stream_ip_out,
                                      int& stream_port_out) {
    if (!directory_) {
        return false;
    }
    return directory_->get_identity(ip, port, guid, session_name, stream_ip_out, stream_port_out);
}

bool SapListener::get_stream_identity_by_ssrc(uint32_t ssrc,
                                              std::string& guid,
                                              std::string& session_name,
                                              std::string& stream_ip_out,
                                              int& stream_port_out) {
    if (!directory_) {
        return false;
    }
    return directory_->get_identity_by_ssrc(ssrc, guid, session_name, stream_ip_out, stream_port_out);
}

void SapListener::run() {
    LOG_CPP_INFO("%s SAP listener thread started.", logger_prefix_.c_str());
    char buffer[2048];

#ifndef _WIN32
    const int kMaxEvents = 64;
    struct epoll_event events[kMaxEvents];
#endif

    while (running_) {
#ifdef _WIN32
        fd_set read_fds = master_read_fds_;
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int n_events = select(static_cast<int>(max_fd_ + 1), &read_fds, nullptr, nullptr, &tv);
#else
        int n_events = epoll_wait(epoll_fd_, events, kMaxEvents, 1000);
#endif

        if (!running_) {
            break;
        }

        if (n_events < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) {
                continue;
            }
            LOG_CPP_ERROR("%s select() error: %d", logger_prefix_.c_str(), WSAGetLastError());
#else
            if (errno == EINTR) {
                continue;
            }
            LOG_CPP_ERROR("%s epoll_wait() error: %s", logger_prefix_.c_str(), strerror(errno));
#endif
            continue;
        }

#ifdef _WIN32
        for (socket_t fd : sockets_) {
            if (!FD_ISSET(fd, &read_fds)) {
                continue;
            }
            struct sockaddr_in cliaddr;
            socklen_t len = sizeof(cliaddr);
            int n_received = recvfrom(fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&cliaddr, &len);
            if (n_received > 0) {
                buffer[n_received] = '\0';
                char client_ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(cliaddr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
                process_sap_packet(buffer, static_cast<int>(n_received), client_ip_str);
            }
        }
#else
        for (int i = 0; i < n_events; ++i) {
            if (!(events[i].events & EPOLLIN)) {
                continue;
            }
            socket_t fd = events[i].data.fd;
            struct sockaddr_in cliaddr;
            socklen_t len = sizeof(cliaddr);
            ssize_t n_received = recvfrom(fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&cliaddr, &len);
            if (n_received > 0) {
                buffer[n_received] = '\0';
                char client_ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(cliaddr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
                process_sap_packet(buffer, static_cast<int>(n_received), client_ip_str);
            }
        }
#endif
    }
    LOG_CPP_INFO("%s SAP listener thread finished.", logger_prefix_.c_str());
}

bool SapListener::setup_sockets() {
#ifdef _WIN32
    FD_ZERO(&master_read_fds_);
    max_fd_ = NAR_INVALID_SOCKET_VALUE;
#else
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        LOG_CPP_ERROR("%s Failed to create epoll instance", logger_prefix_.c_str());
        return false;
    }
#endif

    socket_t fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOG_CPP_ERROR("%s Failed to create socket", logger_prefix_.c_str());
#ifndef _WIN32
        close(epoll_fd_);
        epoll_fd_ = -1;
#endif
        return false;
    }

    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse), sizeof(reuse)) < 0) {
        LOG_CPP_WARNING("%s Failed to set SO_REUSEADDR", logger_prefix_.c_str());
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kSapPort);

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_CPP_ERROR("%s Failed to bind to port %d", logger_prefix_.c_str(), kSapPort);
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
        close(epoll_fd_);
        epoll_fd_ = -1;
#endif
        return false;
    }
    LOG_CPP_INFO("%s Successfully set up listener on 0.0.0.0:%d", logger_prefix_.c_str(), kSapPort);

    for (const auto& group : kMulticastGroups) {
        struct ip_mreq mreq;
        if (inet_pton(AF_INET, group.c_str(), &mreq.imr_multiaddr) <= 0) {
            LOG_CPP_ERROR("%s Failed to parse multicast group address %s", logger_prefix_.c_str(), group.c_str());
            continue;
        }
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<char*>(&mreq), sizeof(mreq)) < 0) {
            LOG_CPP_ERROR("%s Failed to join multicast group %s", logger_prefix_.c_str(), group.c_str());
        } else {
            LOG_CPP_INFO("%s Successfully joined multicast group %s", logger_prefix_.c_str(), group.c_str());
        }
    }

    char loopch = 1;
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loopch, sizeof(loopch)) < 0) {
        LOG_CPP_WARNING("%s Failed to set IP_MULTICAST_LOOP", logger_prefix_.c_str());
    }

#ifdef _WIN32
    FD_SET(fd, &master_read_fds_);
    if (fd > max_fd_) {
        max_fd_ = fd;
    }
    sockets_.push_back(fd);
    return true;
#else
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == -1) {
        LOG_CPP_ERROR("%s Failed to add socket to epoll", logger_prefix_.c_str());
        close(fd);
        close(epoll_fd_);
        epoll_fd_ = -1;
        return false;
    }

    sockets_.push_back(fd);
    return true;
#endif
}

void SapListener::close_sockets() {
#ifdef _WIN32
    FD_ZERO(&master_read_fds_);
    max_fd_ = NAR_INVALID_SOCKET_VALUE;
#else
    if (epoll_fd_ != -1) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
#endif
    for (socket_t fd : sockets_) {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
    }
    sockets_.clear();
    LOG_CPP_INFO("%s All SAP sockets closed.", logger_prefix_.c_str());
}

void SapListener::process_sap_packet(const char* buffer, int size, const std::string& source_ip) {
    LOG_CPP_DEBUG("%s Received SAP packet from %s (%d bytes)", logger_prefix_.c_str(), source_ip.c_str(), size);
    if (!known_ips_.empty()) {
        const bool is_known = std::find(known_ips_.begin(), known_ips_.end(), source_ip) != known_ips_.end();
        if (!is_known) {
            LOG_CPP_WARNING("%s Ignoring SAP packet from unknown IP: %s", logger_prefix_.c_str(), source_ip.c_str());
            return;
        }
    }

    ParsedSapInfo parsed;
    if (!parse_sap_packet(buffer, size, logger_prefix_, parsed)) {
        return;
    }

    if (RtpSenderRegistry::get_instance().is_local_ssrc(parsed.ssrc)) {
        LOG_CPP_DEBUG("%s Ignoring SAP packet for local SSRC %u from %s", logger_prefix_.c_str(), parsed.ssrc, source_ip.c_str());
        return;
    }

    if (session_callback_ && !parsed.connection_ip.empty() && parsed.port > 0) {
        session_callback_(parsed.connection_ip, parsed.port, source_ip);
    }

    if (directory_) {
        directory_->upsert(parsed.ssrc,
                           source_ip,
                           parsed.connection_ip,
                           parsed.port,
                           parsed.properties,
                           parsed.stream_guid,
                           parsed.target_sink,
                           parsed.target_host,
                           parsed.session_name);
    }

    const char* codec_name = "unknown";
    if (parsed.properties.codec == StreamCodec::OPUS) {
        codec_name = "opus";
    } else if (parsed.properties.codec == StreamCodec::PCM) {
        codec_name = "pcm";
    } else if (parsed.properties.codec == StreamCodec::PCMU) {
        codec_name = "pcmu";
    } else if (parsed.properties.codec == StreamCodec::PCMA) {
        codec_name = "pcma";
    }

    LOG_CPP_INFO(
        "%s SAP update: SSRC %u from %s -> %s:%d (pt=%d codec=%s sr=%d ch=%d)",
        logger_prefix_.c_str(),
        parsed.ssrc,
        source_ip.c_str(),
        parsed.connection_ip.empty() ? source_ip.c_str() : parsed.connection_ip.c_str(),
        parsed.port,
        parsed.properties.payload_type,
        codec_name,
        parsed.properties.sample_rate,
        parsed.properties.channels);
}

} // namespace audio
} // namespace screamrouter
