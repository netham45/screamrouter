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
#include <poll.h>
#include <thread>
#endif

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

    while (running_) {
        auto handles = open_control_handles();

        std::vector<pollfd> poll_fds;
        poll_fds.reserve(handles.size());

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

        bool should_rescan = true; // default true to perform periodic refresh
        if (!poll_fds.empty()) {
            int poll_result = poll(poll_fds.data(), static_cast<nfds_t>(poll_fds.size()), kPollTimeoutMs);
            if (poll_result > 0) {
                should_rescan = false; // will flip to true when event read
                for (auto& handle : handles) {
                    auto* ctl = static_cast<snd_ctl_t*>(handle.handle);
                    if (!ctl) {
                        continue;
                    }
                    snd_ctl_event_t* event = nullptr;
                    if (snd_ctl_event_malloc(&event) < 0) {
                        continue;
                    }
                    while (snd_ctl_read(ctl, event) == 1) {
                        should_rescan = true;
                    }
                    snd_ctl_event_free(event);
                }
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
        }
    }

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

#endif // defined(__linux__)

} // namespace system_audio
} // namespace audio
} // namespace screamrouter
