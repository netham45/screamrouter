#include "audio_processor.h"
#include "../utils/cpp_logger.h" // For new C++ logger
#include "../utils/profiler.h"
#include "biquad/biquad.h"
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
#include <array>
#if __has_include(<execution>)
#include <execution>
#define SCREAMROUTER_HAS_EXECUTION 1
#else
#define SCREAMROUTER_HAS_EXECUTION 0
#endif
#include <atomic>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#include <limits>

using namespace screamrouter::audio;

// Undefine min and max macros to prevent conflicts with std::min and std::max
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#define CHUNK_SIZE 1152

AudioProcessor::AudioProcessor(int inputChannels, int outputChannels, int inputBitDepth,
                               int inputSampleRate, int outputSampleRate, float volume,
                               const std::map<int, screamrouter::audio::CppSpeakerLayout>& initial_layouts_config,
                               std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings)
    : m_settings(settings),
      inputChannels(inputChannels), outputChannels(outputChannels), inputBitDepth(inputBitDepth),
      inputSampleRate(inputSampleRate), outputSampleRate(outputSampleRate),
      speaker_layouts_config_(initial_layouts_config), // Initialize the map
      monitor_running(true),
      scaled_float_buffer_(CHUNK_SIZE * 8),
      remixed_float_buffers_(MAX_CHANNELS, std::vector<float>(CHUNK_SIZE * 8 * m_settings->processor_tuning.oversampling_factor)),
      isProcessingRequiredCache(false), isProcessingRequiredCacheSet(false), // Initialize cache flags
      volume_normalization_enabled_(false), eq_normalization_enabled_(false),
      playback_rate_(1.0),
      m_upsampler(nullptr), m_downsampler(nullptr)
{
    smoothing_factor_ = m_settings->processor_tuning.volume_smoothing_factor;
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

    // Initialize filter pointers to nullptr before use
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        for (int band = 0; band < EQ_BANDS; ++band) {
            filters[ch][band] = nullptr;
        }
        dcFilters[ch] = nullptr;
    }
    
    std::fill(eq, eq + EQ_BANDS, 1.0f);
    setupBiquad();
    initializeSampler();
    setupDCFilter();
    select_active_speaker_mix(); // Call to select initial mix

    // Initialize position trackers
    scale_buffer_pos = 0;
    process_buffer_pos = 0;
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

    // Clean up libsamplerate resamplers
    if (m_upsampler) {
        src_delete(m_upsampler);
        m_upsampler = nullptr;
    }
    if (m_downsampler) {
        src_delete(m_downsampler);
        m_downsampler = nullptr;
    }
     
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
    PROFILE_FUNCTION();
    // Playback rate is applied dynamically within resample() and downsample() via src_ratio.
    // No re-initialization is needed for minor clock drift adjustments.

    // --- Processing Pipeline ---
    scaleBuffer(inputBuffer, CHUNK_SIZE);
    volumeAdjust();
    resample();
    splitBufferToChannels();
    mixSpeakers();
    //removeDCOffset();
    equalize();
    downsample(outputBuffer);
    //noiseShapingDither();
    // --- End Pipeline ---

    // Determine samples to copy based on actual samples processed and available
    size_t samples_available = process_buffer_pos; // This is the actual number of samples at outputSampleRate

    if (outputBuffer == nullptr) {
        LOG_CPP_ERROR("[AudioProc] Error: outputBuffer is null in processAudio.");
        return 0;
    }

    const size_t samples_to_write = samples_available;

    // Return the actual number of int32_t samples written to outputBuffer.
    // If samples_to_write is 0 (e.g., due to an error or no data), 0 is returned.
#ifdef ENABLE_AUDIO_PROFILING
    {
        static std::atomic<uint32_t> profile_counter{0};
        const uint32_t current = profile_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((current % 500u) == 0u) {
            utils::FunctionProfiler::instance().log_stats();
            utils::FunctionProfiler::instance().reset();
        }
    }
#endif
    return static_cast<int>(samples_to_write);
}


void AudioProcessor::setVolume(float newVolume) {
    target_volume_.store(newVolume);
}

void AudioProcessor::setVolumeNormalization(bool enabled) {
    volume_normalization_enabled_ = enabled;
}

void AudioProcessor::set_playback_rate(double rate) {
    const double clamped = std::clamp(rate, 1e-6, 8.0);
    playback_rate_.store(clamped);
    setRatio = 1.0 / clamped;
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

void AudioProcessor::flushFilters() {
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

void AudioProcessor::setupBiquad() {
    float max_gain = 1.0f;
    if (eq_normalization_enabled_) {
        for(int i=0; i<EQ_BANDS; ++i) { if (eq[i] > max_gain) max_gain = eq[i]; }
        if (max_gain < 0.01f) max_gain = 1.0f;
    }

    const float frequencies[] = {65.406392f, 92.498606f, 130.81278f, 184.99721f, 261.62557f, 369.99442f, 523.25113f, 739.9884f,
                                 1046.5023f, 1479.9768f, 2093.0045f, 2959.9536f, 4186.0091f, 5919.9072f, 8372.0181f, 11839.814f,
                                 16744.036f, 20000.0f};

    float sampleRateForFilters = static_cast<float>(outputSampleRate * m_settings->processor_tuning.oversampling_factor);
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
    int error;

    // Clean up existing resamplers
    if (m_upsampler) {
        src_delete(m_upsampler);
        m_upsampler = nullptr;
    }
    if (m_downsampler) {
        src_delete(m_downsampler);
        m_downsampler = nullptr;
    }

    if (inputSampleRate <= 0 || outputSampleRate <= 0) {
        LOG_CPP_ERROR("[AudioProc] Error: Invalid input or output sample rate for libsamplerate initialization.");
        return;
    }

    // Create upsampler
    m_upsampler = src_new(SRC_SINC_MEDIUM_QUALITY, inputChannels, &error);
    if (m_upsampler == nullptr) {
        LOG_CPP_ERROR("[AudioProc] Error creating libsamplerate upsampler: %s", src_strerror(error));
    }

    // Create downsampler
    m_downsampler = src_new(SRC_SINC_MEDIUM_QUALITY, outputChannels, &error);
    if (m_downsampler == nullptr) {
        LOG_CPP_ERROR("[AudioProc] Error creating libsamplerate downsampler: %s", src_strerror(error));
    }
}

void AudioProcessor::scaleBuffer(const uint8_t* inputBuffer, size_t inputBytes) {
    PROFILE_FUNCTION();
    scale_buffer_pos = 0;

    if (!inputBuffer) {
        LOG_CPP_ERROR("[AudioProc] Error: Null input buffer passed to scaleBuffer.");
        return;
    }

    if (inputBitDepth != 16 && inputBitDepth != 24 && inputBitDepth != 32) {
        LOG_CPP_ERROR("[AudioProc] Unsupported input bit depth: %d", inputBitDepth);
        return;
    }

    const size_t bytes_per_sample = static_cast<size_t>(inputBitDepth) / 8;
    if (bytes_per_sample == 0) {
        return;
    }

    const size_t available_samples = inputBytes / bytes_per_sample;
    if (available_samples == 0) {
        return;
    }

    try {
        if (scaled_float_buffer_.size() < available_samples) {
            scaled_float_buffer_.resize(available_samples);
        }
    } catch (const std::bad_alloc& e) {
        LOG_CPP_ERROR("[AudioProc] Error resizing scaled_float_buffer_: %s", e.what());
        return;
    }

    const float inv_int32 = 1.0f / static_cast<float>(INT32_MAX);

    const uint8_t* src = inputBuffer;
    for (size_t idx = 0; idx < available_samples; ++idx) {
        int32_t sample32 = 0;
        switch (inputBitDepth) {
        case 16: {
            const int32_t sample16 = static_cast<int16_t>(static_cast<uint16_t>(src[0] | (static_cast<uint16_t>(src[1]) << 8)));
            sample32 = sample16 << 16;
            break;
        }
        case 24: {
            int32_t raw = static_cast<int32_t>(src[0]) |
                          (static_cast<int32_t>(src[1]) << 8) |
                          (static_cast<int32_t>(src[2]) << 16);
            if (raw & 0x00800000) {
                raw |= ~0x00FFFFFF;
            }
            sample32 = raw << 8;
            break;
        }
        case 32: {
            std::memcpy(&sample32, src, sizeof(int32_t));
            break;
        }
        }

        scaled_float_buffer_[idx] = static_cast<float>(sample32) * inv_int32;
        src += bytes_per_sample;
    }

    scale_buffer_pos = available_samples;
}

float AudioProcessor::softClip(float sample) {
    // This is a standard cubic soft-clipping algorithm. It provides a smooth
    // transition into saturation, replacing the previous flawed implementation.
    // NOTE: This simpler, correct implementation does not use the 'soft_clip_threshold' or 
    // 'soft_clip_knee' parameters from the settings. It provides a fixed clipping curve
    // that starts compressing at a level of ~0.67 and clips fully at 1.0.

    if (sample >= 1.0f) {
        return 1.0f;
    }
    if (sample <= -1.0f) {
        return -1.0f;
    }
    
    // Apply the cubic clipping curve for samples within the [-1.0, 1.0] range.
    return sample - (sample * sample * sample) / 3.0f;
}

void AudioProcessor::volumeAdjust() {
    PROFILE_FUNCTION();
    float current_vol = current_volume_.load();
    float target_vol = target_volume_.load();

    if (scaled_float_buffer_.size() < scale_buffer_pos) {
        LOG_CPP_ERROR("[AudioProc] Error: scaled_float_buffer_ too small in volumeAdjust (size=%zu, needed=%zu)", scaled_float_buffer_.size(), scale_buffer_pos);
        return;
    }

    if (volume_normalization_enabled_) {
        double sum_of_squares = 0.0;
        for (size_t i = 0; i < scale_buffer_pos; ++i) {
            float sample = scaled_float_buffer_[i];
            sum_of_squares += static_cast<double>(sample) * static_cast<double>(sample);
        }
        double rms = (scale_buffer_pos > 0) ? sqrt(sum_of_squares / scale_buffer_pos) : 0.0;

        float target_rms = m_settings->processor_tuning.normalization_target_rms;
        float gain = (rms > 0.0) ? target_rms / static_cast<float>(rms) : 1.0f;

        float attack_smoothing_factor = m_settings->processor_tuning.normalization_attack_smoothing;
        float decay_smoothing_factor = m_settings->processor_tuning.normalization_decay_smoothing;

        for (size_t i = 0; i < scale_buffer_pos; ++i) {
            float smoothing_factor = (gain > current_gain_) ? attack_smoothing_factor : decay_smoothing_factor;
            current_gain_ = current_gain_ * (1.0f - smoothing_factor) + gain * smoothing_factor;
            current_vol += (target_vol - current_vol) * smoothing_factor_;
            float sample = scaled_float_buffer_[i];
            sample *= current_vol * current_gain_;
            sample = softClip(sample);
            sample = std::clamp(sample, -1.0f, 1.0f);
            scaled_float_buffer_[i] = sample;
        }
    } else {
        for (size_t i = 0; i < scale_buffer_pos; ++i) {
            current_vol += (target_vol - current_vol) * smoothing_factor_;
            float sample = scaled_float_buffer_[i];
            sample *= current_vol;
            sample = softClip(sample);
            sample = std::clamp(sample, -1.0f, 1.0f);
            scaled_float_buffer_[i] = sample;
        }
    }
    current_volume_.store(current_vol);

    for (size_t i = 0; i < scale_buffer_pos; ++i) {
        scaled_float_buffer_[i] = std::clamp(scaled_float_buffer_[i], -1.0f, 1.0f);
    }
}

void AudioProcessor::resample() {
    PROFILE_FUNCTION();
    if (!isProcessingRequired() || m_upsampler == nullptr) {
        size_t samples_to_copy = scale_buffer_pos;
        if (samples_to_copy > scaled_float_buffer_.size()) {
            LOG_CPP_ERROR("[AudioProc] Error: scale_buffer_pos (%zu) exceeds scaled_float_buffer_ size (%zu) in resample bypass.", samples_to_copy, scaled_float_buffer_.size());
            samples_to_copy = scaled_float_buffer_.size();
        }
        if (resample_float_out_buffer_.size() < samples_to_copy) {
            try { resample_float_out_buffer_.resize(samples_to_copy); }
            catch (const std::bad_alloc& e) { LOG_CPP_ERROR("[AudioProc] Error resizing resample_float_out_buffer_ (bypass): %s", e.what()); resample_buffer_pos = 0; return; }
        }
        std::copy_n(scaled_float_buffer_.data(), samples_to_copy, resample_float_out_buffer_.data());
        resample_float_out_buffer_.resize(samples_to_copy);
        resample_buffer_pos = samples_to_copy;
        return;
    }

    if (scale_buffer_pos == 0) {
        resample_buffer_pos = 0;
        return;
    }

    const double current_playback_rate = std::max(1e-6, playback_rate_.load());
    const int oversample_factor = std::max(1, m_settings ? m_settings->processor_tuning.oversampling_factor : 1);
    const double effective_output_rate = static_cast<double>(outputSampleRate) / (setRatio) * static_cast<double>(oversample_factor);
    const double ratio = ((effective_output_rate) / static_cast<double>(inputSampleRate));

    LOG_CPP_DEBUG("[AudioProc] resample begin rate=%.6f ratio=%.6f over=%d in_sr=%d out_sr=%d scale_pos=%zu",
                  current_playback_rate,
                  ratio,
                  oversample_factor,
                  inputSampleRate,
                  outputSampleRate,
                  scale_buffer_pos);
    if (std::abs(ratio - 1.0) <= std::numeric_limits<double>::epsilon()) {
        size_t samples_to_copy = scale_buffer_pos;
        if (samples_to_copy > scaled_float_buffer_.size()) {
            LOG_CPP_ERROR("[AudioProc] Error: scale_buffer_pos (%zu) exceeds scaled_float_buffer_ size (%zu) in unity resample path.", samples_to_copy, scaled_float_buffer_.size());
            samples_to_copy = scaled_float_buffer_.size();
        }
        if (resample_float_out_buffer_.size() < samples_to_copy) {
            try { resample_float_out_buffer_.resize(samples_to_copy); }
            catch (const std::bad_alloc& e) { LOG_CPP_ERROR("[AudioProc] Error resizing resample_float_out_buffer_ (unity path): %s", e.what()); resample_buffer_pos = 0; return; }
        }
        std::copy_n(scaled_float_buffer_.data(), samples_to_copy, resample_float_out_buffer_.data());
        resample_float_out_buffer_.resize(samples_to_copy);
        resample_buffer_pos = samples_to_copy;
        return;
    }

    const size_t total_input_frames = scale_buffer_pos / inputChannels;
    const size_t valid_input_samples = total_input_frames * static_cast<size_t>(inputChannels);
    if (total_input_frames == 0) {
        resample_buffer_pos = 0;
        return;
    }

    if (valid_input_samples != scale_buffer_pos) {
        LOG_CPP_WARNING("[AudioProc] Dropping %zu trailing samples that do not form a complete frame for upsampling.",
                        scale_buffer_pos - valid_input_samples);
    }

    size_t estimated_output_frames = static_cast<size_t>(std::ceil(static_cast<double>(total_input_frames) * ratio)) + 16;
    size_t estimated_output_samples = estimated_output_frames * static_cast<size_t>(inputChannels);
    if (resample_float_out_buffer_.size() < estimated_output_samples) {
        try { resample_float_out_buffer_.resize(estimated_output_samples * 1.5); }
        catch (const std::bad_alloc& e) { LOG_CPP_ERROR("[AudioProc] Error resizing resample_float_out_buffer_: %s", e.what()); resample_buffer_pos = 0; return; }
    }

    size_t input_frames_consumed = 0;
    size_t output_frames_generated = 0;
    while (input_frames_consumed < total_input_frames) {
        size_t available_output_frames = resample_float_out_buffer_.size() / static_cast<size_t>(inputChannels) - output_frames_generated;
        if (available_output_frames == 0) {
            size_t frames_remaining = total_input_frames - input_frames_consumed;
            size_t grow_samples = (frames_remaining + 16) * static_cast<size_t>(inputChannels);
            try { resample_float_out_buffer_.resize(resample_float_out_buffer_.size() + grow_samples); }
            catch (const std::bad_alloc& e) { LOG_CPP_ERROR("[AudioProc] Error growing resample_float_out_buffer_: %s", e.what()); resample_buffer_pos = 0; return; }
            continue;
        }

        SRC_DATA src_data = {0};
        src_data.data_in = scaled_float_buffer_.data() + input_frames_consumed * inputChannels;
        src_data.input_frames = total_input_frames - input_frames_consumed;
        src_data.data_out = resample_float_out_buffer_.data() + output_frames_generated * inputChannels;
        src_data.output_frames = available_output_frames;
        src_data.src_ratio = ratio;

        int error = src_process(m_upsampler, &src_data);
        LOG_CPP_DEBUG("[AudioProc] resample loop input_used=%ld output_gen=%ld src_ratio=%.6f",
                      src_data.input_frames_used,
                      src_data.output_frames_gen,
                      src_data.src_ratio);
        if (error) {
            LOG_CPP_ERROR("[AudioProc] libsamplerate upsampling error: %s", src_strerror(error));
            resample_buffer_pos = 0;
            return;
        }

        input_frames_consumed += src_data.input_frames_used;
        output_frames_generated += src_data.output_frames_gen;

        if (src_data.input_frames_used == 0 && src_data.output_frames_gen == 0) {
            LOG_CPP_ERROR("[AudioProc] libsamplerate produced no progress during upsampling loop. Aborting chunk.");
            resample_buffer_pos = 0;
            return;
        }
    }

    size_t output_samples = output_frames_generated * static_cast<size_t>(inputChannels);
    resample_float_out_buffer_.resize(output_samples);
    resample_buffer_pos = output_samples;
}


void AudioProcessor::downsample(int32_t* outputBuffer) {
    PROFILE_FUNCTION();

    last_output_buffer_ = nullptr;
    last_output_samples_ = 0;

    if (!outputBuffer) {
        LOG_CPP_ERROR("[AudioProc] Error: Null output buffer passed to downsample.");
        process_buffer_pos = 0;
        return;
    }

    if (outputChannels <= 0 || channel_buffer_pos == 0) {
        process_buffer_pos = 0;
        return;
    }

    const size_t frame_count = channel_buffer_pos;
    const size_t samples_expected = frame_count * static_cast<size_t>(outputChannels);

    auto write_direct_from_channels = [&](int32_t* destination) {
        size_t write_index = 0;
        for (size_t frame = 0; frame < frame_count; ++frame) {
            for (int ch = 0; ch < outputChannels; ++ch) {
                float sample = 0.0f;
                if (static_cast<size_t>(ch) < remixed_float_buffers_.size() &&
                    frame < remixed_float_buffers_[ch].size()) {
                    sample = remixed_float_buffers_[ch][frame];
                }
                sample = std::clamp(sample, -1.0f, 1.0f);
                destination[write_index++] = static_cast<int32_t>(sample * static_cast<float>(INT32_MAX));
            }
        }
        process_buffer_pos = write_index;
        last_output_buffer_ = destination;
        last_output_samples_ = write_index;
    };

    const bool use_downsampler = isProcessingRequired() && m_downsampler != nullptr;
    if (!use_downsampler) {
        write_direct_from_channels(outputBuffer);
        return;
    }

    const double current_playback_rate = std::max(1e-6, playback_rate_.load());
    const int oversample_factor = std::max(1, m_settings ? m_settings->processor_tuning.oversampling_factor : 1);
    const double effective_output_rate = static_cast<double>(outputSampleRate) * static_cast<double>(oversample_factor);
    const double ratio = static_cast<double>(outputSampleRate) / effective_output_rate;
    LOG_CPP_DEBUG("[AudioProc] downsample begin rate=%.6f ratio=%.6f over=%d frames=%zu",
                  current_playback_rate,
                  ratio,
                  oversample_factor,
                  frame_count);

    if (std::abs(ratio - 1.0) <= std::numeric_limits<double>::epsilon()) {
        write_direct_from_channels(outputBuffer);
        return;
    }

    const size_t valid_input_samples = samples_expected;
    if (downsample_float_in_buffer_.size() < valid_input_samples) {
        try {
            downsample_float_in_buffer_.resize(valid_input_samples);
        } catch (const std::bad_alloc& e) {
            LOG_CPP_ERROR("[AudioProc] Error resizing downsample_float_in_buffer_: %s", e.what());
            process_buffer_pos = 0;
            return;
        }
    }

    float* interleaved_in = downsample_float_in_buffer_.data();
    size_t interleave_index = 0;
    for (size_t frame = 0; frame < frame_count; ++frame) {
        for (int ch = 0; ch < outputChannels; ++ch) {
            float sample = 0.0f;
            if (static_cast<size_t>(ch) < remixed_float_buffers_.size() &&
                frame < remixed_float_buffers_[ch].size()) {
                sample = remixed_float_buffers_[ch][frame];
            }
            interleaved_in[interleave_index++] = sample;
        }
    }

    size_t estimated_output_frames = static_cast<size_t>(std::ceil(static_cast<double>(frame_count) * ratio)) + 16;
    size_t estimated_output_samples = estimated_output_frames * static_cast<size_t>(outputChannels);
    if (downsample_float_out_buffer_.size() < estimated_output_samples) {
        try {
            downsample_float_out_buffer_.resize(estimated_output_samples);
        } catch (const std::bad_alloc& e) {
            LOG_CPP_ERROR("[AudioProc] Error resizing downsample_float_out_buffer_: %s", e.what());
            process_buffer_pos = 0;
            return;
        }
    }

    size_t input_frames_consumed = 0;
    size_t output_frames_generated = 0;
    while (input_frames_consumed < frame_count) {
        size_t available_output_frames = downsample_float_out_buffer_.size() / static_cast<size_t>(outputChannels) - output_frames_generated;
        if (available_output_frames == 0) {
            size_t frames_remaining = frame_count - input_frames_consumed;
            size_t grow_samples = (frames_remaining + 16) * static_cast<size_t>(outputChannels);
            try {
                downsample_float_out_buffer_.resize(downsample_float_out_buffer_.size() + grow_samples);
            } catch (const std::bad_alloc& e) {
                LOG_CPP_ERROR("[AudioProc] Error growing downsample_float_out_buffer_: %s", e.what());
                process_buffer_pos = 0;
                return;
            }
            continue;
        }

        SRC_DATA src_data = {0};
        src_data.data_in = downsample_float_in_buffer_.data() + input_frames_consumed * outputChannels;
        src_data.input_frames = frame_count - input_frames_consumed;
        src_data.data_out = downsample_float_out_buffer_.data() + output_frames_generated * outputChannels;
        src_data.output_frames = available_output_frames;
        src_data.src_ratio = ratio;

        int error = src_process(m_downsampler, &src_data);
        if (error) {
            LOG_CPP_ERROR("[AudioProc] libsamplerate downsampling error: %s", src_strerror(error));
            process_buffer_pos = 0;
            return;
        }

        input_frames_consumed += src_data.input_frames_used;
        output_frames_generated += src_data.output_frames_gen;

        if (src_data.input_frames_used == 0 && src_data.output_frames_gen == 0) {
            LOG_CPP_ERROR("[AudioProc] libsamplerate produced no progress during downsampling loop. Aborting chunk.");
            process_buffer_pos = 0;
            return;
        }
    }

    size_t output_samples = output_frames_generated * static_cast<size_t>(outputChannels);
    if (downsample_float_out_buffer_.size() < output_samples) {
        LOG_CPP_ERROR("[AudioProc] Error: downsample_float_out_buffer_ smaller than produced sample count (%zu vs %zu).", downsample_float_out_buffer_.size(), output_samples);
        process_buffer_pos = 0;
        return;
    }

    for (size_t i = 0; i < output_samples; ++i) {
        float sample = std::clamp(downsample_float_out_buffer_[i], -1.0f, 1.0f);
        outputBuffer[i] = static_cast<int32_t>(sample * static_cast<float>(INT32_MAX));
    }
    process_buffer_pos = output_samples;
    last_output_buffer_ = outputBuffer;
    last_output_samples_ = output_samples;
}


void AudioProcessor::splitBufferToChannels() {
    PROFILE_FUNCTION();
    if (inputChannels <= 0 || resample_buffer_pos == 0) {
        channel_buffer_pos = 0;
        for (auto& view : input_channel_views_) {
            view.data = nullptr;
            view.stride = 0;
        }
        return;
    }

    size_t num_frames = resample_buffer_pos / inputChannels;
    if (resample_buffer_pos % inputChannels != 0) {
        LOG_CPP_WARNING("[AudioProc] Warning: resample_buffer_pos (%zu) not divisible by inputChannels (%d). Truncating trailing samples.",
                        resample_buffer_pos, inputChannels);
    }
    channel_buffer_pos = num_frames;

    const float* base_ptr = resample_float_out_buffer_.data();
    const size_t stride = static_cast<size_t>(inputChannels);

    for (int ch = 0; ch < screamrouter::audio::MAX_CHANNELS; ++ch) {
        if (ch < inputChannels) {
            input_channel_views_[ch].data = base_ptr + ch;
            input_channel_views_[ch].stride = stride;
        } else {
            input_channel_views_[ch].data = nullptr;
            input_channel_views_[ch].stride = 0;
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
    rebuild_mix_taps_locked();
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
    rebuild_mix_taps_locked();
}

void AudioProcessor::rebuild_mix_taps_locked() {
    const float epsilon = 1e-6f;
    for (int oc = 0; oc < MAX_CHANNELS; ++oc) {
        auto& taps = mix_taps_[oc];
        taps.clear();
        if (oc >= outputChannels) {
            continue;
        }
        for (int ic = 0; ic < inputChannels && ic < MAX_CHANNELS; ++ic) {
            float gain = speaker_mix[ic][oc];
            if (std::fabs(gain) > epsilon) {
                MixTap tap{};
                tap.input_index = static_cast<uint8_t>(ic);
                tap.gain_scaled = gain;
                taps.push_back(tap);
            }
        }
    }
}

// --- End New/Updated Methods ---
// void AudioProcessor::mixSpeakers() { // Original line before potential extra brace
void AudioProcessor::mixSpeakers() {
    PROFILE_FUNCTION();
    if (outputChannels <= 0 || channel_buffer_pos == 0) {
        return;
    }

    std::array<int, MAX_CHANNELS> oc_indices{};
    for (int oc = 0; oc < outputChannels; ++oc) {
        oc_indices[oc] = oc;
    }

    auto mix_channel = [this](int oc) {
                      if (static_cast<size_t>(oc) >= remixed_float_buffers_.size()) {
                          return;
                      }

                      auto& out_channel = remixed_float_buffers_[oc];
                      if (out_channel.size() < channel_buffer_pos) {
                          try { out_channel.assign(channel_buffer_pos, 0.0f); }
                          catch (const std::bad_alloc& e) {
                              LOG_CPP_ERROR("[AudioProc] Error resizing remixed_float_buffers_[%d]: %s", oc, e.what());
                              return;
                          }
                      } else {
                          std::fill(out_channel.begin(), out_channel.begin() + channel_buffer_pos, 0.0f);
                      }

                      const auto& taps = mix_taps_[oc];
                      if (taps.empty()) {
                          return;
                      }

                      for (const auto& tap : taps) {
                          const auto& view = input_channel_views_[tap.input_index];
                          if (view.data == nullptr || view.stride == 0) {
                              continue;
                          }

                          float* out_ptr = out_channel.data();
                          const float* in_ptr = view.data;
                          const size_t stride = view.stride;
                          const float gain = tap.gain_scaled;

#if defined(__aarch64__)
                          if (stride == 1) {
                              size_t frame = 0;
                              const float32x4_t gain_vec = vdupq_n_f32(gain);
                              for (; frame + 4 <= channel_buffer_pos; frame += 4) {
                                  float32x4_t out_vec = vld1q_f32(out_ptr + frame);
                                  const float32x4_t in_vec = vld1q_f32(in_ptr + frame);
                                  out_vec = vfmaq_f32(out_vec, in_vec, gain_vec);
                                  vst1q_f32(out_ptr + frame, out_vec);
                              }
                              for (; frame < channel_buffer_pos; ++frame) {
                                  out_ptr[frame] += in_ptr[frame] * gain;
                              }
                              continue;
                          }
#elif defined(__SSE2__)
                          if (stride == 1) {
                              size_t frame = 0;
                              const __m128 gain_vec = _mm_set1_ps(gain);
                              for (; frame + 4 <= channel_buffer_pos; frame += 4) {
                                  __m128 out_vec = _mm_loadu_ps(out_ptr + frame);
                                  const __m128 in_vec = _mm_loadu_ps(in_ptr + frame);
                                  out_vec = _mm_add_ps(out_vec, _mm_mul_ps(in_vec, gain_vec));
                                  _mm_storeu_ps(out_ptr + frame, out_vec);
                              }
                              for (; frame < channel_buffer_pos; ++frame) {
                                  out_ptr[frame] += in_ptr[frame] * gain;
                              }
                              continue;
                          }
#endif

                          const float* sample_ptr = in_ptr;
                          for (size_t frame = 0; frame < channel_buffer_pos; ++frame, sample_ptr += stride) {
                              out_ptr[frame] += (*sample_ptr) * gain;
                          }
                      }
                  };

#if SCREAMROUTER_HAS_EXECUTION && defined(__cpp_lib_execution)
    std::for_each(std::execution::par_unseq,
                  oc_indices.begin(), oc_indices.begin() + outputChannels,
                  mix_channel);
#else
    for (int idx = 0; idx < outputChannels; ++idx) {
        mix_channel(oc_indices[idx]);
    }
#endif
}


void AudioProcessor::equalize() {
    PROFILE_FUNCTION();
    bool active_bands[EQ_BANDS] = {false};
    bool has_active_bands = false;
    for (int i = 0; i < EQ_BANDS; ++i) {
        if (eq[i] != 1.0f) {
            active_bands[i] = true;
            has_active_bands = true;
        }
    }
    if (!has_active_bands) return;

    for (int ch = 0; ch < outputChannels; ++ch) {
        if (static_cast<size_t>(ch) >= remixed_float_buffers_.size() || !filters[ch][0]) continue; // Check if channel exists and filters allocated

        size_t current_channel_size = remixed_float_buffers_[ch].size();
        size_t safe_process_len = std::min(channel_buffer_pos, current_channel_size); // Process only available samples

        if (safe_process_len == 0) continue;

        float* channel_samples = remixed_float_buffers_[ch].data();

        for (int band = 0; band < EQ_BANDS; ++band) {
            if (active_bands[band] && filters[ch][band]) {
                filters[ch][band]->processBlock(channel_samples, channel_samples, safe_process_len);
            }
        }

        for (size_t pos = 0; pos < safe_process_len; ++pos) {
            channel_samples[pos] = softClip(channel_samples[pos]);
        }
    }
}


void AudioProcessor::noiseShapingDither() {
    if (last_output_buffer_ == nullptr || last_output_samples_ == 0) {
        return;
    }

    const float ditherAmplitude = (inputBitDepth > 0)
                                      ? (1.0f / (static_cast<unsigned long long>(1) << (inputBitDepth - 1)))
                                      : 0.0f;

    const float shapingFactor = std::clamp(m_settings->processor_tuning.dither_noise_shaping_factor, 0.0f, 1.0f);

    static float error_accumulator = 0.0f;

    static std::default_random_engine generator(((std::chrono::system_clock::now)().time_since_epoch().count()));
    std::uniform_real_distribution<float> distribution(-ditherAmplitude, ditherAmplitude);

    for (size_t i = 0; i < last_output_samples_; ++i) {
        float sample = static_cast<float>(last_output_buffer_[i]) / INT32_MAX;
        sample += error_accumulator * shapingFactor;
        sample += distribution(generator);
        sample = std::clamp(sample, -1.0f, 1.0f);
        const int32_t quantized_sample = static_cast<int32_t>(sample * INT32_MAX);
        error_accumulator = sample - static_cast<float>(quantized_sample) / INT32_MAX;
        last_output_buffer_[i] = quantized_sample;
    }
}


void AudioProcessor::setupDCFilter() {
    float sampleRateForFilters = static_cast<float>(outputSampleRate * m_settings->processor_tuning.oversampling_factor);
     if (sampleRateForFilters <= 0) {
          LOG_CPP_ERROR("[AudioProc] Error: Invalid sample rate (%d) for DC Filter setup.", outputSampleRate);
          for (int channel = 0; channel < MAX_CHANNELS; ++channel) {
              delete dcFilters[channel]; dcFilters[channel] = nullptr;
          }
          return;
     }

    for (int channel = 0; channel < MAX_CHANNELS; ++channel) {
        delete dcFilters[channel];
        float normalized_freq = m_settings->processor_tuning.dc_filter_cutoff_hz / sampleRateForFilters;
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
    PROFILE_FUNCTION();
    if (channel_buffer_pos == 0) return; // Nothing to process

    for (int ch = 0; ch < outputChannels; ++ch) {
        if (static_cast<size_t>(ch) >= remixed_float_buffers_.size() || !dcFilters[ch]) continue;

        size_t current_channel_size = remixed_float_buffers_[ch].size();
        size_t safe_process_len = std::min(channel_buffer_pos, current_channel_size); 

        if (safe_process_len == 0) continue;

        dcFilters[ch]->processBlock(remixed_float_buffers_[ch].data(), remixed_float_buffers_[ch].data(), safe_process_len);
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

    if (std::abs(playback_rate_.load() - 1.0) > 1e-6) return true;
    
    return false;
}
