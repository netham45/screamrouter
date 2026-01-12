/**
 * @file test_audio_processor.cpp
 * @brief Unit tests for AudioProcessor class
 * @details Tests volume adjustment, resampling, channel remapping, EQ, and playback rate
 */
#include <gtest/gtest.h>
#include <memory>
#include <cmath>
#include <vector>
#include <map>

#include "audio_processor/audio_processor.h"
#include "configuration/audio_engine_settings.h"

using namespace screamrouter::audio;

class AudioProcessorTest : public ::testing::Test {
protected:
    std::shared_ptr<AudioEngineSettings> settings;
    
    void SetUp() override {
        settings = std::make_shared<AudioEngineSettings>();
    }
    
    std::unique_ptr<AudioProcessor> make_processor(
        int input_ch = 2, int output_ch = 2,
        int input_bits = 16, int input_rate = 48000, int output_rate = 48000,
        float volume = 1.0f, std::size_t chunk_bytes = 1920) {
        
        std::map<int, CppSpeakerLayout> layouts;
        return std::make_unique<AudioProcessor>(
            input_ch, output_ch, input_bits, input_rate, output_rate,
            volume, layouts, settings, chunk_bytes);
    }
    
    // Generate sine wave test data (16-bit stereo)
    std::vector<uint8_t> generate_sine_wave(int sample_rate, int channels, int samples, float freq = 440.0f) {
        std::vector<uint8_t> data(samples * channels * 2);  // 16-bit = 2 bytes
        for (int i = 0; i < samples; ++i) {
            float t = static_cast<float>(i) / sample_rate;
            int16_t sample = static_cast<int16_t>(32767.0f * std::sin(2.0f * M_PI * freq * t));
            for (int ch = 0; ch < channels; ++ch) {
                int idx = (i * channels + ch) * 2;
                data[idx] = sample & 0xFF;
                data[idx + 1] = (sample >> 8) & 0xFF;
            }
        }
        return data;
    }
    
    // Calculate RMS of int32 output buffer
    double calculate_rms(const int32_t* buffer, size_t samples) {
        double sum = 0.0;
        for (size_t i = 0; i < samples; ++i) {
            double normalized = static_cast<double>(buffer[i]) / 2147483647.0;
            sum += normalized * normalized;
        }
        return std::sqrt(sum / samples);
    }
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(AudioProcessorTest, ConstructWithValidParams) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f);
    EXPECT_NE(processor, nullptr);
}

TEST_F(AudioProcessorTest, ConstructStereoToMono) {
    auto processor = make_processor(2, 1, 16, 48000, 48000, 1.0f);
    EXPECT_NE(processor, nullptr);
}

TEST_F(AudioProcessorTest, ConstructMonoToStereo) {
    auto processor = make_processor(1, 2, 16, 48000, 48000, 1.0f);
    EXPECT_NE(processor, nullptr);
}

TEST_F(AudioProcessorTest, ConstructWithResampling) {
    auto processor = make_processor(2, 2, 16, 44100, 48000, 1.0f);
    EXPECT_NE(processor, nullptr);
}

// ============================================================================
// Volume Tests
// ============================================================================

TEST_F(AudioProcessorTest, VolumeScaling_Unity) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    auto input = generate_sine_wave(48000, 2, 120);  // 120 samples = 480 bytes
    
    std::vector<int32_t> output(120 * 2);
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
    double rms = calculate_rms(output.data(), output.size());
    EXPECT_GT(rms, 0.1);  // Should have significant signal
}

TEST_F(AudioProcessorTest, VolumeScaling_Half) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    auto input = generate_sine_wave(48000, 2, 120);
    
    // First process at full volume
    std::vector<int32_t> output_full(120 * 2);
    processor->processAudio(input.data(), output_full.data());
    double rms_full = calculate_rms(output_full.data(), output_full.size());
    
    // Then at half volume
    processor->setVolume(0.5f);
    std::vector<int32_t> output_half(120 * 2);
    processor->processAudio(input.data(), output_half.data());
    double rms_half = calculate_rms(output_half.data(), output_half.size());
    
    // Half volume should be roughly half the RMS (with some tolerance for smoothing)
    EXPECT_LT(rms_half, rms_full);
}

TEST_F(AudioProcessorTest, VolumeScaling_Zero) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 0.0f, 480);
    auto input = generate_sine_wave(48000, 2, 120);
    
    // Process a few times to let volume smoothing settle
    std::vector<int32_t> output(120 * 2);
    for (int i = 0; i < 10; ++i) {
        processor->processAudio(input.data(), output.data());
    }
    
    double rms = calculate_rms(output.data(), output.size());
    EXPECT_LT(rms, 0.01);  // Should be nearly silent
}

TEST_F(AudioProcessorTest, SetVolume_DynamicChange) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    auto input = generate_sine_wave(48000, 2, 120);
    std::vector<int32_t> output(120 * 2);
    
    // Process at full volume
    processor->processAudio(input.data(), output.data());
    
    // Change volume dynamically
    processor->setVolume(0.25f);
    
    // Process more - volume should change smoothly
    for (int i = 0; i < 5; ++i) {
        processor->processAudio(input.data(), output.data());
    }
    
    double rms = calculate_rms(output.data(), output.size());
    EXPECT_LT(rms, 0.5);  // Should be reduced
}

// ============================================================================
// Resampling Tests
// ============================================================================

TEST_F(AudioProcessorTest, Resampling_SameRate) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    auto input = generate_sine_wave(48000, 2, 120);
    
    std::vector<int32_t> output(120 * 2);
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
}

TEST_F(AudioProcessorTest, Resampling_Upsample) {
    // 44100 -> 48000 (upsample)
    std::size_t input_samples = 110;  // ~2.5ms at 44100
    std::size_t input_bytes = input_samples * 2 * 2;  // stereo, 16-bit
    
    auto processor = make_processor(2, 2, 16, 44100, 48000, 1.0f, input_bytes);
    auto input = generate_sine_wave(44100, 2, input_samples);
    
    std::vector<int32_t> output(256 * 2);  // Larger buffer for output
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
}

TEST_F(AudioProcessorTest, Resampling_Downsample) {
    // 96000 -> 48000 (downsample)
    std::size_t input_samples = 240;  // 2.5ms at 96000
    std::size_t input_bytes = input_samples * 2 * 2;
    
    auto processor = make_processor(2, 2, 16, 96000, 48000, 1.0f, input_bytes);
    auto input = generate_sine_wave(96000, 2, input_samples);
    
    std::vector<int32_t> output(256 * 2);
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
}

TEST_F(AudioProcessorTest, ResampleToFixedOutput) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    
    // Create float input buffer
    std::vector<float> input(256 * 2);  // 256 stereo frames
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = std::sin(2.0f * M_PI * 440.0f * i / 96000.0f);
    }
    
    std::vector<float> output(128 * 2);  // Want exactly 128 output frames
    
    size_t consumed = processor->resample_to_fixed_output(
        input.data(), 256, output.data(), 128, 1.0, 2);
    
    // Should have consumed some input frames
    EXPECT_GT(consumed, 0u);
}

// ============================================================================
// Channel Remapping Tests
// ============================================================================

TEST_F(AudioProcessorTest, ChannelRemap_StereoToStereo) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    auto input = generate_sine_wave(48000, 2, 120);
    
    std::vector<int32_t> output(120 * 2);
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
    EXPECT_GT(calculate_rms(output.data(), output.size()), 0.1);
}

TEST_F(AudioProcessorTest, ChannelRemap_MonoToStereo) {
    std::size_t input_samples = 120;
    std::size_t input_bytes = input_samples * 1 * 2;  // mono, 16-bit
    
    auto processor = make_processor(1, 2, 16, 48000, 48000, 1.0f, input_bytes);
    auto input = generate_sine_wave(48000, 1, input_samples);
    
    std::vector<int32_t> output(256 * 2);  // stereo output
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
}

TEST_F(AudioProcessorTest, ChannelRemap_StereoToMono) {
    std::size_t input_samples = 120;
    std::size_t input_bytes = input_samples * 2 * 2;  // stereo, 16-bit
    
    auto processor = make_processor(2, 1, 16, 48000, 48000, 1.0f, input_bytes);
    auto input = generate_sine_wave(48000, 2, input_samples);
    
    std::vector<int32_t> output(256 * 1);  // mono output
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
}

// ============================================================================
// Playback Rate Tests
// ============================================================================

TEST_F(AudioProcessorTest, PlaybackRate_Normal) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    processor->set_playback_rate(1.0);
    
    auto input = generate_sine_wave(48000, 2, 120);
    std::vector<int32_t> output(120 * 2);
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
}

TEST_F(AudioProcessorTest, PlaybackRate_Faster) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    processor->set_playback_rate(1.05);  // 5% faster
    
    auto input = generate_sine_wave(48000, 2, 120);
    std::vector<int32_t> output(120 * 2);
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
}

TEST_F(AudioProcessorTest, PlaybackRate_Slower) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    processor->set_playback_rate(0.95);  // 5% slower
    
    auto input = generate_sine_wave(48000, 2, 120);
    std::vector<int32_t> output(120 * 2);
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
}

// ============================================================================
// EQ Tests
// ============================================================================

TEST_F(AudioProcessorTest, Equalizer_FlatResponse) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    
    // Set flat EQ (all 0 dB)
    float flat_eq[10] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    processor->setEqualizer(flat_eq);
    
    auto input = generate_sine_wave(48000, 2, 120);
    std::vector<int32_t> output(120 * 2);
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
    EXPECT_GT(calculate_rms(output.data(), output.size()), 0.1);
}

TEST_F(AudioProcessorTest, Equalizer_BassBoost) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    
    // Boost low frequencies
    float bass_boost_eq[10] = {6.0f, 6.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    processor->setEqualizer(bass_boost_eq);
    
    auto input = generate_sine_wave(48000, 2, 120, 100.0f);  // Low frequency sine
    std::vector<int32_t> output(120 * 2);
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
}

// ============================================================================
// Normalization Tests
// ============================================================================

TEST_F(AudioProcessorTest, VolumeNormalization_Toggle) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    
    processor->setVolumeNormalization(true);
    processor->setVolumeNormalization(false);
    
    auto input = generate_sine_wave(48000, 2, 120);
    std::vector<int32_t> output(120 * 2);
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
}

TEST_F(AudioProcessorTest, EqNormalization_Toggle) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    
    processor->setEqNormalization(true);
    processor->setEqNormalization(false);
    
    auto input = generate_sine_wave(48000, 2, 120);
    std::vector<int32_t> output(120 * 2);
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
}

// ============================================================================
// Filter Flush Tests
// ============================================================================

TEST_F(AudioProcessorTest, FlushFilters) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    
    // Process some audio
    auto input = generate_sine_wave(48000, 2, 120);
    std::vector<int32_t> output(120 * 2);
    processor->processAudio(input.data(), output.data());
    
    // Flush filters
    processor->flushFilters();
    
    // Should still work after flush
    int bytes = processor->processAudio(input.data(), output.data());
    EXPECT_GT(bytes, 0);
}

// ============================================================================
// Custom Speaker Mix Tests
// ============================================================================

TEST_F(AudioProcessorTest, CustomSpeakerMix_Identity) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    
    // Identity matrix: left->left, right->right
    std::vector<std::vector<float>> identity = {
        {1.0f, 0.0f},
        {0.0f, 1.0f}
    };
    processor->applyCustomSpeakerMix(identity);
    
    auto input = generate_sine_wave(48000, 2, 120);
    std::vector<int32_t> output(120 * 2);
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
}

TEST_F(AudioProcessorTest, CalculateAndApplyAutoSpeakerMix) {
    auto processor = make_processor(2, 2, 16, 48000, 48000, 1.0f, 480);
    
    processor->calculateAndApplyAutoSpeakerMix();
    
    auto input = generate_sine_wave(48000, 2, 120);
    std::vector<int32_t> output(120 * 2);
    int bytes = processor->processAudio(input.data(), output.data());
    
    EXPECT_GT(bytes, 0);
}
