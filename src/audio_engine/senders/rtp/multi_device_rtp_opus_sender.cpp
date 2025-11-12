/**
 * @file multi_device_rtp_opus_sender.cpp
 * @brief Implementation for sending Opus RTP audio to multiple receivers.
 */

#include "multi_device_rtp_opus_sender.h"
#include "../../utils/cpp_logger.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <random>

namespace screamrouter {
namespace audio {

namespace {
inline int16_t load_le_i16(const uint8_t* ptr) {
    return static_cast<int16_t>(static_cast<uint16_t>(ptr[0]) |
                                (static_cast<uint16_t>(ptr[1]) << 8));
}
} // namespace

MultiDeviceRtpOpusSender::MultiDeviceRtpOpusSender(const SinkMixerConfig& config)
    : config_(config),
      consumed_samples_(0),
      rtp_timestamp_(0),
      rtcp_controller_(nullptr),
      total_packets_sent_(0),
      total_bytes_sent_(0) {
    LOG_CPP_INFO("[MultiDeviceRtpOpusSender:%s] Initializing with %zu receivers",
                 config_.sink_id.c_str(), config_.rtp_receivers.size());

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis32;
    rtp_timestamp_ = dis32(gen);

    LOG_CPP_INFO("[MultiDeviceRtpOpusSender:%s] Initializing RTCP controller (delay=%d ms, forced on)",
                 config_.sink_id.c_str(), config_.time_sync_delay_ms);
    rtcp_controller_ = std::make_unique<RtcpController>(config_.time_sync_delay_ms);
    if (!config_.time_sync_enabled) {
        LOG_CPP_WARNING("[MultiDeviceRtpOpusSender:%s] time_sync_enabled=false but RTCP is always enabled for multi-device RTP.",
                        config_.sink_id.c_str());
    }

    active_receivers_.reserve(config_.rtp_receivers.size());
}

MultiDeviceRtpOpusSender::~MultiDeviceRtpOpusSender() noexcept {
    close();
}

bool MultiDeviceRtpOpusSender::setup() {
    LOG_CPP_INFO("[MultiDeviceRtpOpusSender:%s] Setting up %zu receivers",
                 config_.sink_id.c_str(), config_.rtp_receivers.size());

    std::lock_guard<std::mutex> lock(receivers_mutex_);

    if (config_.output_bitdepth != 16) {
        LOG_CPP_WARNING("[MultiDeviceRtpOpusSender:%s] Expected 16-bit PCM input but sink configured for %d bits. Audio will be treated as 16-bit.",
                        config_.sink_id.c_str(), config_.output_bitdepth);
    }
    if (config_.output_channels != kOpusChannels) {
        LOG_CPP_WARNING("[MultiDeviceRtpOpusSender:%s] Expected %d channels but sink configured for %d. Audio will be mixed as stereo.",
                        config_.sink_id.c_str(), kOpusChannels, config_.output_channels);
    }
    if (config_.output_samplerate != kOpusSampleRate) {
        LOG_CPP_WARNING("[MultiDeviceRtpOpusSender:%s] Expected %d Hz sample rate but sink configured for %d Hz. Audio will be treated as %d Hz.",
                        config_.sink_id.c_str(), kOpusSampleRate,
                        config_.output_samplerate, kOpusSampleRate);
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis32;

    destroy_all_receivers();

    for (const auto& receiver_config : config_.rtp_receivers) {
        if (!receiver_config.enabled) {
            LOG_CPP_INFO("[MultiDeviceRtpOpusSender:%s] Skipping disabled receiver %s",
                         config_.sink_id.c_str(), receiver_config.receiver_id.c_str());
            continue;
        }

        if ((receiver_config.channel_map[0] > 1) || (receiver_config.channel_map[1] > 1)) {
            LOG_CPP_ERROR("[MultiDeviceRtpOpusSender:%s] Receiver %s has invalid channel map [%d,%d]. Expected 0 or 1.",
                          config_.sink_id.c_str(), receiver_config.receiver_id.c_str(),
                          receiver_config.channel_map[0], receiver_config.channel_map[1]);
            continue;
        }

        ActiveReceiver receiver;
        receiver.config = receiver_config;

        const uint32_t ssrc = dis32(gen);
        receiver.sender = std::make_unique<RtpSenderCore>(ssrc);
        receiver.sender->set_payload_type(kOpusPayloadType);

        if (!receiver.sender->setup(receiver_config.ip_address, receiver_config.port)) {
            LOG_CPP_ERROR("[MultiDeviceRtpOpusSender:%s] Failed to set up receiver %s at %s:%d",
                          config_.sink_id.c_str(),
                          receiver_config.receiver_id.c_str(),
                          receiver_config.ip_address.c_str(),
                          receiver_config.port);
            continue;
        }

        int error = 0;
        receiver.encoder = opus_encoder_create(kOpusSampleRate, kOpusChannels,
                                               OPUS_APPLICATION_AUDIO, &error);
        if (error != OPUS_OK || !receiver.encoder) {
            LOG_CPP_ERROR("[MultiDeviceRtpOpusSender:%s] Failed to create Opus encoder for receiver %s: %s",
                          config_.sink_id.c_str(),
                          receiver_config.receiver_id.c_str(),
                          opus_strerror(error));
            receiver.sender->close();
            receiver.sender.reset();
            continue;
        }

        if (opus_encoder_ctl(receiver.encoder, OPUS_SET_BITRATE(192000)) != OPUS_OK) {
            LOG_CPP_WARNING("[MultiDeviceRtpOpusSender:%s] Failed to set bitrate for receiver %s",
                            config_.sink_id.c_str(), receiver_config.receiver_id.c_str());
        }
        if (opus_encoder_ctl(receiver.encoder, OPUS_SET_COMPLEXITY(3)) != OPUS_OK) {
            LOG_CPP_WARNING("[MultiDeviceRtpOpusSender:%s] Failed to set complexity for receiver %s",
                            config_.sink_id.c_str(), receiver_config.receiver_id.c_str());
        }
        if (opus_encoder_ctl(receiver.encoder, OPUS_SET_INBAND_FEC(1)) != OPUS_OK) {
            LOG_CPP_WARNING("[MultiDeviceRtpOpusSender:%s] Failed to enable FEC for receiver %s",
                            config_.sink_id.c_str(), receiver_config.receiver_id.c_str());
        }
        if (opus_encoder_ctl(receiver.encoder, OPUS_SET_PACKET_LOSS_PERC(10)) != OPUS_OK) {
            LOG_CPP_WARNING("[MultiDeviceRtpOpusSender:%s] Failed to set packet loss percentage for receiver %s",
                            config_.sink_id.c_str(), receiver_config.receiver_id.c_str());
        }

        receiver.opus_buffer.resize(4096);
        active_receivers_.push_back(std::move(receiver));

        LOG_CPP_INFO("[MultiDeviceRtpOpusSender:%s] Receiver %s ready at %s:%d (SSRC=0x%08X)",
                     config_.sink_id.c_str(),
                     receiver_config.receiver_id.c_str(),
                     receiver_config.ip_address.c_str(),
                     receiver_config.port,
                     ssrc);
    }

    if (active_receivers_.empty()) {
        LOG_CPP_ERROR("[MultiDeviceRtpOpusSender:%s] No active receivers configured.",
                      config_.sink_id.c_str());
        return false;
    }

    if (rtcp_controller_) {
        for (const auto& receiver : active_receivers_) {
            if (!receiver.sender) {
                continue;
            }
            RtcpController::StreamInfo info;
            info.stream_id = receiver.config.receiver_id;
            info.dest_ip = receiver.config.ip_address;
            info.rtcp_port = receiver.config.port + 1;
            info.ssrc = receiver.sender->get_ssrc();
            info.sender = receiver.sender.get();
            rtcp_controller_->add_stream(info);
        }

        if (rtcp_controller_->start()) {
            LOG_CPP_INFO("[MultiDeviceRtpOpusSender:%s] RTCP controller started for %zu streams.",
                         config_.sink_id.c_str(), active_receivers_.size());
        } else {
            LOG_CPP_ERROR("[MultiDeviceRtpOpusSender:%s] Failed to start RTCP controller.",
                          config_.sink_id.c_str());
        }
    }

    pending_samples_.clear();
    consumed_samples_ = 0;

    return true;
}

void MultiDeviceRtpOpusSender::close() {
    LOG_CPP_INFO("[MultiDeviceRtpOpusSender:%s] Closing sender.", config_.sink_id.c_str());

    if (rtcp_controller_) {
        rtcp_controller_->stop();
    }

    std::lock_guard<std::mutex> lock(receivers_mutex_);
    destroy_all_receivers();
    pending_samples_.clear();
    consumed_samples_ = 0;

    LOG_CPP_INFO("[MultiDeviceRtpOpusSender:%s] Closed. Packets sent=%u bytes=%u",
                 config_.sink_id.c_str(),
                 total_packets_sent_.load(),
                 total_bytes_sent_.load());
}

void MultiDeviceRtpOpusSender::send_payload(const uint8_t* payload_data, size_t payload_size,
                                            const std::vector<uint32_t>& csrcs) {
    if (!payload_data || payload_size == 0) {
        return;
    }

    const size_t sample_bytes = sizeof(int16_t);
    if (payload_size % sample_bytes != 0) {
        LOG_CPP_WARNING("[MultiDeviceRtpOpusSender:%s] Payload size %zu not aligned to 16-bit samples.",
                        config_.sink_id.c_str(), payload_size);
        return;
    }

    std::lock_guard<std::mutex> lock(receivers_mutex_);
    if (active_receivers_.empty()) {
        return;
    }

    const size_t total_samples = payload_size / sample_bytes;
    pending_samples_.reserve(pending_samples_.size() + total_samples);

    // Append new samples (little-endian)
    for (size_t offset = 0; offset < payload_size; offset += sample_bytes) {
        pending_samples_.push_back(load_le_i16(payload_data + offset));
    }

    const size_t frame_samples = static_cast<size_t>(kDefaultFrameSamplesPerChannel) * kOpusChannels;
    uint32_t timestamp = rtp_timestamp_.load();

    while (pending_samples_.size() >= consumed_samples_ + frame_samples) {
        const int16_t* frame_ptr = pending_samples_.data() + consumed_samples_;

        for (auto& receiver : active_receivers_) {
            if (!receiver.sender || !receiver.sender->is_ready() || !receiver.encoder) {
                continue;
            }

            const int encoded_bytes = opus_encode(
                receiver.encoder,
                frame_ptr,
                kDefaultFrameSamplesPerChannel,
                receiver.opus_buffer.data(),
                static_cast<opus_int32>(receiver.opus_buffer.size()));

            if (encoded_bytes < 0) {
                LOG_CPP_ERROR("[MultiDeviceRtpOpusSender:%s] Opus encoding failed for receiver %s: %s",
                              config_.sink_id.c_str(),
                              receiver.config.receiver_id.c_str(),
                              opus_strerror(encoded_bytes));
                continue;
            }

            if (encoded_bytes == 0) {
                LOG_CPP_WARNING("[MultiDeviceRtpOpusSender:%s] Empty Opus frame for receiver %s",
                                config_.sink_id.c_str(),
                                receiver.config.receiver_id.c_str());
                continue;
            }

            if (receiver.sender->send_rtp_packet(
                    receiver.opus_buffer.data(),
                    static_cast<size_t>(encoded_bytes),
                    timestamp,
                    csrcs,
                    false)) {
                total_packets_sent_.fetch_add(1);
                total_bytes_sent_.fetch_add(static_cast<uint32_t>(encoded_bytes));
            } else {
                LOG_CPP_ERROR("[MultiDeviceRtpOpusSender:%s] Failed to send RTP packet to receiver %s",
                              config_.sink_id.c_str(),
                              receiver.config.receiver_id.c_str());
            }
        }

        timestamp += static_cast<uint32_t>(kDefaultFrameSamplesPerChannel);
        consumed_samples_ += frame_samples;
    }

    rtp_timestamp_.store(timestamp);

    if (consumed_samples_ > 0) {
        if (consumed_samples_ == pending_samples_.size()) {
            pending_samples_.clear();
            consumed_samples_ = 0;
        } else if (consumed_samples_ >= frame_samples * 4) {
            pending_samples_.erase(
                pending_samples_.begin(),
                pending_samples_.begin() + static_cast<std::vector<int16_t>::difference_type>(consumed_samples_));
            consumed_samples_ = 0;
        }
    }
}

void MultiDeviceRtpOpusSender::teardown_receiver(ActiveReceiver& receiver) {
    if (receiver.encoder) {
        opus_encoder_destroy(receiver.encoder);
        receiver.encoder = nullptr;
    }
    if (receiver.sender) {
        receiver.sender->close();
        receiver.sender.reset();
    }
}

void MultiDeviceRtpOpusSender::destroy_all_receivers() {
    for (auto& receiver : active_receivers_) {
        teardown_receiver(receiver);
    }
    active_receivers_.clear();
}

} // namespace audio
} // namespace screamrouter
