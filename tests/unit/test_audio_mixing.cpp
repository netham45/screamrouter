#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include <limits>

/**
 * These tests verify the core mixing algorithms used by SinkAudioMixer:
 * 1. Sample accumulation with saturation (clamping)
 * 2. Bit-depth downscaling (32-bit to 16/24/32)
 * 
 * The actual SinkAudioMixer class has heavy dependencies (network senders, LAME, etc.)
 * so we test the algorithm logic directly.
 */

class AudioMixingTest : public ::testing::Test {
protected:
    // Mixing with saturation (same logic as SinkAudioMixer::mix_buffers)
    static void mix_with_saturation(int32_t* dest, const int32_t* src, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            int64_t sum = static_cast<int64_t>(dest[i]) + src[i];
            if (sum > INT32_MAX) {
                dest[i] = INT32_MAX;
            } else if (sum < INT32_MIN) {
                dest[i] = INT32_MIN;
            } else {
                dest[i] = static_cast<int32_t>(sum);
            }
        }
    }
    
    // Downscale 32-bit to 16-bit (same logic as SinkAudioMixer::downscale_buffer)
    static void downscale_to_16bit(const int32_t* samples, uint8_t* output, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            int32_t sample = samples[i];
            output[i * 2 + 0] = static_cast<uint8_t>((sample >> 16) & 0xFF);
            output[i * 2 + 1] = static_cast<uint8_t>((sample >> 24) & 0xFF);
        }
    }
    
    // Downscale 32-bit to 24-bit
    static void downscale_to_24bit(const int32_t* samples, uint8_t* output, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            int32_t sample = samples[i];
            output[i * 3 + 0] = static_cast<uint8_t>((sample >> 8) & 0xFF);
            output[i * 3 + 1] = static_cast<uint8_t>((sample >> 16) & 0xFF);
            output[i * 3 + 2] = static_cast<uint8_t>((sample >> 24) & 0xFF);
        }
    }
};

TEST_F(AudioMixingTest, MixTwoSources) {
    std::vector<int32_t> dest = {1000, 2000, 3000, 4000};
    std::vector<int32_t> src =  {100,  200,  300,  400};
    
    mix_with_saturation(dest.data(), src.data(), dest.size());
    
    EXPECT_EQ(dest[0], 1100);
    EXPECT_EQ(dest[1], 2200);
    EXPECT_EQ(dest[2], 3300);
    EXPECT_EQ(dest[3], 4400);
}

TEST_F(AudioMixingTest, MixWithPositiveSaturation) {
    std::vector<int32_t> dest = {INT32_MAX - 100, INT32_MAX};
    std::vector<int32_t> src =  {200, 1};
    
    mix_with_saturation(dest.data(), src.data(), dest.size());
    
    EXPECT_EQ(dest[0], INT32_MAX);  // Clamped
    EXPECT_EQ(dest[1], INT32_MAX);  // Clamped
}

TEST_F(AudioMixingTest, MixWithNegativeSaturation) {
    std::vector<int32_t> dest = {INT32_MIN + 100, INT32_MIN};
    std::vector<int32_t> src =  {-200, -1};
    
    mix_with_saturation(dest.data(), src.data(), dest.size());
    
    EXPECT_EQ(dest[0], INT32_MIN);  // Clamped
    EXPECT_EQ(dest[1], INT32_MIN);  // Clamped
}

TEST_F(AudioMixingTest, MixNegativeAndPositive) {
    std::vector<int32_t> dest = {1000, -1000};
    std::vector<int32_t> src =  {-500,  500};
    
    mix_with_saturation(dest.data(), src.data(), dest.size());
    
    EXPECT_EQ(dest[0], 500);
    EXPECT_EQ(dest[1], -500);
}

TEST_F(AudioMixingTest, MixMultipleSources) {
    std::vector<int32_t> mix = {0, 0, 0, 0};
    std::vector<int32_t> src1 = {100, 200, 300, 400};
    std::vector<int32_t> src2 = {10, 20, 30, 40};
    std::vector<int32_t> src3 = {1, 2, 3, 4};
    
    mix_with_saturation(mix.data(), src1.data(), mix.size());
    mix_with_saturation(mix.data(), src2.data(), mix.size());
    mix_with_saturation(mix.data(), src3.data(), mix.size());
    
    EXPECT_EQ(mix[0], 111);
    EXPECT_EQ(mix[1], 222);
    EXPECT_EQ(mix[2], 333);
    EXPECT_EQ(mix[3], 444);
}

TEST_F(AudioMixingTest, Downscale32To16Bit) {
    // Full scale 32-bit samples
    std::vector<int32_t> samples = {
        0x7FFFFFFF,   // Max positive
        static_cast<int32_t>(0x80000000),   // Max negative
        0x00000000,   // Zero
        0x40000000    // Half max
    };
    std::vector<uint8_t> output(samples.size() * 2);
    
    downscale_to_16bit(samples.data(), output.data(), samples.size());
    
    // Max positive: 0x7FFF in LE -> FF 7F
    EXPECT_EQ(output[0], 0xFF);
    EXPECT_EQ(output[1], 0x7F);
    
    // Max negative: 0x8000 in LE -> 00 80
    EXPECT_EQ(output[2], 0x00);
    EXPECT_EQ(output[3], 0x80);
    
    // Zero: 0x0000 -> 00 00
    EXPECT_EQ(output[4], 0x00);
    EXPECT_EQ(output[5], 0x00);
    
    // Half max (0x4000) -> 00 40
    EXPECT_EQ(output[6], 0x00);
    EXPECT_EQ(output[7], 0x40);
}

TEST_F(AudioMixingTest, Downscale32To24Bit) {
    std::vector<int32_t> samples = {0x12345678};
    std::vector<uint8_t> output(3);
    
    downscale_to_24bit(samples.data(), output.data(), 1);
    
    // Takes bytes 1,2,3 (skips LSB): 0x56, 0x34, 0x12
    EXPECT_EQ(output[0], 0x56);
    EXPECT_EQ(output[1], 0x34);
    EXPECT_EQ(output[2], 0x12);
}

TEST_F(AudioMixingTest, MixEmptyBuffer) {
    std::vector<int32_t> dest;
    std::vector<int32_t> src;
    
    // Should not crash
    mix_with_saturation(dest.data(), src.data(), 0);
    EXPECT_TRUE(dest.empty());
}

TEST_F(AudioMixingTest, StereoSampleMixing) {
    // Stereo: [L0, R0, L1, R1]
    std::vector<int32_t> dest = {100, 200, 100, 200};  // Source 1
    std::vector<int32_t> src =  {50, 100, 50, 100};    // Source 2
    
    mix_with_saturation(dest.data(), src.data(), dest.size());
    
    EXPECT_EQ(dest[0], 150);  // L0
    EXPECT_EQ(dest[1], 300);  // R0
    EXPECT_EQ(dest[2], 150);  // L1
    EXPECT_EQ(dest[3], 300);  // R1
}
