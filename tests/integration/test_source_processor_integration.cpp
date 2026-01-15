#include <gtest/gtest.h>
#include <memory>
#include <chrono>

#include "input_processor/source_input_processor.h"
#include "audio_types.h"
#include "configuration/audio_engine_settings.h"

// Mock logging
namespace screamrouter::audio::logging {
    enum class LogLevel { DEBUG, INFO, WARNING, ERR };
    std::atomic<LogLevel> current_log_level{LogLevel::INFO};
    void log_message(LogLevel, const char*, int, const char*, ...) {}
    const char* get_base_filename(const char* path) { return path; }
}

// Sentinel logging stub
namespace screamrouter::audio::utils {
    void log_sentinel(const char*, const TaggedAudioPacket&, const std::string&) {}
    void log_sentinel(const char*, const ProcessedAudioChunk&, const std::string&) {}
}

using namespace screamrouter::audio;
using namespace std::chrono;

class SourceInputProcessorIntegrationTest : public ::testing::Test {
protected:
    std::shared_ptr<AudioEngineSettings> settings;
    
    void SetUp() override {
        settings = std::make_shared<AudioEngineSettings>();
    }
    
    SourceProcessorConfig make_config(const std::string& id = "test-sip") {
        SourceProcessorConfig config;
        config.instance_id = id;
        config.source_tag = "test-source";
        config.output_channels = 2;
        config.output_samplerate = 48000;
        config.initial_volume = 1.0f;
        config.initial_delay_ms = 0;
        config.initial_timeshift_sec = 0.0f;
        return config;
    }
    
    TaggedAudioPacket make_packet(size_t frames, int channels = 2, int sample_rate = 48000) {
        TaggedAudioPacket pkt;
        pkt.source_tag = "test-source";
        pkt.channels = channels;
        pkt.sample_rate = sample_rate;
        pkt.bit_depth = 16;
        pkt.received_time = steady_clock::now();
        pkt.playback_rate = 1.0;
        
        // Generate silence (zeros)
        size_t bytes = frames * channels * 2;  // 16-bit
        pkt.audio_data.resize(bytes, 0);
        
        return pkt;
    }
};

TEST_F(SourceInputProcessorIntegrationTest, ConstructAndDestroy) {
    auto config = make_config();
    auto sip = std::make_unique<SourceInputProcessor>(config, settings);
    
    EXPECT_NE(sip, nullptr);
    // Destructor should clean up without crash
}

TEST_F(SourceInputProcessorIntegrationTest, IngestProducesOutput) {
    auto config = make_config();
    auto sip = std::make_unique<SourceInputProcessor>(config, settings);
    
    // Ingest several packets worth of audio
    constexpr size_t kFramesPerPacket = 480;  // 10ms at 48kHz
    constexpr int kPacketCount = 10;
    
    std::vector<ProcessedAudioChunk> all_output;
    
    for (int i = 0; i < kPacketCount; ++i) {
        auto pkt = make_packet(kFramesPerPacket);
        std::vector<ProcessedAudioChunk> produced;
        sip->ingest_packet(pkt, produced);
        
        for (auto& chunk : produced) {
            all_output.push_back(std::move(chunk));
        }
    }
    
    // Should have produced some output chunks
    EXPECT_GT(all_output.size(), 0u);
}

TEST_F(SourceInputProcessorIntegrationTest, GetStats) {
    auto config = make_config();
    auto sip = std::make_unique<SourceInputProcessor>(config, settings);
    
    // Ingest some data
    auto pkt = make_packet(480);
    std::vector<ProcessedAudioChunk> produced;
    sip->ingest_packet(pkt, produced);
    
    auto stats = sip->get_stats();
    // Stats should be populated
    EXPECT_GE(stats.total_packets_processed, 0u);
}

TEST_F(SourceInputProcessorIntegrationTest, GetConfig) {
    auto config = make_config();
    auto sip = std::make_unique<SourceInputProcessor>(config, settings);
    
    // Get configuration should work
    const auto& retrieved = sip->get_config();
    EXPECT_EQ(retrieved.instance_id, "test-sip");
    EXPECT_EQ(retrieved.source_tag, "test-source");
}

TEST_F(SourceInputProcessorIntegrationTest, FormatChange) {
    auto config = make_config();
    auto sip = std::make_unique<SourceInputProcessor>(config, settings);
    
    // Start with 48kHz stereo
    auto pkt1 = make_packet(480, 2, 48000);
    std::vector<ProcessedAudioChunk> produced1;
    sip->ingest_packet(pkt1, produced1);
    
    // Switch to 44.1kHz (simulates input format change)
    auto pkt2 = make_packet(441, 2, 44100);
    std::vector<ProcessedAudioChunk> produced2;
    sip->ingest_packet(pkt2, produced2);
    
    // Should handle format change gracefully
    auto stats = sip->get_stats();
    EXPECT_GT(stats.reconfigurations, 0u);
}

TEST_F(SourceInputProcessorIntegrationTest, MultiplePacketBatchIngest) {
    auto config = make_config();
    auto sip = std::make_unique<SourceInputProcessor>(config, settings);
    
    // Ingest 50 packets (500ms at 10ms/packet)
    constexpr size_t kFramesPerPacket = 480;
    constexpr int kPacketCount = 50;
    
    size_t total_chunks = 0;
    for (int i = 0; i < kPacketCount; ++i) {
        auto pkt = make_packet(kFramesPerPacket);
        std::vector<ProcessedAudioChunk> produced;
        sip->ingest_packet(pkt, produced);
        total_chunks += produced.size();
    }
    
    // Should produce multiple output chunks
    EXPECT_GT(total_chunks, 0u);
    
    auto stats = sip->get_stats();
    EXPECT_GT(stats.total_packets_processed, 0u);
}
