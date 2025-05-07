# Task 01_02: Modify `SourceConfig` and `SourceProcessorConfig` in `audio_types.h`

**Objective:** Update the existing C++ structs `SourceConfig` and `SourceProcessorConfig` in `src/audio_engine/audio_types.h` to include fields for specifying the target output audio format (channels and samplerate). This is essential for the `SourceInputProcessor` to correctly configure its internal `AudioProcessor` to produce audio in the format required by its designated sink.

**File to Modify:** `src/audio_engine/audio_types.h`

**Details:**

1.  **Locate `SourceConfig` Struct:**
    *   Find the `struct SourceConfig` within the `screamrouter::audio` namespace.
    *   This struct is currently used to pass initial settings from Python (via bindings) to `AudioManager::configure_source`.

2.  **Add Target Output Format Fields to `SourceConfig`:**
    *   Add the following new members to `struct SourceConfig`:
        *   `int target_output_channels;`: To specify the number of output channels this source instance should produce (matching the target sink).
        *   `int target_output_samplerate;`: To specify the output sample rate this source instance should produce (matching the target sink).
    *   **Default Values:** Consider appropriate default values. For instance, if these are not explicitly set by Python, `AudioManager` might default them or it might be an error. For now, let's assume they will be explicitly set. If defaults are needed, perhaps `-1` or `0` to indicate "not set" or "use input format," but explicit setting is cleaner for the new model.
        *   Let's add defaults that are clearly indicative of needing to be overridden, or rely on the Python side to always provide them. For C++ struct initialization, we can provide some common defaults.
        *   `int target_output_channels = 2; // Default to stereo`
        *   `int target_output_samplerate = 48000; // Default to 48kHz`

3.  **Locate `SourceProcessorConfig` Struct:**
    *   Find the `struct SourceProcessorConfig` within the `screamrouter::audio` namespace.
    *   This struct is used internally by `AudioManager` to configure a `SourceInputProcessor` instance. It already has `output_channels` and `output_samplerate`.

4.  **Ensure `SourceProcessorConfig` Fields are Correctly Utilized:**
    *   The existing fields `int output_channels` and `int output_samplerate` in `SourceProcessorConfig` are already what we need.
    *   The key change will be in `AudioManager::configure_source` (Task `01_03`) to populate these fields in `SourceProcessorConfig` from the new `target_output_channels` and `target_output_samplerate` fields of the incoming `SourceConfig`.
    *   No direct structural change is needed for `SourceProcessorConfig` itself for these specific fields, but we are noting their importance here.

**Modified `SourceConfig` Example:**

```cpp
// Within src/audio_engine/audio_types.h

namespace screamrouter {
namespace audio {

// ... (other structs)

struct SourceConfig {
    std::string tag;
    float initial_volume = 1.0f;
    std::vector<float> initial_eq; // Size EQ_BANDS expected by AudioProcessor
    int initial_delay_ms = 0;
    // Timeshift duration is often global or sink-related, managed in SourceInputProcessor config

    // --- NEW FIELDS ---
    int target_output_channels = 2;    // Target output channels for this source path
    int target_output_samplerate = 48000; // Target output samplerate for this source path
    // --- END NEW FIELDS ---
};

// ...

struct SourceProcessorConfig {
    std::string instance_id; 
    std::string source_tag; 
    // Target format (NOW to be explicitly set from SourceConfig's new fields)
    int output_channels = 2;         // This will be populated from SourceConfig.target_output_channels
    int output_samplerate = 48000;   // This will be populated from SourceConfig.target_output_samplerate
    // Initial settings
    float initial_volume = 1.0f; // Corrected default to 1.0f to match typical usage
    std::vector<float> initial_eq; // Default constructor will handle empty, or AudioManager can default
    int initial_delay_ms = 0;
    int timeshift_buffer_duration_sec = 5;

    // Constructor to initialize eq_values if not done by default vector behavior
    SourceProcessorConfig() : initial_eq(EQ_BANDS, 1.0f) {} // Ensure EQ is initialized
};

// ... (other structs)

} // namespace audio
} // namespace screamrouter
```
*Self-correction: `SourceProcessorConfig::initial_volume` was `1`, changed to `1.0f`. `initial_eq` in `SourceProcessorConfig` should also be initialized, similar to `AppliedSourcePathParams`.*

**Acceptance Criteria:**

*   The `struct SourceConfig` in `src/audio_engine/audio_types.h` is modified to include `target_output_channels` and `target_output_samplerate` members with appropriate default values.
*   The `struct SourceProcessorConfig` in `src/audio_engine/audio_types.h` is reviewed, and its existing `output_channels` and `output_samplerate` fields are confirmed as suitable for storing the target format. Its constructor ensures `initial_eq` is properly sized and defaulted.
*   The changes are consistent with the overall goal of enabling `SourceInputProcessor` to be configured for a specific sink's output requirements.
