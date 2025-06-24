
#include "audio_processor.h"
#include "../utils/cpp_logger.h" // For new C++ logger
#include "biquad/biquad.h"
#include "../deps/r8brain-free-src/r8bconf.h"
#include "../deps/r8brain-free-src/r8bbase.h"
#include "../deps/r8brain-free-src/CDSPResampler.h"
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <vector>
#include <random>
#include <iostream> // For cpp_logger fallback
#include <chrono>
#include <thread>
#include <new> // Include for std::bad_alloc
#include <sstream> // For logging matrix (will be replaced)
#include <iomanip> // For std::fixed and std::setprecision (will be replaced)

using namespace screamrouter::audio;

// Undefine min and max macros to prevent conflicts with std::min and std::max
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#define CHUNK_SIZE 1152
#define OVERSAMPLING_FACTOR 1

AudioProcessor::AudioProcessor(int inputChannels, int outputChannels, int inputBitDepth, 
                               int inputSampleRate, int outputSampleRate, float volume,
                               const std::map<int, screamrouter::audio::CppSpeakerLayout>& initial_layouts_config) // Changed to audio namespace
    : inputChannels(inputChannels), outputChannels(outputChannels), inputBitDepth(inputBitDepth),
      inputSampleRate(inputSampleRate), outputSampleRate(outputSampleRate), smoothing_factor_(0.005f),
      speaker_layouts_config_(initial_layouts_config), // Initialize the map
      monitor_running(true),
      receive_buffer(CHUNK_SIZE * 4), 
      scaled_buffer(CHUNK_SIZE * 8),  
      resampled_buffer(CHUNK_SIZE * MAX_CHANNELS * 4 * OVERSAMPLING_FACTOR), // Larger for resampling
      channel_buffers(MAX_CHANNELS, std::vector<int32_t>(CHUNK_SIZE * 8 * OVERSAMPLING_FACTOR)), // Larger for resampling
      remixed_channel_buffers(MAX_CHANNELS, std::vector<int32_t>(CHUNK_SIZE * 8 * OVERSAMPLING_FACTOR)), // Larger for resampling
      merged_buffer(CHUNK_SIZE * MAX_CHANNELS * 4 * OVERSAMPLING_FACTOR), // Larger for resampling
      processed_buffer(CHUNK_SIZE * MAX_CHANNELS * 4), // Final output size related
      // Removed resampler_data_in and resampler_data_out (float buffers for libsamplerate)
      // sampler(nullptr), downsampler(nullptr) replaced by r8brain vectors
      isProcessingRequiredCache(false), isProcessingRequiredCacheSet(false), // Initialize cache flags
      volume_normalization_enabled_(false), eq_normalization_enabled_(true)
    // Add new r8brain member variables
    // upsamplers and downsamplers will be initialized in the constructor body or initializer list if default constructible
    // r8brain_upsampler_in_buf and r8brain_downsampler_in_buf will be initialized in constructor body
{
    target_volume_.store(volume);
    current_volume_.store(volume);
    LOG_CPP_INFO("[AudioProc] Constructor: inputChannels=%d, outputChannels=%d, inputSampleRate=%d, outputSampleRate=%d",
                 inputChannels, outputChannels, inputSampleRate, outputSampleRate);
    LOG_CPP_INFO("[AudioProc] Constructor: Initial speaker_layouts_config_ has %zu entries.", initial_layouts_config.size());

    for (const auto& pair : initial_layouts_config) {
        LOG_CPP_INFO("[AudioProc]   Layout for %dch input: auto_mode=%s", pair.first, (pair.second.auto_mode ? "true" : "false"));
        if (!pair.second.auto_mode) {
            LOG_CPP_INFO("[AudioProc]     Matrix:");
            for (const auto& row : pair.second.matrix) {
                std::string row_str = "[AudioProc]       ";
                for (float val : row) {
                    char val_buf[10];
                    snprintf(val_buf, sizeof(val_buf), "%.2f ", val);
                    row_str += val_buf;
                }
                LOG_CPP_INFO("%s", row_str.c_str());
            }
        }
    }

    // Initialize r8brain resampler vectors (they are default constructible)
    // upsamplers; // Default construction
    // downsamplers; // Default construction
    // r8brain_upsampler_in_buf; // Default construction
    // r8brain_downsampler_in_buf; // Default construction
    // Initialize filter pointers to nullptr before use
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        for (int band = 0; band < EQ_BANDS; ++band) {
            filters[ch][band] = nullptr;
        }
        dcFilters[ch] = nullptr;
    }
    
    std::fill(eq, eq + EQ_BANDS, 1.0f); 
    // calculateAndApplyAutoSpeakerMix(); // Old call, select_active_speaker_mix will handle initial setup
    setupBiquad();
    initializeSampler(); // This might need adjustment if it depends on speaker_mix being set prior
    setupDCFilter();
    select_active_speaker_mix(); // Call to select initial mix

    // Initialize position trackers
    scale_buffer_pos = 0;
    process_buffer_pos = 0;
    merged_buffer_pos = 0; 
    resample_buffer_pos = 0;
    channel_buffer_pos = 0;

    // Start the monitoring thread
    monitor_thread = std::thread(&AudioProcessor::monitorBuffers, this);
}

AudioProcessor::~AudioProcessor() {
    monitor_running = false;
    if (monitor_thread.joinable()) {
        monitor_thread.join();
    }

    // Clean up r8brain resamplers
    for (auto ptr : upsamplers) {
        delete ptr;
    }
    upsamplers.clear();
    for (auto ptr : downsamplers) {
        delete ptr;
    }
    downsamplers.clear();
     
    for (int channel = 0; channel < MAX_CHANNELS; channel++) {
        for (int i = 0; i < EQ_BANDS; i++) {
            delete filters[channel][i];
        }
        delete dcFilters[channel];
    }
}

void AudioProcessor::monitorBuffers() {
     // Simplified monitoring - can be re-enabled for debugging
     // while (monitor_running) {
     //     // ... (logging code from previous versions) ...
     //     std::this_thread::sleep_for(std::chrono::seconds(1));
     // }
}

int AudioProcessor::processAudio(const uint8_t* inputBuffer, int32_t* outputBuffer) {
    // 1. Copy input data
    if (receive_buffer.size() < CHUNK_SIZE) {
        try { receive_buffer.resize(CHUNK_SIZE); }
        catch (const std::bad_alloc& e) { LOG_CPP_ERROR("[AudioProc] Error resizing receive_buffer: %s", e.what()); return -1; } // Return error code
    }
    memcpy(receive_buffer.data(), inputBuffer, CHUNK_SIZE);

    // --- Processing Pipeline ---
    scaleBuffer();
    volumeAdjust();
    resample();
    splitBufferToChannels();
    mixSpeakers();
    removeDCOffset();
    equalize();
    mergeChannelsToBuffer();
    downsample();
    noiseShapingDither();
    // --- End Pipeline ---

    // Determine samples to copy based on actual samples processed and available
    size_t samples_available = process_buffer_pos; // This is the actual number of samples at outputSampleRate

    if (outputBuffer == nullptr) {
        LOG_CPP_ERROR("[AudioProc] Error: outputBuffer is null in processAudio.");
        return 0;
    }

    // We will copy all available samples. The caller must ensure outputBuffer is large enough.
    size_t samples_to_write = samples_available;

    if (samples_to_write > 0) {
        // Sanity check: ensure we don't read past the end of processed_buffer.
        // This should ideally not happen if process_buffer_pos is correctly managed by preceding stages.
        if (samples_to_write > processed_buffer.size()) {
             LOG_CPP_ERROR("[AudioProc] Error: samples_available (%zu) exceeds internal processed_buffer size (%zu) in processAudio final copy. Capping write size.",
                           samples_to_write, processed_buffer.size());
             samples_to_write = processed_buffer.size(); // Cap to prevent buffer overflow on read
        }
        memcpy(outputBuffer, processed_buffer.data(), samples_to_write * sizeof(int32_t));
    }
    
    // Return the actual number of int32_t samples written to outputBuffer.
    // If samples_to_write is 0 (e.g., due to an error or no data), memcpy is skipped and 0 is returned.
    return static_cast<int>(samples_to_write);
}


void AudioProcessor::setVolume(float newVolume) {
    target_volume_.store(newVolume);
}

void AudioProcessor::setVolumeNormalization(bool enabled) {
    volume_normalization_enabled_ = enabled;
}

void AudioProcessor::setEqNormalization(bool enabled) {
    eq_normalization_enabled_ = enabled;
    setupBiquad(); // Re-setup biquad to apply the new setting
}

void AudioProcessor::setEqualizer(const float* newEq) {
    if (newEq) {
        std::copy(newEq, newEq + EQ_BANDS, eq);
        setupBiquad();
        for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
            for (int band = 0; band < EQ_BANDS; ++band) {
                if (filters[ch][band]) {
                    filters[ch][band]->flush();
                }
            }
            if (dcFilters[ch]) {
                dcFilters[ch]->flush();
            }
        }
    }
}

void AudioProcessor::setupBiquad() {
    float max_gain = 1.0f;
    if (eq_normalization_enabled_) {
        for(int i=0; i<EQ_BANDS; ++i) { if (eq[i] > max_gain) max_gain = eq[i]; }
        if (max_gain < 0.01f) max_gain = 1.0f;
    }

    const float frequencies[] = {65.406392f, 92.498606f, 130.81278f, 184.99721f, 261.62557f, 369.99442f, 523.25113f, 739.9884f,
                                 1046.5023f, 1479.9768f, 2093.0045f, 2959.9536f, 4186.0091f, 5919.9072f, 8372.0181f, 11839.814f,
                                 16744.036f, 20000.0f};

    float sampleRateForFilters = static_cast<float>(outputSampleRate * OVERSAMPLING_FACTOR);
    if (sampleRateForFilters <= 0) {
         LOG_CPP_ERROR("[AudioProc] Error: Invalid sample rate (%d) for Biquad setup.", outputSampleRate);
         return;
    }

    for (int channel = 0; channel < MAX_CHANNELS; channel++) {
        for (int i = 0; i < EQ_BANDS; i++) {
            delete filters[channel][i]; 
            float gain_db = 10.0f * (eq[i] - 1.0f);
            if (eq_normalization_enabled_) {
                gain_db = 10.0f * ((eq[i] / max_gain) - 1.0f);
            }
            float normalized_freq = frequencies[i] / sampleRateForFilters;
            if (normalized_freq >= 0.5f) {
                 normalized_freq = 0.499f;
            }
            try {
                 filters[channel][i] = new Biquad(bq_type_peak, normalized_freq, 1.0, gain_db);
            } catch (const std::bad_alloc& e) {
                 LOG_CPP_ERROR("[AudioProc] Error allocating Biquad filter [%d][%d]: %s", channel, i, e.what());
                 filters[channel][i] = nullptr;
            }
        }
    }
}

void AudioProcessor::initializeSampler() {
    // Clean up existing r8brain resamplers
    for (auto ptr : upsamplers) {
        delete ptr;
    }
    upsamplers.clear();
    for (auto ptr : downsamplers) {
        delete ptr;
    }
    downsamplers.clear();

    if (inputSampleRate <= 0 || outputSampleRate <= 0) {
        LOG_CPP_ERROR("[AudioProc] Error: Invalid input or output sample rate for r8brain initialization.");
        return;
    }

    // Initialize upsamplers
    if (inputChannels > 0) {
        int max_frames_per_channel_in = 0;
        if (inputBitDepth > 0 && inputChannels > 0) { // Ensure inputBitDepth and inputChannels are positive
             max_frames_per_channel_in = (CHUNK_SIZE / (inputBitDepth / 8)) / inputChannels;
        }
        if (max_frames_per_channel_in <= 0) { // Default to a reasonable capacity if calculation fails
            max_frames_per_channel_in = 2048; // Default capacity
            LOG_CPP_WARNING("[AudioProc] Warning: Could not determine max_frames_per_channel_in, defaulting to %d", max_frames_per_channel_in);
        }

        upsamplers.reserve(inputChannels);
        r8brain_upsampler_in_buf.resize(inputChannels);
        for (int i = 0; i < inputChannels; ++i) {
            try {
                upsamplers.push_back(new r8b::CDSPResampler24(
                    static_cast<double>(inputSampleRate),
                    static_cast<double>(outputSampleRate * OVERSAMPLING_FACTOR),
                    max_frames_per_channel_in
                ));
                r8brain_upsampler_in_buf[i].resize(max_frames_per_channel_in);
            } catch (const std::bad_alloc& e) {
                LOG_CPP_ERROR("[AudioProc] Error allocating r8brain upsampler or buffer for channel %d: %s", i, e.what());
                // Clean up already allocated resamplers in case of partial failure
                for (auto ptr : upsamplers) delete ptr;
                upsamplers.clear();
                r8brain_upsampler_in_buf.clear();
                return;
            }  catch (const std::exception& e) { // Changed to std::exception
                LOG_CPP_ERROR("[AudioProc] Standard exception during upsampler creation for channel %d: %s", i, e.what());
                for (auto ptr : upsamplers) delete ptr;
                upsamplers.clear();
                r8brain_upsampler_in_buf.clear();
                return;
            } catch (...) {
                LOG_CPP_ERROR("[AudioProc] Unknown exception during upsampler creation for channel %d", i);
                for (auto ptr : upsamplers) delete ptr;
                upsamplers.clear();
                r8brain_upsampler_in_buf.clear();
                return;
            }
        }
    }

    // Initialize downsamplers
    if (outputChannels > 0) {
        // Estimate max frames after upsampling. This is a rough guide for buffer capacity.
        // A more precise calculation would involve the resampling ratio.
        // Using a generous estimate based on existing buffer sizes.
        int max_frames_per_channel_out_oversampled = (CHUNK_SIZE * MAX_CHANNELS * 4 * OVERSAMPLING_FACTOR) / outputChannels;
         if (max_frames_per_channel_out_oversampled <= 0) { // Default to a reasonable capacity
            max_frames_per_channel_out_oversampled = 2048 * OVERSAMPLING_FACTOR * 2; // Default capacity, considering oversampling
            LOG_CPP_WARNING("[AudioProc] Warning: Could not determine max_frames_per_channel_out_oversampled, defaulting to %d", max_frames_per_channel_out_oversampled);
        }

        downsamplers.reserve(outputChannels);
        r8brain_downsampler_in_buf.resize(outputChannels);
        for (int i = 0; i < outputChannels; ++i) {
            try {
                downsamplers.push_back(new r8b::CDSPResampler24( // Removed r8brain::
                    static_cast<double>(outputSampleRate * OVERSAMPLING_FACTOR),
                    static_cast<double>(outputSampleRate),
                    max_frames_per_channel_out_oversampled
                ));
                r8brain_downsampler_in_buf[i].resize(max_frames_per_channel_out_oversampled);
            } catch (const std::bad_alloc& e) {
                LOG_CPP_ERROR("[AudioProc] Error allocating r8brain downsampler or buffer for channel %d: %s", i, e.what());
                for (auto ptr : downsamplers) delete ptr;
                downsamplers.clear();
                r8brain_downsampler_in_buf.clear();
                // Also clean up upsamplers if downsampler allocation fails mid-way
                for (auto ptr : upsamplers) delete ptr;
                upsamplers.clear();
                r8brain_upsampler_in_buf.clear();
                return;
            } catch (const std::exception& e) { // Changed to std::exception
                LOG_CPP_ERROR("[AudioProc] Standard exception during downsampler creation for channel %d: %s", i, e.what());
                for (auto ptr : downsamplers) delete ptr;
                downsamplers.clear();
                r8brain_downsampler_in_buf.clear();
                for (auto ptr : upsamplers) delete ptr;
                upsamplers.clear();
                r8brain_upsampler_in_buf.clear();
                return;
            } catch (...) {
                LOG_CPP_ERROR("[AudioProc] Unknown exception during downsampler creation for channel %d", i);
                for (auto ptr : downsamplers) delete ptr;
                downsamplers.clear();
                r8brain_downsampler_in_buf.clear();
                for (auto ptr : upsamplers) delete ptr;
                upsamplers.clear();
                r8brain_upsampler_in_buf.clear();
                return;
            }
        }
    }
}

void AudioProcessor::scaleBuffer() {
    scale_buffer_pos = 0;
    if (inputBitDepth != 16 && inputBitDepth != 24 && inputBitDepth != 32) return;
    
    size_t num_input_samples = (inputBitDepth > 0) ? (CHUNK_SIZE / (inputBitDepth / 8)) : 0;
    if (num_input_samples == 0) return;

    if (scaled_buffer.size() < num_input_samples) {
         try { scaled_buffer.resize(num_input_samples); }
         catch (const std::bad_alloc& e) { LOG_CPP_ERROR("[AudioProc] Error resizing scaled_buffer: %s", e.what()); return; }
    }

    uint8_t* scaled_buffer_as_bytes = reinterpret_cast<uint8_t*>(scaled_buffer.data());
    const uint8_t* receive_ptr = receive_buffer.data();

    for (size_t i = 0; i < num_input_samples; ++i) {
        size_t src_byte_offset = i * (inputBitDepth / 8);
        size_t dest_byte_offset = i * sizeof(int32_t);

        if (dest_byte_offset + sizeof(int32_t) > scaled_buffer.size() * sizeof(int32_t)) break;
        if (src_byte_offset + (inputBitDepth / 8) > receive_buffer.size()) break;

        switch (inputBitDepth) {
        case 16:
            scaled_buffer_as_bytes[dest_byte_offset + 0] = 0; scaled_buffer_as_bytes[dest_byte_offset + 1] = 0;
            scaled_buffer_as_bytes[dest_byte_offset + 2] = receive_ptr[src_byte_offset];
            scaled_buffer_as_bytes[dest_byte_offset + 3] = receive_ptr[src_byte_offset + 1];
            break;
        case 24:
            scaled_buffer_as_bytes[dest_byte_offset + 0] = 0;
            scaled_buffer_as_bytes[dest_byte_offset + 1] = receive_ptr[src_byte_offset];
            scaled_buffer_as_bytes[dest_byte_offset + 2] = receive_ptr[src_byte_offset + 1];
            scaled_buffer_as_bytes[dest_byte_offset + 3] = receive_ptr[src_byte_offset + 2];
            break;
        case 32:
            scaled_buffer_as_bytes[dest_byte_offset + 0] = receive_ptr[src_byte_offset];
            scaled_buffer_as_bytes[dest_byte_offset + 1] = receive_ptr[src_byte_offset + 1];
            scaled_buffer_as_bytes[dest_byte_offset + 2] = receive_ptr[src_byte_offset + 2];
            scaled_buffer_as_bytes[dest_byte_offset + 3] = receive_ptr[src_byte_offset + 3];
            break;
        }
    }
    scale_buffer_pos = num_input_samples; 
}

float AudioProcessor::softClip(float sample) {
    const float threshold = 0.8f;
    const float knee = 0.2f;
    const float kneeStart = threshold - knee / 2.0f;
    const float kneeEnd = threshold + knee / 2.0f;

    float absample = std::abs(sample);
    if (absample <= kneeStart) {
        return sample;
    } else if (absample >= kneeEnd) {
        float sign = (sample > 0) ? 1.0f : -1.0f;
        return sign * (kneeStart + (absample - kneeStart) / (1.0f + std::pow((absample - kneeStart)/(kneeEnd - kneeStart), 2.0f)));
    } else {
        float t = (absample - kneeStart) / knee;
        float sign = (sample > 0) ? 1.0f : -1.0f;
        float smooth_t = t * t * (3.0f - 2.0f * t);
        float clipped_val = sign * (kneeStart + (absample - kneeStart) / (1.0f + std::pow((absample - kneeStart)/(kneeEnd - kneeStart), 2.0f)));
        return sample * (1.0f - smooth_t) + clipped_val * smooth_t;
    }
}

void AudioProcessor::volumeAdjust() {
    float current_vol = current_volume_.load();
    float target_vol = target_volume_.load();

    if (volume_normalization_enabled_) {
        double sum_of_squares = 0.0;
        for (size_t i = 0; i < scale_buffer_pos; ++i) {
            if (i < scaled_buffer.size()) {
                float sample = static_cast<float>(scaled_buffer[i]) / INT32_MAX;
                sum_of_squares += sample * sample;
            }
        }
        double rms = (scale_buffer_pos > 0) ? sqrt(sum_of_squares / scale_buffer_pos) : 0.0;
        
        float target_rms = 0.1; // Target RMS level (e.g., -20 dBFS)
        float gain = (rms > 0) ? target_rms / rms : 1.0;

        // Apply gain with smoothing to avoid clicks
        float attack_smoothing_factor = 0.2f; // Faster attack
        float decay_smoothing_factor = 0.05f; // Slower decay

        for (size_t i = 0; i < scale_buffer_pos; ++i) {
            if (i < scaled_buffer.size()) {
                float smoothing_factor = (gain > current_gain_) ? attack_smoothing_factor : decay_smoothing_factor;
                current_gain_ = current_gain_ * (1.0f - smoothing_factor) + gain * smoothing_factor;
                current_vol += (target_vol - current_vol) * smoothing_factor_;
                float sample = static_cast<float>(scaled_buffer[i]) / INT32_MAX;
                sample *= current_vol * current_gain_;
                sample = softClip(sample);
                scaled_buffer[i] = static_cast<int32_t>(sample * INT32_MAX);
            } else {
                 LOG_CPP_ERROR("[AudioProc] Error: Index out of bounds in volumeAdjust (i=%zu)", i);
                 break;
            }
        }
    } else {
        for (size_t i = 0; i < scale_buffer_pos; ++i) {
            if (i < scaled_buffer.size()) {
                current_vol += (target_vol - current_vol) * smoothing_factor_;
                float sample = static_cast<float>(scaled_buffer[i]) / INT32_MAX;
                sample *= current_vol;
                sample = softClip(sample);
                scaled_buffer[i] = static_cast<int32_t>(sample * INT32_MAX);
            } else {
                 LOG_CPP_ERROR("[AudioProc] Error: Index out of bounds in volumeAdjust (i=%zu)", i);
                 break;
            }
        }
    }
    current_volume_.store(current_vol);
}

void AudioProcessor::resample() {
    // Bypass if no processing needed or rates already match target
    if (!isProcessingRequired() || inputSampleRate == outputSampleRate * OVERSAMPLING_FACTOR) {
        size_t samples_to_copy = scale_buffer_pos;
        if (samples_to_copy > scaled_buffer.size()) {
             LOG_CPP_ERROR("[AudioProc] Error: scale_buffer_pos (%zu) exceeds scaled_buffer size (%zu) in resample bypass.", samples_to_copy, scaled_buffer.size());
             samples_to_copy = scaled_buffer.size();
        }
        if (samples_to_copy > resampled_buffer.capacity()) {
             LOG_CPP_ERROR("[AudioProc] Error: Not enough capacity in resampled_buffer for memcpy. Required: %zu, Capacity: %zu", samples_to_copy, resampled_buffer.capacity());
             resample_buffer_pos = 0; return;
        }
         if (resampled_buffer.size() < samples_to_copy) {
             try { resampled_buffer.resize(samples_to_copy); }
             catch (const std::bad_alloc& e) { LOG_CPP_ERROR("[AudioProc] Error resizing resampled_buffer: %s", e.what()); resample_buffer_pos = 0; return; }
         }
        memcpy(resampled_buffer.data(), scaled_buffer.data(), samples_to_copy * sizeof(int32_t));
        resample_buffer_pos = samples_to_copy;
        return;
    }

    // Proceed with resampling
    if (upsamplers.empty() || inputChannels <= 0 || inputSampleRate <= 0 || outputSampleRate <= 0) {
         LOG_CPP_ERROR("[AudioProc] Error: Upsamplers not initialized or invalid channels/rate for resampling.");
         resample_buffer_pos = 0; return;
    }

    if (scale_buffer_pos == 0) {
        resample_buffer_pos = 0;
        return;
    }
    
    size_t num_input_frames = scale_buffer_pos / inputChannels;
    if (num_input_frames == 0) { resample_buffer_pos = 0; return; }

    // De-interleave and convert to double for r8brain
    for (int ch = 0; ch < inputChannels; ++ch) {
        if (r8brain_upsampler_in_buf[ch].size() < num_input_frames) {
            try {
                r8brain_upsampler_in_buf[ch].resize(num_input_frames);
            } catch (const std::bad_alloc& e) {
                LOG_CPP_ERROR("[AudioProc] Error resizing r8brain_upsampler_in_buf for channel %d: %s", ch, e.what());
                resample_buffer_pos = 0; return;
            }
        }
        for (size_t frame = 0; frame < num_input_frames; ++frame) {
            if ((frame * inputChannels + ch) < scaled_buffer.size()) {
                 r8brain_upsampler_in_buf[ch][frame] = static_cast<double>(scaled_buffer[frame * inputChannels + ch]) / 2147483647.0; // INT32_MAX
            } else {
                LOG_CPP_ERROR("[AudioProc] Error: Out of bounds access in resample data preparation.");
                resample_buffer_pos = 0; return;
            }
        }
    }
    
    std::vector<double*> r8brain_output_ptrs(inputChannels);
    int output_frames_generated = 0; 

    for (int ch = 0; ch < inputChannels; ++ch) {
        if (ch < upsamplers.size() && upsamplers[ch] != nullptr) {
            try {
                // The third argument to process is a reference to a double*, 
                // r8brain will set this pointer to its internal buffer.
                output_frames_generated = upsamplers[ch]->process(r8brain_upsampler_in_buf[ch].data(), num_input_frames, r8brain_output_ptrs[ch]);
                if (output_frames_generated < 0) { // r8brain might return negative on error
                    LOG_CPP_ERROR("[AudioProc] r8brain upsampling error on channel %d. Code: %d", ch, output_frames_generated);
                    resample_buffer_pos = 0; return;
                }
            } catch (const std::exception& e) { // Changed to std::exception
                LOG_CPP_ERROR("[AudioProc] Standard exception during upsampling on channel %d: %s", ch, e.what());
                resample_buffer_pos = 0; return;
            } catch (...) {
                LOG_CPP_ERROR("[AudioProc] Unknown exception during upsampling on channel %d", ch);
                resample_buffer_pos = 0; return;
            }
        } else {
            LOG_CPP_ERROR("[AudioProc] Error: Upsampler for channel %d is null or out of bounds.", ch);
            resample_buffer_pos = 0; return;
        }
    }
    
    size_t total_output_samples = static_cast<size_t>(output_frames_generated) * inputChannels;
    if (resampled_buffer.size() < total_output_samples) {
        try {
            resampled_buffer.resize(total_output_samples);
        } catch (const std::bad_alloc& e) {
            LOG_CPP_ERROR("[AudioProc] Error resizing resampled_buffer: %s", e.what());
            resample_buffer_pos = 0; return;
        }
    }
    
    // Interleave and convert back to int32_t
    for (int frame = 0; frame < output_frames_generated; ++frame) {
        for (int ch = 0; ch < inputChannels; ++ch) {
            if (r8brain_output_ptrs[ch] != nullptr) {
                double sample_double = r8brain_output_ptrs[ch][frame];
                sample_double = std::max(-1.0, std::min(1.0, sample_double)); // Clipping
                resampled_buffer[frame * inputChannels + ch] = static_cast<int32_t>(sample_double * 2147483647.0);
            } else {
                 LOG_CPP_ERROR("[AudioProc] Error: r8brain output pointer for channel %d is null after processing.", ch);
                 resample_buffer_pos = 0; return;
            }
        }
    }
    resample_buffer_pos = total_output_samples;
}


void AudioProcessor::downsample() {
     // Bypass if no processing needed or rates already match final target
     if (!isProcessingRequired() || outputSampleRate * OVERSAMPLING_FACTOR == outputSampleRate) {
         size_t samples_to_copy = merged_buffer_pos;
         if (samples_to_copy > merged_buffer.size()) {
              LOG_CPP_ERROR("[AudioProc] Error: merged_buffer_pos (%zu) exceeds merged_buffer size (%zu) in downsample bypass.", samples_to_copy, merged_buffer.size());
              samples_to_copy = merged_buffer.size();
         }
         if (samples_to_copy > processed_buffer.capacity()) {
              LOG_CPP_ERROR("[AudioProc] Error: Not enough capacity in processed_buffer for memcpy. Required: %zu, Capacity: %zu", samples_to_copy, processed_buffer.capacity());
              process_buffer_pos = 0; return;
         }
          if (processed_buffer.size() < samples_to_copy) {
              try { processed_buffer.resize(samples_to_copy); }
              catch (const std::bad_alloc& e) { LOG_CPP_ERROR("[AudioProc] Error resizing processed_buffer: %s", e.what()); process_buffer_pos = 0; return; }
          }
         memcpy(processed_buffer.data(), merged_buffer.data(), samples_to_copy * sizeof(int32_t));
         process_buffer_pos = samples_to_copy;
         return;
     }

    // Proceed with downsampling
    if (downsamplers.empty() || outputChannels <= 0 || outputSampleRate <= 0) {
         LOG_CPP_ERROR("[AudioProc] Error: Downsamplers not initialized or invalid channels/rate for downsampling.");
         process_buffer_pos = 0; return;
    }

    if (merged_buffer_pos == 0) {
        process_buffer_pos = 0;
        return;
    }

    size_t num_input_frames_for_downsample = merged_buffer_pos / outputChannels;
    if (num_input_frames_for_downsample == 0) { process_buffer_pos = 0; return; }

    // De-interleave and convert to double for r8brain
    for (int ch = 0; ch < outputChannels; ++ch) {
        if (r8brain_downsampler_in_buf[ch].size() < num_input_frames_for_downsample) {
            try {
                r8brain_downsampler_in_buf[ch].resize(num_input_frames_for_downsample);
            } catch (const std::bad_alloc& e) {
                LOG_CPP_ERROR("[AudioProc] Error resizing r8brain_downsampler_in_buf for channel %d: %s", ch, e.what());
                process_buffer_pos = 0; return;
            }
        }
        for (size_t frame = 0; frame < num_input_frames_for_downsample; ++frame) {
            if ((frame * outputChannels + ch) < merged_buffer.size()) {
                r8brain_downsampler_in_buf[ch][frame] = static_cast<double>(merged_buffer[frame * outputChannels + ch]) / 2147483647.0; // INT32_MAX
            } else {
                LOG_CPP_ERROR("[AudioProc] Error: Out of bounds access in downsample data preparation.");
                process_buffer_pos = 0; return;
            }
        }
    }

    std::vector<double*> r8brain_output_ptrs_down(outputChannels);
    int final_output_frames_generated = 0;

    for (int ch = 0; ch < outputChannels; ++ch) {
        if (ch < downsamplers.size() && downsamplers[ch] != nullptr) {
            try {
                final_output_frames_generated = downsamplers[ch]->process(r8brain_downsampler_in_buf[ch].data(), num_input_frames_for_downsample, r8brain_output_ptrs_down[ch]);
                 if (final_output_frames_generated < 0) { // r8brain might return negative on error
                    LOG_CPP_ERROR("[AudioProc] r8brain downsampling error on channel %d. Code: %d", ch, final_output_frames_generated);
                    process_buffer_pos = 0; return;
                }
            } catch (const std::exception& e) { // Changed to std::exception
                LOG_CPP_ERROR("[AudioProc] Standard exception during downsampling on channel %d: %s", ch, e.what());
                process_buffer_pos = 0; return;
            } catch (...) {
                LOG_CPP_ERROR("[AudioProc] Unknown exception during downsampling on channel %d", ch);
                process_buffer_pos = 0; return;
            }
        } else {
            LOG_CPP_ERROR("[AudioProc] Error: Downsampler for channel %d is null or out of bounds.", ch);
            process_buffer_pos = 0; return;
        }
    }

    size_t total_final_output_samples = static_cast<size_t>(final_output_frames_generated) * outputChannels;
    if (processed_buffer.size() < total_final_output_samples) {
        try {
            processed_buffer.resize(total_final_output_samples);
        } catch (const std::bad_alloc& e) {
            LOG_CPP_ERROR("[AudioProc] Error resizing processed_buffer for downsampling: %s", e.what());
            process_buffer_pos = 0; return;
        }
    }

    // Interleave and convert back to int32_t
    for (int frame = 0; frame < final_output_frames_generated; ++frame) {
        for (int ch = 0; ch < outputChannels; ++ch) {
             if (r8brain_output_ptrs_down[ch] != nullptr) {
                double sample_double = r8brain_output_ptrs_down[ch][frame];
                sample_double = std::max(-1.0, std::min(1.0, sample_double)); // Clipping
                processed_buffer[frame * outputChannels + ch] = static_cast<int32_t>(sample_double * 2147483647.0);
            } else {
                 LOG_CPP_ERROR("[AudioProc] Error: r8brain output pointer for channel %d is null after downsampling.", ch);
                 process_buffer_pos = 0; return;
            }
        }
    }
    process_buffer_pos = total_final_output_samples;
}


void AudioProcessor::splitBufferToChannels() {
    if (inputChannels <= 0 || resample_buffer_pos == 0) { 
        channel_buffer_pos = 0; 
        // Optionally clear channel_buffers if needed
        // for(auto& vec : channel_buffers) { vec.clear(); } 
        return; 
    } 
    
    size_t num_frames = resample_buffer_pos / inputChannels;
    // Ensure integer division didn't truncate useful data if resample_buffer_pos wasn't perfectly divisible
    if (resample_buffer_pos % inputChannels != 0) {
         LOG_CPP_WARNING("[AudioProc] Warning: resample_buffer_pos (%zu) not perfectly divisible by inputChannels (%d) in splitBufferToChannels.",
                         resample_buffer_pos, inputChannels);
         // Adjust num_frames or handle incomplete frame? For now, proceed with truncated num_frames.
    }
    channel_buffer_pos = num_frames;

    // Ensure inner vectors are large enough
    for(int ch = 0; ch < inputChannels; ++ch) {
         if (static_cast<size_t>(ch) >= channel_buffers.size()) {
              LOG_CPP_ERROR("[AudioProc] Error: Channel index out of bounds for channel_buffers.");
              channel_buffer_pos = 0; return; // Major error
         }
        if (channel_buffers[ch].size() < num_frames) {
            try { channel_buffers[ch].resize(num_frames); }
            catch (const std::bad_alloc& e) {
                LOG_CPP_ERROR("[AudioProc] Error resizing channel_buffers[%d]: %s", ch, e.what());
                channel_buffer_pos = 0; return;
            }
        }
    }

    for (size_t i = 0; i < resample_buffer_pos; ++i) {
        size_t channel = i % inputChannels;
        size_t pos_in_channel = i / inputChannels;
        
        // Bounds checks (redundant due to prior resize and loop condition, but safe)
        if (channel < channel_buffers.size() &&
            pos_in_channel < channel_buffers[channel].size() &&
            i < resampled_buffer.size())
        {
            channel_buffers[channel][pos_in_channel] = resampled_buffer[i];
        } else {
             // This should ideally not happen if resizing and loop bounds are correct
             LOG_CPP_ERROR("[AudioProc] Error: Out of bounds access detected in splitBufferToChannels loop. i=%zu (resample_buffer_pos=%zu, resampled_buffer.size=%zu), channel=%zu (inputChannels=%d, channel_buffers.size=%zu), pos_in_channel=%zu (num_frames=%zu)",
                           i, resample_buffer_pos, resampled_buffer.size(), channel, inputChannels, channel_buffers.size(), pos_in_channel, num_frames);
             // If this error occurs, it indicates a problem in preceding steps (like resample) or the resize logic here.
             // Stop processing this chunk to prevent further issues.
             channel_buffer_pos = 0; // Mark as having processed 0 valid frames
             return;
        }
    }
}

void AudioProcessor::applyCustomSpeakerMix(const std::vector<std::vector<float>>& custom_matrix) {
    LOG_CPP_INFO("[AudioProc] applyCustomSpeakerMix called.");
    // Clear the existing speaker_mix
    memset(speaker_mix, 0, sizeof(speaker_mix));

    // Assuming custom_matrix is 8x8 and speaker_mix is MAX_CHANNELS x MAX_CHANNELS (where MAX_CHANNELS is 8)
    LOG_CPP_INFO("[AudioProc]   Applying custom matrix to internal speaker_mix[][]:");
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        std::string row_str_log = "[AudioProc]     Row " + std::to_string(i) + ": ";
        if (static_cast<size_t>(i) < custom_matrix.size()) { // Check row bounds for custom_matrix
            for (int j = 0; j < MAX_CHANNELS; ++j) {
                if (static_cast<size_t>(j) < custom_matrix[i].size()) { // Check column bounds for custom_matrix
                    if (i < MAX_CHANNELS && j < MAX_CHANNELS) { // Check bounds for speaker_mix
                         speaker_mix[i][j] = custom_matrix[i][j];
                         char val_buf_log[10];
                         snprintf(val_buf_log, sizeof(val_buf_log), "%.2f ", speaker_mix[i][j]);
                         row_str_log += val_buf_log;
                    }
                } else {
                    // Handle case where custom_matrix[i] is smaller than MAX_CHANNELS (pad with 0, already done by memset)
                    row_str_log += "0.00(pad) ";
                }
            }
        } else {
            // Handle case where custom_matrix has fewer rows than MAX_CHANNELS (pad with 0, already done by memset)
             for (int j = 0; j < MAX_CHANNELS; ++j) row_str_log += "0.00(pad) ";
        }
        LOG_CPP_INFO("%s", row_str_log.c_str());
    }
    // After applying, we might need to re-evaluate if processing is required,
    // so clear the cache.
    isProcessingRequiredCacheSet = false;
}

// void AudioProcessor::updateSpeakerMix() {
void AudioProcessor::calculateAndApplyAutoSpeakerMix() { // Renamed
    LOG_CPP_INFO("[AudioProc] calculateAndApplyAutoSpeakerMix called for inputChannels=%d, outputChannels=%d.", inputChannels, outputChannels);
    // Fills out the speaker mix table speaker_mix[][] with the current configuration.
    memset(speaker_mix, 0, sizeof(speaker_mix));
    // speaker_mix[input channel][output channel] = gain;
    // Ex: To map Left on Stereo to Right on Stereo at half volume you would do:
    // speaker_mix[0][1] = .5;
    switch (inputChannels)
    {
    case 1: // Mono, Ch 0: Left
        // Mono -> All
        for (int output_channel = 0; output_channel < outputChannels && output_channel < MAX_CHANNELS; output_channel++) // Write the left (first) speaker to every channel
            speaker_mix[0][output_channel] = 1;
        break;
    case 2: // Stereo, Ch 0: Left, Ch 1: Right
        switch (outputChannels)
        {
        case 1:                     // Stereo -> Mono
            speaker_mix[0][0] = 0.5f; // Left to mono .5 vol
            speaker_mix[1][0] = 0.5f; // Right to mono .5 vol
            break;
        case 2:                    // Stereo -> Stereo
            speaker_mix[0][0] = 1.0f; // Left to Left
            speaker_mix[1][1] = 1.0f; // Right to Right
            break;
        case 4: // Stereo -> Quad
            speaker_mix[0][0] = 1.0f; // Left to Front Left
            speaker_mix[1][1] = 1.0f; // Right to Front Right
            speaker_mix[0][2] = 1.0f; // Left to Back Left
            speaker_mix[1][3] = 1.0f; // Right to Back Right
            break;
        case 6: // Stereo -> 5.1 Surround
            // FL FR C LFE BL BR - Assuming standard 5.1 layout indices
            speaker_mix[0][0] = 1.0f;  // Left to Front Left
            speaker_mix[1][1] = 1.0f;  // Right to Front Right
            speaker_mix[0][2] = 0.5f; // Left to Center Half Vol
            speaker_mix[1][2] = 0.5f; // Right to Center Half Vol
            // speaker_mix[0][3] = 0.5f; // LFE - Typically derived or silent from stereo
            // speaker_mix[1][3] = 0.5f; 
            speaker_mix[0][4] = 1.0f;  // Left to Rear Left (or Side Left)
            speaker_mix[1][5] = 1.0f;  // Right to Rear Right (or Side Right)
            break;
        case 8: // Stereo -> 7.1 Surround
            // FL FR C LFE BL BR SL SR - Assuming standard 7.1 layout indices
            speaker_mix[0][0] = 1.0f;  // Left to Front Left
            speaker_mix[1][1] = 1.0f;  // Right to Front Right
            speaker_mix[0][2] = 0.5f; // Left to Center Half Vol
            speaker_mix[1][2] = 0.5f; // Right to Center Half Vol
            // speaker_mix[0][3] = 0.5f; // LFE
            // speaker_mix[1][3] = 0.5f; 
            speaker_mix[0][4] = 1.0f;  // Left to Rear Left
            speaker_mix[1][5] = 1.0f;  // Right to Rear Right
            speaker_mix[0][6] = 1.0f;  // Left to Side Left
            speaker_mix[1][7] = 1.0f;  // Right to Side Right
            break;
        }
        break;
    case 4: // Quad Input
        switch (outputChannels)
        {
        case 1:                      // Quad -> Mono
            speaker_mix[0][0] = 0.25f; // Front Left to Mono
            speaker_mix[1][0] = 0.25f; // Front Right to Mono
            speaker_mix[2][0] = 0.25f; // Rear Left to Mono
            speaker_mix[3][0] = 0.25f; // Rear Right to Mono
            break;
        case 2:                     // Quad -> Stereo
            speaker_mix[0][0] = 0.5f; // Front Left to Left
            speaker_mix[1][1] = 0.5f; // Front Right to Right
            speaker_mix[2][0] = 0.5f; // Rear Left to Left
            speaker_mix[3][1] = 0.5f; // Rear Right to Right
            break;
        case 4: // Quad -> Quad
            speaker_mix[0][0] = 1.0f; // Front Left to Front Left
            speaker_mix[1][1] = 1.0f; // Front Right to Front Right
            speaker_mix[2][2] = 1.0f; // Rear Left to Rear Left
            speaker_mix[3][3] = 1.0f; // Rear Right to Rear Right
            break;
        case 6: // Quad -> 5.1 Surround
            speaker_mix[0][0] = 1.0f;   // Front Left to Front Left
            speaker_mix[1][1] = 1.0f;   // Front Right to Front Right
            speaker_mix[0][2] = 0.5f;  // Front Left to Center
            speaker_mix[1][2] = 0.5f;  // Front Right to Center
            speaker_mix[0][3] = 0.25f; // Front Left to LFE
            speaker_mix[1][3] = 0.25f; // Front Right to LFE
            speaker_mix[2][3] = 0.25f; // Rear Left to LFE
            speaker_mix[3][3] = 0.25f; // Rear Right to LFE
            speaker_mix[2][4] = 1.0f;   // Rear Left to Rear Left
            speaker_mix[3][5] = 1.0f;   // Rear Right to Rear Right
            break;
        case 8: // Quad -> 7.1 Surround
            speaker_mix[0][0] = 1.0f;   // Front Left to Front Left
            speaker_mix[1][1] = 1.0f;   // Front Right to Front Right
            speaker_mix[0][2] = 0.5f;  // Front Left to Center
            speaker_mix[1][2] = 0.5f;  // Front Right to Center
            speaker_mix[0][3] = 0.25f; // Front Left to LFE
            speaker_mix[1][3] = 0.25f; // Front Right to LFE
            speaker_mix[2][3] = 0.25f; // Rear Left to LFE
            speaker_mix[3][3] = 0.25f; // Rear Right to LFE
            speaker_mix[2][4] = 1.0f;   // Rear Left to Rear Left
            speaker_mix[3][5] = 1.0f;   // Rear Right to Rear Right
            speaker_mix[0][6] = 0.5f;  // Front Left to Side Left
            speaker_mix[1][7] = 0.5f;  // Front Right to Side Right
            speaker_mix[2][6] = 0.5f;  // Rear Left to Side Left
            speaker_mix[3][7] = 0.5f;  // Rear Right to Side Right
            break;
        }
        break;
    case 6: // 5.1 Input
        switch (outputChannels)
        {
        case 1:                     // 5.1 Surround -> Mono
            speaker_mix[0][0] = 0.2f; // Front Left to Mono
            speaker_mix[1][0] = 0.2f; // Front Right to Mono
            speaker_mix[2][0] = 0.2f; // Center to Mono
            speaker_mix[4][0] = 0.2f; // Rear Left to Mono (Assuming 5.1 layout: FL, FR, C, LFE, RL, RR)
            speaker_mix[5][0] = 0.2f; // Rear Right to Mono
            break;
        case 2:                      // 5.1 Surround -> Stereo
            speaker_mix[0][0] = 0.33f; // Front Left to Left
            speaker_mix[1][1] = 0.33f; // Front Right to Right
            speaker_mix[2][0] = 0.33f; // Center to Left
            speaker_mix[2][1] = 0.33f; // Center to Right
            speaker_mix[4][0] = 0.33f; // Rear Left to Left
            speaker_mix[5][1] = 0.33f; // Rear Right to Right
            break;
        case 4: // 5.1 -> Quad
            speaker_mix[0][0] = 0.66f; // Front Left to Front Left
            speaker_mix[1][1] = 0.66f; // Front Right to Front Right
            speaker_mix[2][0] = 0.33f; // Center to Front Left
            speaker_mix[2][1] = 0.33f; // Center to Front Right
            speaker_mix[4][2] = 1.0f;   // Rear Left to Rear Left
            speaker_mix[5][3] = 1.0f;   // Rear Right to Rear Right
            break;
        case 6: // 5.1 Surround -> 5.1 Surround
            speaker_mix[0][0] = 1.0f; // Front Left to Front Left
            speaker_mix[1][1] = 1.0f; // Front Right to Front Right
            speaker_mix[2][2] = 1.0f; // Center to Center
            speaker_mix[3][3] = 1.0f; // LFE to LFE
            speaker_mix[4][4] = 1.0f; // Rear Left to Rear Left
            speaker_mix[5][5] = 1.0f; // Rear Right to Rear Right
            break;
        case 8: // 5.1 Surround -> 7.1 Surround
            speaker_mix[0][0] = 1.0f;  // Front Left to Front Left
            speaker_mix[1][1] = 1.0f;  // Front Right to Front Right
            speaker_mix[2][2] = 1.0f;  // Center to Center
            speaker_mix[3][3] = 1.0f;  // LFE to LFE
            speaker_mix[4][4] = 1.0f;  // Rear Left to Rear Left
            speaker_mix[5][5] = 1.0f;  // Rear Right to Rear Right
            speaker_mix[0][6] = 0.5f; // Front Left to Side Left
            speaker_mix[1][7] = 0.5f; // Front Right to Side Right
            speaker_mix[4][6] = 0.5f; // Rear Left to Side Left
            speaker_mix[5][7] = 0.5f; // Rear Right to Side Right
            break;
        }
        break;
    case 8: // 7.1 Input
        switch (outputChannels)
        {
        case 1:                              // 7.1 Surround -> Mono
            speaker_mix[0][0] = 1.0f / 7.0f; // Front Left to Mono
            speaker_mix[1][0] = 1.0f / 7.0f; // Front Right to Mono
            speaker_mix[2][0] = 1.0f / 7.0f; // Center to Mono
            speaker_mix[4][0] = 1.0f / 7.0f; // Rear Left to Mono (Assuming 7.1 layout: FL, FR, C, LFE, RL, RR, SL, SR)
            speaker_mix[5][0] = 1.0f / 7.0f; // Rear Right to Mono
            speaker_mix[6][0] = 1.0f / 7.0f; // Side Left to Mono
            speaker_mix[7][0] = 1.0f / 7.0f; // Side Right to Mono
            break;
        case 2:                       // 7.1 Surround -> Stereo
            speaker_mix[0][0] = 0.5f;   // Front Left to Left
            speaker_mix[1][1] = 0.5f;   // Front Right to Right
            speaker_mix[2][0] = 0.25f;  // Center to Left
            speaker_mix[2][1] = 0.25f;  // Center to Right
            speaker_mix[4][0] = 0.125f; // Rear Left to Left
            speaker_mix[5][1] = 0.125f; // Rear Right to Right
            speaker_mix[6][0] = 0.125f; // Side Left to Left
            speaker_mix[7][1] = 0.125f; // Side Right to Right
            break;
        case 4:                      // 7.1 Surround -> Quad
            speaker_mix[0][0] = 0.5f;  // Front Left to Front Left
            speaker_mix[1][1] = 0.5f;  // Front Right to Front Right
            speaker_mix[2][0] = 0.25f; // Center to Front Left
            speaker_mix[2][1] = 0.25f; // Center to Front Right
            speaker_mix[4][2] = 0.66f; // Rear Left to Rear Left
            speaker_mix[5][3] = 0.66f; // Rear Right to Rear Right
            speaker_mix[6][0] = 0.25f; // Side Left to Front Left
            speaker_mix[7][1] = 0.25f; // Side Right to Front Right // Corrected index from 7 to 1
            speaker_mix[6][2] = 0.33f; // Side Left to Rear Left
            speaker_mix[7][3] = 0.33f; // Side Right to Rear Right // Corrected index from 7 to 3
            break;
        case 6: // 7.1 Surround -> 5.1 Surround
            speaker_mix[0][0] = 0.66f; // Front Left to Front Left
            speaker_mix[1][1] = 0.66f; // Front Right to Front Right
            speaker_mix[2][2] = 1.0f;   // Center to Center
            speaker_mix[3][3] = 1.0f;   // LFE to LFE
            speaker_mix[4][4] = 0.66f; // Rear Left to Rear Left
            speaker_mix[5][5] = 0.66f; // Rear Right to Rear Right
            speaker_mix[6][0] = 0.33f; // Side Left to Front Left
            speaker_mix[7][1] = 0.33f; // Side Right to Front Right
            speaker_mix[6][4] = 0.33f; // Side Left to Rear Left
            speaker_mix[7][5] = 0.33f; // Side Right to Rear Right
            break;
        case 8: // 7.1 Surround -> 7.1 Surround
            speaker_mix[0][0] = 1.0f; // Front Left to Front Left
            speaker_mix[1][1] = 1.0f; // Front Right to Front Right
            speaker_mix[2][2] = 1.0f; // Center to Center
            speaker_mix[3][3] = 1.0f; // LFE to LFE
            speaker_mix[4][4] = 1.0f; // Rear Left to Rear Left
            speaker_mix[5][5] = 1.0f; // Rear Right to Rear Right
            speaker_mix[6][6] = 1.0f; // Side Left to Side Left
            speaker_mix[7][7] = 1.0f; // Side Right to Side Right
            break;
        }
        break;
    default: // Fallback for unsupported input channel counts
         // Default to identity mapping for common channels if possible
         int min_ch_default = std::min(inputChannels, outputChannels);
         min_ch_default = std::min(min_ch_default, static_cast<int>(MAX_CHANNELS));
         for(int i = 0; i < min_ch_default; ++i) {
             speaker_mix[i][i] = 1.0f;
         }
         LOG_CPP_WARNING("[AudioProc] Warning: Unsupported input channel count (%d) in calculateAndApplyAutoSpeakerMix. Using basic identity mapping.", inputChannels);
         break;
    }
    // isProcessingRequiredCacheSet = false; // select_active_speaker_mix will handle this
}

// --- New/Updated Methods for Speaker Layouts ---

void AudioProcessor::update_speaker_layouts_config(const std::map<int, screamrouter::audio::CppSpeakerLayout>& new_layouts_config) { // Changed to audio namespace
    LOG_CPP_INFO("[AudioProc] update_speaker_layouts_config called. Received %zu layout entries.", new_layouts_config.size());
    for (const auto& pair : new_layouts_config) {
        LOG_CPP_INFO("[AudioProc]   New layout for %dch input: auto_mode=%s", pair.first, (pair.second.auto_mode ? "true" : "false"));
        if (!pair.second.auto_mode) {
            LOG_CPP_INFO("[AudioProc]     Matrix:");
            for (const auto& row : pair.second.matrix) {
                std::string row_str_log = "[AudioProc]       ";
                for (float val : row) {
                    char val_buf_log[10];
                    snprintf(val_buf_log, sizeof(val_buf_log), "%.2f ", val);
                    row_str_log += val_buf_log;
                }
                LOG_CPP_INFO("%s", row_str_log.c_str());
            }
        }
    }

    std::lock_guard<std::mutex> lock(speaker_layouts_config_mutex_);
    speaker_layouts_config_ = new_layouts_config;
    // After updating the config, re-select the active mix by calling the _locked version
    select_active_speaker_mix_locked();
    // select_active_speaker_mix_locked will call isProcessingRequiredCacheSet = false;
}

// Public method that acquires the lock
void AudioProcessor::select_active_speaker_mix() {
    LOG_CPP_INFO("[AudioProc] select_active_speaker_mix called for current inputChannels=%d.", this->inputChannels);
    std::lock_guard<std::mutex> lock(speaker_layouts_config_mutex_); // Protect read access to map
    select_active_speaker_mix_locked();
}

// Private method that assumes the lock is already held
void AudioProcessor::select_active_speaker_mix_locked() {
    LOG_CPP_INFO("[AudioProc] select_active_speaker_mix_locked called for current inputChannels=%d.", this->inputChannels);
    // No lock acquisition here, as it's assumed to be held by the caller
    
    auto it = speaker_layouts_config_.find(this->inputChannels);
    bool specific_layout_applied = false;

    if (it != speaker_layouts_config_.end()) {
        const screamrouter::audio::CppSpeakerLayout& layout_for_current_input = it->second; // Changed to audio namespace
        LOG_CPP_INFO("[AudioProc]   Found layout for %dch input. auto_mode=%s.",
                     this->inputChannels, (layout_for_current_input.auto_mode ? "true" : "false"));
        if (layout_for_current_input.auto_mode) {
            LOG_CPP_INFO("[AudioProc]   Using AUTO speaker mix for %d input channels.", this->inputChannels);
            calculateAndApplyAutoSpeakerMix(); // This method sets the internal speaker_mix[][]
        } else {
            LOG_CPP_INFO("[AudioProc]   Using CUSTOM speaker matrix for %d input channels.", this->inputChannels);
            LOG_CPP_INFO("[AudioProc]     Provided Matrix from config:");
            for (const auto& row : layout_for_current_input.matrix) {
                std::string row_str_log = "[AudioProc]       ";
                for (float val : row) {
                    char val_buf_log[10];
                    snprintf(val_buf_log, sizeof(val_buf_log), "%.2f ", val);
                    row_str_log += val_buf_log;
                }
                LOG_CPP_INFO("%s", row_str_log.c_str());
            }
            // Validate matrix dimensions before applying
            if (layout_for_current_input.matrix.size() == MAX_CHANNELS &&
                !layout_for_current_input.matrix.empty() &&
                layout_for_current_input.matrix[0].size() == MAX_CHANNELS) {
                applyCustomSpeakerMix(layout_for_current_input.matrix); // This method sets speaker_mix[][]
            } else {
                LOG_CPP_ERROR("[AudioProc] Error: Custom matrix for %d input channels has invalid dimensions (%zu x %zu). Falling back to auto mix.",
                              this->inputChannels, layout_for_current_input.matrix.size(),
                              (layout_for_current_input.matrix.empty() ? 0 : layout_for_current_input.matrix[0].size()));
                calculateAndApplyAutoSpeakerMix();
            }
        }
        specific_layout_applied = true;
    }

    if (!specific_layout_applied) {
        LOG_CPP_INFO("[AudioProc]   No specific layout found for %d input channels in speaker_layouts_config_. Defaulting to AUTO mix.", this->inputChannels);
        calculateAndApplyAutoSpeakerMix(); // Default if no entry for current inputChannels
    }
    
    isProcessingRequiredCacheSet = false; // The effective mix might have changed, so invalidate cache
}

// --- End New/Updated Methods ---
// void AudioProcessor::mixSpeakers() { // Original line before potential extra brace
void AudioProcessor::mixSpeakers() {
    for (size_t oc = 0; oc < static_cast<size_t>(outputChannels); ++oc) {
        if (oc >= remixed_channel_buffers.size()) continue;
        for (size_t pos = 0; pos < channel_buffer_pos; ++pos) {
            if (pos >= remixed_channel_buffers[oc].size()) continue; 
            
            float mixed_sample_float = 0.0f;
            for (size_t ic = 0; ic < static_cast<size_t>(inputChannels); ++ic) {
                // Check bounds for speaker_mix access
                if (ic < MAX_CHANNELS && oc < MAX_CHANNELS && 
                    ic < channel_buffers.size() && pos < channel_buffers[ic].size()) 
                {
                     mixed_sample_float += static_cast<float>(channel_buffers[ic][pos]) * speaker_mix[ic][oc];
                }
            }
            mixed_sample_float = softClip(mixed_sample_float / INT32_MAX); 
            remixed_channel_buffers[oc][pos] = static_cast<int32_t>(mixed_sample_float * INT32_MAX);
        }
    }
}


void AudioProcessor::equalize() {
    bool active_bands[EQ_BANDS] = {false};
    bool has_active_bands = false;
    for (int i = 0; i < EQ_BANDS; ++i) {
        if (eq[i] != 1.0f) {
            active_bands[i] = true;
            has_active_bands = true;
        }
    }
    if (!has_active_bands) return;

    size_t safe_temp_buffer_size = channel_buffer_pos; // Start with needed size
    if (safe_temp_buffer_size == 0) return; // Nothing to process

    // Ensure temp buffers are allocated safely
    std::vector<float> temp_float_buffer;
    std::vector<float> temp_processed_buffer;
    try {
        temp_float_buffer.resize(safe_temp_buffer_size);
        temp_processed_buffer.resize(safe_temp_buffer_size);
    } catch (const std::bad_alloc& e) {
        LOG_CPP_ERROR("[AudioProc] Error allocating temporary buffers in equalize: %s", e.what());
        return;
    }


    for (int ch = 0; ch < outputChannels; ++ch) {
        if (static_cast<size_t>(ch) >= remixed_channel_buffers.size() || !filters[ch][0]) continue; // Check if channel exists and filters allocated

        size_t current_channel_size = remixed_channel_buffers[ch].size();
        size_t safe_process_len = std::min(channel_buffer_pos, current_channel_size); // Process only available samples
        
        if (safe_process_len == 0) continue;
        if (safe_process_len > temp_float_buffer.size()) { // Double check against temp buffer size
             LOG_CPP_ERROR("[AudioProc] Error: safe_process_len (%zu) exceeds temporary buffer size (%zu) in equalize.", safe_process_len, temp_float_buffer.size());
             continue;
        }

        for (size_t pos = 0; pos < safe_process_len; ++pos) {
            temp_float_buffer[pos] = static_cast<float>(remixed_channel_buffers[ch][pos]) / INT32_MAX;
        }

        memcpy(temp_processed_buffer.data(), temp_float_buffer.data(), safe_process_len * sizeof(float));
        
        for (int band = 0; band < EQ_BANDS; ++band) {
            if (active_bands[band] && filters[ch][band]) {
                filters[ch][band]->processBlock(temp_processed_buffer.data(), temp_processed_buffer.data(), safe_process_len);
            }
        }

        for (size_t pos = 0; pos < safe_process_len; ++pos) {
            float sample = temp_processed_buffer[pos];
            sample = softClip(sample);
            remixed_channel_buffers[ch][pos] = static_cast<int32_t>(sample * INT32_MAX);
        }
    }
} 


void AudioProcessor::mergeChannelsToBuffer() {
    merged_buffer_pos = 0; 
    if (outputChannels <= 0 || channel_buffer_pos == 0) return;
    
    size_t required_merged_size = channel_buffer_pos * outputChannels;
    
    if (merged_buffer.capacity() < required_merged_size) {
         try { merged_buffer.reserve(required_merged_size); }
         catch (const std::bad_alloc& e) { LOG_CPP_ERROR("[AudioProc] Error reserving merged_buffer: %s", e.what()); return; }
    }
    if (merged_buffer.size() < required_merged_size) {
         try { merged_buffer.resize(required_merged_size); }
         catch (const std::bad_alloc& e) { LOG_CPP_ERROR("[AudioProc] Error resizing merged_buffer: %s", e.what()); return; }
    }

    for (size_t pos = 0; pos < channel_buffer_pos; ++pos) {
        for (int ch = 0; ch < outputChannels; ++ch) {
            int32_t sample_to_write = 0; 
            if (static_cast<size_t>(ch) < remixed_channel_buffers.size() && 
                pos < remixed_channel_buffers[ch].size()) 
            {
                sample_to_write = remixed_channel_buffers[ch][pos];
            } 
            
            if (merged_buffer_pos < merged_buffer.size()) { 
                 merged_buffer[merged_buffer_pos] = sample_to_write;
            } else {
                 LOG_CPP_ERROR("[AudioProc] Error: Write attempt past merged_buffer bounds (pos=%zu, size=%zu)", merged_buffer_pos, merged_buffer.size());
                 return;
            }
            merged_buffer_pos++;
        }
    }
}


void AudioProcessor::noiseShapingDither() {
    if (process_buffer_pos == 0) return; // Nothing to process

    const float ditherAmplitude = (inputBitDepth > 0) ? (1.0f / (static_cast<unsigned long long>(1) << (inputBitDepth - 1))) : 0.0f;
    const float shapingFactor = 0.25f;
    static float error_accumulator = 0.0f; 
    
    // Consider seeding generator less frequently if performance is critical
    // Wrapped std::chrono::system_clock::now in parentheses to avoid macro expansion issues on Windows
    static std::default_random_engine generator(((std::chrono::system_clock::now)().time_since_epoch().count())); 
    std::uniform_real_distribution<float> distribution(-ditherAmplitude, ditherAmplitude);
    
    size_t num_samples_to_process = std::min(process_buffer_pos, processed_buffer.size());

    for (size_t i = 0; i < num_samples_to_process; ++i) {
        float sample = static_cast<float>(processed_buffer[i]) / INT32_MAX;
        sample += error_accumulator * shapingFactor;
        float dither = distribution(generator);
        sample += dither;
        sample = std::max(-1.0f, std::min(1.0f, sample));
        int32_t quantized_sample = static_cast<int32_t>(sample * INT32_MAX); 
        error_accumulator = sample - static_cast<float>(quantized_sample) / INT32_MAX; 
        processed_buffer[i] = quantized_sample; 
    }
} 


void AudioProcessor::setupDCFilter() {
    float sampleRateForFilters = static_cast<float>(outputSampleRate * OVERSAMPLING_FACTOR);
     if (sampleRateForFilters <= 0) {
          LOG_CPP_ERROR("[AudioProc] Error: Invalid sample rate (%d) for DC Filter setup.", outputSampleRate);
          for (int channel = 0; channel < MAX_CHANNELS; ++channel) {
              delete dcFilters[channel]; dcFilters[channel] = nullptr;
          }
          return;
     }

    for (int channel = 0; channel < MAX_CHANNELS; ++channel) {
        delete dcFilters[channel];
        float normalized_freq = 20.0f / sampleRateForFilters;
         if (normalized_freq >= 0.5f) normalized_freq = 0.499f;
         try {
            dcFilters[channel] = new Biquad(bq_type_highpass, normalized_freq, 0.707f, 0.0f);
         } catch (const std::bad_alloc& e) {
             LOG_CPP_ERROR("[AudioProc] Error allocating DC filter [%d]: %s", channel, e.what());
             dcFilters[channel] = nullptr;
         }
    }
}

void AudioProcessor::removeDCOffset() {
    if (channel_buffer_pos == 0) return; // Nothing to process

    size_t safe_temp_buffer_size = channel_buffer_pos; // Required size
    
    // Allocate temporary buffer safely
    std::vector<float> temp_float_buffer;
     try {
         temp_float_buffer.resize(safe_temp_buffer_size);
     } catch (const std::bad_alloc& e) {
         LOG_CPP_ERROR("[AudioProc] Error allocating temporary buffer in removeDCOffset: %s", e.what());
         return;
     }


    for (int ch = 0; ch < outputChannels; ++ch) {
        if (static_cast<size_t>(ch) >= remixed_channel_buffers.size() || !dcFilters[ch]) continue;

        size_t current_channel_size = remixed_channel_buffers[ch].size();
        size_t safe_process_len = std::min(channel_buffer_pos, current_channel_size); 

        if (safe_process_len == 0) continue;
        if (safe_process_len > temp_float_buffer.size()) { // Should not happen if resize succeeded
             LOG_CPP_ERROR("[AudioProc] Error: safe_process_len (%zu) exceeds temporary buffer size (%zu) in removeDCOffset.", safe_process_len, temp_float_buffer.size());
             continue;
        }

        for (size_t pos = 0; pos < safe_process_len; ++pos) {
            temp_float_buffer[pos] = static_cast<float>(remixed_channel_buffers[ch][pos]) / INT32_MAX;
        }
        
        dcFilters[ch]->processBlock(temp_float_buffer.data(), temp_float_buffer.data(), safe_process_len);
        
        for (size_t pos = 0; pos < safe_process_len; ++pos) {
            remixed_channel_buffers[ch][pos] = static_cast<int32_t>(temp_float_buffer[pos] * INT32_MAX);
        }
    }
} 


bool AudioProcessor::isProcessingRequired() {
    if (!isProcessingRequiredCacheSet) {
        isProcessingRequiredCache = isProcessingRequiredCheck();
        isProcessingRequiredCacheSet = true;
    }
    return isProcessingRequiredCache;
}

bool AudioProcessor::isProcessingRequiredCheck() {
    if (inputSampleRate != outputSampleRate) return true;
    if (target_volume_.load() != 1.0f) return true;
    if (inputChannels != outputChannels) return true;
    
    for (int i = 0; i < inputChannels; ++i) {
        for (int j = 0; j < outputChannels; ++j) {
            // Check bounds for speaker_mix access
            if (i < MAX_CHANNELS && j < MAX_CHANNELS) {
                if ((i == j && speaker_mix[i][j] != 1.0f) || 
                    (i != j && speaker_mix[i][j] != 0.0f)) {
                    return true;
               }
           } else {
                LOG_CPP_WARNING("[AudioProc] Warning: Out-of-bounds access attempt in speaker_mix check.");
                // Decide how to handle this - maybe return true to be safe?
                return true;
           }
        }
    }

    for (int i = 0; i < EQ_BANDS; ++i) {
        if (eq[i] != 1.0f) return true;
    }
    
    return false;
}
