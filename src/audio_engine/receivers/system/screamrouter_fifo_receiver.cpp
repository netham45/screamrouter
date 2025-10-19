#include "screamrouter_fifo_receiver.h"

#include "../../utils/cpp_logger.h"
#include "../../input_processor/timeshift_manager.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#if SCREAMROUTER_FIFO_CAPTURE_AVAILABLE
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif

namespace screamrouter {
namespace audio {

namespace {
constexpr uint8_t kStereoLayout = 0x03;
constexpr uint8_t kMonoLayout = 0x01;
constexpr size_t kFramesPerChunk = 1024;
}

ScreamrouterFifoReceiver::ScreamrouterFifoReceiver(
    std::string device_tag,
    CaptureParams capture_params,
    std::shared_ptr<NotificationQueue> notification_queue,
    TimeshiftManager* timeshift_manager)
    : NetworkAudioReceiver(0, std::move(notification_queue), timeshift_manager, "[SR-FIFO]" + device_tag),
      device_tag_(std::move(device_tag)),
      capture_params_(std::move(capture_params))
{
#if SCREAMROUTER_FIFO_CAPTURE_AVAILABLE
    if (!capture_params_.hw_id.empty()) {
        fifo_path_ = capture_params_.hw_id;
    } else if (device_tag_.rfind("/", 0) == 0) {
        fifo_path_ = device_tag_;
    }

    channels_ = capture_params_.channels ? capture_params_.channels : 2U;
    sample_rate_ = capture_params_.sample_rate ? capture_params_.sample_rate : 48000U;
    bit_depth_ = (capture_params_.bit_depth == 32) ? 32U : 16U;
    const size_t bytes_per_sample = bit_depth_ / 8U;
    bytes_per_frame_ = bytes_per_sample * channels_;
    if (bytes_per_frame_ == 0) {
        bytes_per_frame_ = channels_ * sizeof(int16_t);
        bit_depth_ = 16U;
    }
    chunk_bytes_ = bytes_per_frame_ * kFramesPerChunk;
    read_buffer_.resize(chunk_bytes_);
    chunk_accumulator_.reserve(chunk_bytes_ * 2);
#endif
}

ScreamrouterFifoReceiver::~ScreamrouterFifoReceiver() noexcept {
    stop();
}

bool ScreamrouterFifoReceiver::setup_socket() {
    return true;
}

void ScreamrouterFifoReceiver::close_socket() {
#if SCREAMROUTER_FIFO_CAPTURE_AVAILABLE
    close_fifo();
#endif
}

void ScreamrouterFifoReceiver::run() {
#if SCREAMROUTER_FIFO_CAPTURE_AVAILABLE
    LOG_CPP_INFO("[SR-FIFO:%s] Capture thread starting (channels=%u, rate=%uHz, bit_depth=%u).",
                 device_tag_.c_str(), channels_, sample_rate_, bit_depth_);

    while (!stop_flag_) {
        if (fifo_fd_ < 0) {
            if (!open_fifo()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
        }

        struct pollfd pfd { fifo_fd_, POLLIN, 0 };
        int poll_result = poll(&pfd, 1, 100);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_CPP_DEBUG("[SR-FIFO:%s] poll failed (%s), reopening FIFO.", device_tag_.c_str(), std::strerror(errno));
            close_fifo();
            continue;
        }
        if (poll_result == 0) {
            continue;
        }
        if (!(pfd.revents & POLLIN)) {
            if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
                LOG_CPP_DEBUG("[SR-FIFO:%s] FIFO poll returned hangup/error, reopening.", device_tag_.c_str());
                close_fifo();
            }
            continue;
        }

        ssize_t bytes_read = read(fifo_fd_, read_buffer_.data(), read_buffer_.size());
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            LOG_CPP_DEBUG("[SR-FIFO:%s] read error (%s), reopening FIFO.", device_tag_.c_str(), std::strerror(errno));
            close_fifo();
            continue;
        }
        if (bytes_read == 0) {
            close_fifo();
            continue;
        }

        chunk_accumulator_.insert(chunk_accumulator_.end(), read_buffer_.begin(), read_buffer_.begin() + bytes_read);
        while (chunk_accumulator_.size() >= chunk_bytes_) {
            std::vector<uint8_t> chunk(chunk_bytes_);
            std::copy_n(chunk_accumulator_.begin(), chunk_bytes_, chunk.begin());
            chunk_accumulator_.erase(chunk_accumulator_.begin(), chunk_accumulator_.begin() + chunk_bytes_);
            dispatch_chunk(std::move(chunk));
        }
    }

    close_fifo();
    LOG_CPP_INFO("[SR-FIFO:%s] Capture thread exiting.", device_tag_.c_str());
#else
    LOG_CPP_WARNING("[SR-FIFO:%s] FIFO capture requested on unsupported platform.", device_tag_.c_str());
#endif
}

bool ScreamrouterFifoReceiver::is_valid_packet_structure(const uint8_t* buffer, int size, const struct sockaddr_in& client_addr) {
    (void)buffer;
    (void)size;
    (void)client_addr;
    return false;
}

bool ScreamrouterFifoReceiver::process_and_validate_payload(const uint8_t* buffer,
                                                            int size,
                                                            const struct sockaddr_in& client_addr,
                                                            std::chrono::steady_clock::time_point received_time,
                                                            TaggedAudioPacket& out_packet,
                                                            std::string& out_source_tag) {
    (void)buffer;
    (void)size;
    (void)client_addr;
    (void)received_time;
    (void)out_packet;
    (void)out_source_tag;
    return false;
}

size_t ScreamrouterFifoReceiver::get_receive_buffer_size() const {
#if SCREAMROUTER_FIFO_CAPTURE_AVAILABLE
    return chunk_bytes_;
#else
    return 0;
#endif
}

int ScreamrouterFifoReceiver::get_poll_timeout_ms() const {
    return 100;
}

#if SCREAMROUTER_FIFO_CAPTURE_AVAILABLE

bool ScreamrouterFifoReceiver::open_fifo() {
    if (fifo_fd_ >= 0) {
        return true;
    }

    if (fifo_path_.empty()) {
        fifo_path_ = capture_params_.hw_id;
    }

    if (fifo_path_.empty()) {
        LOG_CPP_ERROR("[SR-FIFO:%s] No FIFO path provided.", device_tag_.c_str());
        return false;
    }

    fifo_fd_ = open(fifo_path_.c_str(), O_RDONLY | O_NONBLOCK);
    if (fifo_fd_ < 0) {
        if (errno != ENXIO) {
            LOG_CPP_DEBUG("[SR-FIFO:%s] Failed to open FIFO %s (%s).", device_tag_.c_str(), fifo_path_.c_str(), std::strerror(errno));
        }
        return false;
    }

    LOG_CPP_INFO("[SR-FIFO:%s] Opened FIFO %s for capture.", device_tag_.c_str(), fifo_path_.c_str());
    return true;
}

void ScreamrouterFifoReceiver::close_fifo() {
    if (fifo_fd_ >= 0) {
        ::close(fifo_fd_);
        fifo_fd_ = -1;
    }
}

void ScreamrouterFifoReceiver::dispatch_chunk(std::vector<uint8_t>&& chunk_data) {
    if (chunk_data.size() != chunk_bytes_) {
        return;
    }

    TaggedAudioPacket packet;
    packet.source_tag = device_tag_;
    packet.audio_data = std::move(chunk_data);
    packet.received_time = std::chrono::steady_clock::now();
    packet.channels = static_cast<int>(channels_);
    packet.sample_rate = static_cast<int>(sample_rate_);
    packet.bit_depth = static_cast<int>(bit_depth_);
    packet.chlayout1 = (channels_ == 1) ? kMonoLayout : kStereoLayout;
    packet.chlayout2 = 0x00;
    packet.playback_rate = 1.0;

    const uint32_t frames_in_chunk = static_cast<uint32_t>(chunk_bytes_ / bytes_per_frame_);
    packet.rtp_timestamp = running_timestamp_;
    running_timestamp_ += frames_in_chunk;

    bool is_new_source = false;
    {
        std::lock_guard<std::mutex> lock(known_tags_mutex_);
        auto result = known_source_tags_.insert(device_tag_);
        is_new_source = result.second;
    }

    {
        std::lock_guard<std::mutex> lock(seen_tags_mutex_);
        if (std::find(seen_tags_.begin(), seen_tags_.end(), device_tag_) == seen_tags_.end()) {
            seen_tags_.push_back(device_tag_);
        }
    }

    if (is_new_source && notification_queue_) {
        notification_queue_->push(DeviceDiscoveryNotification{device_tag_, DeviceDirection::CAPTURE, true});
    }

    if (timeshift_manager_) {
        timeshift_manager_->add_packet(std::move(packet));
    }
}

#endif // SCREAMROUTER_FIFO_CAPTURE_AVAILABLE

} // namespace audio
} // namespace screamrouter
