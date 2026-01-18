#ifndef SCREAMROUTER_AUDIO_SAP_PARSER_H
#define SCREAMROUTER_AUDIO_SAP_PARSER_H

#include <string>

#include "sap_types.h"

namespace screamrouter {
namespace audio {

struct ParsedSapInfo {
    uint32_t ssrc = 0;
    std::string session_name;
    std::string connection_ip;
    int port = 0;
    std::string stream_guid;
    std::string target_sink;
    std::string target_host;
    StreamProperties properties;
    bool is_request = false;
    std::string request_type;
    std::string request_route_id;
    std::string request_source_id;
    std::string request_source_name;
    std::string request_target_sink;
    std::string request_target_host;
    std::string request_destination_ip;
    int request_destination_port = 0;
};

bool parse_sap_packet(const char* buffer,
                      int size,
                      const std::string& logger_prefix,
                      ParsedSapInfo& out);

} // namespace audio
} // namespace screamrouter

#endif // SCREAMROUTER_AUDIO_SAP_PARSER_H
