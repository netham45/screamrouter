#include "audio_processor.h"
#include "biquad/biquad.h"
#include "libsamplerate/include/samplerate.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <emmintrin.h>
#include <immintrin.h>

#define CHUNK_SIZE 1152

AudioProcessor::AudioProcessor(int inputChannels, int outputChannels, int inputSampleRate, int outputSampleRate)
    : inputChannels(inputChannels), outputChannels(outputChannels),
      inputSampleRate(inputSampleRate), outputSampleRate(outputSampleRate),
      inputBitDepth(16), volume(1.0f) {
    
    std::fill(eq, eq + EQ_BANDS, 1.0f);
    updateSpeakerMix();
    setupBiquad();
    initializeSampler();

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
    }
}

int AudioProcessor::processAudio(const uint8_t* inputBuffer, int32_t* outputBuffer) {
    memcpy(receive_buffer, inputBuffer, CHUNK_SIZE);
    scaleBuffer();
    volumeAdjust();
    resample();
    splitBufferToChannels();
    mixSpeakers();
    equalize();
    mergeChannelsToBuffer();
    int processed_samples = channel_buffer_pos * outputChannels;
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

void AudioProcessor::updateSpeakerMix() {
    memset(speaker_mix, 0, sizeof(speaker_mix));

    switch (inputChannels) {
    case 1:
        for (int output_channel = 0; output_channel < MAX_CHANNELS; output_channel++)
            speaker_mix[0][output_channel] = 1;
        break;
    case 2:
        switch (outputChannels) {
        case 1:
            speaker_mix[0][0] = .5; speaker_mix[1][0] = .5;
            break;
        case 2:
            speaker_mix[0][0] = 1; speaker_mix[1][1] = 1;
            break;
        case 4:
            speaker_mix[0][0] = 1; speaker_mix[1][1] = 1;
            speaker_mix[0][2] = 1; speaker_mix[1][3] = 1;
            break;
        case 6:
            speaker_mix[0][0] = 1; speaker_mix[0][5] = 1;
            speaker_mix[1][1] = 1; speaker_mix[1][6] = 1;
            speaker_mix[0][3] = .5; speaker_mix[1][3] = .5;
            speaker_mix[0][4] = .5; speaker_mix[1][4] = .5;
            break;
        case 8:
            speaker_mix[0][0] = 1; speaker_mix[0][6] = 1; speaker_mix[0][4] = 1;
            speaker_mix[1][1] = 1; speaker_mix[1][7] = 1; speaker_mix[1][5] = 1;
            speaker_mix[0][2] = .5; speaker_mix[1][2] = .5;
            speaker_mix[0][3] = .5; speaker_mix[1][3] = .5;
            break;
        }
        break;
    case 4:
        switch (outputChannels) {
        case 1:
            speaker_mix[0][0] = .25; speaker_mix[1][0] = .25;
            speaker_mix[2][0] = .25; speaker_mix[3][0] = .25;
            break;
        case 2:
            speaker_mix[0][0] = .5; speaker_mix[1][1] = .5;
            speaker_mix[2][0] = .5; speaker_mix[3][1] = .5;
            break;
        case 4:
            speaker_mix[0][0] = 1; speaker_mix[1][1] = 1;
            speaker_mix[2][2] = 1; speaker_mix[3][3] = 1;
            break;
        case 6:
            speaker_mix[0][0] = 1; speaker_mix[1][1] = 1;
            speaker_mix[0][2] = .5; speaker_mix[1][2] = .5;
            speaker_mix[0][3] = .25; speaker_mix[1][3] = .25;
            speaker_mix[2][3] = .25; speaker_mix[3][3] = .25;
            speaker_mix[2][4] = 1; speaker_mix[3][5] = 1;
            break;
        case 8:
            speaker_mix[0][0] = 1; speaker_mix[1][1] = 1;
            speaker_mix[0][2] = .5; speaker_mix[1][2] = .5;
            speaker_mix[0][3] = .25; speaker_mix[1][3] = .25;
            speaker_mix[2][3] = .25; speaker_mix[3][3] = .25;
            speaker_mix[2][4] = 1; speaker_mix[3][5] = 1;
            speaker_mix[0][6] = .5; speaker_mix[1][7] = .5;
            speaker_mix[2][6] = .5; speaker_mix[3][7] = .5;
            break;
        }
        break;
    case 6:
        switch (outputChannels) {
        case 1:
            speaker_mix[0][0] = .2; speaker_mix[1][0] = .2;
            speaker_mix[2][0] = .2; speaker_mix[4][0] = .2;
            speaker_mix[5][0] = .2;
            break;
        case 2:
            speaker_mix[0][0] = .33; speaker_mix[1][1] = .33;
            speaker_mix[2][0] = .33; speaker_mix[2][1] = .33;
            speaker_mix[4][0] = .33; speaker_mix[5][1] = .33;
            break;
        case 4:
            speaker_mix[0][0] = .66; speaker_mix[1][1] = .66;
            speaker_mix[2][0] = .33; speaker_mix[2][1] = .33;
            speaker_mix[4][2] = 1; speaker_mix[5][3] = 1;
            break;
        case 6:
            speaker_mix[0][0] = 1; speaker_mix[1][1] = 1;
            speaker_mix[2][2] = 1; speaker_mix[3][3] = 1;
            speaker_mix[4][4] = 1; speaker_mix[5][5] = 1;
            break;
        case 8:
            speaker_mix[0][0] = 1; speaker_mix[1][1] = 1;
            speaker_mix[2][2] = 1; speaker_mix[3][3] = 1;
            speaker_mix[4][4] = 1; speaker_mix[5][5] = 1;
            speaker_mix[0][6] = .5; speaker_mix[1][7] = .5;
            speaker_mix[4][6] = .5; speaker_mix[5][7] = .5;
            break;
        }
        break;
    case 8:
        switch (outputChannels) {
        case 1:
            speaker_mix[0][0] = 1.0f / 7.0f; speaker_mix[1][0] = 1.0f / 7.0f;
            speaker_mix[2][0] = 1.0f / 7.0f; speaker_mix[4][0] = 1.0f / 7.0f;
            speaker_mix[5][0] = 1.0f / 7.0f; speaker_mix[6][0] = 1.0f / 7.0f;
            speaker_mix[7][0] = 1.0f / 7.0f;
            break;
        case 2:
            speaker_mix[0][0] = .5; speaker_mix[1][1] = .5;
            speaker_mix[2][0] = .25; speaker_mix[2][1] = .25;
            speaker_mix[4][0] = .125; speaker_mix[5][1] = .125;
            speaker_mix[6][0] = .125; speaker_mix[7][1] = .125;
            break;
        case 4:
            speaker_mix[0][0] = .5; speaker_mix[1][1] = .5;
            speaker_mix[2][0] = .25; speaker_mix[2][1] = .25;
            speaker_mix[4][2] = .66; speaker_mix[5][3] = .66;
            speaker_mix[6][0] = .25; speaker_mix[7][1] = .25;
            speaker_mix[6][2] = .33; speaker_mix[7][3] = .33;
            break;
        case 6:
            speaker_mix[0][0] = .66; speaker_mix[1][1] = .66;
            speaker_mix[2][2] = 1; speaker_mix[3][3] = 1;
            speaker_mix[4][4] = .66; speaker_mix[5][5] = .66;
            speaker_mix[6][0] = .33; speaker_mix[7][1] = .33;
            speaker_mix[6][4] = .33; speaker_mix[7][5] = .33;
            break;
        case 8:
            speaker_mix[0][0] = 1; speaker_mix[1][1] = 1;
            speaker_mix[2][2] = 1; speaker_mix[3][3] = 1;
            speaker_mix[4][4] = 1; speaker_mix[5][5] = 1;
            speaker_mix[6][6] = 1; speaker_mix[7][7] = 1;
            break;
        }
        break;
    }
}

void AudioProcessor::setupBiquad() {
    #ifdef NORMALIZE_EQ_GAIN
    float max_gain = *std::max_element(eq, eq + EQ_BANDS);
    for (int i = 0; i < EQ_BANDS; i++)
        eq[i] /= max_gain;
    #endif

    float frequencies[] = {65.406392, 92.498606, 130.81278, 184.99721, 261.62557, 369.99442, 523.25113, 739.9884,
                           1046.5023, 1479.9768, 2093.0045, 2959.9536, 4186.0091, 5919.9072, 8372.0181, 11839.814,
                           16744.036, 20000.0};

    for (int channel = 0; channel < MAX_CHANNELS; channel++) {
        for (int i = 0; i < EQ_BANDS; i++) {
            delete filters[channel][i];
            filters[channel][i] = new Biquad(bq_type_peak, frequencies[i] / outputSampleRate, 1.0, 10.0f * (eq[i] - 1));
        }
    }
}

void AudioProcessor::initializeSampler() {
    if (sampler) src_delete(sampler);
    int error = 0;
    sampler = src_new(SRC_SINC_BEST_QUALITY, inputChannels, &error);
    if (!sampler) {
        throw std::runtime_error("Failed to initialize sampler");
    }
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

void AudioProcessor::volumeAdjust() {
    for (int i = 0; i < scale_buffer_pos; i++) {
        scaled_buffer[i] = static_cast<int32_t>(scaled_buffer[i] * volume);
    }
}

void AudioProcessor::resample() {
    if (inputSampleRate != outputSampleRate) {
        int datalen = scale_buffer_pos / inputChannels;
        src_int_to_float_array(scaled_buffer, resampler_data_in, datalen * inputChannels);
        
        SRC_DATA resamplerConfig;
        resamplerConfig.data_in = resampler_data_in;
        resamplerConfig.data_out = resampler_data_out;
        resamplerConfig.src_ratio = static_cast<double>(outputSampleRate) / inputSampleRate;
        resamplerConfig.input_frames = datalen;
        resamplerConfig.output_frames = sizeof(resampler_data_out) / sizeof(float) / inputChannels;
        
        int err = src_process(sampler, &resamplerConfig);
        if (err != 0) {
            throw std::runtime_error("Resampling error: " + std::string(src_strerror(err)));
        }
        
        resample_buffer_pos = resamplerConfig.output_frames_gen * inputChannels;
        src_float_to_int_array(resampler_data_out, resampled_buffer, resample_buffer_pos);
    } else {
        memcpy(resampled_buffer, scaled_buffer, scale_buffer_pos * sizeof(int32_t));
        resample_buffer_pos = scale_buffer_pos;
    }
}

void AudioProcessor::splitBufferToChannels() {
    for (int i = 0; i < resample_buffer_pos; i++) {
        int channel = i % inputChannels;
        int pos = i / inputChannels;
        channel_buffers[channel][pos] = resampled_buffer[i];
    }
    channel_buffer_pos = resample_buffer_pos / inputChannels;
}

void AudioProcessor::mixSpeakers() {
    memset(remixed_channel_buffers, 0, sizeof(remixed_channel_buffers));

    #ifdef __AVX2__
    for (int pos = 0; pos < channel_buffer_pos; pos += 8) {
        for (int input_channel = 0; input_channel < inputChannels; input_channel++) {
            __m256i input_data = _mm256_loadu_si256((__m256i*)&channel_buffers[input_channel][pos]);
            for (int output_channel = 0; output_channel < outputChannels; output_channel++) {
                __m256 mix_factor = _mm256_set1_ps(speaker_mix[input_channel][output_channel]);
                __m256 output_data = _mm256_cvtepi32_ps(_mm256_loadu_si256((__m256i*)&remixed_channel_buffers[output_channel][pos]));
                __m256 mixed_data = _mm256_fmadd_ps(_mm256_cvtepi32_ps(input_data), mix_factor, output_data);
                _mm256_storeu_si256((__m256i*)&remixed_channel_buffers[output_channel][pos], _mm256_cvtps_epi32(mixed_data));
            }
        }
    }
    #elif defined(__SSE2__)
    for (int pos = 0; pos < channel_buffer_pos; pos += 4) {
        for (int input_channel = 0; input_channel < inputChannels; input_channel++) {
            __m128i input_data = _mm_loadu_si128((__m128i*)&channel_buffers[input_channel][pos]);
            for (int output_channel = 0; output_channel < outputChannels; output_channel++) {
                __m128 mix_factor = _mm_set1_ps(speaker_mix[input_channel][output_channel]);
                __m128 output_data = _mm_cvtepi32_ps(_mm_loadu_si128((__m128i*)&remixed_channel_buffers[output_channel][pos]));
                __m128 mixed_data = _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(input_data), mix_factor), output_data);
                _mm_storeu_si128((__m128i*)&remixed_channel_buffers[output_channel][pos], _mm_cvtps_epi32(mixed_data));
            }
        }
    }
    #else
    for (int pos = 0; pos < channel_buffer_pos; pos++) {
        for (int input_channel = 0; input_channel < inputChannels; input_channel++) {
            for (int output_channel = 0; output_channel < outputChannels; output_channel++) {
                remixed_channel_buffers[output_channel][pos] += static_cast<int32_t>(channel_buffers[input_channel][pos] * speaker_mix[input_channel][output_channel]);
            }
        }
    }
    #endif
}

void AudioProcessor::equalize() {
    for (int filter = 0; filter < EQ_BANDS; ++filter) {
        if (eq[filter] != 1.0f) {
            for (int channel = 0; channel < outputChannels; ++channel) {
                for (int pos = 0; pos < channel_buffer_pos; ++pos) {
                    remixed_channel_buffers[channel][pos] = filters[channel][filter]->process(remixed_channel_buffers[channel][pos]);
                }
            }
        }
    }
}

void AudioProcessor::mergeChannelsToBuffer() {
    process_buffer_pos = 0;
    for (int pos = 0; pos < channel_buffer_pos; ++pos) {
        for (int channel = 0; channel < outputChannels; ++channel) {
            processed_buffer[process_buffer_pos++] = remixed_channel_buffers[channel][pos];
        }
    }
}
