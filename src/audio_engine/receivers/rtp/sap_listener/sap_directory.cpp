#include "sap_directory.h"

#include <algorithm>

namespace screamrouter {
namespace audio {

namespace {

bool try_get(const std::unordered_map<std::string, StreamProperties>& map,
             const std::string& key,
             StreamProperties& properties) {
    auto it = map.find(key);
    if (it == map.end()) {
        return false;
    }
    properties = it->second;
    return true;
}

bool try_get_identity(const std::unordered_map<std::string, SapAnnouncement>& map,
                      const std::string& key,
                      std::string& guid,
                      std::string& session_name,
                      std::string& stream_ip_out,
                      int& stream_port_out) {
    auto it = map.find(key);
    if (it == map.end()) {
        return false;
    }
    guid = it->second.stream_guid;
    session_name = it->second.session_name;
    stream_ip_out = it->second.stream_ip;
    stream_port_out = it->second.port;
    return true;
}

} // namespace

bool SapDirectory::get_properties_for_ssrc(uint32_t ssrc, StreamProperties& properties) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ssrc_to_properties_.find(ssrc);
    if (it == ssrc_to_properties_.end()) {
        return false;
    }
    properties = it->second;
    return true;
}

bool SapDirectory::get_properties_for_ip(const std::string& ip, int port, StreamProperties& properties) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!ip.empty() && ip.find("#sap-") != std::string::npos) {
        if (try_get(ip_to_properties_, ip, properties)) {
            return true;
        }
    }

    const std::string full_key = make_ip_port_key(ip, port);
    if (try_get(ip_to_properties_, full_key, properties)) {
        return true;
    }
    if (port > 0 && try_get(ip_to_properties_, ip, properties)) {
        return true;
    }
    return false;
}

bool SapDirectory::get_identity(const std::string& ip,
                                int port,
                                std::string& guid,
                                std::string& session_name,
                                std::string& stream_ip_out,
                                int& stream_port_out) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!ip.empty() && ip.find("#sap-") != std::string::npos) {
        if (try_get_identity(announcements_by_stream_endpoint_, ip, guid, session_name, stream_ip_out, stream_port_out)) {
            return true;
        }
    }

    const std::string base_key = make_ip_port_key(ip, port);
    if (try_get_identity(announcements_by_stream_endpoint_, base_key, guid, session_name, stream_ip_out, stream_port_out)) {
        return true;
    }
    if (port > 0 && try_get_identity(announcements_by_stream_endpoint_, ip, guid, session_name, stream_ip_out, stream_port_out)) {
        return true;
    }
    const std::string tagged = make_tagged_key(base_key, port);
    if (!tagged.empty() &&
        try_get_identity(announcements_by_stream_endpoint_, tagged, guid, session_name, stream_ip_out, stream_port_out)) {
        return true;
    }
    return false;
}

bool SapDirectory::get_identity_by_ssrc(uint32_t ssrc,
                                        std::string& guid,
                                        std::string& session_name,
                                        std::string& stream_ip_out,
                                        int& stream_port_out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ssrc_to_identity_.find(ssrc);
    if (it == ssrc_to_identity_.end()) {
        return false;
    }
    guid = it->second.guid;
    session_name = it->second.session_name;
    stream_ip_out = it->second.stream_ip;
    stream_port_out = it->second.port;
    return true;
}

std::vector<SapAnnouncement> SapDirectory::all_announcements() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SapAnnouncement> announcements;
    announcements.reserve(announcements_by_stream_endpoint_.size());
    for (const auto& entry : announcements_by_stream_endpoint_) {
        announcements.push_back(entry.second);
    }
    return announcements;
}

void SapDirectory::upsert(uint32_t ssrc,
                          const std::string& announcer_ip,
                          const std::string& connection_ip,
                          int port,
                          const StreamProperties& props,
                          const std::string& stream_guid,
                          const std::string& target_sink,
                          const std::string& target_host,
                          const std::string& session_name) {
    SapAnnouncement announcement;
    announcement.stream_ip = connection_ip.empty() ? announcer_ip : connection_ip;
    announcement.announcer_ip = announcer_ip;
    announcement.port = port;
    announcement.properties = props;
    announcement.stream_guid = stream_guid;
    announcement.target_sink = target_sink;
    announcement.target_host = target_host;
    announcement.session_name = session_name;

    std::lock_guard<std::mutex> lock(mutex_);

    if (ssrc != 0) {
        ssrc_to_properties_[ssrc] = props;
        ssrc_to_identity_[ssrc] = {stream_guid, session_name, announcement.stream_ip, port};
    }

    auto publish_keys = [&](const std::string& ip, bool include_tag) {
        if (ip.empty()) {
            return;
        }
        if (port > 0) {
            const std::string key = make_ip_port_key(ip, port);
            ip_to_properties_[key] = props;
            announcements_by_stream_endpoint_[key] = announcement;
            if (include_tag) {
                const std::string tagged = make_tagged_key(key, port);
                if (!tagged.empty()) {
                    ip_to_properties_[tagged] = props;
                    announcements_by_stream_endpoint_[tagged] = announcement;
                }
            }
        }
        ip_to_properties_[ip] = props;
        announcements_by_stream_endpoint_[ip] = announcement;
    };

    publish_keys(announcement.stream_ip, !connection_ip.empty());
    publish_keys(announcer_ip, false);
}

std::string SapDirectory::make_ip_port_key(const std::string& ip, int port) {
    if (port <= 0) {
        return ip;
    }
    return ip + ":" + std::to_string(port);
}

std::string SapDirectory::make_tagged_key(const std::string& base_key, int port) {
    if (base_key.empty() || port <= 0) {
        return {};
    }
    return base_key + "#sap-" + std::to_string(port);
}

} // namespace audio
} // namespace screamrouter
