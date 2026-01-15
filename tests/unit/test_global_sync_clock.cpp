#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "synchronization/global_synchronization_clock.h"

// Mock logging
namespace screamrouter::audio::logging {
    enum class LogLevel { DEBUG, INFO, WARNING, ERR };
    std::atomic<LogLevel> current_log_level{LogLevel::INFO};
    void log_message(LogLevel, const char*, int, const char*, ...) {}
    const char* get_base_filename(const char* path) { return path; }
}

using namespace screamrouter::audio;
using namespace std::chrono;

class GlobalSynchronizationClockTest : public ::testing::Test {
protected:
    static constexpr int kSampleRate = 48000;
    GlobalSynchronizationClock clock{kSampleRate};
};

TEST_F(GlobalSynchronizationClockTest, Construction) {
    EXPECT_EQ(clock.get_sample_rate(), kSampleRate);
    EXPECT_FALSE(clock.is_enabled());
}

TEST_F(GlobalSynchronizationClockTest, EnableDisable) {
    EXPECT_FALSE(clock.is_enabled());
    
    clock.set_enabled(true);
    EXPECT_TRUE(clock.is_enabled());
    
    clock.set_enabled(false);
    EXPECT_FALSE(clock.is_enabled());
}

TEST_F(GlobalSynchronizationClockTest, SinkRegistration) {
    clock.register_sink("sink1", 0);
    clock.register_sink("sink2", 0);
    
    auto stats = clock.get_stats();
    EXPECT_EQ(stats.active_sinks, 2);
    
    clock.unregister_sink("sink1");
    stats = clock.get_stats();
    EXPECT_EQ(stats.active_sinks, 1);
}

TEST_F(GlobalSynchronizationClockTest, InitializeReference) {
    auto now = steady_clock::now();
    clock.initialize_reference(1000, now);
    
    // Should be able to get playback timestamp now
    uint64_t ts = clock.get_current_playback_timestamp();
    EXPECT_GE(ts, 1000u);  // At least the initial value
}

TEST_F(GlobalSynchronizationClockTest, TimingReport) {
    clock.register_sink("sink1", 0);
    
    SinkTimingReport report;
    report.samples_output = 4800;  // 100ms worth
    report.rtp_timestamp_output = 4800;
    report.dispatch_time = steady_clock::now();
    report.had_underrun = false;
    report.buffer_fill_percentage = 0.5;
    
    // Should not crash
    clock.report_sink_timing("sink1", report);
}

TEST_F(GlobalSynchronizationClockTest, RateAdjustmentDefaultsToOne) {
    clock.register_sink("sink1", 0);
    clock.initialize_reference(0, steady_clock::now());
    
    double rate = clock.calculate_rate_adjustment("sink1");
    EXPECT_NEAR(rate, 1.0, 0.001);
}

TEST_F(GlobalSynchronizationClockTest, RateAdjustmentForUnknownSink) {
    double rate = clock.calculate_rate_adjustment("unknown_sink");
    EXPECT_DOUBLE_EQ(rate, 1.0);
}

TEST_F(GlobalSynchronizationClockTest, BarrierBypassWhenDisabled) {
    clock.register_sink("sink1", 0);
    clock.set_enabled(false);
    
    // Should return immediately without blocking
    bool result = clock.wait_for_dispatch_barrier("sink1", 100);
    EXPECT_TRUE(result);
}

TEST_F(GlobalSynchronizationClockTest, BarrierBypassWithSingleSink) {
    clock.register_sink("sink1", 0);
    clock.set_enabled(true);
    
    // Single sink should not need to wait
    bool result = clock.wait_for_dispatch_barrier("sink1", 100);
    EXPECT_TRUE(result);
}

TEST_F(GlobalSynchronizationClockTest, StatsReportZeroWhenUninitialized) {
    auto stats = clock.get_stats();
    EXPECT_EQ(stats.active_sinks, 0);
    EXPECT_EQ(stats.current_playback_timestamp, 0u);
    EXPECT_EQ(stats.total_barrier_timeouts, 0u);
}

TEST_F(GlobalSynchronizationClockTest, MultiSinkBarrierTimeout) {
    clock.register_sink("sink1", 0);
    clock.register_sink("sink2", 0);
    clock.set_enabled(true);
    
    // With two sinks, waiting alone should timeout
    auto start = steady_clock::now();
    bool result = clock.wait_for_dispatch_barrier("sink1", 50);  // 50ms timeout
    auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start);
    
    // Should timeout since sink2 never arrives
    EXPECT_FALSE(result);
    EXPECT_GE(elapsed.count(), 40);  // Should have waited close to timeout
    
    auto stats = clock.get_stats();
    EXPECT_GT(stats.total_barrier_timeouts, 0u);
}
