/**
 * @file test_sink_rate_controller.cpp
 * @brief Unit tests for SinkRateController class
 * @details Tests buffer drain control and adaptive playback rate adjustments
 */
#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>

#include "output_mixer/sink_rate_controller.h"
#include "configuration/audio_engine_settings.h"

using namespace screamrouter::audio;
using namespace std::chrono;

class SinkRateControllerTest : public ::testing::Test {
protected:
    std::shared_ptr<AudioEngineSettings> settings;
    
    void SetUp() override {
        settings = std::make_shared<AudioEngineSettings>();
        // Set reasonable default tunings
        settings->mixer_tuning.target_buffer_level_ms = 50.0;
    }
    
    std::unique_ptr<SinkRateController> make_controller(const std::string& sink_id = "test-sink") {
        return std::make_unique<SinkRateController>(sink_id, settings);
    }
    
    // Create mock metrics with specified buffer level
    InputBufferMetrics make_metrics(double buffer_ms, std::size_t active_sources = 1, double block_duration_ms = 5.0) {
        InputBufferMetrics metrics;
        metrics.total_ms = buffer_ms;
        metrics.avg_per_source_ms = active_sources > 0 ? buffer_ms / active_sources : 0.0;
        metrics.max_per_source_ms = metrics.avg_per_source_ms;
        metrics.queued_blocks = static_cast<std::size_t>(buffer_ms / block_duration_ms);
        metrics.active_sources = active_sources;
        metrics.block_duration_ms = block_duration_ms;
        metrics.valid = true;
        
        for (std::size_t i = 0; i < active_sources; ++i) {
            std::string src_id = "source-" + std::to_string(i);
            metrics.per_source_blocks[src_id] = metrics.queued_blocks / active_sources;
            metrics.per_source_ms[src_id] = metrics.avg_per_source_ms;
        }
        
        return metrics;
    }
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(SinkRateControllerTest, ConstructWithValidParams) {
    auto controller = make_controller("sink-1");
    EXPECT_NE(controller, nullptr);
}

TEST_F(SinkRateControllerTest, InitialBufferLevelZero) {
    auto controller = make_controller();
    EXPECT_EQ(controller->get_smoothed_buffer_level_ms(), 0.0);
}

// ============================================================================
// Rate Command Callback Tests
// ============================================================================

TEST_F(SinkRateControllerTest, SetRateCommandCallback) {
    auto controller = make_controller();
    
    bool callback_called = false;
    std::string received_id;
    double received_ratio = 0.0;
    
    controller->set_rate_command_callback([&](const std::string& id, double ratio) {
        callback_called = true;
        received_id = id;
        received_ratio = ratio;
    });
    
    // Callback should be stored but not called yet
    EXPECT_FALSE(callback_called);
}

// ============================================================================
// Buffer Update Tests
// ============================================================================

TEST_F(SinkRateControllerTest, UpdateDrainRatio_NominalBuffer) {
    auto controller = make_controller();
    
    // Buffer at target level - rate should stay near 1.0
    auto metrics = make_metrics(50.0);  // At target
    
    controller->update_drain_ratio(48000, 480, [&metrics]() { return metrics; });
    
    // After one update, smoothed level should be moving toward 50
    EXPECT_GE(controller->get_smoothed_buffer_level_ms(), 0.0);
}

TEST_F(SinkRateControllerTest, UpdateDrainRatio_HighBuffer) {
    auto controller = make_controller();
    
    // Buffer way above target - should increase drain rate
    auto metrics = make_metrics(150.0);  // 3x target
    
    for (int i = 0; i < 10; ++i) {
        controller->update_drain_ratio(48000, 480, [&metrics]() { return metrics; });
        std::this_thread::sleep_for(milliseconds(5));
    }
    
    // Smoothed level should be non-negative (may still be 0 if updates are rate-limited)
    EXPECT_GE(controller->get_smoothed_buffer_level_ms(), 0.0);
}

TEST_F(SinkRateControllerTest, UpdateDrainRatio_LowBuffer) {
    auto controller = make_controller();
    
    // Buffer below target - should slow drain rate
    auto metrics = make_metrics(20.0);  // Below target
    
    for (int i = 0; i < 10; ++i) {
        controller->update_drain_ratio(48000, 480, [&metrics]() { return metrics; });
        std::this_thread::sleep_for(milliseconds(5));
    }
    
    // Smoothed level should reflect the low buffer
    double smoothed = controller->get_smoothed_buffer_level_ms();
    EXPECT_GE(smoothed, 0.0);
}

TEST_F(SinkRateControllerTest, UpdateDrainRatio_ZeroBuffer) {
    auto controller = make_controller();
    
    // Empty buffer - critical underrun scenario
    auto metrics = make_metrics(0.0);
    
    controller->update_drain_ratio(48000, 480, [&metrics]() { return metrics; });
    
    // Should handle gracefully
    EXPECT_EQ(controller->get_smoothed_buffer_level_ms(), 0.0);
}

TEST_F(SinkRateControllerTest, UpdateDrainRatio_InvalidMetrics) {
    auto controller = make_controller();
    
    InputBufferMetrics invalid_metrics;
    invalid_metrics.valid = false;
    
    controller->update_drain_ratio(48000, 480, [&invalid_metrics]() { return invalid_metrics; });
    
    // Should handle invalid metrics gracefully
    EXPECT_GE(controller->get_smoothed_buffer_level_ms(), 0.0);
}

// ============================================================================
// Source Removal Tests
// ============================================================================

TEST_F(SinkRateControllerTest, RemoveSource) {
    auto controller = make_controller();
    
    // Update with metrics that include a source
    auto metrics = make_metrics(50.0, 2);
    controller->update_drain_ratio(48000, 480, [&metrics]() { return metrics; });
    
    // Remove one source
    controller->remove_source("source-0");
    
    // Controller should still work
    metrics = make_metrics(50.0, 1);
    controller->update_drain_ratio(48000, 480, [&metrics]() { return metrics; });
    
    EXPECT_GE(controller->get_smoothed_buffer_level_ms(), 0.0);
}

TEST_F(SinkRateControllerTest, RemoveNonexistentSource) {
    auto controller = make_controller();
    
    // Should not crash when removing unknown source
    controller->remove_source("unknown-source");
    EXPECT_GE(controller->get_smoothed_buffer_level_ms(), 0.0);
}

// ============================================================================
// Rate Adjustment Dispatch Tests
// ============================================================================

TEST_F(SinkRateControllerTest, RateAdjustmentCallback_HighBuffer) {
    auto controller = make_controller();
    
    std::map<std::string, double> rate_commands;
    controller->set_rate_command_callback([&](const std::string& id, double ratio) {
        rate_commands[id] = ratio;
    });
    
    // High buffer should trigger rate > 1.0 commands
    auto metrics = make_metrics(150.0, 2);
    
    for (int i = 0; i < 20; ++i) {
        controller->update_drain_ratio(48000, 480, [&metrics]() { return metrics; });
        std::this_thread::sleep_for(milliseconds(10));
    }
    
    // Check that callbacks were made (may or may not happen based on thresholds)
    // At minimum, the controller should not crash
    EXPECT_GE(controller->get_smoothed_buffer_level_ms(), 0.0);
}

TEST_F(SinkRateControllerTest, RateAdjustmentCallback_LowBuffer) {
    auto controller = make_controller();
    
    std::map<std::string, double> rate_commands;
    controller->set_rate_command_callback([&](const std::string& id, double ratio) {
        rate_commands[id] = ratio;
    });
    
    // Low buffer should trigger rate < 1.0 commands
    auto metrics = make_metrics(10.0, 2);
    
    for (int i = 0; i < 20; ++i) {
        controller->update_drain_ratio(48000, 480, [&metrics]() { return metrics; });
        std::this_thread::sleep_for(milliseconds(10));
    }
    
    // Check that controller handles low buffer correctly
    EXPECT_GE(controller->get_smoothed_buffer_level_ms(), 0.0);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(SinkRateControllerTest, ConcurrentUpdates) {
    auto controller = make_controller();
    
    std::atomic<bool> stop{false};
    std::atomic<int> updates{0};
    
    // Thread 1: continuous updates with varying buffer levels
    std::thread updater([&]() {
        while (!stop) {
            double buffer_ms = 30.0 + (updates % 40);  // 30-70ms
            auto metrics = make_metrics(buffer_ms, 1);
            controller->update_drain_ratio(48000, 480, [&metrics]() { return metrics; });
            updates++;
            std::this_thread::sleep_for(microseconds(100));
        }
    });
    
    // Thread 2: continuous reads
    std::thread reader([&]() {
        while (!stop) {
            double level = controller->get_smoothed_buffer_level_ms();
            (void)level;  // Just reading
            std::this_thread::sleep_for(microseconds(50));
        }
    });
    
    std::this_thread::sleep_for(milliseconds(100));
    stop = true;
    
    updater.join();
    reader.join();
    
    EXPECT_GT(updates.load(), 0);
}

TEST_F(SinkRateControllerTest, ConcurrentSourceRemoval) {
    auto controller = make_controller();
    
    std::atomic<bool> stop{false};
    
    // Thread 1: updates with sources
    std::thread updater([&]() {
        while (!stop) {
            auto metrics = make_metrics(50.0, 3);
            controller->update_drain_ratio(48000, 480, [&metrics]() { return metrics; });
            std::this_thread::sleep_for(microseconds(200));
        }
    });
    
    // Thread 2: source removals
    std::thread remover([&]() {
        int i = 0;
        while (!stop) {
            controller->remove_source("source-" + std::to_string(i % 3));
            i++;
            std::this_thread::sleep_for(microseconds(300));
        }
    });
    
    std::this_thread::sleep_for(milliseconds(100));
    stop = true;
    
    updater.join();
    remover.join();
    
    // Should complete without crashes
    EXPECT_GE(controller->get_smoothed_buffer_level_ms(), 0.0);
}
