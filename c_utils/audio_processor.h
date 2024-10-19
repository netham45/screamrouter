#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <cstdint>
#include <cstring>
#include "libsamplerate/include/samplerate.h"

#define MAX_CHANNELS 8
#define EQ_BANDS 18
#define CHUNK_SIZE 1152

struct SRC_STATE_tag;
typedef SRC_STATE_tag SRC_STATE;
class Biquad;

class AudioProcessor {
public:
    AudioProcessor(int inputChannels, int outputChannels, int inputBitDepth, int inputSampleRate, int outputSampleRate, float volume);
    ~AudioProcessor();

    int processAudio(const uint8_t* inputBuffer, int32_t* outputBuffer);
    void setVolume(float newVolume);
    void setEqualizer(const float* newEq);

private:
    int inputChannels, outputChannels;
    int inputSampleRate, outputSampleRate;
    int inputBitDepth;
    float volume;
    float eq[EQ_BANDS];
    float speaker_mix[MAX_CHANNELS][MAX_CHANNELS];

    uint8_t receive_buffer[CHUNK_SIZE];
    int32_t scaled_buffer[CHUNK_SIZE * 32];
    uint8_t *scaled_buffer_int8 = (uint8_t *)scaled_buffer;
    int32_t resampled_buffer[CHUNK_SIZE * 32];
    int32_t channel_buffers[MAX_CHANNELS][CHUNK_SIZE * 32];
    int32_t remixed_channel_buffers[MAX_CHANNELS][CHUNK_SIZE * 32];
    int32_t merged_buffer[CHUNK_SIZE * 32];
    int32_t processed_buffer[CHUNK_SIZE * 32]; 

    int scale_buffer_pos = 0;
    int process_buffer_pos = 0;
    int merged_buffer_pos = 0;
    int resample_buffer_pos = 0;
    int channel_buffer_pos = 0;

    SRC_STATE* sampler;
    SRC_STATE* downsampler;
    float resampler_data_in[CHUNK_SIZE * MAX_CHANNELS * 8];
    float resampler_data_out[CHUNK_SIZE * MAX_CHANNELS * 8];

    Biquad* filters[MAX_CHANNELS][EQ_BANDS];
    Biquad* dcFilters[MAX_CHANNELS];

    void updateSpeakerMix();
    void setupBiquad();
    void initializeSampler();
    void scaleBuffer();
    void volumeAdjust();
    float softClip(float sample);
    void resample();
    void downsample();
    void splitBufferToChannels();
    void mixSpeakers();
    void equalize();
    void mergeChannelsToBuffer();
    void noiseShapingDither();
    void setupDCFilter();
    void removeDCOffset();
    bool isProcessingRequired();
    bool isProcessingRequiredCheck();
};

#endif // AUDIO_PROCESSOR_H
