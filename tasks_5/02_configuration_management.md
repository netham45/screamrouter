# Task 2: `AudioProcessor` - Configuration Management

**Objective:**
Introduce a dedicated configuration struct for `AudioProcessor` and update its constructor to accept this struct, aligning with the configuration patterns used elsewhere in `screamrouter::audio`.

**Details:**

1.  **Define `AudioProcessorConfig` Struct:**
    *   In `src/audio_engine/audio_types.h` (or a new dedicated header like `src/audio_engine/audio_processor_types.h` if preferred for modularity), define a new struct `AudioProcessorConfig`.
    *   This struct will encapsulate the static configuration parameters currently passed to the `AudioProcessor` constructor.

    ```cpp
    // Example for audio_types.h
    namespace screamrouter {
    namespace audio {

    // ... other types ...

    struct AudioProcessorConfig {
        int input_channels;
        int output_channels;
        int input_bit_depth;
        int input_sample_rate;
        int output_sample_rate;
        float initial_volume;
        // Add a context string for logging, e.g., parent component's ID
        std::string log_context_id; 
    };

    } // namespace audio
    } // namespace screamrouter
    ```

2.  **Update `AudioProcessor` Constructor:**
    *   Modify the `AudioProcessor` constructor in `audio_processor.h` and `audio_processor.cpp` to accept a single `const AudioProcessorConfig&` argument.
    *   Initialize member variables (e.g., `input_channels_`, `output_channels_`, `volume_`, `log_context_id_`) from the passed-in config struct using a member initializer list.

    ```cpp
    // In audio_processor.h
    class AudioProcessor {
    public:
        explicit AudioProcessor(const AudioProcessorConfig& config);
        // ...
    private:
        AudioProcessorConfig config_; // Store a copy or relevant parts
        // ... or individual members initialized from config
        // int input_channels_;
        // ...
        // std::string log_context_id_; 
    };

    // In audio_processor.cpp
    AudioProcessor::AudioProcessor(const AudioProcessorConfig& config)
        : config_(config) // if storing the whole config
        // Or initialize individual members:
        // input_channels_(config.input_channels),
        // output_channels_(config.output_channels),
        // input_bit_depth_(config.input_bit_depth),
        // input_sample_rate_(config.input_sample_rate),
        // output_sample_rate_(config.output_sample_rate),
        // volume_(config.initial_volume),
        // log_context_id_(config.log_context_id) 
    {
        // ... rest of constructor logic (e.g., buffer resizing, filter setup) ...
        // Ensure eq_ is initialized (e.g., to flat)
        // std::fill(eq_.begin(), eq_.end(), 1.0f); // If eq_ is a std::array or std::vector
    }
    ```

3.  **Update Instantiation Sites:**
    *   Identify where `AudioProcessor` is instantiated (primarily in `SourceInputProcessor.cpp` and `SinkAudioMixer.cpp`).
    *   Modify these instantiation sites to create and pass the new `AudioProcessorConfig` struct.

**Acceptance Criteria:**

*   `AudioProcessorConfig` struct is defined in `audio_types.h` (or a dedicated header).
*   `AudioProcessor` constructor is updated to accept `const AudioProcessorConfig&`.
*   Member variables in `AudioProcessor` are initialized from the config struct.
*   All instantiation sites of `AudioProcessor` are updated to use the new config struct.
*   The `initial_eq` parameter, if previously part of the constructor, is now handled by a separate `setEqualizer` call after construction, or the `eq_` member is initialized to a default flat state in the constructor. (The `AudioProcessorConfig` example above omits `initial_eq` as `setEqualizer` is typically called separately).
