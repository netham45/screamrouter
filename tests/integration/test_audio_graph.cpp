/**
 * Full Audio Graph Integration Tests
 * Tests complete pipeline with ALL receiver/sender combos
 */
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>

#include "managers/audio_manager.h"

using namespace screamrouter::audio;
using namespace std::chrono;

class FullAudioGraphTest : public ::testing::Test {
protected:
    std::shared_ptr<AudioManager> manager;
    
    void SetUp() override {
        manager = std::make_shared<AudioManager>();
    }
    
    void TearDown() override {
        if (manager) {
            manager->shutdown();
        }
    }
    
    SinkConfig make_scream_sink(const std::string& id) {
        SinkConfig config;
        config.id = id;
        config.friendly_name = "Scream Sink " + id;
        config.output_ip = "127.0.0.1";
        config.output_port = 14010;
        config.samplerate = 48000;
        config.channels = 2;
        config.bitdepth = 16;
        config.protocol = "scream";
        return config;
    }
    
    SinkConfig make_rtp_sink(const std::string& id) {
        SinkConfig config;
        config.id = id;
        config.friendly_name = "RTP Sink " + id;
        config.output_ip = "127.0.0.1";
        config.output_port = 15004;
        config.samplerate = 48000;
        config.channels = 2;
        config.bitdepth = 16;
        config.protocol = "rtp";
        return config;
    }
    
    SourceConfig make_source(const std::string& tag) {
        SourceConfig config;
        config.tag = tag;
        config.initial_volume = 1.0f;
        config.target_output_channels = 2;
        config.target_output_samplerate = 48000;
        return config;
    }
};

// ============================================================================
// Lifecycle Tests
// ============================================================================

TEST_F(FullAudioGraphTest, InitializeAndShutdown) {
    EXPECT_TRUE(manager->initialize(0, 10));  // port 0 to avoid binding
    manager->shutdown();
    manager.reset();
}

TEST_F(FullAudioGraphTest, MultipleInitShutdownCycles) {
    for (int i = 0; i < 3; ++i) {
        manager = std::make_shared<AudioManager>();
        EXPECT_TRUE(manager->initialize(0, 10));
        manager->shutdown();
        manager.reset();
    }
}

// ============================================================================
// Scream Protocol Tests
// ============================================================================

TEST_F(FullAudioGraphTest, ScreamSinkCreateDestroy) {
    ASSERT_TRUE(manager->initialize(0, 10));
    
    EXPECT_TRUE(manager->add_sink(make_scream_sink("scream-1")));
    EXPECT_TRUE(manager->remove_sink("scream-1"));
}

TEST_F(FullAudioGraphTest, MultipleScreamSinks) {
    ASSERT_TRUE(manager->initialize(0, 10));
    
    EXPECT_TRUE(manager->add_sink(make_scream_sink("scream-a")));
    EXPECT_TRUE(manager->add_sink(make_scream_sink("scream-b")));
    EXPECT_TRUE(manager->add_sink(make_scream_sink("scream-c")));
    
    EXPECT_TRUE(manager->remove_sink("scream-b"));
    EXPECT_TRUE(manager->remove_sink("scream-a"));
    EXPECT_TRUE(manager->remove_sink("scream-c"));
}

// ============================================================================
// RTP Protocol Tests
// ============================================================================

TEST_F(FullAudioGraphTest, RtpSinkCreateDestroy) {
    ASSERT_TRUE(manager->initialize(0, 10));
    
    EXPECT_TRUE(manager->add_sink(make_rtp_sink("rtp-1")));
    EXPECT_TRUE(manager->remove_sink("rtp-1"));
}

TEST_F(FullAudioGraphTest, MultipleRtpSinks) {
    ASSERT_TRUE(manager->initialize(0, 10));
    
    EXPECT_TRUE(manager->add_sink(make_rtp_sink("rtp-a")));
    EXPECT_TRUE(manager->add_sink(make_rtp_sink("rtp-b")));
    
    EXPECT_TRUE(manager->remove_sink("rtp-a"));
    EXPECT_TRUE(manager->remove_sink("rtp-b"));
}

// ============================================================================
// Mixed Protocol Tests
// ============================================================================

TEST_F(FullAudioGraphTest, MixedProtocolSinks) {
    ASSERT_TRUE(manager->initialize(0, 10));
    
    EXPECT_TRUE(manager->add_sink(make_scream_sink("scream-mixed")));
    EXPECT_TRUE(manager->add_sink(make_rtp_sink("rtp-mixed")));
    
    EXPECT_TRUE(manager->remove_sink("scream-mixed"));
    EXPECT_TRUE(manager->remove_sink("rtp-mixed"));
}

// ============================================================================
// Source Processing Tests
// ============================================================================

TEST_F(FullAudioGraphTest, SourceCreation) {
    ASSERT_TRUE(manager->initialize(0, 10));
    ASSERT_TRUE(manager->add_sink(make_scream_sink("sink-1")));
    
    std::string instance_id = manager->configure_source(make_source("192.168.1.10"));
    EXPECT_FALSE(instance_id.empty());
    
    EXPECT_TRUE(manager->remove_source(instance_id));
}

TEST_F(FullAudioGraphTest, SourceToSinkConnection) {
    ASSERT_TRUE(manager->initialize(0, 10));
    ASSERT_TRUE(manager->add_sink(make_scream_sink("sink-connect")));
    
    std::string src = manager->configure_source(make_source("192.168.1.20"));
    ASSERT_FALSE(src.empty());
    
    EXPECT_TRUE(manager->connect_source_sink(src, "sink-connect"));
    
    std::this_thread::sleep_for(milliseconds(50));
    
    EXPECT_TRUE(manager->disconnect_source_sink(src, "sink-connect"));
    EXPECT_TRUE(manager->remove_source(src));
}

// ============================================================================
// Full Graph with Multiple Sources and Sinks
// ============================================================================

TEST_F(FullAudioGraphTest, FullGraphMultipleSourcesAndSinks) {
    ASSERT_TRUE(manager->initialize(0, 10));
    
    // Create multiple sinks
    ASSERT_TRUE(manager->add_sink(make_scream_sink("living-room")));
    ASSERT_TRUE(manager->add_sink(make_rtp_sink("bedroom")));
    ASSERT_TRUE(manager->add_sink(make_scream_sink("kitchen")));
    
    // Create multiple sources
    std::string src1 = manager->configure_source(make_source("desktop-pc"));
    std::string src2 = manager->configure_source(make_source("laptop"));
    std::string src3 = manager->configure_source(make_source("phone"));
    
    ASSERT_FALSE(src1.empty());
    ASSERT_FALSE(src2.empty());
    ASSERT_FALSE(src3.empty());
    
    // Connect sources to various sinks
    EXPECT_TRUE(manager->connect_source_sink(src1, "living-room"));
    EXPECT_TRUE(manager->connect_source_sink(src1, "bedroom"));
    EXPECT_TRUE(manager->connect_source_sink(src2, "kitchen"));
    EXPECT_TRUE(manager->connect_source_sink(src3, "living-room"));
    
    std::this_thread::sleep_for(milliseconds(100));
    
    // Get stats during operation
    auto stats = manager->get_audio_engine_stats();
    
    // Disconnect all
    EXPECT_TRUE(manager->disconnect_source_sink(src1, "living-room"));
    EXPECT_TRUE(manager->disconnect_source_sink(src1, "bedroom"));
    EXPECT_TRUE(manager->disconnect_source_sink(src2, "kitchen"));
    EXPECT_TRUE(manager->disconnect_source_sink(src3, "living-room"));
    
    // Remove all sources
    EXPECT_TRUE(manager->remove_source(src1));
    EXPECT_TRUE(manager->remove_source(src2));
    EXPECT_TRUE(manager->remove_source(src3));
    
    // Remove all sinks
    EXPECT_TRUE(manager->remove_sink("living-room"));
    EXPECT_TRUE(manager->remove_sink("bedroom"));
    EXPECT_TRUE(manager->remove_sink("kitchen"));
}

// ============================================================================
// Settings and Stats Tests
// ============================================================================

TEST_F(FullAudioGraphTest, GetAndSetSettings) {
    ASSERT_TRUE(manager->initialize(0, 10));
    
    auto settings = manager->get_audio_settings();
    settings.mixer_tuning.mp3_bitrate_kbps = 256;
    settings.timeshift_tuning.target_buffer_level_ms = 100;
    
    manager->set_audio_settings(settings);
    
    auto updated = manager->get_audio_settings();
    EXPECT_EQ(updated.mixer_tuning.mp3_bitrate_kbps, 256);
    EXPECT_EQ(updated.timeshift_tuning.target_buffer_level_ms, 100);
}

TEST_F(FullAudioGraphTest, StatsUnderLoad) {
    ASSERT_TRUE(manager->initialize(0, 10));
    ASSERT_TRUE(manager->add_sink(make_scream_sink("stats-sink")));
    
    std::string src = manager->configure_source(make_source("stats-source"));
    ASSERT_FALSE(src.empty());
    EXPECT_TRUE(manager->connect_source_sink(src, "stats-sink"));
    
    // Get stats multiple times during operation
    for (int i = 0; i < 5; ++i) {
        auto stats = manager->get_audio_engine_stats();
        std::this_thread::sleep_for(milliseconds(20));
    }
    
    EXPECT_TRUE(manager->disconnect_source_sink(src, "stats-sink"));
    EXPECT_TRUE(manager->remove_source(src));
    EXPECT_TRUE(manager->remove_sink("stats-sink"));
}

// ============================================================================
// Dynamic Reconfiguration Tests  
// ============================================================================

TEST_F(FullAudioGraphTest, VolumeUpdate) {
    ASSERT_TRUE(manager->initialize(0, 10));
    ASSERT_TRUE(manager->add_sink(make_scream_sink("vol-sink")));
    
    std::string src = manager->configure_source(make_source("vol-source"));
    ASSERT_FALSE(src.empty());
    
    SourceParameterUpdates updates;
    updates.volume = 0.5f;
    manager->update_source_parameters(src, updates);
    
    updates.volume = 0.0f;
    manager->update_source_parameters(src, updates);
    
    updates.volume = 1.0f;
    manager->update_source_parameters(src, updates);
    
    EXPECT_TRUE(manager->remove_source(src));
}

TEST_F(FullAudioGraphTest, DelayUpdate) {
    ASSERT_TRUE(manager->initialize(0, 10));
    ASSERT_TRUE(manager->add_sink(make_scream_sink("delay-sink")));
    
    std::string src = manager->configure_source(make_source("delay-source"));
    ASSERT_FALSE(src.empty());
    
    SourceParameterUpdates updates;
    updates.delay_ms = 100;
    manager->update_source_parameters(src, updates);
    
    updates.delay_ms = 0;
    manager->update_source_parameters(src, updates);
    
    EXPECT_TRUE(manager->remove_source(src));
}
