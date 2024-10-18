#include "audio_processor.h"
#include "biquad/biquad.h"
#include "libsamplerate/include/samplerate.h"
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <vector>
#include <random>

#define CHUNK_SIZE 1152
#define OVERSAMPLING_FACTOR 2

AudioProcessor::AudioProcessor(int inputChannels, int outputChannels, int inputBitDepth, int inputSampleRate, int outputSampleRate, float volume)
    : inputChannels(inputChannels), outputChannels(outputChannels), inputBitDepth(inputBitDepth),
      inputSampleRate(inputSampleRate), outputSampleRate(outputSampleRate), volume(volume) {
    
    std::fill(eq, eq + EQ_BANDS, 1.0f);
    updateSpeakerMix();
    setupBiquad();
    initializeSampler();
    setupDCFilter();

    scale_buffer_pos = 0;
    process_buffer_pos = 0;
    resample_buffer_pos = 0;
    channel_buffer_pos = 0;
}

AudioProcessor::~AudioProcessor() {
    if (sampler) {
        src_delete(sampler);
    }
    for (int channel = 0; channel < MAX_CHANNELS; channel++) {
        for (int i = 0; i < EQ_BANDS; i++) {
            delete filters[channel][i];
        }
        delete dcFilters[channel];
    }
}

int AudioProcessor::processAudio(const uint8_t* inputBuffer, int32_t* outputBuffer) {
    memcpy(receive_buffer, inputBuffer, CHUNK_SIZE);
    scaleBuffer();
    volumeAdjust();
    resample();
    splitBufferToChannels();
    mixSpeakers();
    removeDCOffset();
    equalize();
    downsample();
    mergeChannelsToBuffer();
    downsample();
    int samples_per_chunk = CHUNK_SIZE / (inputBitDepth / 8);
    int processed_samples = samples_per_chunk * outputChannels / inputChannels;
    memcpy(outputBuffer, processed_buffer, processed_samples * sizeof(int32_t));
    return processed_samples;
}

void AudioProcessor::setVolume(float newVolume) {
    volume = newVolume;
}

void AudioProcessor::setEqualizer(const float* newEq) {
    std::copy(newEq, newEq + EQ_BANDS, eq);
    setupBiquad();
}

void AudioProcessor::setupBiquad() {
    #ifdef NORMALIZE_EQ_GAIN
    float max_gain = *std::max_element(eq, eq + EQ_BANDS);
    for (int i = 0; i < EQ_BANDS; i++)
        eq[i] /= max_gain;
    #endif

    const float frequencies[] = {65.406392, 92.498606, 130.81278, 184.99721, 261.62557, 369.99442, 523.25113, 739.9884,
                                 1046.5023, 1479.9768, 2093.0045, 2959.9536, 4186.0091, 5919.9072, 8372.0181, 11839.814,
                                 16744.036, 20000.0};

    for (int channel = 0; channel < MAX_CHANNELS; channel++) {
        for (int i = 0; i < EQ_BANDS; i++) {
            delete filters[channel][i];
            filters[channel][i] = new Biquad(bq_type_peak, frequencies[i] / (outputSampleRate * OVERSAMPLING_FACTOR), 1.0, 10.0f * (eq[i] - 1));
        }
    }
}

void AudioProcessor::initializeSampler() {
    if (sampler) src_delete(sampler);
    sampler = src_new(SRC_SINC_BEST_QUALITY, inputChannels, NULL);
    if (downsampler) src_delete(downsampler);
    downsampler = src_new(SRC_SINC_BEST_QUALITY, inputChannels, NULL);
}

void AudioProcessor::scaleBuffer() {
    scale_buffer_pos = 0;
    if (inputBitDepth != 16 && inputBitDepth != 24 && inputBitDepth != 32) return;
    for (int i = 0; i < CHUNK_SIZE; i += inputBitDepth / 8) {
        int scale_buffer_uint8_pos = scale_buffer_pos * sizeof(int32_t);
        switch (inputBitDepth) {
        case 16:
            scaled_buffer_int8[scale_buffer_uint8_pos + 0] = 0;
            scaled_buffer_int8[scale_buffer_uint8_pos + 1] = 0;
            scaled_buffer_int8[scale_buffer_uint8_pos + 2] = receive_buffer[i];
            scaled_buffer_int8[scale_buffer_uint8_pos + 3] = receive_buffer[i + 1];
            break;
        case 24:
            scaled_buffer_int8[scale_buffer_uint8_pos + 0] = 0;
            scaled_buffer_int8[scale_buffer_uint8_pos + 1] = receive_buffer[i];
            scaled_buffer_int8[scale_buffer_uint8_pos + 2] = receive_buffer[i + 1];
            scaled_buffer_int8[scale_buffer_uint8_pos + 3] = receive_buffer[i + 2];
            break;
        case 32:
            scaled_buffer_int8[scale_buffer_uint8_pos + 0] = receive_buffer[i];
            scaled_buffer_int8[scale_buffer_uint8_pos + 1] = receive_buffer[i + 1];
            scaled_buffer_int8[scale_buffer_uint8_pos + 2] = receive_buffer[i + 2];
            scaled_buffer_int8[scale_buffer_uint8_pos + 3] = receive_buffer[i + 3];
            break;
        }
        scale_buffer_pos++;
    }
}

float AudioProcessor::softClip(float sample) {
    const float threshold = 0.8f;
    const float knee = 0.2f;  // Width of the soft knee
    const float kneeStart = threshold - knee / 2.0f;
    const float kneeEnd = threshold + knee / 2.0f;

    float absample = std::abs(sample);
    if (absample <= kneeStart) {
        // Below the knee, no clipping
        return sample;
    } else if (absample >= kneeEnd) {
        // Above the knee, full clipping
        float sign = (sample > 0) ? 1.0f : -1.0f;
        return sign * (threshold + (1.0f - threshold) * tanh((absample - threshold) / (1.0f - threshold)));
    } else {
        // Within the knee, gradual transition
        float t = (absample - kneeStart) / knee;  // 0 to 1 as we move through the knee
        float sign = (sample > 0) ? 1.0f : -1.0f;
        float linear = sample;
        float clipped = sign * (threshold + (1.0f - threshold) * tanh((absample - threshold) / (1.0f - threshold)));
        return linear + t * t * (3 - 2 * t) * (clipped - linear);  // Smooth interpolation
    }
}

void AudioProcessor::volumeAdjust() {
    for (int i = 0; i < scale_buffer_pos; i++) {
        float sample = static_cast<float>(scaled_buffer[i]) / INT32_MAX;
        sample *= volume;
        sample = softClip(sample);
        scaled_buffer[i] = static_cast<int32_t>(sample * INT32_MAX);
    }
}

void AudioProcessor::resample() {
    if (isProcessingRequired()) {
        int datalen = scale_buffer_pos / inputChannels;
        src_int_to_float_array(scaled_buffer, resampler_data_in, datalen * inputChannels);
        sampler_config.data_in = resampler_data_in;
        sampler_config.data_out = resampler_data_out;
        sampler_config.src_ratio = (float)(outputSampleRate * OVERSAMPLING_FACTOR) / (float)inputSampleRate;
        sampler_config.input_frames = datalen;
        sampler_config.output_frames = datalen * 32;
        src_process(sampler, &sampler_config);
        src_float_to_int_array(resampler_data_out, resampled_buffer, sampler_config.output_frames_gen * inputChannels);
        resample_buffer_pos = sampler_config.output_frames_gen * inputChannels;
    } else {
        memcpy(resampled_buffer, scaled_buffer, scale_buffer_pos * sizeof(int32_t));
        resample_buffer_pos = scale_buffer_pos;
    }
}

void AudioProcessor::downsample() {
    if (isProcessingRequired()) {
        int datalen = merged_buffer_pos / outputChannels;
        src_int_to_float_array(merged_buffer, resampler_data_in, datalen * outputChannels);
        downsampler_config.data_in = resampler_data_in;
        downsampler_config.data_out = resampler_data_out;
        downsampler_config.src_ratio = (float)outputSampleRate / (float)(outputSampleRate * OVERSAMPLING_FACTOR);
        downsampler_config.input_frames = datalen;
        downsampler_config.output_frames = datalen * 32;
        src_process(downsampler, &downsampler_config);
        src_float_to_int_array(resampler_data_out, processed_buffer, downsampler_config.output_frames_gen * outputChannels);
        process_buffer_pos = downsampler_config.output_frames_gen * outputChannels;
    } else {
        memcpy(processed_buffer, merged_buffer, merged_buffer_pos * sizeof(int32_t));
        process_buffer_pos = merged_buffer_pos;
    }
}

void AudioProcessor::equalize() {
    for (int filter = 0; filter < EQ_BANDS; ++filter) {
        if (eq[filter] != 1.0f) {
            for (int channel = 0; channel < outputChannels; ++channel) {
                for (int pos = 0; pos < channel_buffer_pos; ++pos) {
                    float sample = static_cast<float>(remixed_channel_buffers[channel][pos]) / INT32_MAX;
                    sample = filters[channel][filter]->process(sample);
                    sample = softClip(sample);
                    remixed_channel_buffers[channel][pos] = static_cast<int32_t>(sample * INT32_MAX);
                }
            }
        }
    }
}

void AudioProcessor::noiseShapingDither() {
    const float ditherAmplitude = 1.0f / (1 << (inputBitDepth - 1));
    const float shapingFactor = 0.25f;
    static float error = 0.0f;
    static std::default_random_engine generator;
    std::uniform_real_distribution<float> distribution(-ditherAmplitude, ditherAmplitude);
    
    for (int i = 0; i < process_buffer_pos; ++i) {
        float sample = static_cast<float>(processed_buffer[i]) / INT32_MAX;
        
        // Apply gentle noise shaping
        sample += error * shapingFactor;
        
        // Add dither
        float dither = distribution(generator);
        sample += dither;
        
        // Clip the sample to [-1, 1] range
        sample = std::max(-1.0f, std::min(1.0f, sample));
        
        // Quantize
        int32_t quantized = static_cast<int32_t>(sample * INT32_MAX);
        
        // Update error
        error = sample - static_cast<float>(quantized) / INT32_MAX;
        
        processed_buffer[i] = quantized;
    }
}

void AudioProcessor::setupDCFilter() {
    for (int channel = 0; channel < MAX_CHANNELS; channel++) {
        dcFilters[channel] = new Biquad(bq_type_highpass, 20.0f / (outputSampleRate * OVERSAMPLING_FACTOR), 0.707f, 0.0f);
    }
}

void AudioProcessor::removeDCOffset() {
    for (int channel = 0; channel < outputChannels; ++channel) {
        for (int pos = 0; pos < channel_buffer_pos; ++pos) {
            remixed_channel_buffers[channel][pos] = dcFilters[channel]->process(remixed_channel_buffers[channel][pos]);
        }
    }
}

bool AudioProcessor::isProcessingRequired() {
    // Check if any processing is needed
    if (inputSampleRate != outputSampleRate) return true;
    if (volume != 1.0f) return true;
    if (inputChannels != outputChannels) return true;
    
    // Check if speaker mix is not identity matrix
    for (int i = 0; i < inputChannels; ++i) {
        for (int j = 0; j < outputChannels; ++j) {
            if ((i == j && speaker_mix[i][j] != 1.0f) || 
                (i != j && speaker_mix[i][j] != 0.0f)) {
                return true;
            }
        }
    }

    // Check if any EQ band is not flat
    for (int i = 0; i < EQ_BANDS; ++i) {
        if (eq[i] != 1.0f) return true;
    }
    
    return false;
}
