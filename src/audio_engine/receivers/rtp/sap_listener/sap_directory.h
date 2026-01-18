#ifndef SCREAMROUTER_AUDIO_SAP_DIRECTORY_H
#define SCREAMROUTER_AUDIO_SAP_DIRECTORY_H

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "sap_types.h"

namespace screamrouter {
namespace audio {

struct SapDirectoryIdentity {
    std::string guid;
    std::string session_name;
    std::string stream_ip;
    int port = 0;
};

class SapDirectory {
public:
    SapDirectory() = default;

    bool get_properties_for_ssrc(uint32_t ssrc, StreamProperties& properties) const;
    bool get_properties_for_ip(const std::string& ip, int port, StreamProperties& properties) const;

    bool get_identity(const std::string& ip,
                      int port,
                      std::string& guid,
                      std::string& session_name,
                      std::string& stream_ip_out,
                      int& stream_port_out) const;
    bool get_identity_by_ssrc(uint32_t ssrc,
                              std::string& guid,
                              std::string& session_name,
                              std::string& stream_ip_out,
                              int& stream_port_out) const;

    std::vector<SapAnnouncement> all_announcements() const;

    void upsert(uint32_t ssrc,
                const std::string& announcer_ip,
                const std::string& connection_ip,
                int port,
                const StreamProperties& props,
                const std::string& stream_guid,
                const std::string& target_sink,
                const std::string& target_host,
                const std::string& session_name);

private:
    static std::string make_ip_port_key(const std::string& ip, int port);
    static std::string make_tagged_key(const std::string& base_key, int port);

    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, StreamProperties> ssrc_to_properties_;
    std::unordered_map<uint32_t, SapDirectoryIdentity> ssrc_to_identity_;
    std::unordered_map<std::string, StreamProperties> ip_to_properties_;
    std::unordered_map<std::string, SapAnnouncement> announcements_by_stream_endpoint_;
};

} // namespace audio
} // namespace screamrouter

#endif // SCREAMROUTER_AUDIO_SAP_DIRECTORY_H
