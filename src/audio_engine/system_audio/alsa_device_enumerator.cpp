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
#include <cstdlib>
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
    LOG_CPP_INFO("[ALSA-Enumerator] stop() requested (running=%d)", running_.load() ? 1 : 0);
    if (running_.exchange(false)) {
        // Proactively tear down inotify to break out of poll() promptly
        teardown_fifo_watch();
        // Signal wake pipe to break out of poll immediately
        if (wake_pipe_[1] >= 0) {
            const char b = 'x';
            (void)write(wake_pipe_[1], &b, 1);
        }
        if (monitor_thread_.joinable()) {
            LOG_CPP_INFO("[ALSA-Enumerator] Joining monitor thread (with timeout)...");
#if defined(__linux__)
            // Try a timed join using pthread_timedjoin_np to avoid indefinite hangs
            pthread_t th = monitor_thread_.native_handle();
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 2; // 2s timeout
            int rc = pthread_timedjoin_np(th, nullptr, &ts);
            if (rc == 0) {
                LOG_CPP_INFO("[ALSA-Enumerator] Monitor thread joined.");
                // std::thread is now joined by pthread; invalidate it safely
                // Workaround: create a dummy thread and swap to reset joinable state
                std::thread().swap(monitor_thread_);
            } else if (rc == ETIMEDOUT) {
                LOG_CPP_WARNING("[ALSA-Enumerator] Monitor thread join timed out; detaching to avoid hang.");
                monitor_thread_.detach();
            } else {
                LOG_CPP_WARNING("[ALSA-Enumerator] pthread_timedjoin_np returned rc=%d (%s); detaching thread.", rc, std::strerror(rc));
                monitor_thread_.detach();
            }
#else
            monitor_thread_.join();
            LOG_CPP_INFO("[ALSA-Enumerator] Monitor thread joined.");
#endif
        } else {
            LOG_CPP_INFO("[ALSA-Enumerator] Monitor thread not joinable.");
        }
    } else {
        LOG_CPP_INFO("[ALSA-Enumerator] Already stopped.");
    }
#else
    running_ = false;
#endif
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        registry_.clear();
    }
    LOG_CPP_INFO("[ALSA-Enumerator] Registry cleared.");
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

std::string trim_whitespace(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::optional<int> parse_numeric_token(const std::string& token) {
    if (token.empty()) {
        return std::nullopt;
    }
    try {
        return static_cast<int>(std::stol(token));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<int> resolve_card_index(const std::string& card_token) {
    if (card_token.empty()) {
        return std::nullopt;
    }
    if (auto numeric = parse_numeric_token(card_token)) {
        return numeric;
    }
    int resolved = snd_card_get_index(card_token.c_str());
    if (resolved >= 0) {
        return resolved;
    }
    return std::nullopt;
}

std::optional<int> resolve_device_index(const std::string& device_token) {
    return parse_numeric_token(device_token);
}

std::string clean_description(const std::string& description) {
    if (description.empty()) {
        return {};
    }
    std::string cleaned = description;
    std::replace(cleaned.begin(), cleaned.end(), '\n', ' ');
    std::replace(cleaned.begin(), cleaned.end(), '\r', ' ');
    while (true) {
        const auto double_space = cleaned.find("  ");
        if (double_space == std::string::npos) {
            break;
        }
        cleaned.erase(double_space, 1);
    }
    return trim_whitespace(cleaned);
}

bool is_probe_safe(const std::string& device_name) {
    if (device_name.rfind("hw:", 0) == 0) {
        return true;
    }
    if (device_name.rfind("plughw:", 0) == 0) {
        return true;
    }
    return false;
}

void populate_pcm_capabilities(SystemDeviceInfo& info, snd_pcm_stream_t stream) {
    snd_pcm_t* pcm_handle = nullptr;
    int open_err = snd_pcm_open(&pcm_handle, info.hw_id.c_str(), stream, SND_PCM_NONBLOCK);
    if (open_err < 0) {
        LOG_CPP_DEBUG("[ALSA-Enumerator] Unable to open %s for capability query: %s", info.hw_id.c_str(), snd_strerror(open_err));
        return;
    }

    snd_pcm_hw_params_t* params = nullptr;
    if (snd_pcm_hw_params_malloc(&params) < 0) {
        LOG_CPP_DEBUG("[ALSA-Enumerator] Failed to allocate hw params for %s", info.hw_id.c_str());
        snd_pcm_close(pcm_handle);
        return;
    }

    if (snd_pcm_hw_params_any(pcm_handle, params) < 0) {
        LOG_CPP_DEBUG("[ALSA-Enumerator] Failed to query hw params for %s", info.hw_id.c_str());
        snd_pcm_hw_params_free(params);
        snd_pcm_close(pcm_handle);
        return;
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
}

SystemDeviceInfo create_hint_device_info(
    const std::string& tag,
    const std::string& device_name,
    DeviceDirection direction,
    const std::string& description,
    const std::optional<int>& card_index,
    const std::optional<int>& device_index,
    const SystemDeviceInfo* previous_info)
{
    SystemDeviceInfo info;
    info.direction = direction;
    info.tag = tag;
    info.hw_id = device_name;
    info.endpoint_id = device_name;
    info.present = true;
    info.card_index = card_index.value_or(-1);
    info.device_index = device_index.value_or(-1);

    if (previous_info && previous_info->hw_id == info.hw_id) {
        info.channels = previous_info->channels;
        info.sample_rates = previous_info->sample_rates;
        info.bit_depth = previous_info->bit_depth;
    }

    std::string friendly = clean_description(description);
    if (friendly.empty()) {
        friendly = device_name;
    }
    if (!friendly.empty()) {
        friendly += (direction == DeviceDirection::CAPTURE) ? " (Capture)" : " (Playback)";
    }
    info.friendly_name = friendly;

    const snd_pcm_stream_t stream = (direction == DeviceDirection::CAPTURE) ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK;

    bool need_probe = !previous_info || previous_info->hw_id != info.hw_id ||
                      previous_info->channels.min == 0 || previous_info->sample_rates.min == 0;

    if (need_probe && is_probe_safe(device_name)) {
        populate_pcm_capabilities(info, stream);
    }

    return info;
}

} // namespace

void AlsaDeviceEnumerator::monitor_loop() {
    LOG_CPP_INFO("[ALSA-Enumerator] Monitoring thread started.");

    enumerate_devices();
    setup_fifo_watch();

    // Setup wake pipe for prompt shutdown
    if (wake_pipe_[0] < 0 || wake_pipe_[1] < 0) {
        int fds[2];
        if (pipe(fds) == 0) {
            wake_pipe_[0] = fds[0];
            wake_pipe_[1] = fds[1];
            LOG_CPP_DEBUG("[ALSA-Enumerator] Wake pipe created rd=%d wr=%d", wake_pipe_[0], wake_pipe_[1]);
        } else {
            LOG_CPP_WARNING("[ALSA-Enumerator] Failed to create wake pipe: %s", std::strerror(errno));
        }
    }

    while (running_) {
        LOG_CPP_DEBUG("[ALSA-Enumerator] Loop begin (running=%d)", running_.load() ? 1 : 0);
        auto handles = open_control_handles();
        LOG_CPP_DEBUG("[ALSA-Enumerator] Opened %zu control handles", handles.size());

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
        if (wake_pipe_[0] >= 0) {
            pollfd wake_fd{};
            wake_fd.fd = wake_pipe_[0];
            wake_fd.events = POLLIN | POLLERR | POLLHUP;
            wake_fd.revents = 0;
            poll_fds.push_back(wake_fd);
        }

        // Attempt to re-establish the directory watch if it previously failed.
        if (running_) {
            setup_fifo_watch();
        }

        bool should_rescan = poll_fds.empty();
        if (!poll_fds.empty()) {
            int poll_result = poll(poll_fds.data(), static_cast<nfds_t>(poll_fds.size()), kPollTimeoutMs);
            LOG_CPP_DEBUG("[ALSA-Enumerator] poll() returned %d", poll_result);
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
                if (wake_pipe_[0] >= 0) {
                    for (const auto& pfd : poll_fds) {
                        if (pfd.fd == wake_pipe_[0] && (pfd.revents & (POLLIN | POLLERR | POLLHUP))) {
                            char buf[64];
                            (void)read(wake_pipe_[0], buf, sizeof(buf));
                            LOG_CPP_INFO("[ALSA-Enumerator] Wake pipe signaled");
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
        LOG_CPP_DEBUG("[ALSA-Enumerator] Closed control handles");

        if (!running_) {
            LOG_CPP_INFO("[ALSA-Enumerator] Stop flag detected; breaking loop");
            break;
        }

        if (should_rescan) {
            LOG_CPP_DEBUG("[ALSA-Enumerator] Triggering device rescan (running=%d)", running_.load() ? 1 : 0);
            enumerate_devices();
            setup_fifo_watch();
        }
    }

    teardown_fifo_watch();
    if (wake_pipe_[0] >= 0) { close(wake_pipe_[0]); wake_pipe_[0] = -1; }
    if (wake_pipe_[1] >= 0) { close(wake_pipe_[1]); wake_pipe_[1] = -1; }
    LOG_CPP_INFO("[ALSA-Enumerator] Monitoring thread exiting.");
}

void AlsaDeviceEnumerator::enumerate_devices() {
    LOG_CPP_INFO("[ALSA-Enumerator] Starting full ALSA device enumeration pass.");
    if (!running_) {
        LOG_CPP_INFO("[ALSA-Enumerator] Stop requested before enumeration; skipping.");
        return;
    }
    Registry scanned_registry;

    Registry previous_registry;
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        previous_registry = registry_;
    }

    size_t processed_hints = 0;
    void** hints = nullptr;
    int err = snd_device_name_hint(-1, "pcm", &hints);
    if (err < 0) {
        LOG_CPP_WARNING("[ALSA-Enumerator] snd_device_name_hint failed: %s", snd_strerror(err));
    } else if (!hints) {
        LOG_CPP_DEBUG("[ALSA-Enumerator] No ALSA device hints returned.");
    } else {
        for (void** hint = hints; *hint != nullptr; ++hint) {
            if (!running_) {
                LOG_CPP_INFO("[ALSA-Enumerator] Stop requested during hint scan; breaking.");
                break;
            }
            char* name_raw = snd_device_name_get_hint(*hint, "NAME");
            if (!name_raw) {
                continue;
            }
            std::string device_name = trim_whitespace(name_raw);
            std::free(name_raw);
            if (device_name.empty()) {
                continue;
            }

            ++processed_hints;

            char* ioid_raw = snd_device_name_get_hint(*hint, "IOID");
            std::string ioid_value;
            if (ioid_raw) {
                ioid_value = trim_whitespace(ioid_raw);
                std::free(ioid_raw);
            }

            std::vector<DeviceDirection> directions;
            if (ioid_value.empty()) {
                directions = {DeviceDirection::CAPTURE, DeviceDirection::PLAYBACK};
            } else {
                std::string ioid_lower = ioid_value;
                std::transform(ioid_lower.begin(), ioid_lower.end(), ioid_lower.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (ioid_lower == "input" || ioid_lower == "capture") {
                    directions = {DeviceDirection::CAPTURE};
                } else if (ioid_lower == "output" || ioid_lower == "playback") {
                    directions = {DeviceDirection::PLAYBACK};
                } else {
                    directions = {DeviceDirection::CAPTURE, DeviceDirection::PLAYBACK};
                }
            }

            char* desc_raw = snd_device_name_get_hint(*hint, "DESC");
            std::string description;
            if (desc_raw) {
                description = desc_raw;
                std::free(desc_raw);
            }

            char* card_raw = snd_device_name_get_hint(*hint, "CARD");
            std::string card_token;
            if (card_raw) {
                card_token = trim_whitespace(card_raw);
                std::free(card_raw);
            }

            char* dev_raw = snd_device_name_get_hint(*hint, "DEV");
            std::string device_token;
            if (dev_raw) {
                device_token = trim_whitespace(dev_raw);
                std::free(dev_raw);
            }

            auto card_index = resolve_card_index(card_token);
            auto device_index = resolve_device_index(device_token);

            for (DeviceDirection direction : directions) {
                if (!running_) {
                    LOG_CPP_INFO("[ALSA-Enumerator] Stop requested mid-enumeration; breaking device loop");
                    break;
                }
                const char* prefix = (direction == DeviceDirection::CAPTURE) ? kAlsaCapturePrefix : kAlsaPlaybackPrefix;
                std::string tag = std::string(prefix) + device_name;
                const SystemDeviceInfo* previous_info = nullptr;
                auto prev_it = previous_registry.find(tag);
                if (prev_it != previous_registry.end()) {
                    previous_info = &prev_it->second;
                }

                SystemDeviceInfo info = create_hint_device_info(tag, device_name, direction, description, card_index, device_index, previous_info);
                LOG_CPP_INFO("[ALSA-Enumerator]   Discovered %s -> %s", info.tag.c_str(), info.friendly_name.c_str());
                LOG_CPP_DEBUG("[ALSA-Enumerator]    alsa_id=%s channels=%u-%u rates=%u-%u",
                               info.hw_id.c_str(),
                               info.channels.min,
                               info.channels.max,
                               info.sample_rates.min,
                               info.sample_rates.max);
                scanned_registry[info.tag] = info;
            }
        }
        snd_device_name_free_hint(hints);
    }

    append_screamrouter_runtime_devices(scanned_registry);

    size_t capture_devices = 0;
    size_t playback_devices = 0;
    for (const auto& [tag, info] : scanned_registry) {
        (void)tag;
        if (info.direction == DeviceDirection::CAPTURE) {
            ++capture_devices;
        } else if (info.direction == DeviceDirection::PLAYBACK) {
            ++playback_devices;
        }
    }

    LOG_CPP_INFO("[ALSA-Enumerator] Enumeration pass complete: hints=%zu capture=%zu playback=%zu total=%zu",
                 processed_hints,
                 capture_devices,
                 playback_devices,
                 scanned_registry.size());

    std::vector<DeviceDiscoveryNotification> notifications;

    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        Registry previous = std::move(previous_registry);
        Registry updated = registry_;

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
        if (!running_) {
            LOG_CPP_INFO("[ALSA-Enumerator] Stop requested while opening control handles; aborting.");
            break;
        }
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
