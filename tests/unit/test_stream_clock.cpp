#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include "input_processor/stream_clock.h"

using namespace screamrouter::audio;
using namespace std::chrono;

class StreamClockTest : public ::testing::Test {
protected:
    static constexpr double kSampleRate = 48000.0;
    StreamClock clock{kSampleRate};
    
    // Helper: convert samples to duration
    steady_clock::duration samples_to_duration(uint32_t samples) {
        return duration_cast<steady_clock::duration>(
            duration<double>(static_cast<double>(samples) / kSampleRate));
    }
};

TEST_F(StreamClockTest, InitialState) {
    EXPECT_FALSE(clock.is_initialized());
    EXPECT_DOUBLE_EQ(clock.get_offset_seconds(), 0.0);
    EXPECT_DOUBLE_EQ(clock.get_drift_ppm(), 0.0);
}

TEST_F(StreamClockTest, InitializesAfterFirstUpdate) {
    auto t0 = steady_clock::now();
    clock.update(0, t0);
    
    EXPECT_TRUE(clock.is_initialized());
}

TEST_F(StreamClockTest, StableClockNoDrift) {
    auto t0 = steady_clock::now();
    
    // Simulate packets arriving exactly on time
    for (uint32_t i = 0; i < 100; ++i) {
        uint32_t rtp_ts = i * 480;  // 10ms worth of samples at 48kHz
        auto arrival = t0 + samples_to_duration(rtp_ts);
        clock.update(rtp_ts, arrival);
    }
    
    // Drift should be near zero for a perfect clock
    double drift_ppm = clock.get_drift_ppm();
    EXPECT_NEAR(drift_ppm, 0.0, 50.0);  // Within 50 ppm
}

TEST_F(StreamClockTest, DetectsSignificantDrift) {
    auto t0 = steady_clock::now();
    
    // Simulate remote clock running 0.1% faster (1000 ppm)
    const double drift_ratio = 1.001;
    
    for (uint32_t i = 0; i < 200; ++i) {
        uint32_t rtp_ts = i * 480;
        auto expected_arrival = t0 + samples_to_duration(rtp_ts);
        // Adjust arrival to simulate clock drift
        auto actual_arrival = t0 + duration_cast<steady_clock::duration>(
            duration<double>(static_cast<double>(rtp_ts) / kSampleRate / drift_ratio));
        clock.update(rtp_ts, actual_arrival);
    }
    
    double drift_ppm = clock.get_drift_ppm();
    // Should detect significant drift (magnitude > 500 ppm)
    // Sign depends on implementation interpretation
    EXPECT_GT(std::abs(drift_ppm), 500.0);
    EXPECT_LT(std::abs(drift_ppm), 2000.0);
}

TEST_F(StreamClockTest, PredictArrivalTime) {
    auto t0 = steady_clock::now();
    
    // Initialize with first packet
    clock.update(0, t0);
    
    // A few more to stabilize
    for (uint32_t i = 1; i <= 10; ++i) {
        clock.update(i * 480, t0 + samples_to_duration(i * 480));
    }
    
    // Predict arrival of a future packet
    uint32_t future_ts = 20 * 480;
    auto predicted = clock.get_expected_arrival_time(future_ts);
    auto expected = t0 + samples_to_duration(future_ts);
    
    // Should be close (within 10ms)
    auto diff = duration_cast<milliseconds>(predicted - expected).count();
    EXPECT_LT(std::abs(diff), 10);
}

TEST_F(StreamClockTest, Reset) {
    auto t0 = steady_clock::now();
    clock.update(0, t0);
    EXPECT_TRUE(clock.is_initialized());
    
    clock.reset();
    EXPECT_FALSE(clock.is_initialized());
}

TEST_F(StreamClockTest, HandlesRtpTimestampWraparound) {
    auto t0 = steady_clock::now();
    
    // Start near wrap point
    uint32_t rtp_ts = 0xFFFFFFFF - 1000;
    clock.update(rtp_ts, t0);
    
    // Continue past wrap
    for (int i = 1; i <= 50; ++i) {
        rtp_ts += 480;  // Will wrap around
        auto arrival = t0 + samples_to_duration(i * 480);
        clock.update(rtp_ts, arrival);
    }
    
    // Should still be initialized and functioning
    EXPECT_TRUE(clock.is_initialized());
    // Drift should remain reasonable
    EXPECT_LT(std::abs(clock.get_drift_ppm()), 100.0);
}

TEST_F(StreamClockTest, HandlesJitter) {
    auto t0 = steady_clock::now();
    
    // Simulate packets with ±5ms jitter
    for (uint32_t i = 0; i < 100; ++i) {
        uint32_t rtp_ts = i * 480;
        auto ideal = t0 + samples_to_duration(rtp_ts);
        // Add pseudo-random jitter
        int jitter_ms = ((i * 7) % 11) - 5;  // -5 to +5 ms
        auto arrival = ideal + milliseconds(jitter_ms);
        clock.update(rtp_ts, arrival);
    }
    
    // Kalman filter should produce estimates; drift may not be zero due to jitter pattern
    // Just verify the filter remains stable and produces finite values
    EXPECT_TRUE(std::isfinite(clock.get_drift_ppm()));
    EXPECT_TRUE(clock.is_initialized());
}

TEST_F(StreamClockTest, InnovationReported) {
    auto t0 = steady_clock::now();
    clock.update(0, t0);
    
    // Send a packet 50ms late
    clock.update(480, t0 + milliseconds(50));
    
    // Innovation should reflect the deviation
    double innovation_ms = clock.get_last_innovation_seconds() * 1000.0;
    // First innovation after init should show the error
    EXPECT_GT(std::abs(innovation_ms), 1.0);
}

TEST_F(StreamClockTest, GetLastUpdateTime) {
    auto t0 = steady_clock::now();
    clock.update(0, t0);
    
    auto last_update = clock.get_last_update_time();
    auto diff = duration_cast<milliseconds>(last_update - t0).count();
    EXPECT_LT(std::abs(diff), 1);  // Should be very close
}
