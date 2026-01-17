#include "runtime_device_advertiser.h"

#include "../utils/cpp_logger.h"
#include "runtime_paths.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace screamrouter {
namespace audio {
namespace system_audio {

namespace {

#if defined(__linux__)
bool ensure_runtime_dir_present() {
    const std::string runtime_dir = screamrouter_runtime_dir();
    struct stat st {};
    if (stat(runtime_dir.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    if (errno != ENOENT) {
        LOG_CPP_WARNING("[RuntimeDeviceAdvertiser] Failed to stat %s (%s)",
                        runtime_dir.c_str(),
                        std::strerror(errno));
        return false;
    }
    if (mkdir(runtime_dir.c_str(), 0775) == 0) {
        return true;
    }
    if (errno == EEXIST) {
        return true;
    }
    LOG_CPP_WARNING("[RuntimeDeviceAdvertiser] Failed to create %s (%s)",
                    runtime_dir.c_str(),
                    std::strerror(errno));
    return false;
}
#endif

std::string sanitize_tag_for_filename(const std::string& tag) {
    std::string result;
    result.reserve(tag.size());
    for (char c : tag) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (c == ':' || c == '-') {
            result.push_back('-');
        } else {
            result.push_back('_');
        }
    }
    return result;
}

std::string sanitize_value(const std::string& value) {
    std::string cleaned = value;
    std::replace(cleaned.begin(), cleaned.end(), '\n', ' ');
    std::replace(cleaned.begin(), cleaned.end(), '\r', ' ');
    return cleaned;
}

std::string join_bit_depths(const std::vector<unsigned int>& depths) {
    if (depths.empty()) {
        return {};
    }
    std::ostringstream oss;
    for (size_t i = 0; i < depths.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << depths[i];
    }
    return oss.str();
}

#if defined(__linux__)
void write_manifest_payload(std::ofstream& out, const SystemDeviceInfo& info) {
    out << "tag=" << sanitize_value(info.tag) << "\n";
    out << "direction=" << (info.direction == DeviceDirection::CAPTURE ? "capture" : "playback") << "\n";
    out << "friendly_name=" << sanitize_value(info.friendly_name) << "\n";
    out << "hw_id=" << sanitize_value(info.hw_id) << "\n";
    out << "endpoint_id=" << sanitize_value(info.endpoint_id) << "\n";
    out << "card_index=" << info.card_index << "\n";
    out << "device_index=" << info.device_index << "\n";
    out << "channels_min=" << info.channels.min << "\n";
    out << "channels_max=" << info.channels.max << "\n";
    out << "sample_rate_min=" << info.sample_rates.min << "\n";
    out << "sample_rate_max=" << info.sample_rates.max << "\n";
    out << "bit_depth=" << info.bit_depth << "\n";
    out << "bit_depths=" << sanitize_value(join_bit_depths(info.bit_depths)) << "\n";
    out << "present=" << (info.present ? 1 : 0) << "\n";
}
#endif

} // namespace

RuntimeDeviceAdvertiser::~RuntimeDeviceAdvertiser() {
    withdraw();
}

void RuntimeDeviceAdvertiser::publish(const SystemDeviceInfo& info) {
#if defined(__linux__)
    if (info.tag.empty()) {
        return;
    }
    if (!ensure_runtime_dir_present()) {
        return;
    }

    const std::string runtime_dir = screamrouter_runtime_dir();
    tag_ = info.tag;
    manifest_path_ = runtime_dir + "/srmeta." + sanitize_tag_for_filename(tag_);
    const std::string temp_path = manifest_path_ + ".tmp";

    std::ofstream out(temp_path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        LOG_CPP_WARNING("[RuntimeDeviceAdvertiser] Failed to open %s for writing", temp_path.c_str());
        manifest_path_.clear();
        return;
    }
    write_manifest_payload(out, info);
    out.close();

    if (std::rename(temp_path.c_str(), manifest_path_.c_str()) != 0) {
        LOG_CPP_WARNING("[RuntimeDeviceAdvertiser] Failed to rename %s -> %s (%s)",
                        temp_path.c_str(),
                        manifest_path_.c_str(),
                        std::strerror(errno));
        std::remove(temp_path.c_str());
        manifest_path_.clear();
    } else {
        LOG_CPP_INFO("[RuntimeDeviceAdvertiser] Published manifest for %s at %s",
                     info.tag.c_str(),
                     manifest_path_.c_str());
    }
#else
    (void)info;
#endif
}

void RuntimeDeviceAdvertiser::withdraw() {
#if defined(__linux__)
    if (!manifest_path_.empty()) {
        if (std::remove(manifest_path_.c_str()) == 0) {
            LOG_CPP_INFO("[RuntimeDeviceAdvertiser] Removed manifest %s", manifest_path_.c_str());
        }
        manifest_path_.clear();
    }
    tag_.clear();
#endif
}

} // namespace system_audio
} // namespace audio
} // namespace screamrouter
