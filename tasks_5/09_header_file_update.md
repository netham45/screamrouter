# Task 9: `AudioProcessor` - Header File (`audio_processor.h`) Update

**Objective:**
Update the `audio_processor.h` header file to reflect all changes made in previous tasks, including the new configuration struct, updated member variable declarations (types and names), `static constexpr` constants, and removal of obsolete members.

**Details:**

1.  **Include Guards:**
    *   Ensure standard include guards are present:
        ```cpp
        #ifndef AUDIO_PROCESSOR_H
        #define AUDIO_PROCESSOR_H
        // ... content ...
        #endif // AUDIO_PROCESSOR_H
        ```

2.  **Necessary Includes:**
    *   Include `<vector>`, `<string>`, `<memory>` (for `std::unique_ptr`), `<array>` (if `std::array` is used for `eq_` or `speaker_mix_`).
    *   Include `libsamplerate/include/samplerate.h` for `SRC_STATE`.
    *   Forward declare `class Biquad;` or include its header if not already.
    *   Include `audio_types.h` for `AudioProcessorConfig` (if defined there).

3.  **Namespace:**
    *   Ensure the `AudioProcessor` class is within the `screamrouter::audio` namespace if that's the convention for audio engine components (currently it's not, but consider for consistency). If it remains in the global namespace, ensure clarity. *Self-correction: `AudioProcessor` is currently in the global namespace, and other components like `SourceInputProcessor` include `../c_utils/audio_processor.h`. For this refactor, we will keep it in the global namespace to minimize disruption to existing includes, unless a broader namespacing effort is undertaken.*

4.  **Class Definition Update:**
    *   **Constructor:** Update to `explicit AudioProcessor(const AudioProcessorConfig& config);`
    *   **Constants:** Add `static constexpr` members for `MAX_CHANNELS_`, `EQ_BANDS_`, `CHUNK_SIZE_BYTES_`, `OVERSAMPLING_FACTOR_`.
    *   **Member Variables:**
        *   Declare the `AudioProcessorConfig config_;` member or individual configuration members (e.g., `input_channels_`, `log_context_id_`).
        *   Update declarations for Biquad filters: `std::vector<std::vector<std::unique_ptr<Biquad>>> filters_;`, `std::vector<std::unique_ptr<Biquad>> dc_filters_;`.
        *   Update declarations for sampler states: `std::unique_ptr<SRC_STATE, SrcStateDeleter> sampler_;`, `std::unique_ptr<SRC_STATE, SrcStateDeleter> downsampler_;` (and include `SrcStateDeleter` definition or forward declaration if it's in the .cpp).
        *   Update declarations for all internal buffers to `std::vector` with trailing underscore names (e.g., `std::vector<uint8_t> receive_buffer_;`).
        *   Update `eq` and `speaker_mix` to `std::array` or ensure their C-style array declarations use the new `static constexpr` constants and trailing underscore names (e.g., `float eq_[EQ_BANDS_];`).
        *   Update all other member variables (e.g., `volume_`, `scale_buffer_pos_`, `is_processing_required_cache_`) to use trailing underscores.
        *   Remove declarations for `monitor_thread`, `monitor_running`, and `scaled_buffer_int8`.
    *   **Public Methods:** Ensure `processAudio`, `setVolume`, `setEqualizer` are correctly declared.
    *   **Protected/Private Methods:** Ensure all internal helper methods (`updateSpeakerMix`, `setupBiquad`, etc.) are declared in the appropriate section (likely private) and use updated member names.

5.  **`SrcStateDeleter`:**
    *   If the `SrcStateDeleter` struct is small and only used by `AudioProcessor`, it can be defined directly within the private section of the `AudioProcessor` class in the header, or in the .cpp file if preferred.

**Acceptance Criteria:**

*   `audio_processor.h` accurately reflects all refactoring changes:
    *   Correct constructor signature.
    *   `static constexpr` constants are defined.
    *   Member variables use `std::unique_ptr` for managed resources.
    *   Member variables for buffers are `std::vector`.
    *   All member variables follow the trailing underscore naming convention.
    *   Obsolete members are removed.
*   Necessary headers are included, and include guards are correct.
*   The header file is clean and well-organized.
*   The project compiles successfully with the updated header.
