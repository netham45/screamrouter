#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace screamrouter {
namespace audio {
namespace pulseaudio {

struct PulseServerConfig {
    std::string bind_address = "0.0.0.0";
    uint16_t tcp_port = 4713;
    std::string unix_socket_path;
    bool enable_unix_socket = false;
    bool auth_anonymous = true;
};

class PulseCoreHost {
public:
    PulseCoreHost();
    ~PulseCoreHost();

    PulseCoreHost(const PulseCoreHost&) = delete;
    PulseCoreHost& operator=(const PulseCoreHost&) = delete;

    bool initialize(const PulseServerConfig& config);
    void shutdown();

    bool is_running() const noexcept;

private:
    void mainloop_thread();

private:
    PulseServerConfig config_{};
    bool running_ = false;
    std::thread mainloop_thread_;

    // TODO: hold pa_mainloop*, pa_core*, pa_module* handles once libpulse is wired in.
};

} // namespace pulseaudio
} // namespace audio
} // namespace screamrouter
