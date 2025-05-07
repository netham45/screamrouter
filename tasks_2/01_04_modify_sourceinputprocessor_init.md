# Task 01_04: Update `SourceInputProcessor::initialize_audio_processor`

**Objective:** Modify the `SourceInputProcessor::initialize_audio_processor` method (and potentially the constructor if `initialize_audio_processor` is called from there) in `src/audio_engine/source_input_processor.cpp` to use the `output_channels` and `output_samplerate` from its `config_` member (which is a `SourceProcessorConfig`) when creating the `AudioProcessor` instance.

**Files to Modify:**

*   `src/audio_engine/source_input_processor.cpp` (Method implementation)
*   `src/audio_engine/source_input_processor.h` (If constructor signature or member initialization changes significantly, though unlikely for this task)

**Details:**

1.  **Locate `SourceInputProcessor::initialize_audio_processor` (in `source_input_processor.cpp`):**
    *   This method is responsible for creating and configuring the `std::unique_ptr<AudioProcessor> audio_processor_`.
    *   Currently, it might be passing default input format values and potentially hardcoded or default output format values to the `AudioProcessor` constructor.
        ```cpp
        // Current (or similar) logic:
        // audio_processor_ = std::make_unique<AudioProcessor>(
        //     DEFAULT_INPUT_CHANNELS,    // inputChannels
        //     config_.output_channels,   // outputChannels - THIS IS THE KEY FIELD
        //     DEFAULT_INPUT_BITDEPTH,    // inputBitDepth
        //     DEFAULT_INPUT_SAMPLERATE,  // inputSampleRate
        //     config_.output_samplerate, // outputSampleRate - THIS IS THE KEY FIELD
        //     current_volume_            // volume
        // );
        ```

2.  **Confirm `AudioProcessor` Constructor Parameters:**
    *   The `AudioProcessor` constructor (from `c_utils/audio_processor.h` and `.cpp`) takes:
        *   `inputChannels`
        *   `outputChannels`
        *   `inputBitDepth`
        *   `inputSampleRate`
        *   `outputSampleRate`
        *   `volume`
    *   The `config_.output_channels` and `config_.output_samplerate` members of `SourceInputProcessor::config_` (which is a `SourceProcessorConfig`) are the correct values to pass as `outputChannels` and `outputSampleRate` to the `AudioProcessor` constructor. These fields in `config_` will have been populated by `AudioManager` (Task `01_03`) based on the sink-specific requirements.

3.  **Ensure Correct Values are Passed to `AudioProcessor` Constructor:**
    *   The existing code already seems to use `config_.output_channels` and `config_.output_samplerate` for the `AudioProcessor`'s output format. This task is primarily to confirm this is the case and that no hardcoded defaults are used for the *output* format passed to `AudioProcessor`.
    *   The input format parameters (`DEFAULT_INPUT_CHANNELS`, `DEFAULT_INPUT_BITDEPTH`, `DEFAULT_INPUT_SAMPLERATE`) passed to `AudioProcessor` constructor can remain as they are, assuming the raw input audio format is consistent (e.g., 16-bit, 2ch, 48kHz from RTP). The `AudioProcessor` is responsible for converting this input format to the *specified output format*.

4.  **No Change to `SourceInputProcessor` Constructor or Header (Likely):**
    *   The `SourceInputProcessor` constructor already takes a `SourceProcessorConfig config`. Since `config_` (the member variable) is populated from this, and `initialize_audio_processor` uses `config_`, no direct changes to the constructor signature or header file for this specific aspect are anticipated.

**Example of `initialize_audio_processor` (Confirming Correct Usage):**

```cpp
// Within src/audio_engine/source_input_processor.cpp

void SourceInputProcessor::initialize_audio_processor() {
    LOG("Initializing AudioProcessor...");
    std::lock_guard<std::mutex> lock(audio_processor_mutex_);
    try {
        // Assuming input format is fixed for now (e.g., 16-bit, 2ch, 48kHz from RTP)
        // The 'config_' member now holds the target output format for this specific path.
        audio_processor_ = std::make_unique<AudioProcessor>(
            DEFAULT_INPUT_CHANNELS,      // Assumed input format channels
            config_.output_channels,     // Use target output channels from config_
            DEFAULT_INPUT_BITDEPTH,      // Assumed input format bit depth
            DEFAULT_INPUT_SAMPLERATE,    // Assumed input format sample rate
            config_.output_samplerate,   // Use target output sample rate from config_
            current_volume_              // Initial volume
        );
        // Set initial EQ
        if (audio_processor_ && !current_eq_.empty()) { // Ensure current_eq_ is populated
             audio_processor_->setEqualizer(current_eq_.data());
        } else if (current_eq_.empty()) {
            LOG("Warning: Initial EQ empty during AudioProcessor initialization. EQ will be flat.");
            // AudioProcessor likely defaults to flat if setEqualizer is not called or with null.
        }
        LOG("AudioProcessor created and configured for output: " + 
            std::to_string(config_.output_channels) + "ch @ " + 
            std::to_string(config_.output_samplerate) + "Hz.");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize AudioProcessor: " + std::string(e.what()));
        throw; 
    }
}
```
*Self-correction: Added a check for `current_eq_` being populated before calling `setEqualizer` and ensured logging reflects the configured output format.*

**Acceptance Criteria:**

*   The `SourceInputProcessor::initialize_audio_processor` method correctly uses `config_.output_channels` and `config_.output_samplerate` (from its `SourceProcessorConfig` member) as the `outputChannels` and `outputSampleRate` arguments when constructing its `AudioProcessor` instance.
*   The input format arguments passed to `AudioProcessor` constructor remain based on the assumed raw input audio format (e.g., `DEFAULT_INPUT_CHANNELS`, etc.).
*   The `AudioProcessor` is thus initialized to convert from the standard input format to the specific output format required by the sink this `SourceInputProcessor` instance is serving.
