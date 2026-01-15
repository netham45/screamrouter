/**
 * @file test_receiver_stress.cpp
 * @brief Stress tests for receiver/sender build-up and tear-down scenarios.
 * @details This test suite aggressively exercises the AudioManager's add/remove
 *          operations for sinks, sources, and connections to detect deadlocks
 *          and race conditions. It brute-forces through various parameter
 *          combinations including equalization, channel layouts, volume, etc.
 *
 * Purpose: Track down build-up and tear-down scenarios that deadlock and hang.
 */
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>
#include <vector>
#include <string>
#include <future>
#include <functional>

#include "managers/audio_manager.h"
#include "audio_constants.h"

using namespace screamrouter::audio;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace {

// Test configuration constants
constexpr int NUM_STRESS_ITERATIONS = 50;
constexpr int NUM_CONCURRENT_OPERATIONS = 10;
constexpr auto OPERATION_TIMEOUT = 5s;
constexpr auto STRESS_SETTLE_TIME = 10ms;

// Parameter ranges for brute-force testing
constexpr int SAMPLE_RATES[] = {44100, 48000, 96000};
constexpr int BIT_DEPTHS[] = {16, 24, 32};
constexpr int CHANNEL_COUNTS[] = {1, 2, 4, 6, 8};
const std::string PROTOCOLS[] = {"scream", "rtp"};

}  // namespace

/**
 * @class ReceiverStressTest
 * @brief Base test fixture for receiver lifecycle stress testing.
 */
class ReceiverStressTest : public ::testing::Test {
protected:
    std::shared_ptr<AudioManager> manager;
    std::mt19937 rng{std::random_device{}()};

    void SetUp() override {
        manager = std::make_shared<AudioManager>();
        // Use port 0 to avoid binding, and a small timeshift buffer
        ASSERT_TRUE(manager->initialize(0, 10));
    }

    void TearDown() override {
        if (manager) {
            manager->shutdown();
            manager.reset();
        }
    }

    // Generate a random sink configuration with all parameters varied
    SinkConfig make_random_sink(const std::string& id) {
        SinkConfig config;
        config.id = id;
        config.friendly_name = "Stress Sink " + id;
        config.output_ip = "127.0.0.1";
        config.output_port = 14000 + (rng() % 1000);
        config.samplerate = SAMPLE_RATES[rng() % 3];
        config.bitdepth = BIT_DEPTHS[rng() % 3];
        config.channels = CHANNEL_COUNTS[rng() % 5];
        config.protocol = PROTOCOLS[rng() % 2];
        config.chlayout1 = static_cast<uint8_t>(rng() % 256);
        config.chlayout2 = static_cast<uint8_t>(rng() % 256);
        config.enable_mp3 = (rng() % 2) == 0;
        config.time_sync_enabled = (rng() % 2) == 0;
        config.time_sync_delay_ms = rng() % 100;
        
        // Random speaker layout
        config.speaker_layout.auto_mode = (rng() % 2) == 0;
        if (!config.speaker_layout.auto_mode) {
            config.speaker_layout.matrix.assign(MAX_CHANNELS, 
                std::vector<float>(MAX_CHANNELS, 0.0f));
            for (int i = 0; i < MAX_CHANNELS; ++i) {
                config.speaker_layout.matrix[i][i] = 
                    0.5f + static_cast<float>(rng() % 100) / 200.0f;
            }
        }
        return config;
    }

    // Generate a Scream sink with specific parameters
    SinkConfig make_scream_sink(const std::string& id, int samplerate, 
                                int bitdepth, int channels) {
        SinkConfig config;
        config.id = id;
        config.friendly_name = "Scream Sink " + id;
        config.output_ip = "127.0.0.1";
        config.output_port = 14010;
        config.samplerate = samplerate;
        config.bitdepth = bitdepth;
        config.channels = channels;
        config.protocol = "scream";
        return config;
    }

    // Generate an RTP sink with specific parameters
    SinkConfig make_rtp_sink(const std::string& id, int samplerate, 
                             int bitdepth, int channels) {
        SinkConfig config;
        config.id = id;
        config.friendly_name = "RTP Sink " + id;
        config.output_ip = "127.0.0.1";
        config.output_port = 15004;
        config.samplerate = samplerate;
        config.bitdepth = bitdepth;
        config.channels = channels;
        config.protocol = "rtp";
        return config;
    }

    // Generate a source configuration with random parameters
    SourceConfig make_random_source(const std::string& tag) {
        SourceConfig config;
        config.tag = tag;
        config.initial_volume = 0.1f + static_cast<float>(rng() % 90) / 100.0f;
        config.target_output_channels = CHANNEL_COUNTS[rng() % 5];
        config.target_output_samplerate = SAMPLE_RATES[rng() % 3];
        config.initial_delay_ms = rng() % 500;
        config.initial_timeshift_sec = static_cast<float>(rng() % 50) / 10.0f;
        
        // Random initial EQ
        config.initial_eq.resize(EQ_BANDS);
        for (int i = 0; i < EQ_BANDS; ++i) {
            config.initial_eq[i] = 0.5f + static_cast<float>(rng() % 100) / 100.0f;
        }
        return config;
    }

    // Generate a source configuration with specific parameters
    SourceConfig make_source(const std::string& tag, int channels, int samplerate) {
        SourceConfig config;
        config.tag = tag;
        config.initial_volume = 1.0f;
        config.target_output_channels = channels;
        config.target_output_samplerate = samplerate;
        return config;
    }

    // Generate random parameter updates
    SourceParameterUpdates make_random_updates() {
        SourceParameterUpdates updates;
        
        // Randomly enable different update options
        if (rng() % 2 == 0) {
            updates.volume = 0.1f + static_cast<float>(rng() % 90) / 100.0f;
        }
        if (rng() % 2 == 0) {
            std::vector<float> eq(EQ_BANDS);
            for (int i = 0; i < EQ_BANDS; ++i) {
                eq[i] = 0.5f + static_cast<float>(rng() % 100) / 100.0f;
            }
            updates.eq_values = eq;
        }
        if (rng() % 2 == 0) {
            updates.eq_normalization = (rng() % 2) == 0;
        }
        if (rng() % 2 == 0) {
            updates.volume_normalization = (rng() % 2) == 0;
        }
        if (rng() % 2 == 0) {
            updates.delay_ms = rng() % 500;
        }
        if (rng() % 2 == 0) {
            updates.timeshift_sec = static_cast<float>(rng() % 50) / 10.0f;
        }
        if (rng() % 2 == 0) {
            std::map<int, CppSpeakerLayout> layouts;
            for (int ch : CHANNEL_COUNTS) {
                CppSpeakerLayout layout;
                layout.auto_mode = (rng() % 2) == 0;
                if (!layout.auto_mode) {
                    layout.matrix.assign(MAX_CHANNELS, 
                        std::vector<float>(MAX_CHANNELS, 0.0f));
                    for (int i = 0; i < std::min(ch, MAX_CHANNELS); ++i) {
                        layout.matrix[i][i] = 1.0f;
                    }
                }
                layouts[ch] = layout;
            }
            updates.speaker_layouts_map = layouts;
        }
        return updates;
    }

    // Execute function with timeout, returns true if completed in time
    template<typename Func>
    bool with_timeout(Func&& func, std::chrono::seconds timeout = OPERATION_TIMEOUT) {
        auto future = std::async(std::launch::async, std::forward<Func>(func));
        return future.wait_for(timeout) != std::future_status::timeout;
    }
};

// ============================================================================
// Basic Lifecycle Stress Tests
// ============================================================================

/**
 * @brief Rapidly create and destroy sinks with various parameters.
 */
TEST_F(ReceiverStressTest, RapidSinkCreateDestroy) {
    constexpr int QUICK_ITERATIONS = 5;  // Reduced for debugging
    for (int i = 0; i < QUICK_ITERATIONS; ++i) {
        std::string sink_id = "rapid-sink-" + std::to_string(i);
        
        std::cerr << "[STRESS] iter=" << i << " Starting add_sink for " << sink_id << std::endl << std::flush;
        bool add_result = false;
        auto add_start = std::chrono::steady_clock::now();
        ASSERT_TRUE(with_timeout([&]() {
            add_result = manager->add_sink(make_random_sink(sink_id));
            auto add_end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(add_end - add_start).count();
            std::cerr << "[STRESS] iter=" << i << " add_sink returned " << (add_result ? "true" : "false") 
                      << " for " << sink_id << " (took " << ms << " ms)" << std::endl << std::flush;
            EXPECT_TRUE(add_result);
        })) << "add_sink timed out at iteration " << i;
        
        if (!add_result) {
            std::cerr << "[STRESS] iter=" << i << " add_sink returned false, skipping remove" << std::endl << std::flush;
            continue;
        }
        
        std::cerr << "[STRESS] iter=" << i << " Starting remove_sink for " << sink_id << std::endl << std::flush;
        auto remove_start = std::chrono::steady_clock::now();
        ASSERT_TRUE(with_timeout([&]() {
            bool remove_result = manager->remove_sink(sink_id);
            auto remove_end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(remove_end - remove_start).count();
            std::cerr << "[STRESS] iter=" << i << " remove_sink returned " << (remove_result ? "true" : "false") 
                      << " for " << sink_id << " (took " << ms << " ms)" << std::endl << std::flush;
            EXPECT_TRUE(remove_result);
        })) << "remove_sink timed out at iteration " << i;
    }
}

/**
 * @brief Rapidly create and destroy sources with various parameters.
 */
TEST_F(ReceiverStressTest, RapidSourceCreateDestroy) {
    // Need at least one sink for sources to connect to
    ASSERT_TRUE(manager->add_sink(make_scream_sink("main-sink", 48000, 16, 2)));
    
    for (int i = 0; i < NUM_STRESS_ITERATIONS; ++i) {
        std::string tag = "192.168.1." + std::to_string(i % 255);
        
        std::string instance_id;
        ASSERT_TRUE(with_timeout([&]() {
            instance_id = manager->configure_source(make_random_source(tag));
            EXPECT_FALSE(instance_id.empty()) << "configure_source returned empty at " << i;
        })) << "configure_source timed out at iteration " << i;
        
        if (!instance_id.empty()) {
            ASSERT_TRUE(with_timeout([&]() {
                EXPECT_TRUE(manager->remove_source(instance_id));
            })) << "remove_source timed out at iteration " << i;
        }
    }
    
    manager->remove_sink("main-sink");
}

// ============================================================================
// Brute-Force Parameter Combination Tests
// ============================================================================

/**
 * @brief Test all sample rate / bit depth / channel combinations for Scream.
 */
TEST_F(ReceiverStressTest, ScreamAllParameterCombinations) {
    int combo = 0;
    for (int sr : SAMPLE_RATES) {
        for (int bd : BIT_DEPTHS) {
            for (int ch : CHANNEL_COUNTS) {
                std::string sink_id = "scream-combo-" + std::to_string(combo++);
                
                ASSERT_TRUE(with_timeout([&]() {
                    EXPECT_TRUE(manager->add_sink(make_scream_sink(sink_id, sr, bd, ch)));
                })) << "add_sink timed out for sr=" << sr << " bd=" << bd << " ch=" << ch;
                
                // Add a source and connect it
                std::string source_tag = "source-for-" + sink_id;
                std::string instance_id;
                ASSERT_TRUE(with_timeout([&]() {
                    instance_id = manager->configure_source(make_source(source_tag, ch, sr));
                    EXPECT_FALSE(instance_id.empty());
                })) << "configure_source timed out";
                
                if (!instance_id.empty()) {
                    ASSERT_TRUE(with_timeout([&]() {
                        EXPECT_TRUE(manager->connect_source_sink(instance_id, sink_id));
                    })) << "connect_source_sink timed out";
                    
                    std::this_thread::sleep_for(STRESS_SETTLE_TIME);
                    
                    ASSERT_TRUE(with_timeout([&]() {
                        EXPECT_TRUE(manager->disconnect_source_sink(instance_id, sink_id));
                    })) << "disconnect_source_sink timed out";
                    
                    ASSERT_TRUE(with_timeout([&]() {
                        EXPECT_TRUE(manager->remove_source(instance_id));
                    })) << "remove_source timed out";
                }
                
                ASSERT_TRUE(with_timeout([&]() {
                    EXPECT_TRUE(manager->remove_sink(sink_id));
                })) << "remove_sink timed out";
            }
        }
    }
}

/**
 * @brief Test all sample rate / bit depth / channel combinations for RTP.
 */
TEST_F(ReceiverStressTest, RtpAllParameterCombinations) {
    int combo = 0;
    for (int sr : SAMPLE_RATES) {
        for (int bd : BIT_DEPTHS) {
            for (int ch : CHANNEL_COUNTS) {
                std::string sink_id = "rtp-combo-" + std::to_string(combo++);
                
                ASSERT_TRUE(with_timeout([&]() {
                    EXPECT_TRUE(manager->add_sink(make_rtp_sink(sink_id, sr, bd, ch)));
                })) << "add_sink timed out for sr=" << sr << " bd=" << bd << " ch=" << ch;
                
                // Add a source and connect it
                std::string source_tag = "source-for-" + sink_id;
                std::string instance_id;
                ASSERT_TRUE(with_timeout([&]() {
                    instance_id = manager->configure_source(make_source(source_tag, ch, sr));
                    EXPECT_FALSE(instance_id.empty());
                })) << "configure_source timed out";
                
                if (!instance_id.empty()) {
                    ASSERT_TRUE(with_timeout([&]() {
                        EXPECT_TRUE(manager->connect_source_sink(instance_id, sink_id));
                    })) << "connect_source_sink timed out";
                    
                    std::this_thread::sleep_for(STRESS_SETTLE_TIME);
                    
                    ASSERT_TRUE(with_timeout([&]() {
                        EXPECT_TRUE(manager->disconnect_source_sink(instance_id, sink_id));
                    })) << "disconnect_source_sink timed out";
                    
                    ASSERT_TRUE(with_timeout([&]() {
                        EXPECT_TRUE(manager->remove_source(instance_id));
                    })) << "remove_source timed out";
                }
                
                ASSERT_TRUE(with_timeout([&]() {
                    EXPECT_TRUE(manager->remove_sink(sink_id));
                })) << "remove_sink timed out";
            }
        }
    }
}

// ============================================================================
// Parameter Update Storm Tests
// ============================================================================

/**
 * @brief Spam parameter updates during active connections.
 */
TEST_F(ReceiverStressTest, ParameterUpdateStorm) {
    ASSERT_TRUE(manager->add_sink(make_scream_sink("storm-sink", 48000, 16, 2)));
    
    std::string instance_id = manager->configure_source(make_source("storm-source", 2, 48000));
    ASSERT_FALSE(instance_id.empty());
    ASSERT_TRUE(manager->connect_source_sink(instance_id, "storm-sink"));
    
    // Spam parameter updates
    for (int i = 0; i < NUM_STRESS_ITERATIONS * 2; ++i) {
        ASSERT_TRUE(with_timeout([&]() {
            manager->update_source_parameters(instance_id, make_random_updates());
        })) << "update_source_parameters timed out at iteration " << i;
    }
    
    ASSERT_TRUE(manager->disconnect_source_sink(instance_id, "storm-sink"));
    ASSERT_TRUE(manager->remove_source(instance_id));
    ASSERT_TRUE(manager->remove_sink("storm-sink"));
}

/**
 * @brief Update all EQ bands rapidly.
 */
TEST_F(ReceiverStressTest, EqBandStormAllBands) {
    ASSERT_TRUE(manager->add_sink(make_scream_sink("eq-sink", 48000, 16, 2)));
    
    std::string instance_id = manager->configure_source(make_source("eq-source", 2, 48000));
    ASSERT_FALSE(instance_id.empty());
    ASSERT_TRUE(manager->connect_source_sink(instance_id, "eq-sink"));
    
    // Cycle through all EQ bands with different values
    for (int iteration = 0; iteration < NUM_STRESS_ITERATIONS; ++iteration) {
        for (int band = 0; band < EQ_BANDS; ++band) {
            SourceParameterUpdates updates;
            std::vector<float> eq(EQ_BANDS, 1.0f);
            // Set a specific band to a random value
            eq[band] = 0.1f + static_cast<float>(rng() % 180) / 100.0f;
            updates.eq_values = eq;
            
            ASSERT_TRUE(with_timeout([&]() {
                manager->update_source_parameters(instance_id, updates);
            })) << "update_source_parameters timed out at band " << band;
        }
    }
    
    ASSERT_TRUE(manager->disconnect_source_sink(instance_id, "eq-sink"));
    ASSERT_TRUE(manager->remove_source(instance_id));
    ASSERT_TRUE(manager->remove_sink("eq-sink"));
}

/**
 * @brief Test all speaker layout matrix configurations.
 */
TEST_F(ReceiverStressTest, SpeakerLayoutStorm) {
    ASSERT_TRUE(manager->add_sink(make_scream_sink("layout-sink", 48000, 16, 8)));
    
    std::string instance_id = manager->configure_source(make_source("layout-source", 8, 48000));
    ASSERT_FALSE(instance_id.empty());
    ASSERT_TRUE(manager->connect_source_sink(instance_id, "layout-sink"));
    
    // Test different speaker layout configurations
    for (int iteration = 0; iteration < NUM_STRESS_ITERATIONS; ++iteration) {
        for (int input_channels : CHANNEL_COUNTS) {
            SourceParameterUpdates updates;
            std::map<int, CppSpeakerLayout> layouts;
            
            CppSpeakerLayout layout;
            layout.auto_mode = (iteration % 2) == 0;
            if (!layout.auto_mode) {
                layout.matrix.assign(MAX_CHANNELS, std::vector<float>(MAX_CHANNELS, 0.0f));
                // Create various cross-feed patterns
                for (int i = 0; i < MAX_CHANNELS; ++i) {
                    for (int j = 0; j < MAX_CHANNELS; ++j) {
                        layout.matrix[i][j] = static_cast<float>((iteration + i + j) % 100) / 100.0f;
                    }
                }
            }
            layouts[input_channels] = layout;
            updates.speaker_layouts_map = layouts;
            
            ASSERT_TRUE(with_timeout([&]() {
                manager->update_source_parameters(instance_id, updates);
            })) << "update_source_parameters timed out for " << input_channels << " channels";
        }
    }
    
    ASSERT_TRUE(manager->disconnect_source_sink(instance_id, "layout-sink"));
    ASSERT_TRUE(manager->remove_source(instance_id));
    ASSERT_TRUE(manager->remove_sink("layout-sink"));
}

// ============================================================================
// Multi-Sink / Multi-Source Stress Tests
// ============================================================================

/**
 * @brief Create many sinks and sources, connect them all, then remove.
 */
TEST_F(ReceiverStressTest, ManyToManyConnections) {
    const int NUM_SINKS = 5;
    const int NUM_SOURCES = 5;
    
    std::vector<std::string> sink_ids;
    std::vector<std::string> source_instances;
    
    // Create sinks with varied configurations
    for (int i = 0; i < NUM_SINKS; ++i) {
        std::string sink_id = "m2m-sink-" + std::to_string(i);
        sink_ids.push_back(sink_id);
        
        ASSERT_TRUE(with_timeout([&]() {
            EXPECT_TRUE(manager->add_sink(make_random_sink(sink_id)));
        })) << "add_sink timed out for sink " << i;
    }
    
    // Create sources
    for (int i = 0; i < NUM_SOURCES; ++i) {
        std::string tag = "m2m-source-" + std::to_string(i);
        
        std::string instance_id;
        ASSERT_TRUE(with_timeout([&]() {
            instance_id = manager->configure_source(make_random_source(tag));
            EXPECT_FALSE(instance_id.empty());
        })) << "configure_source timed out for source " << i;
        
        if (!instance_id.empty()) {
            source_instances.push_back(instance_id);
        }
    }
    
    // Connect all sources to all sinks (N×M connections)
    for (const auto& src : source_instances) {
        for (const auto& sink : sink_ids) {
            ASSERT_TRUE(with_timeout([&]() {
                manager->connect_source_sink(src, sink);
            })) << "connect timed out for " << src << " -> " << sink;
        }
    }
    
    std::this_thread::sleep_for(STRESS_SETTLE_TIME * 5);
    
    // Disconnect all
    for (const auto& src : source_instances) {
        for (const auto& sink : sink_ids) {
            ASSERT_TRUE(with_timeout([&]() {
                manager->disconnect_source_sink(src, sink);
            })) << "disconnect timed out for " << src << " -> " << sink;
        }
    }
    
    // Remove all sources
    for (const auto& src : source_instances) {
        ASSERT_TRUE(with_timeout([&]() {
            manager->remove_source(src);
        })) << "remove_source timed out for " << src;
    }
    
    // Remove all sinks
    for (const auto& sink : sink_ids) {
        ASSERT_TRUE(with_timeout([&]() {
            manager->remove_sink(sink);
        })) << "remove_sink timed out for " << sink;
    }
}

/**
 * @brief Repeatedly reconfigure the same source while keeping it connected.
 */
TEST_F(ReceiverStressTest, ReconfigurationWhileConnected) {
    ASSERT_TRUE(manager->add_sink(make_scream_sink("reconfig-sink", 48000, 16, 2)));
    ASSERT_TRUE(manager->add_sink(make_rtp_sink("reconfig-rtp-sink", 48000, 16, 2)));
    
    std::string instance_id = manager->configure_source(make_source("reconfig-source", 2, 48000));
    ASSERT_FALSE(instance_id.empty());
    
    // Connect to both sinks
    ASSERT_TRUE(manager->connect_source_sink(instance_id, "reconfig-sink"));
    ASSERT_TRUE(manager->connect_source_sink(instance_id, "reconfig-rtp-sink"));
    
    // Spam updates while connected
    for (int i = 0; i < NUM_STRESS_ITERATIONS; ++i) {
        // Update volume
        SourceParameterUpdates vol_update;
        vol_update.volume = static_cast<float>(i % 100) / 100.0f;
        ASSERT_TRUE(with_timeout([&]() {
            manager->update_source_parameters(instance_id, vol_update);
        }));
        
        // Update EQ
        SourceParameterUpdates eq_update;
        std::vector<float> eq(EQ_BANDS);
        for (int b = 0; b < EQ_BANDS; ++b) {
            eq[b] = 0.5f + static_cast<float>((i + b) % 50) / 100.0f;
        }
        eq_update.eq_values = eq;
        ASSERT_TRUE(with_timeout([&]() {
            manager->update_source_parameters(instance_id, eq_update);
        }));
        
        // Update delay
        SourceParameterUpdates delay_update;
        delay_update.delay_ms = i % 200;
        ASSERT_TRUE(with_timeout([&]() {
            manager->update_source_parameters(instance_id, delay_update);
        }));
        
        // Update timeshift
        SourceParameterUpdates ts_update;
        ts_update.timeshift_sec = static_cast<float>(i % 30) / 10.0f;
        ASSERT_TRUE(with_timeout([&]() {
            manager->update_source_parameters(instance_id, ts_update);
        }));
    }
    
    ASSERT_TRUE(manager->disconnect_source_sink(instance_id, "reconfig-sink"));
    ASSERT_TRUE(manager->disconnect_source_sink(instance_id, "reconfig-rtp-sink"));
    ASSERT_TRUE(manager->remove_source(instance_id));
    ASSERT_TRUE(manager->remove_sink("reconfig-sink"));
    ASSERT_TRUE(manager->remove_sink("reconfig-rtp-sink"));
}

// ============================================================================
// Concurrent Operation Tests
// ============================================================================

/**
 * @brief Test concurrent sink add/remove operations.
 */
TEST_F(ReceiverStressTest, ConcurrentSinkOperations) {
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};
    
    for (int i = 0; i < NUM_CONCURRENT_OPERATIONS; ++i) {
        threads.emplace_back([this, i, &success_count, &failure_count]() {
            for (int j = 0; j < NUM_STRESS_ITERATIONS / NUM_CONCURRENT_OPERATIONS; ++j) {
                std::string sink_id = "concurrent-sink-" + std::to_string(i) + "-" + std::to_string(j);
                
                if (manager->add_sink(make_random_sink(sink_id))) {
                    std::this_thread::sleep_for(1ms);
                    if (manager->remove_sink(sink_id)) {
                        success_count++;
                    } else {
                        failure_count++;
                    }
                } else {
                    failure_count++;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // We expect most operations to succeed
    EXPECT_GT(success_count.load(), 0);
}

/**
 * @brief Test concurrent source configure/remove operations.
 */
TEST_F(ReceiverStressTest, ConcurrentSourceOperations) {
    // Create a sink for sources to target
    ASSERT_TRUE(manager->add_sink(make_scream_sink("concurrent-sink", 48000, 16, 2)));
    
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    for (int i = 0; i < NUM_CONCURRENT_OPERATIONS; ++i) {
        threads.emplace_back([this, i, &success_count]() {
            for (int j = 0; j < NUM_STRESS_ITERATIONS / NUM_CONCURRENT_OPERATIONS; ++j) {
                std::string tag = "concurrent-source-" + std::to_string(i) + "-" + std::to_string(j);
                
                std::string instance_id = manager->configure_source(make_random_source(tag));
                if (!instance_id.empty()) {
                    std::this_thread::sleep_for(1ms);
                    if (manager->remove_source(instance_id)) {
                        success_count++;
                    }
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_GT(success_count.load(), 0);
    ASSERT_TRUE(manager->remove_sink("concurrent-sink"));
}

/**
 * @brief Test concurrent connect/disconnect operations.
 */
TEST_F(ReceiverStressTest, ConcurrentConnectDisconnect) {
    // Create multiple sinks
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(manager->add_sink(make_scream_sink("cc-sink-" + std::to_string(i), 48000, 16, 2)));
    }
    
    // Create multiple sources
    std::vector<std::string> source_ids;
    for (int i = 0; i < 5; ++i) {
        std::string id = manager->configure_source(make_source("cc-source-" + std::to_string(i), 2, 48000));
        ASSERT_FALSE(id.empty());
        source_ids.push_back(id);
    }
    
    std::vector<std::thread> threads;
    std::atomic<int> operations{0};
    
    for (int t = 0; t < NUM_CONCURRENT_OPERATIONS; ++t) {
        threads.emplace_back([this, t, &source_ids, &operations]() {
            std::mt19937 local_rng{std::random_device{}() + static_cast<uint32_t>(t)};
            
            for (int i = 0; i < NUM_STRESS_ITERATIONS / NUM_CONCURRENT_OPERATIONS; ++i) {
                int src_idx = local_rng() % source_ids.size();
                int sink_idx = local_rng() % 5;
                std::string sink_id = "cc-sink-" + std::to_string(sink_idx);
                
                manager->connect_source_sink(source_ids[src_idx], sink_id);
                std::this_thread::sleep_for(1ms);
                manager->disconnect_source_sink(source_ids[src_idx], sink_id);
                operations++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_GT(operations.load(), 0);
    
    // Cleanup
    for (const auto& src : source_ids) {
        manager->remove_source(src);
    }
    for (int i = 0; i < 5; ++i) {
        manager->remove_sink("cc-sink-" + std::to_string(i));
    }
}

// ============================================================================
// Edge Case Tests
// ============================================================================

/**
 * @brief Test removing a sink while sources are still connected.
 */
TEST_F(ReceiverStressTest, RemoveSinkWhileConnected) {
    for (int i = 0; i < NUM_STRESS_ITERATIONS; ++i) {
        std::string sink_id = "connected-sink-" + std::to_string(i);
        ASSERT_TRUE(manager->add_sink(make_scream_sink(sink_id, 48000, 16, 2)));
        
        std::string src_id = manager->configure_source(make_source("src-" + std::to_string(i), 2, 48000));
        ASSERT_FALSE(src_id.empty());
        
        ASSERT_TRUE(manager->connect_source_sink(src_id, sink_id));
        
        std::this_thread::sleep_for(1ms);
        
        // Remove sink without explicitly disconnecting first
        ASSERT_TRUE(with_timeout([&]() {
            manager->remove_sink(sink_id);
        })) << "remove_sink timed out at iteration " << i;
        
        // Cleanup source
        ASSERT_TRUE(with_timeout([&]() {
            manager->remove_source(src_id);
        })) << "remove_source timed out at iteration " << i;
    }
}

/**
 * @brief Test removing a source while connected to multiple sinks.
 */
TEST_F(ReceiverStressTest, RemoveSourceWhileMultiConnected) {
    for (int iter = 0; iter < NUM_STRESS_ITERATIONS / 5; ++iter) {
        // Create multiple sinks
        for (int i = 0; i < 3; ++i) {
            ASSERT_TRUE(manager->add_sink(make_scream_sink(
                "multi-sink-" + std::to_string(iter) + "-" + std::to_string(i), 48000, 16, 2)));
        }
        
        std::string src_id = manager->configure_source(make_source("multi-src-" + std::to_string(iter), 2, 48000));
        ASSERT_FALSE(src_id.empty());
        
        // Connect to all sinks
        for (int i = 0; i < 3; ++i) {
            ASSERT_TRUE(manager->connect_source_sink(src_id, 
                "multi-sink-" + std::to_string(iter) + "-" + std::to_string(i)));
        }
        
        std::this_thread::sleep_for(STRESS_SETTLE_TIME);
        
        // Remove source without explicitly disconnecting
        ASSERT_TRUE(with_timeout([&]() {
            manager->remove_source(src_id);
        })) << "remove_source timed out at iteration " << iter;
        
        // Cleanup sinks
        for (int i = 0; i < 3; ++i) {
            ASSERT_TRUE(with_timeout([&]() {
                manager->remove_sink("multi-sink-" + std::to_string(iter) + "-" + std::to_string(i));
            })) << "remove_sink timed out";
        }
    }
}

/**
 * @brief Test rapid init/shutdown cycles.
 */
TEST_F(ReceiverStressTest, RapidInitShutdownCycles) {
    // First tear down the manager from SetUp
    manager->shutdown();
    manager.reset();
    
    for (int i = 0; i < NUM_STRESS_ITERATIONS / 5; ++i) {
        manager = std::make_shared<AudioManager>();
        
        ASSERT_TRUE(with_timeout([&]() {
            EXPECT_TRUE(manager->initialize(0, 10));
        })) << "initialize timed out at iteration " << i;
        
        // Add some entities
        manager->add_sink(make_random_sink("cycle-sink"));
        std::string src = manager->configure_source(make_random_source("cycle-src"));
        if (!src.empty()) {
            manager->connect_source_sink(src, "cycle-sink");
        }
        
        ASSERT_TRUE(with_timeout([&]() {
            manager->shutdown();
        })) << "shutdown timed out at iteration " << i;
        
        manager.reset();
    }
    
    // Re-create for TearDown
    manager = std::make_shared<AudioManager>();
    manager->initialize(0, 10);
}

/**
 * @brief Get stats during rapid reconfiguration.
 */
TEST_F(ReceiverStressTest, StatsDuringReconfiguration) {
    ASSERT_TRUE(manager->add_sink(make_scream_sink("stats-sink", 48000, 16, 2)));
    std::string src_id = manager->configure_source(make_source("stats-source", 2, 48000));
    ASSERT_FALSE(src_id.empty());
    ASSERT_TRUE(manager->connect_source_sink(src_id, "stats-sink"));
    
    std::atomic<bool> running{true};
    
    // Thread that continuously gets stats
    std::thread stats_thread([&]() {
        while (running) {
            auto stats = manager->get_audio_engine_stats();
            std::this_thread::sleep_for(1ms);
        }
    });
    
    // Main thread does rapid parameter updates
    for (int i = 0; i < NUM_STRESS_ITERATIONS; ++i) {
        manager->update_source_parameters(src_id, make_random_updates());
    }
    
    running = false;
    stats_thread.join();
    
    ASSERT_TRUE(manager->disconnect_source_sink(src_id, "stats-sink"));
    ASSERT_TRUE(manager->remove_source(src_id));
    ASSERT_TRUE(manager->remove_sink("stats-sink"));
}

