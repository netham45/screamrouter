#include "src/audio_engine/input_processor/timeshift_manager.h"
#include "src/audio_engine/utils/thread_safe_queue.h"
#include "src/audio_engine/audio_types.h"
#include "src/audio_engine/utils/cpp_logger.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace screamrouter::audio;

namespace {
struct SimulationConfig {
    double packet_ms = 5.0;
    double stop_gap_ms = 20.0;
    int cycle_packets = 8;
    int stop_packet_index = 4;
    int total_packets = 400;
    int sample_rate = 48000;
    int channels = 2;
    int bit_depth = 16;
};

TaggedAudioPacket make_packet(const std::string& tag,
                              uint32_t rtp_timestamp,
                              std::chrono::steady_clock::time_point ts,
                              const SimulationConfig& cfg) {
    TaggedAudioPacket packet;
    packet.source_tag = tag;
    packet.received_time = ts;
    packet.rtp_timestamp = rtp_timestamp;
    packet.sample_rate = cfg.sample_rate;
    packet.channels = cfg.channels;
    packet.bit_depth = cfg.bit_depth;
    const size_t bytes_per_sample = static_cast<size_t>(cfg.bit_depth / 8);
    const size_t frames_per_packet = static_cast<size_t>(cfg.sample_rate * cfg.packet_ms / 1000.0);
    const size_t payload_bytes = frames_per_packet * static_cast<size_t>(cfg.channels) * bytes_per_sample;
    packet.audio_data.resize(payload_bytes, 0);
    packet.playback_rate = 1.0;
    return packet;
}
}

int main() {
    SimulationConfig cfg;

    auto settings = std::make_shared<AudioEngineSettings>();
    settings->timeshift_tuning.target_buffer_level_ms = 20.0;
    settings->timeshift_tuning.proportional_gain_kp = 0.05;
    settings->timeshift_tuning.min_playback_rate = 0.80;
    settings->timeshift_tuning.max_playback_rate = 1.25;
    settings->timeshift_tuning.absolute_max_playback_rate = 1.35;
    settings->timeshift_tuning.jitter_smoothing_factor = 8.0;
    settings->timeshift_tuning.jitter_safety_margin_multiplier = 0.5;

    TimeshiftManager manager(std::chrono::seconds(5), settings);
    manager.start();

    auto queue = std::make_shared<PacketQueue>();
    manager.register_processor("sim-instance", "sim-source", queue, 0, 0.0f);

    const size_t frames_per_packet = static_cast<size_t>(cfg.sample_rate * cfg.packet_ms / 1000.0);

    auto t0 = std::chrono::steady_clock::now();
    uint32_t rtp_timestamp = 0;

    std::vector<double> intervals(cfg.total_packets, cfg.packet_ms);
    for (int i = 0; i < cfg.total_packets; ++i) {
        if ((i % cfg.cycle_packets) == cfg.stop_packet_index) {
            intervals[i] = cfg.packet_ms + cfg.stop_gap_ms;
        }
    }

    std::thread producer([&]() {
        for (int i = 0; i < cfg.total_packets; ++i) {
            auto sleep_us = static_cast<int64_t>(intervals[i] * 1000.0);
            if (sleep_us > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
            }
            auto arrival_time = std::chrono::steady_clock::now();
            auto packet = make_packet("sim-source", rtp_timestamp, arrival_time, cfg);
            manager.add_packet(std::move(packet));
            rtp_timestamp += static_cast<uint32_t>(frames_per_packet);
        }
    });

    std::atomic<bool> collecting{true};
    std::thread consumer([&]() {
        size_t idx = 0;
        while (collecting) {
            TaggedAudioPacket packet;
            if (!queue->pop(packet)) {
                break;
            }
            idx++;
            auto stats = manager.get_stats();
            double target_ms = 0.0;
            double fill_pct = 0.0;
            auto target_it = stats.stream_target_buffer_level_ms.find("sim-source");
            if (target_it != stats.stream_target_buffer_level_ms.end()) {
                target_ms = target_it->second;
            }
            auto fill_it = stats.stream_buffer_target_fill_percentage.find("sim-source");
            if (fill_it != stats.stream_buffer_target_fill_percentage.end()) {
                fill_pct = fill_it->second;
            }
            double estimated_buffer_ms = (target_ms > 0.0) ? (target_ms * fill_pct / 100.0) : 0.0;
            auto recv_ms = std::chrono::duration<double, std::milli>(packet.received_time - t0).count();

            std::cout << "chunk=" << idx
                      << " recv_ms=" << recv_ms
                      << " rate=" << packet.playback_rate
                      << " target_ms=" << target_ms
                      << " buffer_est_ms=" << estimated_buffer_ms
                      << std::endl;
        }
    });

    producer.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    queue->stop();
    collecting = false;
    consumer.join();

    manager.stop();
    return 0;
}
