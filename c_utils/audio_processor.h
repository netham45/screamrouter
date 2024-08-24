#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <cstdint>

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
    int32_t scaled_buffer[CHUNK_SIZE * 8];
    uint8_t *scaled_buffer_int8 = (uint8_t *)scaled_buffer;
    int32_t resampled_buffer[CHUNK_SIZE * 16];
    int32_t channel_buffers[MAX_CHANNELS][CHUNK_SIZE];
    int32_t remixed_channel_buffers[MAX_CHANNELS][CHUNK_SIZE];
    int32_t processed_buffer[CHUNK_SIZE * 8];

    int scale_buffer_pos;
    int process_buffer_pos;
    int resample_buffer_pos;
    int channel_buffer_pos;

    SRC_STATE* sampler;
    float resampler_data_in[CHUNK_SIZE * MAX_CHANNELS];
    float resampler_data_out[CHUNK_SIZE * MAX_CHANNELS * 8];

    Biquad* filters[MAX_CHANNELS][EQ_BANDS];

    void updateSpeakerMix();
    void setupBiquad();
    void initializeSampler();
    void scaleBuffer();
    void volumeAdjust();
    void resample();
    void splitBufferToChannels();
    void mixSpeakers();
    void equalize();
    void mergeChannelsToBuffer();
};

#endif // AUDIO_PROCESSOR_H
