#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <cstdint>
#include <cstring>
#include <thread>
#include <atomic>
#include <vector> // Added for std::vector
#include <map>    // Added for std::map
#include <mutex>  // Added for std::mutex
#include "../audio_constants.h" // For MAX_CHANNELS, EQ_BANDS
#include "../configuration/audio_engine_config_types.h" // For CppSpeakerLayout

// r8brain includes moved here, before class definition
#include "../deps/r8brain-free-src/r8bconf.h"
#include "../deps/r8brain-free-src/CDSPResampler.h"

// MAX_CHANNELS and EQ_BANDS are now in audio_constants.h
#define CHUNK_SIZE 1152

// struct SRC_STATE_tag; // No longer needed
// typedef SRC_STATE_tag SRC_STATE; // No longer needed
class Biquad;


class AudioProcessor {
public:
    AudioProcessor(int inputChannels, int outputChannels, int inputBitDepth, 
                   int inputSampleRate, int outputSampleRate, float volume,
                   const std::map<int, screamrouter::audio::CppSpeakerLayout>& initial_layouts_config); // Changed to audio namespace
    ~AudioProcessor();

    int processAudio(const uint8_t* inputBuffer, int32_t* outputBuffer);
    void setVolume(float newVolume);
    void setEqualizer(const float* newEq);
    void setVolumeNormalization(bool enabled);
    void setEqNormalization(bool enabled);
    
    // --- Speaker Layout Configuration Method ---
    void update_speaker_layouts_config(const std::map<int, screamrouter::audio::CppSpeakerLayout>& new_layouts_config); // Changed to audio namespace

    // --- Old Speaker Mix Methods (still used by select_active_speaker_mix) ---
    void applyCustomSpeakerMix(const std::vector<std::vector<float>>& custom_matrix);
    void calculateAndApplyAutoSpeakerMix();

private: // Changed from protected to private for better encapsulation
    // --- New Speaker Layouts Map ---
    std::map<int, screamrouter::audio::CppSpeakerLayout> speaker_layouts_config_; // Changed to audio namespace
    std::mutex speaker_layouts_config_mutex_; // To protect access to speaker_layouts_config_

    // --- Core Audio Parameters ---
    int inputChannels, outputChannels;
    int inputSampleRate, outputSampleRate;
    int inputBitDepth;
    std::atomic<float> target_volume_;
    std::atomic<float> current_volume_;
    float smoothing_factor_;
    float eq[screamrouter::audio::EQ_BANDS];
    float speaker_mix[screamrouter::audio::MAX_CHANNELS][screamrouter::audio::MAX_CHANNELS];

    // --- Normalization Flags ---
    bool volume_normalization_enabled_ = false;
    bool eq_normalization_enabled_ = true;
    float current_gain_ = 1.0f;

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

    Biquad* filters[screamrouter::audio::MAX_CHANNELS][screamrouter::audio::EQ_BANDS];
    Biquad* dcFilters[screamrouter::audio::MAX_CHANNELS];

    // void updateSpeakerMix(); // This line will be removed or commented out if calculateAndApplyAutoSpeakerMix replaces it
    // calculateAndApplyAutoSpeakerMix is now public
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

    // --- Private Helper Methods ---
    void select_active_speaker_mix(); 
    void select_active_speaker_mix_locked(); // New private method, assumes lock is held
};

#endif // AUDIO_PROCESSOR_H
