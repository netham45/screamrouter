#include "alsa_device_enumerator.h"
#include "../utils/cpp_logger.h"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#if defined(__linux__)
extern "C" {
#include <alsa/asoundlib.h>
}
#include <cctype>
#include <dirent.h>
#include <poll.h>
#include <string_view>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <algorithm>
#include <sstream>
#endif

#include "system_audio_tags.h"

using namespace screamrouter::audio;

namespace screamrouter {
namespace audio {
namespace system_audio {

AlsaDeviceEnumerator::AlsaDeviceEnumerator(std::shared_ptr<NotificationQueue> notification_queue)
    : notification_queue_(std::move(notification_queue)) {}

AlsaDeviceEnumerator::~AlsaDeviceEnumerator() {
    stop();
}

void AlsaDeviceEnumerator::start() {
#if defined(__linux__)
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    monitor_thread_ = std::thread(&AlsaDeviceEnumerator::monitor_loop, this);
#else
    (void)notification_queue_;
#endif
}

void AlsaDeviceEnumerator::stop() {
#if defined(__linux__)
    if (running_.exchange(false)) {
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }
#else
    running_ = false;
#endif
    std::lock_guard<std::mutex> lock(registry_mutex_);
    registry_.clear();
}

AlsaDeviceEnumerator::Registry AlsaDeviceEnumerator::get_registry_snapshot() const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    return registry_;
}

#if defined(__linux__)

namespace {
constexpr int kPollTimeoutMs = 2000;
constexpr const char* kScreamrouterRuntimeDir = "/var/run/screamrouter";

bool ensure_runtime_dir_exists() {
    struct stat st {};
    if (stat(kScreamrouterRuntimeDir, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;
        }
        LOG_CPP_WARNING("[ALSA-Enumerator] %s exists but is not a directory", kScreamrouterRuntimeDir);
        return false;
    }

    if (errno != ENOENT) {
        LOG_CPP_WARNING("[ALSA-Enumerator] Failed to stat %s (%s)", kScreamrouterRuntimeDir, std::strerror(errno));
        return false;
    }

    if (mkdir(kScreamrouterRuntimeDir, 0775) == 0) {
        LOG_CPP_INFO("[ALSA-Enumerator] Created runtime directory %s", kScreamrouterRuntimeDir);
        return true;
    }

    if (errno == EEXIST) {
        return true;
    }

    LOG_CPP_WARNING("[ALSA-Enumerator] Failed to create %s (%s)", kScreamrouterRuntimeDir, std::strerror(errno));
    return false;
}

std::string sanitize_label(const std::string& label) {
    std::string result;
    result.reserve(label.size());
    for (char c : label) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else {
            result.push_back('_');
        }
    }
    return result;
}

std::string friendly_from_label(const std::string& label) {
    std::string result;
    result.reserve(label.size());
    bool capitalize = true;
    for (char c : label) {
        char out = c;
        if (c == '_') {
            out = ' ';
            capitalize = true;
        } else if (capitalize) {
            out = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            capitalize = false;
        }
        result.push_back(out);
    }
    return result;
}

std::string build_device_tag(const std::string& prefix, int card, int device) {
    return prefix + std::to_string(card) + "." + std::to_string(device);
}

std::string build_hw_id(int card, int device) {
    return "hw:" + std::to_string(card) + "," + std::to_string(device);
}

SystemDeviceInfo create_device_info(
    snd_ctl_card_info_t* card_info,
    snd_pcm_info_t* pcm_info,
    int card,
    int device,
    snd_pcm_stream_t stream)
{
    SystemDeviceInfo info;
    info.direction = (stream == SND_PCM_STREAM_CAPTURE) ? DeviceDirection::CAPTURE : DeviceDirection::PLAYBACK;
    const std::string tag_prefix = (info.direction == DeviceDirection::CAPTURE) ? "ac:" : "ap:";
    info.tag = build_device_tag(tag_prefix, card, device);
    info.hw_id = build_hw_id(card, device);
    info.present = true;
    info.card_index = card;
    info.device_index = device;

    const char* card_name = snd_ctl_card_info_get_name(card_info);
    const char* device_name = snd_pcm_info_get_name(pcm_info);
    const char* device_id = snd_pcm_info_get_id(pcm_info);

    std::string friendly_name;
    if (card_name) {
        friendly_name = card_name;
    }
    if (device_name && std::strlen(device_name) > 0) {
        if (!friendly_name.empty()) {
            friendly_name += " - ";
        }
        friendly_name += device_name;
    } else if (device_id && std::strlen(device_id) > 0) {
        if (!friendly_name.empty()) {
            friendly_name += " - ";
        }
        friendly_name += device_id;
    }

    if (!friendly_name.empty()) {
        friendly_name += (info.direction == DeviceDirection::CAPTURE) ? " (Capture)" : " (Playback)";
    } else {
        friendly_name = info.hw_id + ((info.direction == DeviceDirection::CAPTURE) ? " (Capture)" : " (Playback)");
    }
    info.friendly_name = friendly_name;

    snd_pcm_t* pcm_handle = nullptr;
    int open_err = snd_pcm_open(&pcm_handle, info.hw_id.c_str(), stream, SND_PCM_NONBLOCK);
    if (open_err < 0) {
        LOG_CPP_WARNING("[ALSA-Enumerator] Failed to open %s for capability query: %s", info.hw_id.c_str(), snd_strerror(open_err));
        return info;
    }

    snd_pcm_hw_params_t* params = nullptr;
    if (snd_pcm_hw_params_malloc(&params) < 0) {
        LOG_CPP_WARNING("[ALSA-Enumerator] Failed to allocate hw params for %s", info.hw_id.c_str());
        snd_pcm_close(pcm_handle);
        return info;
    }

    if (snd_pcm_hw_params_any(pcm_handle, params) < 0) {
        LOG_CPP_WARNING("[ALSA-Enumerator] Failed to query hw params for %s", info.hw_id.c_str());
        snd_pcm_hw_params_free(params);
        snd_pcm_close(pcm_handle);
        return info;
    }

    unsigned int min_channels = 0;
    unsigned int max_channels = 0;
    if (snd_pcm_hw_params_get_channels_min(params, &min_channels) == 0) {
        info.channels.min = min_channels;
    }
    if (snd_pcm_hw_params_get_channels_max(params, &max_channels) == 0) {
        info.channels.max = max_channels;
    }

    unsigned int min_rate = 0;
    unsigned int max_rate = 0;
    int dir = 0;
    if (snd_pcm_hw_params_get_rate_min(params, &min_rate, &dir) == 0) {
        info.sample_rates.min = min_rate;
    }
    dir = 0;
    if (snd_pcm_hw_params_get_rate_max(params, &max_rate, &dir) == 0) {
        info.sample_rates.max = max_rate;
    }

    snd_pcm_hw_params_free(params);
    snd_pcm_close(pcm_handle);

    return info;
}

} // namespace

void AlsaDeviceEnumerator::monitor_loop() {
    LOG_CPP_INFO("[ALSA-Enumerator] Monitoring thread started.");

    enumerate_devices();
    setup_fifo_watch();

    while (running_) {
        auto handles = open_control_handles();

        std::vector<pollfd> poll_fds;
        poll_fds.reserve(handles.size() + 1);

        for (auto& handle : handles) {
            auto* ctl = static_cast<snd_ctl_t*>(handle.handle);
            if (!ctl) {
                continue;
            }
            int count = snd_ctl_poll_descriptors_count(ctl);
            if (count <= 0) {
                continue;
            }
            std::vector<pollfd> local(count);
            if (snd_ctl_poll_descriptors(ctl, local.data(), count) >= 0) {
                poll_fds.insert(poll_fds.end(), local.begin(), local.end());
            }
        }

        if (inotify_fd_ >= 0) {
            pollfd fifo_fd{};
            fifo_fd.fd = inotify_fd_;
            fifo_fd.events = POLLIN | POLLERR | POLLHUP;
            fifo_fd.revents = 0;
            poll_fds.push_back(fifo_fd);
        }

        // Attempt to re-establish the directory watch if it previously failed.
        if (running_) {
            setup_fifo_watch();
        }

        bool should_rescan = poll_fds.empty();
        if (!poll_fds.empty()) {
            int poll_result = poll(poll_fds.data(), static_cast<nfds_t>(poll_fds.size()), kPollTimeoutMs);
            if (poll_result > 0) {
                bool control_event_seen = false;
                bool fifo_event_seen = false;
                for (auto& handle : handles) {
                    auto* ctl = static_cast<snd_ctl_t*>(handle.handle);
                    if (!ctl) {
                        continue;
                    }
                    snd_ctl_event_t* event = nullptr;
                    if (snd_ctl_event_malloc(&event) < 0) {
                        continue;
                    }
                    bool handle_event_seen = false;
                    while (snd_ctl_read(ctl, event) == 1) {
                        handle_event_seen = true;
                    }
                    snd_ctl_event_free(event);
                    if (handle_event_seen) {
                        control_event_seen = true;
                    }
                }

                if (inotify_fd_ >= 0) {
                    for (const auto& pfd : poll_fds) {
                        if (pfd.fd != inotify_fd_) {
                            continue;
                        }
                        if (pfd.revents & (POLLIN | POLLERR | POLLHUP)) {
                            fifo_event_seen = true;
                            bool events_seen = drain_fifo_watch_events();
                            if (pfd.revents & (POLLERR | POLLHUP)) {
                                teardown_fifo_watch();
                            } else if (!events_seen) {
                                LOG_CPP_DEBUG("[ALSA-Enumerator] Inotify signaled but no detailed events read");
                            }
                        }
                    }
                }

                should_rescan = control_event_seen || fifo_event_seen;
            } else if (poll_result == 0) {
                should_rescan = true; // timeout -> periodic rescan
            } else {
                int err_code = errno;
                LOG_CPP_WARNING("[ALSA-Enumerator] poll() failed while monitoring ALSA controls: %s", std::strerror(err_code));
                should_rescan = true;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollTimeoutMs));
        }

        close_control_handles(handles);

        if (!running_) {
            break;
        }

        if (should_rescan) {
            LOG_CPP_DEBUG("[ALSA-Enumerator] Triggering device rescan (running=%d)", running_.load() ? 1 : 0);
            enumerate_devices();
            setup_fifo_watch();
        }
    }

    teardown_fifo_watch();
    LOG_CPP_INFO("[ALSA-Enumerator] Monitoring thread exiting.");
}

void AlsaDeviceEnumerator::enumerate_devices() {
    LOG_CPP_INFO("[ALSA-Enumerator] Starting full ALSA device enumeration pass.");
    Registry scanned_registry;

    int card = -1;
    int err = snd_card_next(&card);
    if (err < 0) {
        LOG_CPP_ERROR("[ALSA-Enumerator] snd_card_next failed: %s", snd_strerror(err));
        return;
    }

    int total_cards = 0;
    int total_devices = 0;
    while (card >= 0) {
        ++total_cards;
        char hw_name[32];
        std::snprintf(hw_name, sizeof(hw_name), "hw:%d", card);

        snd_ctl_t* ctl = nullptr;
        snd_ctl_card_info_t* card_info = nullptr;
        snd_pcm_info_t* pcm_info = nullptr;
        int device = -1;

        do {
            if ((err = snd_ctl_open(&ctl, hw_name, 0)) < 0) {
                LOG_CPP_WARNING("[ALSA-Enumerator] Failed to open control for %s: %s", hw_name, snd_strerror(err));
                break;
            }

            LOG_CPP_INFO("[ALSA-Enumerator] Inspecting ALSA card %s", hw_name);

            if (snd_ctl_card_info_malloc(&card_info) < 0) {
                LOG_CPP_WARNING("[ALSA-Enumerator] Failed to allocate card info for %s", hw_name);
                break;
            }

            if ((err = snd_ctl_card_info(ctl, card_info)) < 0) {
                LOG_CPP_WARNING("[ALSA-Enumerator] Failed to get card info for %s: %s", hw_name, snd_strerror(err));
                break;
            }

            if (snd_pcm_info_malloc(&pcm_info) < 0) {
                LOG_CPP_WARNING("[ALSA-Enumerator] Failed to allocate pcm info for %s", hw_name);
                break;
            }

            device = -1;
            while (true) {
                if ((err = snd_ctl_pcm_next_device(ctl, &device)) < 0) {
                    LOG_CPP_WARNING("[ALSA-Enumerator] snd_ctl_pcm_next_device failed on %s: %s", hw_name, snd_strerror(err));
                    break;
                }
                if (device < 0) {
                    break;
                }

                ++total_devices;
                LOG_CPP_INFO("[ALSA-Enumerator]  Card %d -> PCM device %d", card, device);

                for (auto stream : {SND_PCM_STREAM_CAPTURE, SND_PCM_STREAM_PLAYBACK}) {
                    snd_pcm_info_set_device(pcm_info, device);
                    snd_pcm_info_set_subdevice(pcm_info, 0);
                    snd_pcm_info_set_stream(pcm_info, stream);

                    if ((err = snd_ctl_pcm_info(ctl, pcm_info)) < 0) {
                        LOG_CPP_DEBUG("[ALSA-Enumerator]   Stream %s unsupported for card %d device %d (%s)",
                                      (stream == SND_PCM_STREAM_CAPTURE ? "CAPTURE" : "PLAYBACK"),
                                      card,
                                      device,
                                      snd_strerror(err));
                        continue; // direction unsupported for this device
                    }

                    SystemDeviceInfo info = create_device_info(card_info, pcm_info, card, device, stream);
                    LOG_CPP_INFO("[ALSA-Enumerator]   Discovered %s -> %s", info.tag.c_str(), info.friendly_name.c_str());
                    LOG_CPP_DEBUG("[ALSA-Enumerator]    hw_id=%s channels=%u-%u rates=%u-%u",
                                   info.hw_id.c_str(),
                                   info.channels.min,
                                   info.channels.max,
                                   info.sample_rates.min,
                                   info.sample_rates.max);
                    scanned_registry[info.tag] = info;
                }
            }

        } while (false);

        if (pcm_info) {
            snd_pcm_info_free(pcm_info);
        }
        if (card_info) {
            snd_ctl_card_info_free(card_info);
        }
        if (ctl) {
            snd_ctl_close(ctl);
        }

        if ((err = snd_card_next(&card)) < 0) {
            LOG_CPP_WARNING("[ALSA-Enumerator] snd_card_next iteration failed: %s", snd_strerror(err));
            break;
        }
    }

    append_screamrouter_runtime_devices(scanned_registry);

    LOG_CPP_INFO("[ALSA-Enumerator] Enumeration pass complete: cards=%d pcm_devices=%d discovered=%zu", total_cards, total_devices, scanned_registry.size());

    std::vector<DeviceDiscoveryNotification> notifications;

    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        Registry previous = registry_;
        Registry updated = previous;

        for (const auto& [tag, info] : scanned_registry) {
            auto prev_it = previous.find(tag);
            if (prev_it == previous.end()) {
                updated[tag] = info;
                LOG_CPP_INFO("[ALSA-Enumerator] Device added: %s", tag.c_str());
                notifications.push_back(DeviceDiscoveryNotification{tag, info.direction, true});
            } else {
                bool changed = !(prev_it->second == info);
                updated[tag] = info;
                if (changed) {
                    LOG_CPP_INFO("[ALSA-Enumerator] Device updated: %s", tag.c_str());
                    notifications.push_back(DeviceDiscoveryNotification{tag, info.direction, true});
                }
            }
        }

        for (const auto& [tag, prev_info] : previous) {
            if (scanned_registry.find(tag) == scanned_registry.end() && prev_info.present) {
                SystemDeviceInfo removed_info = prev_info;
                removed_info.present = false;
                updated[tag] = removed_info;
                LOG_CPP_INFO("[ALSA-Enumerator] Device removed: %s", tag.c_str());
                notifications.push_back(DeviceDiscoveryNotification{tag, prev_info.direction, false});
            }
        }

        registry_ = std::move(updated);
    }

    if (notifications.empty()) {
        LOG_CPP_INFO("[ALSA-Enumerator] No registry changes detected on this pass.");
    }

    if (!notifications.empty() && notification_queue_) {
        LOG_CPP_INFO("[ALSA-Enumerator] Pushing %zu notifications to queue.", notifications.size());
        for (auto& notification : notifications) {
            notification_queue_->push(notification);
        }
    }
}

std::vector<AlsaDeviceEnumerator::ControlHandle> AlsaDeviceEnumerator::open_control_handles() {
    std::vector<ControlHandle> handles;
    int card = -1;
    int err = snd_card_next(&card);
    if (err < 0) {
        LOG_CPP_WARNING("[ALSA-Enumerator] snd_card_next failed while opening controls: %s", snd_strerror(err));
        return handles;
    }
    while (card >= 0) {
        char hw_name[32];
        std::snprintf(hw_name, sizeof(hw_name), "hw:%d", card);
        snd_ctl_t* ctl = nullptr;
        if ((err = snd_ctl_open(&ctl, hw_name, 0)) == 0) {
            if (snd_ctl_subscribe_events(ctl, 1) < 0) {
                snd_ctl_close(ctl);
            } else {
                LOG_CPP_DEBUG("[ALSA-Enumerator] Subscribed to control events for card %d", card);
                handles.push_back(ControlHandle{card, ctl});
            }
        }
        if ((err = snd_card_next(&card)) < 0) {
            LOG_CPP_WARNING("[ALSA-Enumerator] snd_card_next failed while building control handle list: %s", snd_strerror(err));
            break;
        }
    }
    LOG_CPP_DEBUG("[ALSA-Enumerator] Opened %zu control handles for event monitoring.", handles.size());
    return handles;
}

void AlsaDeviceEnumerator::close_control_handles(std::vector<ControlHandle>& handles) {
    for (auto& handle : handles) {
        if (handle.handle) {
            snd_ctl_close(static_cast<snd_ctl_t*>(handle.handle));
            LOG_CPP_DEBUG("[ALSA-Enumerator] Closed control handle for card %d", handle.card_index);
            handle.handle = nullptr;
        }
    }
    handles.clear();
}

void AlsaDeviceEnumerator::setup_fifo_watch() {
    if (inotify_fd_ < 0) {
        inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (inotify_fd_ < 0) {
            LOG_CPP_DEBUG("[ALSA-Enumerator] Failed to create inotify fd (%s)", std::strerror(errno));
            return;
        }
    }

    if (inotify_fd_ >= 0 && inotify_watch_fd_ < 0) {
        if (!ensure_runtime_dir_exists()) {
            return;
        }
        int watch_flags = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB |
                           IN_CLOSE_WRITE | IN_CLOSE_NOWRITE | IN_OPEN |
                           IN_DELETE_SELF | IN_MOVE_SELF;
        int watch_fd = inotify_add_watch(inotify_fd_, kScreamrouterRuntimeDir, watch_flags);
        if (watch_fd < 0) {
            if (errno != ENOENT) {
                LOG_CPP_DEBUG("[ALSA-Enumerator] inotify_add_watch failed for %s (%s)", kScreamrouterRuntimeDir, std::strerror(errno));
            }
        } else {
            inotify_watch_fd_ = watch_fd;
            LOG_CPP_DEBUG("[ALSA-Enumerator] Watching %s for FIFO changes", kScreamrouterRuntimeDir);
        }
    }
}

void AlsaDeviceEnumerator::teardown_fifo_watch() {
    if (inotify_fd_ >= 0) {
        if (inotify_watch_fd_ >= 0) {
            inotify_rm_watch(inotify_fd_, inotify_watch_fd_);
            inotify_watch_fd_ = -1;
        }
        close(inotify_fd_);
        inotify_fd_ = -1;
    }
}

bool AlsaDeviceEnumerator::drain_fifo_watch_events() {
    if (inotify_fd_ < 0) {
        return false;
    }

    bool rescan_needed = false;
    alignas(struct inotify_event) char buffer[4096];
    while (true) {
        ssize_t bytes_read = read(inotify_fd_, buffer, sizeof(buffer));
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                break;
            }
            LOG_CPP_DEBUG("[ALSA-Enumerator] inotify read error: %s", std::strerror(errno));
            return true;
        }
        if (bytes_read == 0) {
            break;
        }

        ssize_t offset = 0;
        while (offset < bytes_read) {
            auto* event = reinterpret_cast<struct inotify_event*>(buffer + offset);
            LOG_CPP_DEBUG("[ALSA-Enumerator] Inotify mask=0x%x name=%s", event->mask, event->len ? event->name : "<none>");
            if (event->mask & IN_Q_OVERFLOW) {
                LOG_CPP_WARNING("[ALSA-Enumerator] Inotify queue overflow; forcing rescan");
                rescan_needed = true;
            }
            if (event->mask & (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB |
                               IN_CLOSE_WRITE | IN_CLOSE_NOWRITE | IN_OPEN)) {
                rescan_needed = true;
            }
            if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
                LOG_CPP_DEBUG("[ALSA-Enumerator] FIFO directory watch invalidated; removing watch");
                teardown_fifo_watch();
                return true;
            }
            offset += sizeof(struct inotify_event) + event->len;
        }
    }

    return rescan_needed;
}

std::optional<SystemDeviceInfo> AlsaDeviceEnumerator::parse_screamrouter_fifo_entry(const std::string& filename) const {
    if (filename.empty() || filename[0] == '.') {
        return std::nullopt;
    }

    std::vector<std::string> tokens;
    std::stringstream ss(filename);
    std::string token;
    while (std::getline(ss, token, '.')) {
        tokens.push_back(token);
    }

    if (tokens.size() < 6) {
        return std::nullopt;
    }

    const std::string& direction_token = tokens[0];
    bool is_capture;
    if (direction_token == "out") {
        is_capture = true;
    } else if (direction_token == "in") {
        is_capture = false;
    } else {
        return std::nullopt;
    }

    const std::string label_raw = tokens[1];

    auto parse_suffix = [](const std::string& value, const char* suffix) -> std::optional<unsigned int> {
        const size_t suffix_len = std::strlen(suffix);
        if (value.size() <= suffix_len || value.rfind(suffix) != value.size() - suffix_len) {
            return std::nullopt;
        }
        try {
            return static_cast<unsigned int>(std::stoul(value.substr(0, value.size() - suffix_len)));
        } catch (...) {
            return std::nullopt;
        }
    };

    const auto rate_opt = parse_suffix(tokens[2], "Hz");
    const auto channels_opt = parse_suffix(tokens[3], "ch");
    const auto bits_opt = parse_suffix(tokens[4], "bit");
    if (!rate_opt || !channels_opt || !bits_opt) {
        return std::nullopt;
    }

    std::string format_token = tokens[5];
    for (size_t i = 6; i < tokens.size(); ++i) {
        format_token.append(".");
        format_token.append(tokens[i]);
    }
    std::string format_upper = format_token;
    std::transform(format_upper.begin(), format_upper.end(), format_upper.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });

    unsigned int rate = *rate_opt;
    unsigned int channels = *channels_opt;
    unsigned int bits = *bits_opt;

    std::string sanitized = sanitize_label(label_raw);
    std::string friendly_label = friendly_from_label(label_raw);

    SystemDeviceInfo info;
    info.tag = std::string(is_capture ? kScreamrouterCapturePrefix : kScreamrouterPlaybackPrefix) + sanitized;
    info.direction = is_capture ? DeviceDirection::CAPTURE : DeviceDirection::PLAYBACK;
    info.friendly_name = std::string("Screamrouter Alsa ") + (is_capture ? "Loopback - " : "Playback - ") +
                         friendly_label + " (" + std::to_string(channels) + "ch, " + std::to_string(rate) +
                         " Hz, " + std::to_string(bits) + "-bit " + format_upper + ")";
    info.channels.min = channels;
    info.channels.max = channels;
    info.sample_rates.min = rate;
    info.sample_rates.max = rate;
    info.bit_depth = bits;
    info.card_index = -1;
    info.device_index = -1;
    info.present = true;
    return info;
}

void AlsaDeviceEnumerator::append_screamrouter_runtime_devices(Registry& registry) {
    if (!ensure_runtime_dir_exists()) {
        return;
    }
    DIR* dir = opendir(kScreamrouterRuntimeDir);
    if (!dir) {
        return;
    }

    while (auto* entry = readdir(dir)) {
        if (entry->d_type != DT_FIFO && entry->d_type != DT_UNKNOWN) {
            continue;
        }
        const std::string filename(entry->d_name);
        auto info_opt = parse_screamrouter_fifo_entry(filename);
        if (!info_opt.has_value()) {
            continue;
        }

        std::string fifo_path = std::string(kScreamrouterRuntimeDir) + "/" + filename;
        SystemDeviceInfo info = std::move(info_opt.value());
        info.hw_id = fifo_path;
        info.endpoint_id = fifo_path;
        registry[info.tag] = info;
    }

    closedir(dir);
}

#endif // defined(__linux__)

} // namespace system_audio
} // namespace audio
} // namespace screamrouter
