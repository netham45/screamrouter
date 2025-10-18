#include "pulse_core_host.h"

#include <atomic>
#include <chrono>

namespace screamrouter {
namespace audio {
namespace pulseaudio {

PulseCoreHost::PulseCoreHost() = default;

PulseCoreHost::~PulseCoreHost() {
    shutdown();
}

bool PulseCoreHost::initialize(const PulseServerConfig& config) {
    if (running_) {
        return true;
    }

    config_ = config;
    running_ = true;

    mainloop_thread_ = std::thread([this]() { mainloop_thread(); });

    // TODO: replace with actual PulseAudio initialization once dependency is wired.
    return true;
}

void PulseCoreHost::shutdown() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (mainloop_thread_.joinable()) {
        mainloop_thread_.join();
    }

    // TODO: unload modules and free PulseAudio resources.
}

bool PulseCoreHost::is_running() const noexcept {
    return running_;
}

void PulseCoreHost::mainloop_thread() {
    // TODO: bootstrap pa_mainloop and run until shutdown() flips running_ to false.
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace pulseaudio
} // namespace audio
} // namespace screamrouter

