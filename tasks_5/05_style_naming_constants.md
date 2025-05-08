# Task 5: `AudioProcessor` - Code Style, Naming, and Constants

**Objective:**
Enforce consistent code style, naming conventions (particularly for member variables and constants), and replace `#define` macros with `static constexpr` variables.

**Details:**

1.  **Member Variable Naming:**
    *   Rename all private and protected member variables in `AudioProcessor` to use a trailing underscore.
    *   Examples:
        *   `inputChannels` -> `input_channels_`
        *   `outputChannels` -> `output_channels_`
        *   `inputBitDepth` -> `input_bit_depth_`
        *   `inputSampleRate` -> `input_sample_rate_`
        *   `outputSampleRate` -> `output_sample_rate_`
        *   `volume` -> `volume_`
        *   `eq` (array) -> `eq_` (will become `std::array` or similar, or remain as a C-style array if `std::array` is not used for it)
        *   `speaker_mix` -> `speaker_mix_`
        *   `receive_buffer` -> `receive_buffer_` (will become `std::vector`)
        *   `scaled_buffer` -> `scaled_buffer_` (will become `std::vector`)
        *   ...and so on for all other member buffers and state variables like `scale_buffer_pos`, `isProcessingRequiredCache`, etc.

2.  **Constants (`#define` to `static constexpr`):**
    *   Identify all `#define` macros used for constants within `audio_processor.h` and `audio_processor.cpp`.
    *   Replace them with `static constexpr` members within the `AudioProcessor` class definition in `audio_processor.h`.
    *   Examples:
        *   `#define MAX_CHANNELS 8` -> `static constexpr int MAX_CHANNELS_ = 8;`
        *   `#define EQ_BANDS 18` -> `static constexpr int EQ_BANDS_ = 18;`
        *   `#define CHUNK_SIZE 1152` (in .h) -> `static constexpr int CHUNK_SIZE_BYTES_ = 1152;` (assuming it's bytes, clarify if it's samples)
        *   `#define OVERSAMPLING_FACTOR 2` (in .cpp) -> Move to `.h` as `static constexpr int OVERSAMPLING_FACTOR_ = 2;`
    *   Update all usages of these macros to use the new `static constexpr` members (e.g., `AudioProcessor::MAX_CHANNELS_`).

3.  **Code Formatting:**
    *   Review `audio_processor.cpp` and `audio_processor.h` for consistent code formatting (indentation, spacing, brace style) to match the prevailing style in the `screamrouter::audio` codebase. This is often 2-space or 4-space indentation and consistent brace placement.

4.  **`eq` and `speaker_mix` Arrays:**
    *   The `eq` array (`float eq[EQ_BANDS]`) and `speaker_mix` array (`float speaker_mix[MAX_CHANNELS][MAX_CHANNELS]`) are fixed-size based on constants.
    *   Consider changing them to `std::array<float, EQ_BANDS_> eq_;` and `std::array<std::array<float, MAX_CHANNELS_>, MAX_CHANNELS_> speaker_mix_;` for better type safety and modern C++ style.
    *   If changed to `std::array`, update initialization and access patterns accordingly (e.g., `std::fill(eq_.begin(), eq_.end(), 1.0f)`).

**Acceptance Criteria:**

*   All private/protected member variables in `AudioProcessor` use a trailing underscore.
*   `#define` macros for constants (`MAX_CHANNELS`, `EQ_BANDS`, `CHUNK_SIZE`, `OVERSAMPLING_FACTOR`) are replaced with `static constexpr` members in `AudioProcessor`.
*   Usages of these constants are updated to refer to the `static constexpr` members.
*   Code formatting in `audio_processor.cpp` and `audio_processor.h` is consistent with the project's style.
*   `eq_` and `speaker_mix_` arrays are considered for conversion to `std::array` and updated if deemed appropriate.
*   The code compiles and all existing audio processing functionality remains intact.
