#include "sap_listener.h"
#include "../../senders/rtp/rtp_sender_registry.h"
#include "../../utils/cpp_logger.h"
#include "../../audio_constants.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <cerrno>
#ifndef _WIN32
    #include <sys/select.h>
#else
    // Define ssize_t for Windows if not already defined (e.g., by Python headers)
    #include <BaseTsd.h>
    #ifndef ssize_t
        typedef SSIZE_T ssize_t;
    #endif
#endif

namespace screamrouter {
namespace audio {

const int SAP_PORT = 9875;
const std::vector<std::string> MULTICAST_GROUPS = {"224.2.127.254", "224.0.0.56"};
const std::string UNICAST_ADDR = "0.0.0.0";

SapListener::SapListener(std::string logger_prefix, const std::vector<std::string>& known_ips)
    : logger_prefix_(logger_prefix), known_ips_(known_ips) {
}

SapListener::~SapListener() noexcept {
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

bool SapListener::get_stream_properties(uint32_t ssrc, StreamProperties& properties) {
    std::lock_guard<std::mutex> lock(ssrc_map_mutex_);
    auto it = ssrc_to_properties_.find(ssrc);
    if (it != ssrc_to_properties_.end()) {
        properties = it->second;
        return true;
    }
    return false;
}

bool SapListener::get_stream_properties_by_ip(const std::string& ip, StreamProperties& properties) {
    std::lock_guard<std::mutex> lock(ip_map_mutex_);
    auto it = ip_to_properties_.find(ip);
    if (it != ip_to_properties_.end()) {
        properties = it->second;
        return true;
    }
    return false;
}

std::vector<uint32_t> SapListener::get_known_ssrcs() {
    std::lock_guard<std::mutex> lock(ssrc_map_mutex_);
    std::vector<uint32_t> known_ssrcs;
    known_ssrcs.reserve(ssrc_to_properties_.size());
    for (const auto& pair : ssrc_to_properties_) {
        known_ssrcs.push_back(pair.first);
    }
    return known_ssrcs;
}

std::vector<SapAnnouncement> SapListener::get_announcements() {
    std::lock_guard<std::mutex> lock(ip_map_mutex_);
    std::vector<SapAnnouncement> announcements;
    announcements.reserve(announcements_by_stream_ip_.size());
    for (const auto& entry : announcements_by_stream_ip_) {
        announcements.push_back(entry.second);
    }
    return announcements;
}

void SapListener::set_session_callback(SessionCallback callback) {
    session_callback_ = std::move(callback);
}

void SapListener::run() {
    LOG_CPP_INFO("%s SAP listener thread started.", logger_prefix_.c_str());
    char buffer[2048];
    
    #ifndef _WIN32
        const int MAX_EVENTS = 64;
        struct epoll_event events[MAX_EVENTS];
    #endif

    while (running_) {
        #ifdef _WIN32
            // Windows: Use select()
            fd_set read_fds = master_read_fds_;
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            
            int n_events = select(max_fd_ + 1, &read_fds, NULL, NULL, &tv);
        #else
            // Linux: Use epoll
            int n_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, 1000); // 1-second timeout
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
            // Windows select: iterate through all sockets
            for (socket_t fd : sockets_) {
                if (FD_ISSET(fd, &read_fds)) {
                    struct sockaddr_in cliaddr;
                    socklen_t len = sizeof(cliaddr);
                    ssize_t n_received = recvfrom(fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&cliaddr, &len);

                    if (n_received > 0) {
                        buffer[n_received] = '\0';
                        char client_ip_str[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &(cliaddr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
                        process_sap_packet(buffer, n_received, client_ip_str);
                    }
                }
            }
        #else
            // Linux epoll: iterate through ready events
            for (int i = 0; i < n_events; ++i) {
                if ((events[i].events & EPOLLIN)) {
                    socket_t fd = events[i].data.fd;
                    struct sockaddr_in cliaddr;
                    socklen_t len = sizeof(cliaddr);
                    ssize_t n_received = recvfrom(fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&cliaddr, &len);

                    if (n_received > 0) {
                        buffer[n_received] = '\0';
                        char client_ip_str[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &(cliaddr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
                        process_sap_packet(buffer, n_received, client_ip_str);
                    }
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
        #ifdef _WIN32
            // epoll_fd_ doesn't exist on Windows
        #else
            close(epoll_fd_);
            epoll_fd_ = -1;
        #endif
        return false;
    }

    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) < 0) {
        LOG_CPP_WARNING("%s Failed to set SO_REUSEADDR", logger_prefix_.c_str());
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SAP_PORT);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_CPP_ERROR("%s Failed to bind to port %d", logger_prefix_.c_str(), SAP_PORT);
        #ifdef _WIN32
            closesocket(fd);
            // epoll_fd_ doesn't exist on Windows
        #else
            close(fd);
            close(epoll_fd_);
            epoll_fd_ = -1;
        #endif
        return false;
    }
    LOG_CPP_INFO("%s Successfully set up listener on 0.0.0.0:%d", logger_prefix_.c_str(), SAP_PORT);

    for (const auto& group : MULTICAST_GROUPS) {
        struct ip_mreq mreq;
        if (inet_pton(AF_INET, group.c_str(), &mreq.imr_multiaddr) <= 0) {
            LOG_CPP_ERROR("%s Failed to parse multicast group address %s", logger_prefix_.c_str(), group.c_str());
            continue;
        }
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0) {
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
            #ifdef _WIN32
                closesocket(fd);
                // epoll_fd_ doesn't exist on Windows
            #else
                close(fd);
                close(epoll_fd_);
                epoll_fd_ = -1;
            #endif
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
    if (!known_ips_.empty()) {
        bool is_known = false;
        for (const auto& ip : known_ips_) {
            if (ip == source_ip) {
                is_known = true;
                break;
            }
        }
        if (!is_known) {
            LOG_CPP_WARNING("%s Ignoring SAP packet from unknown IP: %s", logger_prefix_.c_str(), source_ip.c_str());
            return;
        }
    }

    // SAP Header Parsing
    if (size < 4) { // Minimum header size
        LOG_CPP_WARNING("%s SAP packet too small for header: %d bytes", logger_prefix_.c_str(), size);
        return;
    }

    uint8_t first_byte = buffer[0];
    uint8_t auth_len = (first_byte & 0x10) ? buffer[1] * 4 : 0;
    uint16_t msg_id_hash = ntohs(*(uint16_t*)(buffer + 2));

    const char* sdp_start = buffer + 4 + auth_len;
    int sdp_size = size - (4 + auth_len);

    if (sdp_size <= 0) {
        LOG_CPP_WARNING("%s Invalid SAP packet, no SDP data found", logger_prefix_.c_str());
        return;
    }
    
    std::string sdp_data(sdp_start, sdp_size);

    // Find SSRC from o= line
    uint32_t ssrc = 0;
    size_t o_pos = sdp_data.find("o=");
    if (o_pos != std::string::npos) {
        std::string o_line = sdp_data.substr(o_pos);
        o_line = o_line.substr(0, o_line.find('\n'));
        
        char username[64];
        unsigned long long session_id;
        int items_scanned = sscanf(o_line.c_str(), "o=%s %llu", username, &session_id);
        if (items_scanned == 2) {
            ssrc = static_cast<uint32_t>(session_id);
        } else {
            LOG_CPP_WARNING("%s Failed to parse SSRC from o-line: %s", logger_prefix_.c_str(), o_line.c_str());
            return;
        }
    } else {
        LOG_CPP_WARNING("%s o-line not found in SAP packet", logger_prefix_.c_str());
        return;
    }

    if (RtpSenderRegistry::get_instance().is_local_ssrc(ssrc)) {
        return;
    }

    // Find connection data from c= line
    std::string connection_ip;
    size_t c_pos = sdp_data.find("c=IN IP4 ");
    if (c_pos != std::string::npos) {
        std::string c_line = sdp_data.substr(c_pos);
        c_line = c_line.substr(0, c_line.find('\n'));
        char ip_addr[INET_ADDRSTRLEN];
        int items_scanned = sscanf(c_line.c_str(), "c=IN IP4 %s", ip_addr);
        if (items_scanned == 1) {
            connection_ip = ip_addr;
        } else {
            LOG_CPP_WARNING("%s Failed to parse IP from c-line: %s", logger_prefix_.c_str(), c_line.c_str());
        }
    } else {
        LOG_CPP_WARNING("%s c-line not found in SAP packet", logger_prefix_.c_str());
    }

    int port = 0;

    // Find media port from m= line
    size_t m_pos = sdp_data.find("m=audio ");
    if (m_pos != std::string::npos) {
        std::string m_line = sdp_data.substr(m_pos);
        m_line = m_line.substr(0, m_line.find('\n'));
        int items_scanned = sscanf(m_line.c_str(), "m=audio %d", &port);
        if (items_scanned == 1 && port > 0) {
            if (session_callback_ && !connection_ip.empty()) {
                session_callback_(connection_ip, port, source_ip);
            }
        } else {
            LOG_CPP_WARNING("%s Failed to parse port from m-line: %s", logger_prefix_.c_str(), m_line.c_str());
        }
    } else {
        LOG_CPP_WARNING("%s m-line not found in SAP packet", logger_prefix_.c_str());
    }
 
    // Find rtpmap
    size_t rtpmap_pos = sdp_data.find("a=rtpmap:");
    if (rtpmap_pos != std::string::npos) {
        std::string rtpmap_line = sdp_data.substr(rtpmap_pos);
        rtpmap_line = rtpmap_line.substr(0, rtpmap_line.find('\n'));

        int pt = 0, rate = 0, channels = 2; // default to 2 channels
        char encoding[32] = {0};

        int items_scanned = sscanf(rtpmap_line.c_str(), "a=rtpmap:%d %[^/]/%d/%d", &pt, encoding, &rate, &channels);
        
        if (items_scanned < 3) {
            LOG_CPP_ERROR("%s Failed to parse rtpmap line: %s", logger_prefix_.c_str(), rtpmap_line.c_str());
            return;
        }

        StreamProperties props;
        props.sample_rate = rate;
        props.channels = channels;
        
        if (strstr(encoding, "L16") != nullptr) {
            props.bit_depth = 16;
            props.endianness = Endianness::BIG;
        } else if (strstr(encoding, "L24") != nullptr) {
            props.bit_depth = 24;
            props.endianness = Endianness::BIG;
        } else if (strstr(encoding, "S16LE") != nullptr) {
            props.bit_depth = 16;
            props.endianness = Endianness::LITTLE;
        } else {
            props.bit_depth = 16; // default
            props.endianness = Endianness::BIG; // default
        }

        props.port = port;

        std::lock_guard<std::mutex> lock(ssrc_map_mutex_);
        ssrc_to_properties_[ssrc] = props;
        
        std::lock_guard<std::mutex> lock2(ip_map_mutex_);
        ip_to_properties_[source_ip] = props;
        if (!connection_ip.empty()) {
            ip_to_properties_[connection_ip] = props;
            SapAnnouncement announcement;
            announcement.stream_ip = connection_ip;
            announcement.announcer_ip = source_ip;
            announcement.port = port;
            announcement.properties = props;
            announcements_by_stream_ip_[connection_ip] = announcement;
        }

        LOG_CPP_DEBUG("%s Updated stream properties for SSRC %u from %s: %d Hz, %d channels, %d bits",
            logger_prefix_.c_str(), ssrc, source_ip.c_str(), props.sample_rate, props.channels, props.bit_depth);
    } else {
        LOG_CPP_WARNING("%s rtpmap not found in SAP packet", logger_prefix_.c_str());
    }
}

} // namespace audio
} // namespace screamrouter
