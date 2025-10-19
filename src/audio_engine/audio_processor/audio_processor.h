/**
 * @file audio_processor.h
 * @brief Defines the AudioProcessor class for handling core audio processing tasks.
 * @details This class is responsible for a pipeline of audio operations including
 *          volume adjustment, equalization, speaker mixing, and resampling. It is designed
 *          to be used within a larger audio engine.
 */
#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <cstdint>
#include <cstring>
#include <thread>
#include <atomic>
#include <vector>
#include <map>
#include <mutex>
#include "../audio_constants.h"
#include "../configuration/audio_engine_config_types.h"
#include "../configuration/audio_engine_settings.h"

// libsamplerate include
#include <samplerate.h>

/**
 * @def CHUNK_SIZE
 * @brief The size of the audio chunk in bytes to be processed at a time.
 */
#define CHUNK_SIZE 1152

class Biquad;

/**
 * @class AudioProcessor
 * @brief Manages the core audio processing pipeline.
 * @details This class encapsulates all the logic for processing raw audio data.
 *          This includes format conversion, volume control, equalization, speaker mixing,
 *          and sample rate conversion. It uses the libsamplerate library for high-quality resampling.
 */
class AudioProcessor {
public:
    /**
     * @brief Constructs an AudioProcessor instance.
     * @param inputChannels Number of input audio channels.
     * @param outputChannels Number of output audio channels.
     * @param inputBitDepth Bit depth of the input audio.
     * @param inputSampleRate Sample rate of the input audio.
     * @param outputSampleRate Sample rate of the output audio.
     * @param volume Initial volume level.
     * @param initial_layouts_config Initial map of speaker layouts keyed by input channel count.
     */
    AudioProcessor(int inputChannels, int outputChannels, int inputBitDepth,
                   int inputSampleRate, int outputSampleRate, float volume,
                   const std::map<int, screamrouter::audio::CppSpeakerLayout>& initial_layouts_config,
                   std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings);
    /**
     * @brief Destructor for the AudioProcessor.
     */
    ~AudioProcessor();

    /**
     * @brief Processes a chunk of audio data.
     * @param inputBuffer Pointer to the input audio buffer.
     * @param outputBuffer Pointer to the output buffer for processed audio.
     * @return The number of bytes written to the output buffer.
     */
    int processAudio(const uint8_t* inputBuffer, int32_t* outputBuffer);

    /**
     * @brief Sets the volume level.
     * @param newVolume The new volume level (e.g., 1.0 for normal).
     */
    void setVolume(float newVolume);

    /**
     * @brief Sets the equalizer band gains.
     * @param newEq Pointer to an array of floats representing the new EQ gains.
     */
    void setEqualizer(const float* newEq);

    /**
     * @brief Sets the playback rate for time-stretching or compression.
     * @param rate The new playback rate (1.0 is normal speed).
     */
    void set_playback_rate(double rate);

    /**
     * @brief Enables or disables volume normalization.
     * @param enabled True to enable, false to disable.
     */
    void setVolumeNormalization(bool enabled);

    /**
     * @brief Enables or disables equalizer normalization.
     * @param enabled True to enable, false to disable.
     */
    void setEqNormalization(bool enabled);
    
    /**
     * @brief Updates the speaker layout configuration.
     * @param new_layouts_config A map of new speaker layouts.
     */
    void update_speaker_layouts_config(const std::map<int, screamrouter::audio::CppSpeakerLayout>& new_layouts_config);

    /**
     * @brief Flushes the internal state of all filters.
     */
    void flushFilters();

    /**
     * @brief Applies a custom speaker mix matrix.
     * @param custom_matrix The custom speaker mix matrix to apply.
     */
    void applyCustomSpeakerMix(const std::vector<std::vector<float>>& custom_matrix);

    /**
     * @brief Calculates and applies an automatic speaker mix based on input/output channels.
     */
    void calculateAndApplyAutoSpeakerMix();

private:
    std::shared_ptr<screamrouter::audio::AudioEngineSettings> m_settings;
    /** @brief Map of speaker layouts, keyed by the number of input channels. */
    std::map<int, screamrouter::audio::CppSpeakerLayout> speaker_layouts_config_;
    /** @brief Mutex to protect access to the speaker layouts configuration. */
    std::mutex speaker_layouts_config_mutex_;

    // --- Core Audio Parameters ---
    int inputChannels, outputChannels;
    int inputSampleRate, outputSampleRate;
    int inputBitDepth;
    std::atomic<float> target_volume_;
    std::atomic<float> current_volume_;
    float smoothing_factor_;
    float eq[screamrouter::audio::EQ_BANDS];
    float speaker_mix[screamrouter::audio::MAX_CHANNELS][screamrouter::audio::MAX_CHANNELS];
    std::atomic<double> playback_rate_{1.0};
    double m_last_known_playback_rate = 1.0;

    // --- Normalization Flags ---
    bool volume_normalization_enabled_ = false;
    bool eq_normalization_enabled_ = false;
    float current_gain_ = 1.0f;

    // --- Internal Buffers ---
    std::vector<uint8_t> receive_buffer;
    std::vector<int32_t> scaled_buffer;
    std::vector<int32_t> resampled_buffer;
    std::vector<std::vector<int32_t>> channel_buffers;
    std::vector<std::vector<int32_t>> remixed_channel_buffers;
    std::vector<int32_t> merged_buffer;
    std::vector<int32_t> processed_buffer;

    // --- Buffer Position Trackers ---
    size_t scale_buffer_pos = 0;
    size_t process_buffer_pos = 0;
    size_t merged_buffer_pos = 0;
    size_t resample_buffer_pos = 0;
    size_t channel_buffer_pos = 0;

    // --- libsamplerate Resampler Members ---
    SRC_STATE* m_upsampler;
    SRC_STATE* m_downsampler;

    // --- Filters ---
    Biquad* filters[screamrouter::audio::MAX_CHANNELS][screamrouter::audio::EQ_BANDS];
    Biquad* dcFilters[screamrouter::audio::MAX_CHANNELS];

    // --- Private Methods for Audio Pipeline Stages ---
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

    // --- Buffer Monitoring Thread ---
    std::thread monitor_thread;
    std::atomic<bool> monitor_running;

    // --- Caching for Processing Requirement ---
    bool isProcessingRequiredCache = false;
    bool isProcessingRequiredCacheSet = false;

    // --- Private Helper Methods ---
    void select_active_speaker_mix();
    void select_active_speaker_mix_locked();
};

#endif // AUDIO_PROCESSOR_H
