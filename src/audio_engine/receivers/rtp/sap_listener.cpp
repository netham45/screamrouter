#include "sap_listener.h"
#include "../../senders/rtp/rtp_sender_registry.h"
#include "../../utils/cpp_logger.h"
#include "../../audio_constants.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <cerrno>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <limits>
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

namespace {

std::string trim_copy(const std::string& input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = input.find_last_not_of(" \t\r\n");
    return input.substr(first, last - first + 1);
}

int safe_atoi(const std::string& value, int fallback = 0) {
    char* end_ptr = nullptr;
    const long parsed = std::strtol(value.c_str(), &end_ptr, 10);
    if (end_ptr == value.c_str() || *end_ptr != '\0') {
        return fallback;
    }
    return static_cast<int>(parsed);
}

std::vector<uint8_t> parse_channel_mapping(const std::string& mapping_value) {
    std::vector<uint8_t> mapping;
    std::string normalized = mapping_value;
    std::replace(normalized.begin(), normalized.end(), '/', ',');
    std::stringstream ss(normalized);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = trim_copy(token);
        if (token.empty()) {
            continue;
        }
        int value = safe_atoi(token, -1);
        if (value < 0 || value > std::numeric_limits<uint8_t>::max()) {
            mapping.clear();
            return mapping;
        }
        mapping.push_back(static_cast<uint8_t>(value));
    }
    return mapping;
}

void lowercase_in_place(std::string& text) {
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
}

std::string make_ip_port_key(const std::string& ip, int port) {
    if (port <= 0) {
        return ip;
    }
    return ip + ":" + std::to_string(port);
}

} // namespace

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

bool SapListener::get_stream_properties_by_ip(const std::string& ip, int port, StreamProperties& properties) {
    const std::string key = make_ip_port_key(ip, port);
    std::lock_guard<std::mutex> lock(ip_map_mutex_);
    auto it = ip_to_properties_.find(key);
    if (it == ip_to_properties_.end() && port > 0) {
        // Fall back to matching on IP only if no per-port entry exists.
        it = ip_to_properties_.find(ip);
    }
    if (it != ip_to_properties_.end()) {
        properties = it->second;
        return true;
    }
    return false;
}

std::vector<SapAnnouncement> SapListener::get_announcements() {
    std::lock_guard<std::mutex> lock(ip_map_mutex_);
    std::vector<SapAnnouncement> announcements;
    announcements.reserve(announcements_by_stream_endpoint_.size());
    for (const auto& entry : announcements_by_stream_endpoint_) {
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

    std::vector<std::string> sdp_lines;
    sdp_lines.reserve(16);
    {
        std::istringstream sdp_stream(sdp_data);
        std::string raw_line;
        while (std::getline(sdp_stream, raw_line)) {
            if (!raw_line.empty() && raw_line.back() == '\r') {
                raw_line.pop_back();
            }
            sdp_lines.push_back(raw_line);
        }
    }

    if (sdp_lines.empty()) {
        LOG_CPP_WARNING("%s SDP payload was empty in SAP packet from %s", logger_prefix_.c_str(), source_ip.c_str());
        return;
    }

    uint32_t ssrc = 0;
    bool ssrc_found = false;
    for (const auto& line : sdp_lines) {
        if (line.rfind("o=", 0) == 0) {
            char username[64] = {0};
            unsigned long long session_id = 0;
            if (std::sscanf(line.c_str(), "o=%63s %llu", username, &session_id) == 2) {
                ssrc = static_cast<uint32_t>(session_id);
                ssrc_found = true;
            } else {
                LOG_CPP_WARNING("%s Failed to parse SSRC from o-line: %s", logger_prefix_.c_str(), line.c_str());
            }
            break;
        }
    }

    if (!ssrc_found) {
        LOG_CPP_WARNING("%s o-line not found or malformed in SAP packet", logger_prefix_.c_str());
        return;
    }

    if (RtpSenderRegistry::get_instance().is_local_ssrc(ssrc)) {
        return;
    }

    std::string connection_ip;
    for (const auto& line : sdp_lines) {
        if (line.rfind("c=IN IP4 ", 0) == 0) {
            char ip_addr[INET_ADDRSTRLEN];
            if (std::sscanf(line.c_str(), "c=IN IP4 %15s", ip_addr) == 1) {
                connection_ip = ip_addr;
            } else {
                LOG_CPP_WARNING("%s Failed to parse IP from c-line: %s", logger_prefix_.c_str(), line.c_str());
            }
            break;
        }
    }

    int port = 0;
    std::string target_sink;
    std::string target_host;
    std::vector<int> audio_payload_types;
    for (const auto& line : sdp_lines) {
        if (line.rfind("m=audio ", 0) == 0) {
            std::string m_body = line.substr(std::strlen("m=audio "));
            std::stringstream m_stream(m_body);
            m_stream >> port;
            std::string proto;
            m_stream >> proto;
            int payload_type = 0;
            while (m_stream >> payload_type) {
                audio_payload_types.push_back(payload_type);
            }
            if (session_callback_ && !connection_ip.empty() && port > 0) {
                session_callback_(connection_ip, port, source_ip);
            }
            break;
        } else if (line.rfind("a=x-screamrouter-target:", 0) == 0) {
            std::string target_block = trim_copy(line.substr(std::strlen("a=x-screamrouter-target:")));
            if (!target_block.empty()) {
                std::stringstream ss(target_block);
                std::string token;
                while (std::getline(ss, token, ';')) {
                    const auto eq_pos = token.find('=');
                    std::string key = trim_copy(token.substr(0, eq_pos));
                    std::string value = (eq_pos != std::string::npos) ? trim_copy(token.substr(eq_pos + 1)) : "";
                    lowercase_in_place(key);
                    if (key == "sink") {
                        target_sink = trim_copy(value);
                    } else if (key == "host") {
                        target_host = trim_copy(value);
                        lowercase_in_place(target_host);
                    }
                }
                if (target_sink.empty()) {
                    target_sink = trim_copy(target_block);
                }
            }
        }
    }

    struct RtpmapEntry {
        std::string encoding;
        int sample_rate = 0;
        int channels = 0;
        bool has_explicit_channels = false;
    };

    std::unordered_map<int, RtpmapEntry> rtpmap_entries;
    std::unordered_map<int, std::unordered_map<std::string, std::string>> fmtp_entries;

    for (const auto& line : sdp_lines) {
        if (line.rfind("a=rtpmap:", 0) == 0) {
            std::string remainder = trim_copy(line.substr(std::strlen("a=rtpmap:")));
            const auto space_pos = remainder.find(' ');
            if (space_pos == std::string::npos) {
                LOG_CPP_WARNING("%s Malformed rtpmap line (missing space): %s", logger_prefix_.c_str(), line.c_str());
                continue;
            }

            const std::string pt_str = remainder.substr(0, space_pos);
            const int payload_type = safe_atoi(pt_str, -1);
            if (payload_type < 0) {
                LOG_CPP_WARNING("%s Failed to parse payload type in rtpmap: %s", logger_prefix_.c_str(), line.c_str());
                continue;
            }

            std::string encoding_block = trim_copy(remainder.substr(space_pos + 1));
            const auto first_slash = encoding_block.find('/');
            if (first_slash == std::string::npos) {
                LOG_CPP_WARNING("%s Malformed rtpmap payload descriptor: %s", logger_prefix_.c_str(), line.c_str());
                continue;
            }

            std::string encoding_name = encoding_block.substr(0, first_slash);
            lowercase_in_place(encoding_name);
            encoding_block.erase(0, first_slash + 1);

            int sample_rate = 0;
            int channels = 0;
            bool has_explicit_channels = false;

            const auto second_slash = encoding_block.find('/');
            if (second_slash == std::string::npos) {
                sample_rate = safe_atoi(trim_copy(encoding_block));
            } else {
                sample_rate = safe_atoi(trim_copy(encoding_block.substr(0, second_slash)));
                const std::string channels_str = trim_copy(encoding_block.substr(second_slash + 1));
                channels = safe_atoi(channels_str);
                has_explicit_channels = channels > 0;
            }

            RtpmapEntry entry;
            entry.encoding = encoding_name;
            entry.sample_rate = sample_rate;
            entry.channels = channels;
            entry.has_explicit_channels = has_explicit_channels;
            rtpmap_entries[payload_type] = entry;
        } else if (line.rfind("a=fmtp:", 0) == 0) {
            std::string remainder = trim_copy(line.substr(std::strlen("a=fmtp:")));
            const auto space_pos = remainder.find(' ');
            if (space_pos == std::string::npos) {
                continue;
            }
            const std::string pt_str = remainder.substr(0, space_pos);
            const int payload_type = safe_atoi(pt_str, -1);
            if (payload_type < 0) {
                continue;
            }

            std::string params_block = remainder.substr(space_pos + 1);
            auto& params = fmtp_entries[payload_type];

            std::stringstream param_stream(params_block);
            std::string param;
            while (std::getline(param_stream, param, ';')) {
                param = trim_copy(param);
                if (param.empty()) {
                    continue;
                }
                const auto equals_pos = param.find('=');
                std::string key;
                std::string value;
                if (equals_pos == std::string::npos) {
                    key = param;
                } else {
                    key = trim_copy(param.substr(0, equals_pos));
                    value = trim_copy(param.substr(equals_pos + 1));
                }
                lowercase_in_place(key);
                params[key] = value;
            }
        }
    }

    // Extract target hints from fmtp before payload selection
    for (const auto& kv : fmtp_entries) {
        const auto target_it = kv.second.find("x-screamrouter-target");
        if (target_it != kv.second.end()) {
            std::string target_block = target_it->second;
            std::stringstream ss(target_block);
            std::string token;
            while (std::getline(ss, token, ';')) {
                const auto eq_pos = token.find('=');
                std::string key = trim_copy(token.substr(0, eq_pos));
                std::string value = (eq_pos != std::string::npos) ? trim_copy(token.substr(eq_pos + 1)) : "";
                lowercase_in_place(key);
                if (key == "sink") {
                    target_sink = trim_copy(value);
                } else if (key == "host") {
                    target_host = trim_copy(value);
                    lowercase_in_place(target_host);
                }
            }
            if (target_sink.empty()) {
                target_sink = trim_copy(target_block);
            }
        }
    }

    int chosen_payload_type = -1;
    StreamCodec chosen_codec = StreamCodec::UNKNOWN;
    const RtpmapEntry* chosen_entry = nullptr;

    auto try_select_payload = [&](const std::string& needle, StreamCodec codec) -> bool {
        for (int pt : audio_payload_types) {
            auto it = rtpmap_entries.find(pt);
            if (it != rtpmap_entries.end() && it->second.encoding.find(needle) != std::string::npos) {
                chosen_payload_type = pt;
                chosen_codec = codec;
                chosen_entry = &it->second;
                return true;
            }
        }
        for (const auto& kv : rtpmap_entries) {
            if (kv.second.encoding.find(needle) != std::string::npos) {
                chosen_payload_type = kv.first;
                chosen_codec = codec;
                chosen_entry = &kv.second;
                return true;
            }
        }
        return false;
    };

    if (!audio_payload_types.empty() || !rtpmap_entries.empty()) {
        if (!try_select_payload("opus", StreamCodec::OPUS)) {
            if (!try_select_payload("l24", StreamCodec::PCM) &&
                !try_select_payload("l16", StreamCodec::PCM) &&
                !try_select_payload("s16le", StreamCodec::PCM) &&
                !try_select_payload("pcm", StreamCodec::PCM)) {
                for (int pt : audio_payload_types) {
                    auto it = rtpmap_entries.find(pt);
                    if (it != rtpmap_entries.end()) {
                        chosen_payload_type = pt;
                        chosen_entry = &it->second;
                        break;
                    }
                }
                if (chosen_entry == nullptr && !rtpmap_entries.empty()) {
                    chosen_payload_type = rtpmap_entries.begin()->first;
                    chosen_entry = &rtpmap_entries.begin()->second;
                }
            }
        }
    }

    if (!chosen_entry) {
        LOG_CPP_WARNING("%s No usable rtpmap entry found in SAP packet for SSRC %u", logger_prefix_.c_str(), ssrc);
        return;
    }

    if (chosen_codec == StreamCodec::UNKNOWN) {
        if (chosen_entry->encoding.find("opus") != std::string::npos) {
            chosen_codec = StreamCodec::OPUS;
        } else if (chosen_entry->encoding.find("l24") != std::string::npos ||
                   chosen_entry->encoding.find("l16") != std::string::npos ||
                   chosen_entry->encoding.find("s16le") != std::string::npos ||
                   chosen_entry->encoding.find("pcm") != std::string::npos) {
            chosen_codec = StreamCodec::PCM;
        }
    }

    int derived_channels = chosen_entry->has_explicit_channels ? chosen_entry->channels : 0;
    int fmtp_streams = 0;
    int fmtp_coupled_streams = 0;
    int fmtp_mapping_family = -1;
    std::vector<uint8_t> fmtp_channel_mapping;

    const auto fmtp_it = fmtp_entries.find(chosen_payload_type);
    if (fmtp_it != fmtp_entries.end()) {
        const auto& params = fmtp_it->second;
        auto channel_param = params.find("channels");
        if (channel_param != params.end()) {
            const int fmtp_channels = safe_atoi(channel_param->second, 0);
            if (fmtp_channels > 0) {
                derived_channels = fmtp_channels;
            }
        }

        auto channel_mapping_param = params.find("channelmapping");
        if (channel_mapping_param == params.end()) {
            channel_mapping_param = params.find("channel_mapping");
        }
        if (channel_mapping_param != params.end()) {
            const auto parsed_mapping = parse_channel_mapping(channel_mapping_param->second);
            if (!parsed_mapping.empty()) {
                fmtp_channel_mapping = parsed_mapping;
                const int mapping_channels = static_cast<int>(fmtp_channel_mapping.size());
                if (mapping_channels > 0) {
                    derived_channels = mapping_channels;
                }
            }
        }

        auto mapping_family_param = params.find("mappingfamily");
        if (mapping_family_param == params.end()) {
            mapping_family_param = params.find("mapping_family");
        }
        if (mapping_family_param != params.end()) {
            const int value = safe_atoi(mapping_family_param->second, -1);
            if (value >= 0) {
                fmtp_mapping_family = value;
            }
        }

        auto stereo_param = params.find("stereo");
        if (stereo_param == params.end()) {
            stereo_param = params.find("sprop-stereo");
        }
        if (stereo_param != params.end()) {
            const int stereo_flag = safe_atoi(stereo_param->second, -1);
            if (stereo_flag == 1 && derived_channels < 2) {
                derived_channels = 2;
            } else if (stereo_flag == 0 && derived_channels == 0) {
                derived_channels = 1;
            }
        }

        auto streams_param = params.find("streams");
        if (streams_param != params.end()) {
            const int value = safe_atoi(streams_param->second, 0);
            if (value > 0) {
                fmtp_streams = value;
            }
        }

        auto coupled_param = params.find("coupledstreams");
        if (coupled_param == params.end()) {
            coupled_param = params.find("coupled_streams");
        }
        if (coupled_param != params.end()) {
            const int value = safe_atoi(coupled_param->second, 0);
            if (value >= 0) {
                fmtp_coupled_streams = value;
            }
        }
    }

    if (derived_channels <= 0 && chosen_entry->has_explicit_channels) {
        derived_channels = chosen_entry->channels;
    }

    if (derived_channels <= 0) {
        derived_channels = (chosen_codec == StreamCodec::OPUS) ? 2 : 1;
    }

    StreamProperties props;
    props.codec = chosen_codec;
    props.sample_rate = chosen_entry->sample_rate;
    if (props.sample_rate <= 0 && chosen_codec == StreamCodec::OPUS) {
        props.sample_rate = 48000;
    }
    props.channels = derived_channels;
    props.opus_streams = fmtp_streams;
    props.opus_coupled_streams = fmtp_coupled_streams;
    props.opus_mapping_family = (fmtp_mapping_family >= 0) ? fmtp_mapping_family : 0;
    props.opus_channel_mapping = fmtp_channel_mapping;
    props.port = port;

    if (chosen_codec == StreamCodec::OPUS) {
        props.bit_depth = 16;
        props.endianness = Endianness::LITTLE;
    } else if (chosen_entry->encoding.find("l24") != std::string::npos) {
        props.bit_depth = 24;
        props.endianness = Endianness::BIG;
        props.codec = StreamCodec::PCM;
    } else if (chosen_entry->encoding.find("l16") != std::string::npos) {
        props.bit_depth = 16;
        props.endianness = Endianness::BIG;
        props.codec = StreamCodec::PCM;
    } else if (chosen_entry->encoding.find("s16le") != std::string::npos) {
        props.bit_depth = 16;
        props.endianness = Endianness::LITTLE;
        props.codec = StreamCodec::PCM;
    } else {
        props.bit_depth = 16;
        props.endianness = Endianness::BIG;
    }

    std::lock_guard<std::mutex> lock(ssrc_map_mutex_);
    ssrc_to_properties_[ssrc] = props;

    std::lock_guard<std::mutex> lock2(ip_map_mutex_);
    const std::string source_key = make_ip_port_key(source_ip, port);
    ip_to_properties_[source_key] = props;
    if (source_key != source_ip) {
        ip_to_properties_[source_ip] = props;
    }

    if (!connection_ip.empty()) {
        const std::string connection_key = make_ip_port_key(connection_ip, port);
        std::string tagged_connection_key = connection_key;
        if (port > 0) {
            tagged_connection_key += "#sap-" + std::to_string(port);
        }
        ip_to_properties_[connection_key] = props;
        if (connection_key != connection_ip) {
            ip_to_properties_[connection_ip] = props;
        }

        SapAnnouncement announcement;
        announcement.stream_ip = connection_ip;
        announcement.announcer_ip = source_ip;
        announcement.port = port;
        announcement.properties = props;
        announcement.target_sink = target_sink;
        announcement.target_host = target_host;
        announcements_by_stream_endpoint_[connection_key] = announcement;
        if (!tagged_connection_key.empty()) {
            ip_to_properties_[tagged_connection_key] = props;
            announcements_by_stream_endpoint_[tagged_connection_key] = announcement;
        }
    }

    LOG_CPP_DEBUG(
        "%s Updated stream properties for SSRC %u from %s: %d Hz, %d channels, %d bits",
        logger_prefix_.c_str(),
        ssrc,
        source_ip.c_str(),
        props.sample_rate,
        props.channels,
        props.bit_depth);
}

} // namespace audio
} // namespace screamrouter
