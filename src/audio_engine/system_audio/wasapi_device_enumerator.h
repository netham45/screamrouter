#pragma once

#ifdef _WIN32

#include "system_device_enumerator.h"

#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <string>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace screamrouter {
namespace audio {
namespace system_audio {

class WasapiDeviceEnumerator : public SystemDeviceEnumerator {
public:
    explicit WasapiDeviceEnumerator(std::shared_ptr<NotificationQueue> notification_queue);
    ~WasapiDeviceEnumerator() override;

    void start() override;
    void stop() override;
    Registry get_registry_snapshot() const override;

private:
    class NotificationClient;

    void HandleDeviceChange();
    void RefreshRegistry(bool emit_notifications);
    void EnumerateFlow(EDataFlow flow, bool loopback, Registry& out_registry);
    SystemDeviceInfo BuildDeviceInfo(IMMDevice* device,
                                     const std::string& endpoint_id,
                                     DeviceDirection direction,
                                     bool loopback);
    void AddDefaultAliases(Registry& registry);
    void add_default_alias_for_flow(EDataFlow flow,
                                    const char* alias_tag,
                                    const char* prefix,
                                    Registry& registry,
                                    const std::function<void(const std::string&, const char*)>& add_alias,
                                    bool loopback = false);
    void cleanup_com();

    std::shared_ptr<NotificationQueue> notification_queue_;
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> device_enumerator_;
    Microsoft::WRL::ComPtr<NotificationClient> notification_client_;
    mutable std::mutex registry_mutex_;
    Registry registry_;
    std::atomic<bool> running_{false};
    bool com_initialized_ = false;
};

} // namespace system_audio
} // namespace audio
} // namespace screamrouter

#endif // _WIN32
