# Task 01_03: Update `AudioManager::configure_source`

**Objective:** Modify the `AudioManager::configure_source` method in `src/audio_engine/audio_manager.cpp` (and its declaration in `audio_manager.h`) to utilize the new `target_output_channels` and `target_output_samplerate` fields from the `SourceConfig` struct. These values will then be used to populate the `output_channels` and `output_samplerate` fields of the `SourceProcessorConfig` for the `SourceInputProcessor` being created.

**Files to Modify:**

*   `src/audio_engine/audio_manager.h` (Method declaration, if parameter type changes - though `SourceConfig` itself is the parameter, its internal structure changes)
*   `src/audio_engine/audio_manager.cpp` (Method implementation)

**Details:**

1.  **Review `AudioManager::configure_source` Declaration (in `audio_manager.h`):**
    *   The method signature is `std::string configure_source(const SourceConfig& config);`.
    *   No change to the signature itself is needed, as `SourceConfig` is passed by const reference, and we've modified `SourceConfig` internally (Task `01_02`).

2.  **Modify `AudioManager::configure_source` Implementation (in `audio_manager.cpp`):**
    *   Locate the section where `SourceProcessorConfig proc_config;` is created and populated.
    *   Currently, it might be setting `proc_config.output_channels` and `proc_config.output_samplerate` to default values (e.g., `DEFAULT_INPUT_CHANNELS`, `DEFAULT_INPUT_SAMPLERATE`).
        ```cpp
        // Current (or similar) logic:
        // SourceProcessorConfig proc_config;
        // proc_config.instance_id = instance_id;
        // proc_config.source_tag = validated_config.tag;
        // proc_config.output_channels = DEFAULT_INPUT_CHANNELS; // <-- To be changed
        // proc_config.output_samplerate = DEFAULT_INPUT_SAMPLERATE; // <-- To be changed
        // ...
        ```
    *   **Change this logic to:** Populate `proc_config.output_channels` and `proc_config.output_samplerate` from the `config` parameter (which is a `const SourceConfig&` that now contains `target_output_channels` and `target_output_samplerate`).
        ```cpp
        // New logic:
        SourceProcessorConfig proc_config; // Its constructor now initializes initial_eq
        proc_config.instance_id = instance_id;
        proc_config.source_tag = validated_config.tag; // Assuming validated_config is a copy of input 'config'
        
        // --- MODIFIED LINES ---
        // Use the target output format specified in the input SourceConfig
        proc_config.output_channels = validated_config.target_output_channels; 
        proc_config.output_samplerate = validated_config.target_output_samplerate;
        // --- END MODIFIED LINES ---
        
        proc_config.initial_volume = validated_config.initial_volume;
        proc_config.initial_eq = validated_config.initial_eq; // validated_config.initial_eq should be correctly sized
        proc_config.initial_delay_ms = validated_config.initial_delay_ms;
        // proc_config.timeshift_buffer_duration_sec remains default or could be made configurable
        ```
    *   **Validation (Optional but Recommended):**
        *   Add validation for `config.target_output_channels` and `config.target_output_samplerate` to ensure they are sensible values (e.g., channels > 0, samplerate is a common audio rate). If invalid, log an error and potentially return an empty `instance_id` or use fallback defaults.
        *   Example validation:
            ```cpp
            if (validated_config.target_output_channels <= 0 || validated_config.target_output_channels > 8) { // Max 8 channels for example
                LOG_ERROR_AM("Invalid target_output_channels (" + std::to_string(validated_config.target_output_channels) + ") for source tag " + validated_config.tag + ". Defaulting to 2.");
                validated_config.target_output_channels = 2; // Fallback
            }
            // Similar validation for samplerate
            const std::vector<int> valid_samplerates = {8000, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 192000};
            if (std::find(valid_samplerates.begin(), valid_samplerates.end(), validated_config.target_output_samplerate) == valid_samplerates.end()) {
                LOG_ERROR_AM("Invalid target_output_samplerate (" + std::to_string(validated_config.target_output_samplerate) + ") for source tag " + validated_config.tag + ". Defaulting to 48000.");
                validated_config.target_output_samplerate = 48000; // Fallback
            }
            ```
    *   Ensure that `validated_config.initial_eq` is correctly sized (e.g., `EQ_BANDS`). The existing logic for this in `configure_source` should still apply:
        ```cpp
        // Existing EQ validation logic (should be kept)
        if (!validated_config.initial_eq.empty() && validated_config.initial_eq.size() != EQ_BANDS) {
            LOG_ERROR_AM("Invalid initial EQ size for source tag " + validated_config.tag + ". Expected " + std::to_string(EQ_BANDS) + ", got " + std::to_string(validated_config.initial_eq.size()) + ". Resetting to flat.");
            validated_config.initial_eq.assign(EQ_BANDS, 1.0f);
        } else if (validated_config.initial_eq.empty()) {
             LOG_AM("No initial EQ provided for source tag " + validated_config.tag + ". Setting to flat.");
             validated_config.initial_eq.assign(EQ_BANDS, 1.0f);
        }
        // Then assign to proc_config.initial_eq
        proc_config.initial_eq = validated_config.initial_eq;
        ```
        *Self-correction: `SourceProcessorConfig`'s constructor now initializes `initial_eq`, so direct assignment `proc_config.initial_eq = validated_config.initial_eq;` is fine if `validated_config.initial_eq` is already correctly sized by the logic above.*

**Acceptance Criteria:**

*   The `AudioManager::configure_source` method in `audio_manager.cpp` correctly reads `target_output_channels` and `target_output_samplerate` from the input `SourceConfig` parameter.
*   These values are then used to set the `output_channels` and `output_samplerate` members of the `SourceProcessorConfig` struct that is passed to the `SourceInputProcessor` constructor.
*   Optional: Basic validation for the new target format parameters is added.
*   The method continues to correctly handle other aspects of `SourceConfig` (tag, volume, EQ, delay).
