#include "screamrouter_fifo_sender.h"

#include "../../utils/cpp_logger.h"

#if SCREAMROUTER_FIFO_SENDER_AVAILABLE
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace screamrouter {
namespace audio {

ScreamrouterFifoSender::ScreamrouterFifoSender(const SinkMixerConfig& config)
    : config_(config),
      fifo_path_(config.output_ip)
{
#if SCREAMROUTER_FIFO_SENDER_AVAILABLE
    if (fifo_path_.rfind("sr_in:", 0) == 0) {
        LOG_CPP_WARNING("[SR-FIFO-Sender:%s] Expected FIFO path but received tag '%s'. Configure output_ip with the FIFO path from system device enumeration.",
                        config_.sink_id.c_str(), fifo_path_.c_str());
    }
#endif
}

ScreamrouterFifoSender::~ScreamrouterFifoSender() {
    close();
}

bool ScreamrouterFifoSender::setup() {
#if SCREAMROUTER_FIFO_SENDER_AVAILABLE
    std::lock_guard<std::mutex> lock(state_mutex_);
    open_fifo_locked();
    return true;
#else
    return false;
#endif
}

void ScreamrouterFifoSender::close() {
#if SCREAMROUTER_FIFO_SENDER_AVAILABLE
    std::lock_guard<std::mutex> lock(state_mutex_);
    close_fifo_locked();
#endif
}

void ScreamrouterFifoSender::send_payload(const uint8_t* payload_data, size_t payload_size, const std::vector<uint32_t>& csrcs) {
    (void)csrcs;
#if SCREAMROUTER_FIFO_SENDER_AVAILABLE
    if (!payload_data || payload_size == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!open_fifo_locked()) {
        return;
    }

    const uint8_t* ptr = payload_data;
    size_t remaining = payload_size;
    while (remaining > 0) {
        ssize_t written = write(fifo_fd_, ptr, remaining);
        if (written < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                break;
            }
            if (errno == EPIPE || errno == ENXIO) {
                LOG_CPP_DEBUG("[SR-FIFO-Sender:%s] FIFO consumer disconnected (%s).", config_.sink_id.c_str(), std::strerror(errno));
                close_fifo_locked();
            } else {
                LOG_CPP_DEBUG("[SR-FIFO-Sender:%s] Write error (%s).", config_.sink_id.c_str(), std::strerror(errno));
                close_fifo_locked();
            }
            break;
        }
        if (written == 0) {
            break;
        }
        ptr += static_cast<size_t>(written);
        remaining -= static_cast<size_t>(written);
    }
#else
    (void)payload_size;
#endif
}

#if SCREAMROUTER_FIFO_SENDER_AVAILABLE

bool ScreamrouterFifoSender::open_fifo_locked() {
    if (fifo_fd_ >= 0) {
        return true;
    }

    if (fifo_path_.empty()) {
        LOG_CPP_ERROR("[SR-FIFO-Sender:%s] FIFO path not provided.", config_.sink_id.c_str());
        return false;
    }

    fifo_fd_ = open(fifo_path_.c_str(), O_WRONLY | O_NONBLOCK);
    if (fifo_fd_ < 0) {
        if (errno != ENXIO) {
            LOG_CPP_DEBUG("[SR-FIFO-Sender:%s] Failed to open FIFO %s (%s).", config_.sink_id.c_str(), fifo_path_.c_str(), std::strerror(errno));
        }
        return false;
    }

    LOG_CPP_INFO("[SR-FIFO-Sender:%s] Opened FIFO %s for playback.", config_.sink_id.c_str(), fifo_path_.c_str());
    return true;
}

void ScreamrouterFifoSender::close_fifo_locked() {
    if (fifo_fd_ >= 0) {
        ::close(fifo_fd_);
        fifo_fd_ = -1;
    }
}

#endif // SCREAMROUTER_FIFO_SENDER_AVAILABLE

} // namespace audio
} // namespace screamrouter
