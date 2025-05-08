# Task 5: Modify SourceInputProcessor Config Handling

**Goal**: Update `SourceInputProcessor` to store the expected input protocol type and the current input format settings of its internal `AudioProcessor`.

**Files to Modify**:
*   `src/audio_engine/source_input_processor.h`
*   `src/audio_engine/source_input_processor.cpp`

**Steps**:

1.  **Add Member Variables** (`source_input_processor.h`):
    *   Add a private member variable to store the protocol type determined during construction.
      ```cpp
      InputProtocolType m_protocol_type;
      ```
    *   Add private member variables to store the current input format settings of the internal `AudioProcessor`. Initialize them to indicate an unconfigured state (e.g., 0 or -1).
      ```cpp
      int m_current_ap_input_channels = 0;
      int m_current_ap_input_samplerate = 0;
      int m_current_ap_input_bitdepth = 0;
      ```

2.  **Modify Constructor** (`source_input_processor.cpp`):
    *   In the `SourceInputProcessor` constructor's initializer list, initialize `m_protocol_type` using the `protocol_type` field from the passed `SourceProcessorConfig`.
      ```cpp
      SourceInputProcessor::SourceInputProcessor(
          SourceProcessorConfig config, // config now includes instance_id and protocol_type
          // ... other params ...
      )
          : config_(std::move(config)),
            m_protocol_type(config_.protocol_type), // Initialize protocol type
            // ... other initializers ...
            m_current_ap_input_channels(0), // Initialize current format state
            m_current_ap_input_samplerate(0),
            m_current_ap_input_bitdepth(0)
      {
          // ... existing constructor body ...
          // Remove the initial call to initialize_audio_processor() here.
          // It will be initialized dynamically when the first valid packet arrives
          // OR potentially initialize with defaults if needed for RTP type?
          // Let's defer initialization until the first packet or reconfiguration logic.
          // Set audio_processor_ to nullptr initially.
          audio_processor_ = nullptr;
          LOG("Initialization complete. Protocol Type: " + std::to_string(static_cast<int>(m_protocol_type)));
      }
      ```
    *   **Remove** the direct call to `initialize_audio_processor()` from the constructor body. The `AudioProcessor` will now be created/recreated dynamically within the processing logic (Task 6). Set `audio_processor_` to `nullptr` initially.

3.  **Remove `initialize_audio_processor` Method** (`.h` and `.cpp`):
    *   Since the `AudioProcessor` is now created/recreated dynamically in the processing logic, the separate `initialize_audio_processor` method is no longer needed and can be removed.
