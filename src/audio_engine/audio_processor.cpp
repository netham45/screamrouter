#include "audio_processor.h"
#include "biquad/biquad.h"
#include "libsamplerate/include/samplerate.h"
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <vector>
#include <random>
#include <iostream>
#include <chrono>
#include <thread>
#include <new> // Include for std::bad_alloc

#define CHUNK_SIZE 1152
#define OVERSAMPLING_FACTOR 2

AudioProcessor::AudioProcessor(int inputChannels, int outputChannels, int inputBitDepth, int inputSampleRate, int outputSampleRate, float volume)
    : inputChannels(inputChannels), outputChannels(outputChannels), inputBitDepth(inputBitDepth),
      inputSampleRate(inputSampleRate), outputSampleRate(outputSampleRate), volume(volume), monitor_running(true),
      receive_buffer(CHUNK_SIZE * 4), // Initialize vectors with reasonable starting sizes
      scaled_buffer(CHUNK_SIZE * 8),  // Adjusted initial sizes based on potential usage
      resampled_buffer(CHUNK_SIZE * MAX_CHANNELS * 4 * OVERSAMPLING_FACTOR), // Larger for resampling
      channel_buffers(MAX_CHANNELS, std::vector<int32_t>(CHUNK_SIZE * 8 * OVERSAMPLING_FACTOR)), // Larger for resampling
      remixed_channel_buffers(MAX_CHANNELS, std::vector<int32_t>(CHUNK_SIZE * 8 * OVERSAMPLING_FACTOR)), // Larger for resampling
      merged_buffer(CHUNK_SIZE * MAX_CHANNELS * 4 * OVERSAMPLING_FACTOR), // Larger for resampling
      processed_buffer(CHUNK_SIZE * MAX_CHANNELS * 4), // Final output size related
      resampler_data_in(CHUNK_SIZE * MAX_CHANNELS * 8), // Float buffers for libsamplerate
      resampler_data_out(CHUNK_SIZE * MAX_CHANNELS * 8 * OVERSAMPLING_FACTOR * 2), // Generous output float buffer
      sampler(nullptr), downsampler(nullptr), // Initialize sampler/downsampler to nullptr
      isProcessingRequiredCache(false), isProcessingRequiredCacheSet(false) // Initialize cache flags
{
    // Initialize filter pointers to nullptr before use
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        for (int band = 0; band < EQ_BANDS; ++band) {
            filters[ch][band] = nullptr;
        }
        dcFilters[ch] = nullptr;
    }
    
    std::fill(eq, eq + EQ_BANDS, 1.0f); 
    updateSpeakerMix(); // Call the restored function
    setupBiquad();
    initializeSampler();
    setupDCFilter();

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

    if (sampler) src_delete(sampler);
    if (downsampler) src_delete(downsampler);
     
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
        catch (const std::bad_alloc& e) { std::cerr << "Error resizing receive_buffer: " << e.what() << std::endl; return -1; } // Return error code
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
    size_t samples_available = process_buffer_pos; // Use the count from the last processing step
    size_t samples_to_copy = 0;
    
    // Calculate the theoretical number of samples for one output chunk based on the *original* input chunk size
    int samples_per_input_chunk = (inputBitDepth > 0) ? (CHUNK_SIZE / (inputBitDepth / 8)) : 0;
    int input_frames = (inputChannels > 0) ? (samples_per_input_chunk / inputChannels) : 0;
    int expected_output_samples_per_chunk = (outputChannels > 0) ? (input_frames * outputChannels) : 0;

    if (outputBuffer && expected_output_samples_per_chunk > 0) {
        samples_to_copy = std::min(samples_available, static_cast<size_t>(expected_output_samples_per_chunk));

        if (samples_to_copy > processed_buffer.size()) {
            std::cerr << "Error: samples_to_copy (" << samples_to_copy 
                      << ") exceeds internal processed_buffer size (" << processed_buffer.size() 
                      << ") in processAudio final copy." << std::endl;
            samples_to_copy = 0; // Prevent overflow
        }

        if (samples_to_copy > 0) {
            memcpy(outputBuffer, processed_buffer.data(), samples_to_copy * sizeof(int32_t));
        }

        // Zero-pad if fewer samples were available than expected for a full chunk
        if (samples_to_copy < static_cast<size_t>(expected_output_samples_per_chunk)) {
             // Ensure outputBuffer is large enough for padding
             // This assumes caller allocated enough space for expected_output_samples_per_chunk
             memset(outputBuffer + samples_to_copy, 0, (expected_output_samples_per_chunk - samples_to_copy) * sizeof(int32_t));
        }
        // Return the number of samples corresponding to a full output chunk, even if padded
        return expected_output_samples_per_chunk; 
    } else {
        // If outputBuffer is null or expected samples is zero, return 0
        return 0;
    }
}


void AudioProcessor::setVolume(float newVolume) {
    volume = newVolume;
}

void AudioProcessor::setEqualizer(const float* newEq) {
    if (newEq) {
        std::copy(newEq, newEq + EQ_BANDS, eq);
        setupBiquad(); 
    }
}

void AudioProcessor::setupBiquad() {
    #ifdef NORMALIZE_EQ_GAIN
    float max_gain = 1.0f;
    for(int i=0; i<EQ_BANDS; ++i) { if (eq[i] > max_gain) max_gain = eq[i]; }
    if (max_gain < 0.01f) max_gain = 1.0f; 
    #endif

    const float frequencies[] = {65.406392, 92.498606, 130.81278, 184.99721, 261.62557, 369.99442, 523.25113, 739.9884,
                                 1046.5023, 1479.9768, 2093.0045, 2959.9536, 4186.0091, 5919.9072, 8372.0181, 11839.814,
                                 16744.036, 20000.0}; 

    float sampleRateForFilters = static_cast<float>(outputSampleRate * OVERSAMPLING_FACTOR);
    if (sampleRateForFilters <= 0) {
         std::cerr << "Error: Invalid sample rate (" << outputSampleRate << ") for Biquad setup." << std::endl;
         return; 
    }

    for (int channel = 0; channel < MAX_CHANNELS; channel++) {
        for (int i = 0; i < EQ_BANDS; i++) {
            delete filters[channel][i]; 
            float gain_db = 10.0f * (eq[i] - 1.0f); 
            #ifdef NORMALIZE_EQ_GAIN
            gain_db = 10.0f * ((eq[i] / max_gain) - 1.0f);
            #endif
            float normalized_freq = frequencies[i] / sampleRateForFilters;
            if (normalized_freq >= 0.5f) {
                 normalized_freq = 0.499f; 
            }
            try {
                 filters[channel][i] = new Biquad(bq_type_peak, normalized_freq, 1.0, gain_db);
            } catch (const std::bad_alloc& e) {
                 std::cerr << "Error allocating Biquad filter [" << channel << "][" << i << "]: " << e.what() << std::endl;
                 filters[channel][i] = nullptr; 
            }
        }
    }
}

void AudioProcessor::initializeSampler() {
    int error = 0;
    if (sampler) { src_delete(sampler); sampler = nullptr; }
    if (downsampler) { src_delete(downsampler); downsampler = nullptr; }

    if (inputChannels > 0) { 
        sampler = src_new(SRC_SINC_BEST_QUALITY, inputChannels, &error); 
        if (!sampler) { std::cerr << "Error creating sampler: " << src_strerror(error) << std::endl; }
    }
    if (outputChannels > 0) { 
        downsampler = src_new(SRC_SINC_BEST_QUALITY, outputChannels, &error); 
        if (!downsampler) { std::cerr << "Error creating downsampler: " << src_strerror(error) << std::endl; }
    }
}

void AudioProcessor::scaleBuffer() {
    scale_buffer_pos = 0;
    if (inputBitDepth != 16 && inputBitDepth != 24 && inputBitDepth != 32) return;
    
    size_t num_input_samples = (inputBitDepth > 0) ? (CHUNK_SIZE / (inputBitDepth / 8)) : 0;
    if (num_input_samples == 0) return;

    if (scaled_buffer.size() < num_input_samples) {
         try { scaled_buffer.resize(num_input_samples); } 
         catch (const std::bad_alloc& e) { std::cerr << "Error resizing scaled_buffer: " << e.what() << std::endl; return; }
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
    for (size_t i = 0; i < scale_buffer_pos; ++i) {
        if (i < scaled_buffer.size()) {
            float sample = static_cast<float>(scaled_buffer[i]) / INT32_MAX;
            sample *= volume;
            sample = softClip(sample); 
            scaled_buffer[i] = static_cast<int32_t>(sample * INT32_MAX);
        } else {
             std::cerr << "Error: Index out of bounds in volumeAdjust (i=" << i << ")" << std::endl;
             break; 
        }
    }
}

void AudioProcessor::resample() {
    // Bypass if no processing needed or rates already match target
    if (!isProcessingRequired() || inputSampleRate == outputSampleRate * OVERSAMPLING_FACTOR) {
        size_t samples_to_copy = scale_buffer_pos;
        if (samples_to_copy > scaled_buffer.size()) {
             std::cerr << "Error: scale_buffer_pos exceeds scaled_buffer size in resample bypass." << std::endl;
             samples_to_copy = scaled_buffer.size(); 
        }
        if (samples_to_copy > resampled_buffer.capacity()) {
             std::cerr << "Error: Not enough capacity in resampled_buffer for memcpy. Required: " << samples_to_copy << ", Capacity: " << resampled_buffer.capacity() << std::endl;
             resample_buffer_pos = 0; return; 
        }
         if (resampled_buffer.size() < samples_to_copy) {
             try { resampled_buffer.resize(samples_to_copy); } 
             catch (const std::bad_alloc& e) { std::cerr << "Error resizing resampled_buffer: " << e.what() << std::endl; resample_buffer_pos = 0; return; }
         }
        memcpy(resampled_buffer.data(), scaled_buffer.data(), samples_to_copy * sizeof(int32_t));
        resample_buffer_pos = samples_to_copy;
        return;
    }

    // Proceed with resampling
    if (!sampler || inputChannels <= 0 || inputSampleRate <= 0 || outputSampleRate <= 0) {
         std::cerr << "Error: Sampler not initialized or invalid channels/rate for resampling." << std::endl;
         resample_buffer_pos = 0; return;
    }

    long datalen_frames = scale_buffer_pos / inputChannels;
    size_t datalen_samples = datalen_frames * inputChannels;

    if (datalen_frames == 0) { resample_buffer_pos = 0; return; }
    if (datalen_samples > scaled_buffer.size() || datalen_samples > resampler_data_in.capacity()) {
         std::cerr << "Error: Not enough data/capacity in source buffers for resampling." << std::endl;
         resample_buffer_pos = 0; return;
    }
     if (resampler_data_in.size() < datalen_samples) {
         try { resampler_data_in.resize(datalen_samples); } 
         catch (const std::bad_alloc& e) { std::cerr << "Error resizing resampler_data_in: " << e.what() << std::endl; resample_buffer_pos = 0; return; }
     }

    src_int_to_float_array(scaled_buffer.data(), resampler_data_in.data(), datalen_samples);
    
    SRC_DATA sampler_config = {0};
    sampler_config.data_in = resampler_data_in.data();
    sampler_config.data_out = resampler_data_out.data();
    sampler_config.src_ratio = static_cast<double>(outputSampleRate * OVERSAMPLING_FACTOR) / inputSampleRate;
    sampler_config.input_frames = datalen_frames;
    sampler_config.output_frames = resampler_data_out.capacity() / inputChannels; 
    
    int error = src_process(sampler, &sampler_config);
    if (error != 0) {
        std::cerr << "Error in src_process (sampler): " << src_strerror(error) << std::endl; 
        resample_buffer_pos = 0; return;
    }
    
    size_t frames_gen = sampler_config.output_frames_gen;
    size_t samples_gen = frames_gen * inputChannels;

    if (samples_gen > resampled_buffer.capacity()) {
         std::cerr << "Error: Not enough capacity in resampled_buffer for generated samples. Required: " << samples_gen << ", Capacity: " << resampled_buffer.capacity() << std::endl;
         resample_buffer_pos = 0; return; 
    }
    if (resampled_buffer.size() < samples_gen) {
         try { resampled_buffer.resize(samples_gen); } 
         catch (const std::bad_alloc& e) { std::cerr << "Error resizing resampled_buffer: " << e.what() << std::endl; resample_buffer_pos = 0; return; }
    }
    
    src_float_to_int_array(resampler_data_out.data(), resampled_buffer.data(), samples_gen);
    resample_buffer_pos = samples_gen;
}


void AudioProcessor::downsample() {
     // Bypass if no processing needed or rates already match final target
     if (!isProcessingRequired() || outputSampleRate * OVERSAMPLING_FACTOR == outputSampleRate) {
         size_t samples_to_copy = merged_buffer_pos;
         if (samples_to_copy > merged_buffer.size()) {
              std::cerr << "Error: merged_buffer_pos exceeds merged_buffer size in downsample bypass." << std::endl;
              samples_to_copy = merged_buffer.size();
         }
         if (samples_to_copy > processed_buffer.capacity()) {
              std::cerr << "Error: Not enough capacity in processed_buffer for memcpy. Required: " << samples_to_copy << ", Capacity: " << processed_buffer.capacity() << std::endl;
              process_buffer_pos = 0; return; 
         }
          if (processed_buffer.size() < samples_to_copy) {
              try { processed_buffer.resize(samples_to_copy); } 
              catch (const std::bad_alloc& e) { std::cerr << "Error resizing processed_buffer: " << e.what() << std::endl; process_buffer_pos = 0; return; }
          }
         memcpy(processed_buffer.data(), merged_buffer.data(), samples_to_copy * sizeof(int32_t));
         process_buffer_pos = samples_to_copy;
         return;
     }

    // Proceed with downsampling
    if (!downsampler || outputChannels <= 0 || outputSampleRate <= 0) {
         std::cerr << "Error: Downsampler not initialized or invalid channels/rate for downsampling." << std::endl;
         process_buffer_pos = 0; return;
    }

    long datalen_frames = merged_buffer_pos / outputChannels;
    size_t datalen_samples = datalen_frames * outputChannels;

    if (datalen_frames == 0) { process_buffer_pos = 0; return; }
    if (datalen_samples > merged_buffer.size() || datalen_samples > resampler_data_in.capacity()) {
         std::cerr << "Error: Not enough data/capacity in source buffers for downsampling." << std::endl;
         process_buffer_pos = 0; return;
    }
     if (resampler_data_in.size() < datalen_samples) {
         try { resampler_data_in.resize(datalen_samples); } 
         catch (const std::bad_alloc& e) { std::cerr << "Error resizing resampler_data_in: " << e.what() << std::endl; process_buffer_pos = 0; return; }
     }

    src_int_to_float_array(merged_buffer.data(), resampler_data_in.data(), datalen_samples);
    
    SRC_DATA downsampler_config = {0};
    downsampler_config.data_in = resampler_data_in.data();
    downsampler_config.data_out = resampler_data_out.data();
    downsampler_config.src_ratio = static_cast<double>(outputSampleRate) / (outputSampleRate * OVERSAMPLING_FACTOR); 
    downsampler_config.input_frames = datalen_frames;
    downsampler_config.output_frames = resampler_data_out.capacity() / outputChannels; 

    int error = src_process(downsampler, &downsampler_config);
    if (error != 0) {
        std::cerr << "Error in src_process (downsampler): " << src_strerror(error) << std::endl; 
        process_buffer_pos = 0; return;
    }
    
    size_t frames_gen = downsampler_config.output_frames_gen;
    size_t samples_gen = frames_gen * outputChannels;

    if (samples_gen > processed_buffer.capacity()) {
         std::cerr << "Error: Not enough capacity in processed_buffer for generated samples. Required: " << samples_gen << ", Capacity: " << processed_buffer.capacity() << std::endl;
         process_buffer_pos = 0; return; 
    }
    if (processed_buffer.size() < samples_gen) {
         try { processed_buffer.resize(samples_gen); } 
         catch (const std::bad_alloc& e) { std::cerr << "Error resizing processed_buffer: " << e.what() << std::endl; process_buffer_pos = 0; return; }
    }

    src_float_to_int_array(resampler_data_out.data(), processed_buffer.data(), samples_gen);
    process_buffer_pos = samples_gen;
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
         std::cerr << "Warning: resample_buffer_pos (" << resample_buffer_pos 
                   << ") not perfectly divisible by inputChannels (" << inputChannels 
                   << ") in splitBufferToChannels." << std::endl;
         // Adjust num_frames or handle incomplete frame? For now, proceed with truncated num_frames.
    }
    channel_buffer_pos = num_frames; 

    // Ensure inner vectors are large enough
    for(int ch = 0; ch < inputChannels; ++ch) {
         if (static_cast<size_t>(ch) >= channel_buffers.size()) {
              std::cerr << "Error: Channel index out of bounds for channel_buffers." << std::endl;
              channel_buffer_pos = 0; return; // Major error
         }
        if (channel_buffers[ch].size() < num_frames) {
            try { channel_buffers[ch].resize(num_frames); } 
            catch (const std::bad_alloc& e) { 
                std::cerr << "Error resizing channel_buffers[" << ch << "]: " << e.what() << std::endl; 
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
             std::cerr << "Error: Out of bounds access detected in splitBufferToChannels loop. " 
                       << "i=" << i << " (resample_buffer_pos=" << resample_buffer_pos << ", resampled_buffer.size=" << resampled_buffer.size() << "), "
                       << "channel=" << channel << " (inputChannels=" << inputChannels << ", channel_buffers.size=" << channel_buffers.size() << "), "
                       << "pos_in_channel=" << pos_in_channel << " (num_frames=" << num_frames << ")" << std::endl;
             // If this error occurs, it indicates a problem in preceding steps (like resample) or the resize logic here.
             // Stop processing this chunk to prevent further issues.
             channel_buffer_pos = 0; // Mark as having processed 0 valid frames
             return; 
        }
    }
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
        for (int output_channel = 0; output_channel < outputChannels && output_channel < MAX_CHANNELS; output_channel++) // Write the left (first) speaker to every channel
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
        case 4: // Stereo -> Quad
            speaker_mix[0][0] = 1; // Left to Front Left
            speaker_mix[1][1] = 1; // Right to Front Right
            speaker_mix[0][2] = 1; // Left to Back Left
            speaker_mix[1][3] = 1; // Right to Back Right
            break;
        case 6: // Stereo -> 5.1 Surround
            // FL FR C LFE BL BR - Assuming standard 5.1 layout indices
            speaker_mix[0][0] = 1;  // Left to Front Left
            speaker_mix[1][1] = 1;  // Right to Front Right
            speaker_mix[0][2] = .5; // Left to Center Half Vol
            speaker_mix[1][2] = .5; // Right to Center Half Vol
            // speaker_mix[0][3] = .5; // LFE - Typically derived or silent from stereo
            // speaker_mix[1][3] = .5; 
            speaker_mix[0][4] = 1;  // Left to Rear Left (or Side Left)
            speaker_mix[1][5] = 1;  // Right to Rear Right (or Side Right)
            break;
        case 8: // Stereo -> 7.1 Surround
            // FL FR C LFE BL BR SL SR - Assuming standard 7.1 layout indices
            speaker_mix[0][0] = 1;  // Left to Front Left
            speaker_mix[1][1] = 1;  // Right to Front Right
            speaker_mix[0][2] = .5; // Left to Center Half Vol
            speaker_mix[1][2] = .5; // Right to Center Half Vol
            // speaker_mix[0][3] = .5; // LFE
            // speaker_mix[1][3] = .5; 
            speaker_mix[0][4] = 1;  // Left to Rear Left
            speaker_mix[1][5] = 1;  // Right to Rear Right
            speaker_mix[0][6] = 1;  // Left to Side Left
            speaker_mix[1][7] = 1;  // Right to Side Right
            break;
        }
        break;
    case 4: // Quad Input
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
        case 4: // Quad -> Quad
            speaker_mix[0][0] = 1; // Front Left to Front Left
            speaker_mix[1][1] = 1; // Front Right to Front Right
            speaker_mix[2][2] = 1; // Rear Left to Rear Left
            speaker_mix[3][3] = 1; // Rear Right to Rear Right
            break;
        case 6: // Quad -> 5.1 Surround
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
    case 6: // 5.1 Input
        switch (outputChannels)
        {
        case 1:                     // 5.1 Surround -> Mono
            speaker_mix[0][0] = .2; // Front Left to Mono
            speaker_mix[1][0] = .2; // Front Right to Mono
            speaker_mix[2][0] = .2; // Center to Mono
            speaker_mix[4][0] = .2; // Rear Left to Mono (Assuming 5.1 layout: FL, FR, C, LFE, RL, RR)
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
        case 4: // 5.1 -> Quad
            speaker_mix[0][0] = .66; // Front Left to Front Left
            speaker_mix[1][1] = .66; // Front Right to Front Right
            speaker_mix[2][0] = .33; // Center to Front Left
            speaker_mix[2][1] = .33; // Center to Front Right
            speaker_mix[4][2] = 1;   // Rear Left to Rear Left
            speaker_mix[5][3] = 1;   // Rear Right to Rear Right
            break;
        case 6: // 5.1 Surround -> 5.1 Surround
            speaker_mix[0][0] = 1; // Front Left to Front Left
            speaker_mix[1][1] = 1; // Front Right to Front Right
            speaker_mix[2][2] = 1; // Center to Center
            speaker_mix[3][3] = 1; // LFE to LFE
            speaker_mix[4][4] = 1; // Rear Left to Rear Left
            speaker_mix[5][5] = 1; // Rear Right to Rear Right
            break;
        case 8: // 5.1 Surround -> 7.1 Surround
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
            speaker_mix[7][1] = .25; // Side Right to Front Right // Corrected index from 7 to 1
            speaker_mix[6][2] = .33; // Side Left to Rear Left
            speaker_mix[7][3] = .33; // Side Right to Rear Right // Corrected index from 7 to 3
            break;
        case 6: // 7.1 Surround -> 5.1 Surround
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
    default: // Fallback for unsupported input channel counts
         // Default to identity mapping for common channels if possible
         int min_ch_default = std::min(inputChannels, outputChannels);
         min_ch_default = std::min(min_ch_default, static_cast<int>(MAX_CHANNELS));
         for(int i = 0; i < min_ch_default; ++i) {
             speaker_mix[i][i] = 1.0f;
         }
         std::cerr << "Warning: Unsupported input channel count (" << inputChannels << ") in updateSpeakerMix. Using basic identity mapping." << std::endl;
         break;
    }
}

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
        std::cerr << "Error allocating temporary buffers in equalize: " << e.what() << std::endl;
        return;
    }


    for (int ch = 0; ch < outputChannels; ++ch) {
        if (static_cast<size_t>(ch) >= remixed_channel_buffers.size() || !filters[ch][0]) continue; // Check if channel exists and filters allocated

        size_t current_channel_size = remixed_channel_buffers[ch].size();
        size_t safe_process_len = std::min(channel_buffer_pos, current_channel_size); // Process only available samples
        
        if (safe_process_len == 0) continue; 
        if (safe_process_len > temp_float_buffer.size()) { // Double check against temp buffer size
             std::cerr << "Error: safe_process_len exceeds temporary buffer size in equalize." << std::endl;
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
         catch (const std::bad_alloc& e) { std::cerr << "Error reserving merged_buffer: " << e.what() << std::endl; return; }
    }
    if (merged_buffer.size() < required_merged_size) {
         try { merged_buffer.resize(required_merged_size); } 
         catch (const std::bad_alloc& e) { std::cerr << "Error resizing merged_buffer: " << e.what() << std::endl; return; }
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
                 std::cerr << "Error: Write attempt past merged_buffer bounds (pos=" << merged_buffer_pos << ", size=" << merged_buffer.size() << ")" << std::endl;
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
    static std::default_random_engine generator(std::chrono::system_clock::now().time_since_epoch().count()); 
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
          std::cerr << "Error: Invalid sample rate (" << outputSampleRate << ") for DC Filter setup." << std::endl;
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
             std::cerr << "Error allocating DC filter [" << channel << "]: " << e.what() << std::endl;
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
         std::cerr << "Error allocating temporary buffer in removeDCOffset: " << e.what() << std::endl;
         return;
     }


    for (int ch = 0; ch < outputChannels; ++ch) {
        if (static_cast<size_t>(ch) >= remixed_channel_buffers.size() || !dcFilters[ch]) continue;

        size_t current_channel_size = remixed_channel_buffers[ch].size();
        size_t safe_process_len = std::min(channel_buffer_pos, current_channel_size); 

        if (safe_process_len == 0) continue; 
        if (safe_process_len > temp_float_buffer.size()) { // Should not happen if resize succeeded
             std::cerr << "Error: safe_process_len exceeds temporary buffer size in removeDCOffset." << std::endl;
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
    if (volume != 1.0f) return true;
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
                 std::cerr << "Warning: Out-of-bounds access attempt in speaker_mix check." << std::endl;
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
