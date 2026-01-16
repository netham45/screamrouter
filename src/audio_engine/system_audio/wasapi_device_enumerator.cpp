#ifdef _WIN32

#include "wasapi_device_enumerator.h"

#include "system_audio_tags.h"
#include "windows_utils.h"
#include "../utils/cpp_logger.h"

#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <propvarutil.h>

#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <cstring>
#include <functional>
#include <mmreg.h>

namespace screamrouter {
namespace audio {
namespace system_audio {

namespace {

using Microsoft::WRL::ComPtr;

constexpr REFERENCE_TIME kDefaultBufferDuration = 0; // use system default

std::string BuildTag(const char* prefix, const std::string& endpoint_id) {
    return std::string(prefix) + endpoint_id;
}

std::string LoadFriendlyName(IMMDevice* device) {
    if (!device) {
        return {};
    }
    ComPtr<IPropertyStore> props;
    HRESULT hr = device->OpenPropertyStore(STGM_READ, &props);
    if (FAILED(hr) || !props) {
        return {};
    }

    PROPVARIANT var;
    PropVariantInit(&var);
    hr = props->GetValue(PKEY_Device_FriendlyName, &var);
    std::string friendly;
    if (SUCCEEDED(hr) && var.vt == VT_LPWSTR) {
        friendly = WideToUtf8(var.pwszVal);
    }
    PropVariantClear(&var);
    return friendly;
}

DeviceCapabilityRange BuildChannelRange(const WAVEFORMATEX* format) {
    DeviceCapabilityRange range;
    if (!format) {
        return range;
    }
    range.min = 1;
    range.max = format->nChannels;
    return range;
}

DeviceCapabilityRange BuildSampleRateRange(const WAVEFORMATEX* format) {
    DeviceCapabilityRange range;
    if (!format) {
        return range;
    }
    range.min = format->nSamplesPerSec;
    range.max = format->nSamplesPerSec;
    return range;
}

const WAVEFORMATEX* ResolveBaseFormat(const WAVEFORMATEX* format) {
    if (!format) {
        return nullptr;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return &extensible->Format;
    }
    return format;
}

} // namespace

class WasapiDeviceEnumerator::NotificationClient : public IMMNotificationClient {
public:
    explicit NotificationClient(WasapiDeviceEnumerator* owner)
        : ref_count_(1), owner_(owner) {}

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvInterface) override {
        if (!ppvInterface) {
            return E_POINTER;
        }
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *ppvInterface = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppvInterface = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = static_cast<ULONG>(InterlockedDecrement(&ref_count_));
        if (ref == 0) {
            delete this;
        }
        return ref;
    }

    // IMMNotificationClient
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override {
        notify_owner();
        return S_OK;
    }

    STDMETHODIMP OnDeviceAdded(LPCWSTR) override {
        notify_owner();
        return S_OK;
    }

    STDMETHODIMP OnDeviceRemoved(LPCWSTR) override {
        notify_owner();
        return S_OK;
    }

    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override {
        notify_owner();
        return S_OK;
    }

    STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
        notify_owner();
        return S_OK;
    }

    void Detach() {
        owner_.store(nullptr);
    }

private:
    void notify_owner() {
        auto* owner = owner_.load();
        if (owner) {
            owner->HandleDeviceChange();
        }
    }

    std::atomic<WasapiDeviceEnumerator*> owner_;
    volatile LONG ref_count_;
};

WasapiDeviceEnumerator::WasapiDeviceEnumerator(std::shared_ptr<NotificationQueue> notification_queue)
    : notification_queue_(std::move(notification_queue)) {}

WasapiDeviceEnumerator::~WasapiDeviceEnumerator() {
    stop();
}

void WasapiDeviceEnumerator::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        LOG_CPP_WARNING("[WASAPI-Enumerator] start() called while already running.");
        return;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        LOG_CPP_WARNING("[WASAPI-Enumerator] COM already initialized on different mode. Continuing without own COM lifetime.");
        com_initialized_ = false;
    } else if (SUCCEEDED(hr)) {
        com_initialized_ = true;
    } else {
        LOG_CPP_ERROR("[WASAPI-Enumerator] CoInitializeEx failed: 0x%lx", hr);
        running_ = false;
        return;
    }

    LOG_CPP_INFO("[WASAPI-Enumerator] COM initialized, enumerating audio endpoints.");
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator_));
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WASAPI-Enumerator] Failed to create MMDeviceEnumerator: 0x%lx", hr);
        cleanup_com();
        running_ = false;
        return;
    }

    notification_client_.Attach(new NotificationClient(this));
    hr = device_enumerator_->RegisterEndpointNotificationCallback(notification_client_.Get());
    if (FAILED(hr)) {
        LOG_CPP_ERROR("[WASAPI-Enumerator] Failed to register notification callback: 0x%lx", hr);
        notification_client_->Detach();
        notification_client_.Reset();
        device_enumerator_.Reset();
        cleanup_com();
        running_ = false;
        return;
    }

    LOG_CPP_INFO("[WASAPI-Enumerator] Device notification callback registered successfully.");
    RefreshRegistry(false);
}

void WasapiDeviceEnumerator::stop() {
    bool was_running = running_.exchange(false);
    if (!was_running) {
        return;
    }

    if (device_enumerator_ && notification_client_) {
        device_enumerator_->UnregisterEndpointNotificationCallback(notification_client_.Get());
        notification_client_->Detach();
    }

    notification_client_.Reset();
    device_enumerator_.Reset();

    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        registry_.clear();
    }

    cleanup_com();
}

SystemDeviceEnumerator::Registry WasapiDeviceEnumerator::get_registry_snapshot() const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    return registry_;
}

void WasapiDeviceEnumerator::HandleDeviceChange() {
    if (!running_) {
        return;
    }
    RefreshRegistry(true);
}

void WasapiDeviceEnumerator::RefreshRegistry(bool emit_notifications) {
    if (!device_enumerator_) {
        LOG_CPP_WARNING("[WASAPI-Enumerator] RefreshRegistry called without an active device enumerator.");
        return;
    }

    Registry new_registry;
    EnumerateFlow(eCapture, false, new_registry);
    EnumerateFlow(eRender, false, new_registry);
    EnumerateFlow(eRender, true, new_registry);

    AddDefaultAliases(new_registry);

    std::vector<DeviceDiscoveryNotification> notifications;

    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        if (emit_notifications) {
            for (const auto& [tag, info] : new_registry) {
                auto it = registry_.find(tag);
                if (it == registry_.end()) {
                    notifications.push_back(DeviceDiscoveryNotification{tag, info.direction, true});
                } else if (!(it->second == info)) {
                    notifications.push_back(DeviceDiscoveryNotification{tag, info.direction, true});
                }
            }
            for (const auto& [tag, info] : registry_) {
                if (new_registry.find(tag) == new_registry.end()) {
                    notifications.push_back(DeviceDiscoveryNotification{tag, info.direction, false});
                }
            }
        }
        registry_ = std::move(new_registry);
    }

    if (!notifications.empty() && notification_queue_) {
        for (const auto& note : notifications) {
            notification_queue_->push(note);
        }
    }

    LOG_CPP_INFO("[WASAPI-Enumerator] Registry refreshed (%zu devices tracked).", registry_.size());
}

void WasapiDeviceEnumerator::EnumerateFlow(EDataFlow flow, bool loopback, Registry& out_registry) {
    if (!device_enumerator_) {
        return;
    }

    DWORD state_mask = DEVICE_STATE_ACTIVE;
    ComPtr<IMMDeviceCollection> collection;
    HRESULT hr = device_enumerator_->EnumAudioEndpoints(flow, state_mask, &collection);
    if (FAILED(hr) || !collection) {
        LOG_CPP_WARNING("[WASAPI-Enumerator] EnumAudioEndpoints failed: 0x%lx", hr);
        return;
    }

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        hr = collection->Item(i, &device);
        if (FAILED(hr) || !device) {
            continue;
        }

        LPWSTR device_id_w = nullptr;
        hr = device->GetId(&device_id_w);
        if (FAILED(hr)) {
            continue;
        }
        std::string endpoint_id = WideToUtf8(device_id_w);
        CoTaskMemFree(device_id_w);

        if (endpoint_id.empty()) {
            continue;
        }

        std::string tag;
        DeviceDirection direction = DeviceDirection::CAPTURE;
        if (flow == eCapture && !loopback) {
            tag = BuildTag(kWasapiCapturePrefix, endpoint_id);
            direction = DeviceDirection::CAPTURE;
        } else if (flow == eRender && loopback) {
            tag = BuildTag(kWasapiLoopbackPrefix, endpoint_id);
            direction = DeviceDirection::CAPTURE;
        } else {
            tag = BuildTag(kWasapiPlaybackPrefix, endpoint_id);
            direction = DeviceDirection::PLAYBACK;
        }

        auto info = BuildDeviceInfo(device.Get(), endpoint_id, direction, loopback);
        info.tag = tag;
        out_registry[tag] = info;
    }

    LOG_CPP_INFO("[WASAPI-Enumerator] Enumerated %u endpoint(s) for flow %s%s.",
                 count,
                 (flow == eCapture) ? "Capture" : "Render",
                 loopback ? " (Loopback)" : "");
}

SystemDeviceInfo WasapiDeviceEnumerator::BuildDeviceInfo(IMMDevice* device,
                                                         const std::string& endpoint_id,
                                                         DeviceDirection direction,
                                                         bool loopback) {
    SystemDeviceInfo info;
    info.present = true;
    info.endpoint_id = endpoint_id;
    info.hw_id = endpoint_id;
    info.direction = direction;
    std::string friendly = LoadFriendlyName(device);
    if (friendly.empty()) {
        friendly = endpoint_id;
    }
    if (loopback) {
        friendly += " (Loopback)";
    }
    info.friendly_name = friendly;

    ComPtr<IAudioClient> audio_client;
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audio_client);
    if (SUCCEEDED(hr) && audio_client) {
        WAVEFORMATEX* mix_format = nullptr;
        hr = audio_client->GetMixFormat(&mix_format);
        if (SUCCEEDED(hr) && mix_format) {
            const WAVEFORMATEX* base = ResolveBaseFormat(mix_format);
            info.channels = BuildChannelRange(base);
            info.sample_rates = BuildSampleRateRange(base);
            unsigned int bits = base ? base->wBitsPerSample : 0;
            if (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix_format);
                if (extensible->Samples.wValidBitsPerSample > 0) {
                    bits = extensible->Samples.wValidBitsPerSample;
                }
            }
            if (bits > 0) {
                info.bit_depth = bits;
                info.bit_depths = {bits};
            }
            CoTaskMemFree(mix_format);
        }
    }

    return info;
}

void WasapiDeviceEnumerator::AddDefaultAliases(Registry& registry) {
    if (!device_enumerator_) {
        return;
    }

    auto add_alias = [&registry](const std::string& source_tag, const char* alias_tag) {
        auto it = registry.find(source_tag);
        if (it == registry.end()) {
            return;
        }
        SystemDeviceInfo info = it->second;
        info.tag = alias_tag;
        registry[alias_tag] = info;
    };

    add_default_alias_for_flow(eCapture, kWasapiDefaultCaptureTag, kWasapiCapturePrefix, registry, add_alias);
    add_default_alias_for_flow(eRender, kWasapiDefaultPlaybackTag, kWasapiPlaybackPrefix, registry, add_alias);
    add_default_alias_for_flow(eRender, kWasapiDefaultLoopbackTag, kWasapiLoopbackPrefix, registry, add_alias, true);
}

void WasapiDeviceEnumerator::add_default_alias_for_flow(EDataFlow flow,
                                                        const char* alias_tag,
                                                        const char* prefix,
                                                        Registry& registry,
                                                        const std::function<void(const std::string&, const char*)>& add_alias,
                                                        bool loopback) {
    ComPtr<IMMDevice> default_device;
    HRESULT hr = device_enumerator_->GetDefaultAudioEndpoint(flow, eConsole, &default_device);
    if (FAILED(hr) || !default_device) {
        return;
    }

    LPWSTR default_id_w = nullptr;
    hr = default_device->GetId(&default_id_w);
    if (FAILED(hr) || !default_id_w) {
        return;
    }
    std::string endpoint_id = WideToUtf8(default_id_w);
    CoTaskMemFree(default_id_w);
    if (endpoint_id.empty()) {
        return;
    }

    std::string source_tag = BuildTag(prefix, endpoint_id);
    if (loopback) {
        source_tag = BuildTag(kWasapiLoopbackPrefix, endpoint_id);
    }
    add_alias(source_tag, alias_tag);
}

void WasapiDeviceEnumerator::cleanup_com() {
    if (com_initialized_) {
        CoUninitialize();
        com_initialized_ = false;
    }
}

} // namespace system_audio
} // namespace audio
} // namespace screamrouter

#endif // _WIN32
