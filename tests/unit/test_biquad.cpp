#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "audio_processor/biquad/biquad.h"

class BiquadTest : public ::testing::Test {
protected:
    Biquad filter;
    
    // Generate sine wave
    std::vector<float> generate_sine(float freq, float samplerate, int samples) {
        std::vector<float> wave(samples);
        for (int i = 0; i < samples; ++i) {
            wave[i] = std::sin(2.0f * M_PI * freq * i / samplerate);
        }
        return wave;
    }
    
    // Calculate RMS power
    float rms(const std::vector<float>& signal) {
        float sum = 0.0f;
        for (float s : signal) {
            sum += s * s;
        }
        return std::sqrt(sum / signal.size());
    }
};

TEST_F(BiquadTest, DefaultConstruction) {
    // Should not crash
    Biquad b;
    float out = b.process(1.0f);
    EXPECT_TRUE(std::isfinite(out));
}

TEST_F(BiquadTest, ParameterizedConstruction) {
    // Lowpass at Fc=0.1 (normalized), Q=0.707
    Biquad b(bq_type_lowpass, 0.1, 0.707, 0.0);
    float out = b.process(1.0f);
    EXPECT_TRUE(std::isfinite(out));
}

TEST_F(BiquadTest, LowpassAttenuatesHighFrequency) {
    // Lowpass at 1kHz, sample rate 48kHz -> Fc = 1000/48000 ≈ 0.0208
    filter.setBiquad(bq_type_lowpass, 1000.0 / 48000.0, 0.707, 0.0);
    
    // Generate 100Hz signal (should pass) and 10kHz (should attenuate)
    auto low_freq = generate_sine(100.0f, 48000.0f, 4800);
    auto high_freq = generate_sine(10000.0f, 48000.0f, 4800);
    
    std::vector<float> low_out(low_freq.size());
    std::vector<float> high_out(high_freq.size());
    
    filter.processBlock(low_freq.data(), low_out.data(), low_freq.size());
    filter.flush();
    filter.processBlock(high_freq.data(), high_out.data(), high_freq.size());
    
    float low_power = rms(low_out);
    float high_power = rms(high_out);
    
    // Low frequency should have more power than high frequency after filtering
    EXPECT_GT(low_power, high_power * 2.0f);
}

TEST_F(BiquadTest, HighpassAttenuatesLowFrequency) {
    // Highpass at 5kHz
    filter.setBiquad(bq_type_highpass, 5000.0 / 48000.0, 0.707, 0.0);
    
    auto low_freq = generate_sine(100.0f, 48000.0f, 4800);
    auto high_freq = generate_sine(10000.0f, 48000.0f, 4800);
    
    std::vector<float> low_out(low_freq.size());
    std::vector<float> high_out(high_freq.size());
    
    filter.processBlock(low_freq.data(), low_out.data(), low_freq.size());
    filter.flush();
    filter.processBlock(high_freq.data(), high_out.data(), high_freq.size());
    
    float low_power = rms(low_out);
    float high_power = rms(high_out);
    
    // High frequency should have more power than low frequency
    EXPECT_GT(high_power, low_power * 2.0f);
}

TEST_F(BiquadTest, FlushClearsState) {
    filter.setBiquad(bq_type_lowpass, 0.1, 0.707, 0.0);
    
    // Process some samples
    for (int i = 0; i < 100; ++i) {
        filter.process(1.0f);
    }
    
    filter.flush();
    
    // After flush, first output should be same as from a fresh filter
    Biquad fresh(bq_type_lowpass, 0.1, 0.707, 0.0);
    
    float flushed_out = filter.process(1.0f);
    float fresh_out = fresh.process(1.0f);
    
    EXPECT_FLOAT_EQ(flushed_out, fresh_out);
}

TEST_F(BiquadTest, ProcessBlockMatchesSingleSample) {
    filter.setBiquad(bq_type_lowpass, 0.1, 0.707, 0.0);
    
    std::vector<float> input = {1.0f, 0.5f, -0.5f, 1.0f, 0.0f};
    std::vector<float> block_out(input.size());
    std::vector<float> single_out(input.size());
    
    // Process as block
    filter.processBlock(input.data(), block_out.data(), input.size());
    
    // Reset and process individually
    filter.flush();
    for (size_t i = 0; i < input.size(); ++i) {
        single_out[i] = filter.process(input[i]);
    }
    
    // Results should match
    for (size_t i = 0; i < input.size(); ++i) {
        EXPECT_NEAR(block_out[i], single_out[i], 1e-6f);
    }
}

TEST_F(BiquadTest, PeakFilterBoosts) {
    // Peak filter at 1kHz with +12dB gain
    filter.setBiquad(bq_type_peak, 1000.0 / 48000.0, 1.0, 12.0);
    
    auto on_freq = generate_sine(1000.0f, 48000.0f, 4800);
    auto off_freq = generate_sine(100.0f, 48000.0f, 4800);
    
    std::vector<float> on_out(on_freq.size());
    std::vector<float> off_out(off_freq.size());
    
    filter.processBlock(on_freq.data(), on_out.data(), on_freq.size());
    filter.flush();
    filter.processBlock(off_freq.data(), off_out.data(), off_freq.size());
    
    float on_power = rms(on_out);
    float off_power = rms(off_out);
    
    // Signal at peak frequency should be boosted compared to off-peak
    EXPECT_GT(on_power, off_power);
}

TEST_F(BiquadTest, setBiquadUpdatesFilter) {
    filter.setBiquad(bq_type_lowpass, 0.01, 0.707, 0.0);  // Very low cutoff
    
    float out1 = filter.process(1.0f);
    
    filter.flush();
    filter.setBiquad(bq_type_lowpass, 0.5, 0.707, 0.0);  // High cutoff
    
    float out2 = filter.process(1.0f);
    
    // Different coefficients should give different outputs
    EXPECT_NE(out1, out2);
}
