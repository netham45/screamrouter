#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <cstdint>
#include <cstring>
#include <thread>
#include <atomic>
#include <vector> // Added for std::vector
// #include "samplerate.h" // Removed libsamplerate include

// r8brain includes moved here, before class definition
#include "r8brain-free-src/r8bconf.h"
#include "r8brain-free-src/CDSPResampler.h"

#define MAX_CHANNELS 8
#define EQ_BANDS 18
#define CHUNK_SIZE 1152

// struct SRC_STATE_tag; // No longer needed
// typedef SRC_STATE_tag SRC_STATE; // No longer needed
class Biquad;


class AudioProcessor {
public:
    AudioProcessor(int inputChannels, int outputChannels, int inputBitDepth, int inputSampleRate, int outputSampleRate, float volume);
    ~AudioProcessor();

    int processAudio(const uint8_t* inputBuffer, int32_t* outputBuffer);
    void setVolume(float newVolume);
    void setEqualizer(const float* newEq);

protected:
    int inputChannels, outputChannels;
    int inputSampleRate, outputSampleRate;
    int inputBitDepth;
    float volume;
    float eq[EQ_BANDS];
    float speaker_mix[MAX_CHANNELS][MAX_CHANNELS];

    std::vector<uint8_t> receive_buffer;
    std::vector<int32_t> scaled_buffer;
    // uint8_t *scaled_buffer_int8 will be handled by reinterpret_cast<uint8_t*>(scaled_buffer.data())
    std::vector<int32_t> resampled_buffer;
    std::vector<std::vector<int32_t>> channel_buffers;
    std::vector<std::vector<int32_t>> remixed_channel_buffers;
    std::vector<int32_t> merged_buffer;
    std::vector<int32_t> processed_buffer;

    size_t scale_buffer_pos = 0; // Changed to size_t for consistency with vector sizes
    size_t process_buffer_pos = 0; // Changed to size_t
    size_t merged_buffer_pos = 0;  // Changed to size_t
    size_t resample_buffer_pos = 0; // Changed to size_t
    size_t channel_buffer_pos = 0;  // Changed to size_t

    // libsamplerate members removed
    // SRC_STATE* sampler;
    // SRC_STATE* downsampler;
    // std::vector<float> resampler_data_in;
    // std::vector<float> resampler_data_out;

    // r8brain members added
    std::vector<r8b::CDSPResampler24*> upsamplers; // Removed r8brain:: namespace
    std::vector<r8b::CDSPResampler24*> downsamplers; // Removed r8brain:: namespace
    std::vector<std::vector<double>> r8brain_upsampler_in_buf;
    std::vector<std::vector<double>> r8brain_downsampler_in_buf;

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
    void monitorBuffers();

    // Buffer monitoring thread
    std::thread monitor_thread;
    std::atomic<bool> monitor_running;

    // Cache for isProcessingRequired result
    bool isProcessingRequiredCache = false;
    bool isProcessingRequiredCacheSet = false;
};

#endif // AUDIO_PROCESSOR_H
