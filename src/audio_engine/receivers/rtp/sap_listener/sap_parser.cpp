#include "sap_parser.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "../../../utils/cpp_logger.h"

namespace screamrouter {
namespace audio {

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

void lowercase_in_place(std::string& text) {
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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

struct SapTargetHints {
    std::string sink;
    std::string host;

    void merge_from(const SapTargetHints& other) {
        if (!other.sink.empty()) {
            sink = other.sink;
        }
        if (!other.host.empty()) {
            host = other.host;
        }
    }
};

struct RtpmapEntry {
    std::string encoding;
    int sample_rate = 0;
    int channels = 0;
    bool has_explicit_channels = false;
};

struct SdpAudioDescription {
    int port = 0;
    std::vector<int> payload_types;
    std::unordered_map<int, RtpmapEntry> rtpmap_entries;
    std::unordered_map<int, std::unordered_map<std::string, std::string>> fmtp_entries;
};

struct SdpMetadata {
    std::string session_name;
    uint32_t ssrc = 0;
    bool has_ssrc = false;
    std::string connection_ip;
    std::string stream_guid;
    SapTargetHints target_hints;
    SdpAudioDescription audio;
};

struct PayloadSelection {
    int payload_type = -1;
    StreamCodec codec = StreamCodec::UNKNOWN;
    const RtpmapEntry* entry = nullptr;
};

bool extract_sdp_payload(const char* buffer, int size, const std::string& logger_prefix, const char*& sdp_start, int& sdp_size) {
    if (size < 4) {
        LOG_CPP_WARNING("%s SAP packet too small for header: %d bytes", logger_prefix.c_str(), size);
        return false;
    }

    uint8_t first_byte = static_cast<uint8_t>(buffer[0]);
    const bool has_auth = (first_byte & 0x10u) != 0;
    const uint8_t auth_len = has_auth ? static_cast<uint8_t>(buffer[1]) * 4u : 0u;
    const int header_len = 4 + auth_len;

    if (size <= header_len) {
        LOG_CPP_WARNING("%s Invalid SAP packet, no SDP data found", logger_prefix.c_str());
        return false;
    }

    sdp_start = buffer + header_len;
    sdp_size = size - header_len;
    return true;
}

std::vector<std::string> split_sdp_lines(const char* sdp_start, int sdp_size) {
    std::vector<std::string> lines;
    if (!sdp_start || sdp_size <= 0) {
        return lines;
    }

    std::string payload(sdp_start, sdp_size);
    std::istringstream stream(payload);
    std::string raw_line;
    while (std::getline(stream, raw_line)) {
        if (!raw_line.empty() && raw_line.back() == '\r') {
            raw_line.pop_back();
        }
        if (!raw_line.empty()) {
            lines.push_back(raw_line);
        }
    }
    return lines;
}

SapTargetHints parse_target_hint_block(const std::string& block) {
    SapTargetHints hints;
    if (block.empty()) {
        return hints;
    }

    std::stringstream ss(block);
    std::string token;
    while (std::getline(ss, token, ';')) {
        const auto eq_pos = token.find('=');
        std::string key = trim_copy(eq_pos == std::string::npos ? token : token.substr(0, eq_pos));
        std::string value = eq_pos == std::string::npos ? "" : trim_copy(token.substr(eq_pos + 1));
        lowercase_in_place(key);
        if (key == "sink") {
            hints.sink = value;
        } else if (key == "host") {
            lowercase_in_place(value);
            hints.host = value;
        }
    }

    if (hints.sink.empty()) {
        hints.sink = trim_copy(block);
    }
    return hints;
}

bool parse_rtpmap_line(const std::string& line, SdpAudioDescription& audio, const std::string& logger_prefix) {
    std::string remainder = trim_copy(line.substr(std::strlen("a=rtpmap:")));
    const auto space_pos = remainder.find(' ');
    if (space_pos == std::string::npos) {
        LOG_CPP_WARNING("%s Malformed rtpmap line (missing space): %s", logger_prefix.c_str(), line.c_str());
        return false;
    }

    const std::string pt_str = remainder.substr(0, space_pos);
    const int payload_type = safe_atoi(pt_str, -1);
    if (payload_type < 0) {
        LOG_CPP_WARNING("%s Failed to parse payload type in rtpmap: %s", logger_prefix.c_str(), line.c_str());
        return false;
    }

    std::string encoding_block = trim_copy(remainder.substr(space_pos + 1));
    const auto first_slash = encoding_block.find('/');
    if (first_slash == std::string::npos) {
        LOG_CPP_WARNING("%s Malformed rtpmap payload descriptor: %s", logger_prefix.c_str(), line.c_str());
        return false;
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
    audio.rtpmap_entries[payload_type] = entry;
    return true;
}

bool parse_fmtp_line(const std::string& line, SdpAudioDescription& audio) {
    std::string remainder = trim_copy(line.substr(std::strlen("a=fmtp:")));
    const auto space_pos = remainder.find(' ');
    if (space_pos == std::string::npos) {
        return false;
    }
    const std::string pt_str = remainder.substr(0, space_pos);
    const int payload_type = safe_atoi(pt_str, -1);
    if (payload_type < 0) {
        return false;
    }

    std::string params_block = remainder.substr(space_pos + 1);
    auto& params = audio.fmtp_entries[payload_type];

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
    return true;
}

void apply_target_overrides(SdpMetadata& metadata) {
    for (const auto& kv : metadata.audio.fmtp_entries) {
        const auto& params = kv.second;
        const auto target_it = params.find("x-screamrouter-target");
        if (target_it != params.end()) {
            metadata.target_hints.merge_from(parse_target_hint_block(target_it->second));
        }
        const auto guid_it = params.find("x-screamrouter-guid");
        if (guid_it != params.end() && metadata.stream_guid.empty()) {
            metadata.stream_guid = trim_copy(guid_it->second);
        }
    }
}

bool parse_sdp_metadata(const std::vector<std::string>& lines, SdpMetadata& metadata, const std::string& logger_prefix) {
    bool media_line_found = false;

    for (const auto& line : lines) {
        if (line.rfind("s=", 0) == 0) {
            metadata.session_name = trim_copy(line.substr(2));
        } else if (line.rfind("o=", 0) == 0) {
            char username[64] = {0};
            unsigned long long session_id = 0;
            if (std::sscanf(line.c_str(), "o=%63s %llu", username, &session_id) == 2) {
                metadata.ssrc = static_cast<uint32_t>(session_id);
                metadata.has_ssrc = true;
            } else {
                LOG_CPP_WARNING("%s Failed to parse SSRC from o-line: %s", logger_prefix.c_str(), line.c_str());
            }
        } else if (line.rfind("c=IN IP4 ", 0) == 0) {
            std::string ip = trim_copy(line.substr(std::strlen("c=IN IP4 ")));
            metadata.connection_ip = ip;
        } else if (line.rfind("m=audio ", 0) == 0) {
            media_line_found = true;
            std::string m_body = line.substr(std::strlen("m=audio "));
            std::stringstream m_stream(m_body);
            m_stream >> metadata.audio.port;
            std::string proto;
            m_stream >> proto;
            int payload_type = 0;
            while (m_stream >> payload_type) {
                metadata.audio.payload_types.push_back(payload_type);
            }
        } else if (line.rfind("a=x-screamrouter-guid:", 0) == 0) {
            metadata.stream_guid = trim_copy(line.substr(std::strlen("a=x-screamrouter-guid:")));
        } else if (line.rfind("a=x-screamrouter-target:", 0) == 0) {
            metadata.target_hints.merge_from(parse_target_hint_block(trim_copy(line.substr(std::strlen("a=x-screamrouter-target:")))));
        } else if (line.rfind("a=rtpmap:", 0) == 0) {
            parse_rtpmap_line(line, metadata.audio, logger_prefix);
        } else if (line.rfind("a=fmtp:", 0) == 0) {
            parse_fmtp_line(line, metadata.audio);
        }
    }

    if (!metadata.has_ssrc) {
        LOG_CPP_WARNING("%s o-line not found or malformed in SAP packet", logger_prefix.c_str());
        return false;
    }
    if (!media_line_found) {
        LOG_CPP_WARNING("%s No m=audio line found in SAP packet (SSRC=%u)", logger_prefix.c_str(), metadata.ssrc);
        return false;
    }
    if (metadata.audio.port <= 0) {
        LOG_CPP_WARNING("%s Invalid/unknown RTP port in SAP packet (SSRC=%u)", logger_prefix.c_str(), metadata.ssrc);
        return false;
    }
    if (metadata.connection_ip.empty()) {
        LOG_CPP_WARNING("%s No connection IP found in SAP packet (SSRC=%u)", logger_prefix.c_str(), metadata.ssrc);
    }

    apply_target_overrides(metadata);
    return true;
}

bool select_payload(const SdpAudioDescription& audio, PayloadSelection& selection, const std::string& logger_prefix) {
    auto try_select = [&](const std::string& needle, StreamCodec codec) -> bool {
        for (int pt : audio.payload_types) {
            auto it = audio.rtpmap_entries.find(pt);
            if (it != audio.rtpmap_entries.end() && it->second.encoding.find(needle) != std::string::npos) {
                selection.payload_type = pt;
                selection.codec = codec;
                selection.entry = &it->second;
                return true;
            }
        }
        for (const auto& kv : audio.rtpmap_entries) {
            if (kv.second.encoding.find(needle) != std::string::npos) {
                selection.payload_type = kv.first;
                selection.codec = codec;
                selection.entry = &kv.second;
                return true;
            }
        }
        return false;
    };

    if (!audio.payload_types.empty() || !audio.rtpmap_entries.empty()) {
        if (!try_select("opus", StreamCodec::OPUS)) {
            if (!try_select("l24", StreamCodec::PCM) &&
                !try_select("l16", StreamCodec::PCM) &&
                !try_select("s16le", StreamCodec::PCM) &&
                !try_select("pcm", StreamCodec::PCM) &&
                !try_select("pcmu", StreamCodec::PCMU) &&
                !try_select("pcma", StreamCodec::PCMA)) {
                for (int pt : audio.payload_types) {
                    auto it = audio.rtpmap_entries.find(pt);
                    if (it != audio.rtpmap_entries.end()) {
                        selection.payload_type = pt;
                        selection.entry = &it->second;
                        break;
                    }
                }
                if (!selection.entry && !audio.rtpmap_entries.empty()) {
                    selection.payload_type = audio.rtpmap_entries.begin()->first;
                    selection.entry = &audio.rtpmap_entries.begin()->second;
                }
            }
        }
    }

    if (!selection.entry) {
        LOG_CPP_WARNING("%s No usable rtpmap entry found in SAP packet", logger_prefix.c_str());
        return false;
    }

    if (selection.codec == StreamCodec::UNKNOWN) {
        if (selection.entry->encoding.find("opus") != std::string::npos) {
            selection.codec = StreamCodec::OPUS;
        } else if (selection.entry->encoding.find("pcmu") != std::string::npos) {
            selection.codec = StreamCodec::PCMU;
        } else if (selection.entry->encoding.find("pcma") != std::string::npos) {
            selection.codec = StreamCodec::PCMA;
        } else if (selection.entry->encoding.find("l24") != std::string::npos ||
                   selection.entry->encoding.find("l16") != std::string::npos ||
                   selection.entry->encoding.find("s16le") != std::string::npos ||
                   selection.entry->encoding.find("pcm") != std::string::npos) {
            selection.codec = StreamCodec::PCM;
        }
    }

    if (selection.payload_type < 0) {
        selection.payload_type = audio.payload_types.empty() ? 0 : audio.payload_types.front();
    }
    return true;
}

StreamProperties build_stream_properties(const SdpMetadata& metadata, const PayloadSelection& selection) {
    StreamProperties props;
    props.payload_type = selection.payload_type;
    props.codec = selection.codec;
    props.sample_rate = selection.entry ? selection.entry->sample_rate : 0;
    if (props.sample_rate <= 0 && props.codec == StreamCodec::OPUS) {
        props.sample_rate = 48000;
    } else if (props.sample_rate <= 0 &&
               (props.codec == StreamCodec::PCMU || props.codec == StreamCodec::PCMA)) {
        props.sample_rate = 8000;
    }

    int derived_channels = (selection.entry && selection.entry->has_explicit_channels)
                               ? selection.entry->channels
                               : 0;
    int fmtp_streams = 0;
    int fmtp_coupled_streams = 0;
    int fmtp_mapping_family = -1;
    std::vector<uint8_t> fmtp_channel_mapping;

    const auto fmtp_it = metadata.audio.fmtp_entries.find(selection.payload_type);
    if (fmtp_it != metadata.audio.fmtp_entries.end()) {
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

    if (derived_channels <= 0 && selection.entry && selection.entry->has_explicit_channels) {
        derived_channels = selection.entry->channels;
    }
    if (derived_channels <= 0) {
        derived_channels = (selection.codec == StreamCodec::OPUS) ? 2 : 1;
    }

    props.channels = derived_channels;
    props.opus_streams = fmtp_streams;
    props.opus_coupled_streams = fmtp_coupled_streams;
    props.opus_mapping_family = (fmtp_mapping_family >= 0) ? fmtp_mapping_family : 0;
    props.opus_channel_mapping = fmtp_channel_mapping;
    props.port = metadata.audio.port;

    if (selection.codec == StreamCodec::OPUS) {
        props.bit_depth = 16;
        props.endianness = Endianness::LITTLE;
    } else if (selection.codec == StreamCodec::PCMU || selection.codec == StreamCodec::PCMA) {
        props.bit_depth = 8;
        props.endianness = Endianness::BIG;
    } else if (selection.entry && (selection.entry->encoding.find("s32le") != std::string::npos ||
                                   selection.entry->encoding.find("l32le") != std::string::npos ||
                                   selection.entry->encoding.find("pcm32le") != std::string::npos)) {
        props.bit_depth = 32;
        props.endianness = Endianness::LITTLE;
        props.codec = StreamCodec::PCM;
    } else if (selection.entry && (selection.entry->encoding.find("l32") != std::string::npos ||
                                   selection.entry->encoding.find("s32") != std::string::npos ||
                                   selection.entry->encoding.find("pcm32") != std::string::npos)) {
        props.bit_depth = 32;
        props.endianness = Endianness::BIG;
        props.codec = StreamCodec::PCM;
    } else if (selection.entry && (selection.entry->encoding.find("s24le") != std::string::npos ||
                                   selection.entry->encoding.find("pcm24le") != std::string::npos)) {
        props.bit_depth = 24;
        props.endianness = Endianness::LITTLE;
        props.codec = StreamCodec::PCM;
    } else if (selection.entry && (selection.entry->encoding.find("l24") != std::string::npos ||
                                   selection.entry->encoding.find("pcm24") != std::string::npos)) {
        props.bit_depth = 24;
        props.endianness = Endianness::BIG;
        props.codec = StreamCodec::PCM;
    } else if (selection.entry && (selection.entry->encoding.find("s16le") != std::string::npos ||
                                   selection.entry->encoding.find("pcm16le") != std::string::npos)) {
        props.bit_depth = 16;
        props.endianness = Endianness::LITTLE;
        props.codec = StreamCodec::PCM;
    } else if (selection.entry && (selection.entry->encoding.find("l16") != std::string::npos ||
                                   selection.entry->encoding.find("pcm") != std::string::npos)) {
        props.bit_depth = 16;
        props.endianness = Endianness::BIG;
        props.codec = StreamCodec::PCM;
    } else if (selection.entry && selection.entry->encoding.find("pcmu") != std::string::npos) {
        props.bit_depth = 8;
        props.endianness = Endianness::BIG;
        props.codec = StreamCodec::PCMU;
    } else if (selection.entry && selection.entry->encoding.find("pcma") != std::string::npos) {
        props.bit_depth = 8;
        props.endianness = Endianness::BIG;
        props.codec = StreamCodec::PCMA;
    } else {
        props.bit_depth = 16;
        props.endianness = Endianness::BIG;
    }

    return props;
}

} // namespace

bool parse_sap_packet(const char* buffer,
                      int size,
                      const std::string& logger_prefix,
                      ParsedSapInfo& out) {
    const char* sdp_start = nullptr;
    int sdp_size = 0;
    if (!extract_sdp_payload(buffer, size, logger_prefix, sdp_start, sdp_size)) {
        return false;
    }

    auto sdp_lines = split_sdp_lines(sdp_start, sdp_size);
    if (sdp_lines.empty()) {
        LOG_CPP_WARNING("%s SDP payload was empty in SAP packet", logger_prefix.c_str());
        return false;
    }

    SdpMetadata metadata;
    if (!parse_sdp_metadata(sdp_lines, metadata, logger_prefix)) {
        return false;
    }

    PayloadSelection selection;
    if (!select_payload(metadata.audio, selection, logger_prefix)) {
        return false;
    }

    StreamProperties props = build_stream_properties(metadata, selection);

    out.ssrc = metadata.ssrc;
    out.session_name = metadata.session_name;
    out.connection_ip = metadata.connection_ip;
    out.port = metadata.audio.port;
    out.stream_guid = metadata.stream_guid;
    out.target_sink = metadata.target_hints.sink;
    out.target_host = metadata.target_hints.host;
    out.properties = std::move(props);
    return true;
}

} // namespace audio
} // namespace screamrouter
