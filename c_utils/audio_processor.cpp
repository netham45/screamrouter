#include "audio_processor.h"
#include "biquad/biquad.h"
#include "libsamplerate/include/samplerate.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <emmintrin.h>
#include <immintrin.h>

#define CHUNK_SIZE 1152

AudioProcessor::AudioProcessor(int inputChannels, int outputChannels, int inputBitDepth, int inputSampleRate, int outputSampleRate, float volume)
    : inputChannels(inputChannels), outputChannels(outputChannels), inputBitDepth(inputBitDepth),
      inputSampleRate(inputSampleRate), outputSampleRate(outputSampleRate), volume(volume) {
    
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

void AudioProcessor::updateSpeakerMix() {
 // Fills out the speaker mix table speaker_mix[][] with the current configuration.
    memset(speaker_mix, 0, sizeof(speaker_mix));
    // speaker_mix[input channel][output channel] = gain;
    // Ex: To map Left on Stereo to Right on Stereo at half volume you would do:
    // speaker_mix[0][1] = .5;
    switch (inputChannels)
    {
    case 1: // Mono, Ch 0: Left
        // Mono -> All
        for (int output_channel = 0; output_channel < MAX_CHANNELS; output_channel++) // Write the left (first) speaker to every channel
            speaker_mix[0][output_channel] = 1;
        break;
    case 2: // Stereo, Ch 0: Left, Ch 1: Right
        switch (outputChannels)
        {
        case 1:                     // Stereo -> Mono
            speaker_mix[0][0] = .5; // Left to mono .5 vol
            speaker_mix[1][0] = .5; // Right to mono .5 vol
            break;
        case 2:                    // Stereo -> Stereo
            speaker_mix[0][0] = 1; // Left to Left
            speaker_mix[1][1] = 1; // Right to Right
            break;
        case 4:
            speaker_mix[0][0] = 1; // Left to Front Left
            speaker_mix[1][1] = 1; // Right to Front Right
            speaker_mix[0][2] = 1; // Left to Back Left
            speaker_mix[1][3] = 1; // Right to Back Right
            break;
        case 6: // Stereo -> 5.1 Surround
            // FL FR C LFE BL BR
            speaker_mix[0][0] = 1;  // Left to Front Left
            speaker_mix[0][5] = 1;  // Left to Rear Left
            speaker_mix[1][1] = 1;  // Right to Front Right
            speaker_mix[1][6] = 1;  // Right to Rear Right
            speaker_mix[0][3] = .5; // Left to Center Half Vol
            speaker_mix[1][3] = .5; // Right to Center Half Vol
            speaker_mix[0][4] = .5; // Right to Sub Half Vol
            speaker_mix[1][4] = .5; // Left to Sub Half Vol
            break;
        case 8: // Stereo -> 7.1 Surround
            // FL FR C LFE BL BR SL SR
            speaker_mix[0][0] = 1;  // Left to Front Left
            speaker_mix[0][6] = 1;  // Left to Side Left
            speaker_mix[0][4] = 1;  // Left to Rear Left
            speaker_mix[1][1] = 1;  // Right to Front Right
            speaker_mix[1][7] = 1;  // Right to Side Right
            speaker_mix[1][5] = 1;  // Right to Rear Right
            speaker_mix[0][2] = .5; // Left to Center Half Vol
            speaker_mix[1][2] = .5; // Right to Center Half Vol
            speaker_mix[0][3] = .5; // Right to Sub Half Vol
            speaker_mix[1][3] = .5; // Left to Sub Half Vol
            break;
        }
        break;
    case 4:
        switch (outputChannels)
        {
        case 1:                      // Quad -> Mono
            speaker_mix[0][0] = .25; // Front Left to Mono
            speaker_mix[1][0] = .25; // Front Right to Mono
            speaker_mix[2][0] = .25; // Rear Left to Mono
            speaker_mix[3][0] = .25; // Rear Right to Mono
            break;
        case 2:                     // Quad -> Stereo
            speaker_mix[0][0] = .5; // Front Left to Left
            speaker_mix[1][1] = .5; // Front Right to Right
            speaker_mix[2][0] = .5; // Rear Left to Left
            speaker_mix[3][1] = .5; // Rear Right to Right
            break;
        case 4:
            speaker_mix[0][0] = 1; // Front Left to Front Left
            speaker_mix[1][1] = 1; // Front Right to Front Right
            speaker_mix[2][2] = 1; // Rear Left to Rear Left
            speaker_mix[3][3] = 1; // Rear Right to Rear Right
            break;
        case 6: // Quad -> 5.1 Surround
            // FL FR C LFE BL BR
            speaker_mix[0][0] = 1;   // Front Left to Front Left
            speaker_mix[1][1] = 1;   // Front Right to Front Right
            speaker_mix[0][2] = .5;  // Front Left to Center
            speaker_mix[1][2] = .5;  // Front Right to Center
            speaker_mix[0][3] = .25; // Front Left to LFE
            speaker_mix[1][3] = .25; // Front Right to LFE
            speaker_mix[2][3] = .25; // Rear Left to LFE
            speaker_mix[3][3] = .25; // Rear Right to LFE
            speaker_mix[2][4] = 1;   // Rear Left to Rear Left
            speaker_mix[3][5] = 1;   // Rear Right to Rear Right
            break;
        case 8: // Quad -> 7.1 Surround
            // FL FR C LFE BL BR SL SR
            speaker_mix[0][0] = 1;   // Front Left to Front Left
            speaker_mix[1][1] = 1;   // Front Right to Front Right
            speaker_mix[0][2] = .5;  // Front Left to Center
            speaker_mix[1][2] = .5;  // Front Right to Center
            speaker_mix[0][3] = .25; // Front Left to LFE
            speaker_mix[1][3] = .25; // Front Right to LFE
            speaker_mix[2][3] = .25; // Rear Left to LFE
            speaker_mix[3][3] = .25; // Rear Right to LFE
            speaker_mix[2][4] = 1;   // Rear Left to Rear Left
            speaker_mix[3][5] = 1;   // Rear Right to Rear Right
            speaker_mix[0][6] = .5;  // Front Left to Side Left
            speaker_mix[1][7] = .5;  // Front Right to Side Right
            speaker_mix[2][6] = .5;  // Rear Left to Side Left
            speaker_mix[3][7] = .5;  // Rear Right to Side Right
            break;
        }
        break;
    case 6:
        switch (outputChannels)
        {
        case 1:                     // 5.1 Surround -> Mono
            speaker_mix[0][0] = .2; // Front Left to Mono
            speaker_mix[1][0] = .2; // Front Right to Mono
            speaker_mix[2][0] = .2; // Center to Mono
            speaker_mix[4][0] = .2; // Rear Left to Mono
            speaker_mix[5][0] = .2; // Rear Right to Mono
            break;
        case 2:                      // 5.1 Surround -> Stereo
            speaker_mix[0][0] = .33; // Front Left to Left
            speaker_mix[1][1] = .33; // Front Right to Right
            speaker_mix[2][0] = .33; // Center to Left
            speaker_mix[2][1] = .33; // Center to Right
            speaker_mix[4][0] = .33; // Rear Left to Left
            speaker_mix[5][1] = .33; // Rear Right to Right
            break;
        case 4:
            speaker_mix[0][0] = .66; // Front Left to Front Left
            speaker_mix[1][1] = .66; // Front Right to Front Right
            speaker_mix[2][0] = .33; // Center to Front Left
            speaker_mix[2][1] = .33; // Center to Front Right
            speaker_mix[4][2] = 1;   // Rear Left to Rear Left
            speaker_mix[5][3] = 1;   // Rear Right to Rear Right
            break;
        case 6: // 5.1 Surround -> 5.1 Surround
            // FL FR C LFE BL BR
            speaker_mix[0][0] = 1; // Front Left to Front Left
            speaker_mix[1][1] = 1; // Front Right to Front Right
            speaker_mix[2][2] = 1; // Center to Center
            speaker_mix[3][3] = 1; // LFE to LFE
            speaker_mix[4][4] = 1; // Rear Left to Rear Left
            speaker_mix[5][5] = 1; // Rear Right to Rear Right
            break;
        case 8: // 5.1 Surround -> 7.1 Surround
            // FL FR C LFE BL BR SL SR
            speaker_mix[0][0] = 1;  // Front Left to Front Left
            speaker_mix[1][1] = 1;  // Front Right to Front Right
            speaker_mix[2][2] = 1;  // Center to Center
            speaker_mix[3][3] = 1;  // LFE to LFE
            speaker_mix[4][4] = 1;  // Rear Left to Rear Left
            speaker_mix[5][5] = 1;  // Rear Right to Rear Right
            speaker_mix[0][6] = .5; // Front Left to Side Left
            speaker_mix[1][7] = .5; // Front Right to Side Right
            speaker_mix[4][6] = .5; // Rear Left to Side Left
            speaker_mix[5][7] = .5; // Rear Right to Side Right
            break;
        }
        break;
    case 8:
        switch (outputChannels)
        {
        case 1:                              // 7.1 Surround -> Mono
            speaker_mix[0][0] = 1.0f / 7.0f; // Front Left to Mono
            speaker_mix[1][0] = 1.0f / 7.0f; // Front Right to Mono
            speaker_mix[2][0] = 1.0f / 7.0f; // Center to Mono
            speaker_mix[4][0] = 1.0f / 7.0f; // Rear Left to Mono
            speaker_mix[5][0] = 1.0f / 7.0f; // Rear Right to Mono
            speaker_mix[6][0] = 1.0f / 7.0f; // Side Left to Mono
            speaker_mix[7][0] = 1.0f / 7.0f; // Side Right to Mono
            break;
        case 2:                       // 7.1 Surround -> Stereo
            speaker_mix[0][0] = .5;   // Front Left to Left
            speaker_mix[1][1] = .5;   // Front Right to Right
            speaker_mix[2][0] = .25;  // Center to Left
            speaker_mix[2][1] = .25;  // Center to Right
            speaker_mix[4][0] = .125; // Rear Left to Left
            speaker_mix[5][1] = .125; // Rear Right to Right
            speaker_mix[6][0] = .125; // Side Left to Left
            speaker_mix[7][1] = .125; // Side Right to Right
            break;
        case 4:                      // 7.1 Surround -> Quad
            speaker_mix[0][0] = .5;  // Front Left to Front Left
            speaker_mix[1][1] = .5;  // Front Right to Front Right
            speaker_mix[2][0] = .25; // Center to Front Left
            speaker_mix[2][1] = .25; // Center to Front Right
            speaker_mix[4][2] = .66; // Rear Left to Rear Left
            speaker_mix[5][3] = .66; // Rear Right to Rear Right
            speaker_mix[6][0] = .25; // Side Left to Front Left
            speaker_mix[7][1] = .25; // Side Left to Front Right
            speaker_mix[6][2] = .33; // Side Left to Rear Left
            speaker_mix[7][3] = .33; // Side Left to Rear Right
            break;
        case 6: // 7.1 Surround -> 5.1 Surround
            // FL FR C LFE BL BR
            speaker_mix[0][0] = .66; // Front Left to Front Left
            speaker_mix[1][1] = .66; // Front Right to Front Right
            speaker_mix[2][2] = 1;   // Center to Center
            speaker_mix[3][3] = 1;   // LFE to LFE
            speaker_mix[4][4] = .66; // Rear Left to Rear Left
            speaker_mix[5][5] = .66; // Rear Right to Rear Right
            speaker_mix[6][0] = .33; // Side Left to Front Left
            speaker_mix[7][1] = .33; // Side Right to Front Right
            speaker_mix[6][4] = .33; // Side Left to Rear Left
            speaker_mix[7][5] = .33; // Side Right to Rear Right
            break;
        case 8: // 7.1 Surround -> 7.1 Surround
            // FL FR C LFE BL BR SL SR
            speaker_mix[0][0] = 1; // Front Left to Front Left
            speaker_mix[1][1] = 1; // Front Right to Front Right
            speaker_mix[2][2] = 1; // Center to Center
            speaker_mix[3][3] = 1; // LFE to LFE
            speaker_mix[4][4] = 1; // Rear Left to Rear Left
            speaker_mix[5][5] = 1; // Rear Right to Rear Right
            speaker_mix[6][6] = 1; // Side Left to Side Left
            speaker_mix[7][7] = 1; // Side Right to Side Right
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
