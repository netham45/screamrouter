#pragma once
/**
 * Mock implementations for integration testing.
 * These allow testing the full audio pipeline without real network I/O.
 */

#include "senders/i_network_sender.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>

namespace screamrouter {
namespace audio {
namespace testing {

/**
 * Mock network sender that captures all sent data for verification.
 */
class MockNetworkSender : public INetworkSender {
public:
    MockNetworkSender() = default;
    ~MockNetworkSender() override = default;

    bool setup() override {
        std::lock_guard<std::mutex> lock(mutex_);
        is_open_ = true;
        setup_called_ = true;
        return !fail_setup_;
    }

    void close() override {
        std::lock_guard<std::mutex> lock(mutex_);
        is_open_ = false;
        close_called_ = true;
    }

    void send_payload(const uint8_t* payload_data, size_t payload_size, 
                      const std::vector<uint32_t>& csrcs) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_open_) return;
        
        SentPacket pkt;
        pkt.data.assign(payload_data, payload_data + payload_size);
        pkt.csrcs = csrcs;
        pkt.timestamp = std::chrono::steady_clock::now();
        sent_packets_.push_back(std::move(pkt));
        total_bytes_sent_ += payload_size;
        ++packet_count_;
    }

    // Test inspection methods
    struct SentPacket {
        std::vector<uint8_t> data;
        std::vector<uint32_t> csrcs;
        std::chrono::steady_clock::time_point timestamp;
    };

    std::vector<SentPacket> get_sent_packets() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sent_packets_;
    }

    size_t get_packet_count() const { return packet_count_.load(); }
    size_t get_total_bytes() const { return total_bytes_sent_.load(); }
    bool was_setup_called() const { return setup_called_.load(); }
    bool was_close_called() const { return close_called_.load(); }
    bool is_open() const { return is_open_.load(); }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        sent_packets_.clear();
        total_bytes_sent_ = 0;
        packet_count_ = 0;
    }

    // Test control
    void set_fail_setup(bool fail) { fail_setup_ = fail; }

private:
    mutable std::mutex mutex_;
    std::vector<SentPacket> sent_packets_;
    std::atomic<size_t> total_bytes_sent_{0};
    std::atomic<size_t> packet_count_{0};
    std::atomic<bool> setup_called_{false};
    std::atomic<bool> close_called_{false};
    std::atomic<bool> is_open_{false};
    std::atomic<bool> fail_setup_{false};
};

/**
 * Packet generator for injecting test audio data.
 */
class TestPacketGenerator {
public:
    TestPacketGenerator(int sample_rate, int channels, int bit_depth)
        : sample_rate_(sample_rate), channels_(channels), bit_depth_(bit_depth) {
        bytes_per_sample_ = (bit_depth_ / 8) * channels_;
    }

    // Generate a packet of silence
    std::vector<uint8_t> generate_silence(size_t frames) {
        return std::vector<uint8_t>(frames * bytes_per_sample_, 0);
    }

    // Generate a sine wave packet (16-bit, interleaved)
    std::vector<uint8_t> generate_sine(size_t frames, float frequency) {
        std::vector<uint8_t> data(frames * bytes_per_sample_);
        if (bit_depth_ == 16) {
            int16_t* samples = reinterpret_cast<int16_t*>(data.data());
            for (size_t f = 0; f < frames; ++f) {
                double t = static_cast<double>(f + sample_offset_) / sample_rate_;
                int16_t value = static_cast<int16_t>(
                    32767.0 * std::sin(2.0 * 3.14159265358979 * frequency * t));
                for (int c = 0; c < channels_; ++c) {
                    samples[f * channels_ + c] = value;
                }
            }
            sample_offset_ += frames;
        }
        return data;
    }

    void reset() { sample_offset_ = 0; }

private:
    int sample_rate_;
    int channels_;
    int bit_depth_;
    int bytes_per_sample_;
    size_t sample_offset_ = 0;
};

} // namespace testing
} // namespace audio
} // namespace screamrouter
