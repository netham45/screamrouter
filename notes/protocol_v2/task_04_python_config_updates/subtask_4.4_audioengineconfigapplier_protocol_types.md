# Sub-Task 4.4: Update `AudioEngineConfigApplier` for Protocol Types

**Objective:** Modify `AudioEngineConfigApplier::apply_state` in C++ to interpret the `protocol_type` field from `SourceConfig` and `SinkConfig` and call the appropriate `AudioManager` methods for setting up different types of network senders and receivers (Legacy Scream, RTP, WebRTC).

**Parent Task:** [Python Configuration System Updates for Protocol v2](../task_04_python_config_updates.md)
**Previous Sub-Task:** [Sub-Task 4.3: Update C++ Config Structs and `ConfigurationManager` Translation](./subtask_4.3_cpp_config_structs_translation.md)

## Key Steps & Considerations:

1.  **Review `AudioEngineConfigApplier::apply_state` Logic:**
    *   **File:** `src/configuration/audio_engine_config_applier.cpp`
    *   Currently, this method likely iterates through desired sources and sinks and calls generic `AudioManager::add_source`, `AudioManager::add_sink`, `AudioManager::update_source`, `AudioManager::update_sink`.
    *   These `AudioManager` methods will need to be enhanced or new ones created to handle the `protocol_type` and associated protocol-specific configurations.

2.  **Modify `AudioManager` Interface (Conceptual - to be implemented in respective tasks):**
    *   `AudioManager`'s methods for adding/updating sources and sinks need to be aware of `protocol_type`.
    *   **Option 1 (Modify existing methods):**
        *   `AudioManager::add_source(const SourceConfig& config)`: Inside this method, `AudioManager` checks `config.protocol_type`.
            *   If `LEGACY_SCREAM`, instantiate `RawScreamReceiver` or `PerProcessScreamReceiver`.
            *   If `RTP`, instantiate `RtpReceiver`, passing `config.rtp_config`.
            *   If `SIP_MANAGED` and it's an RTP source via SIP, instantiate `RtpReceiver` using details from `config.rtp_config` (which might have been populated by SIP/SDP info).
        *   `AudioManager::add_sink(const SinkConfig& config)`: Inside, `SinkAudioMixer` is created. The `SinkConfig` (including `protocol_type`, `rtp_config`, `webrtc_config`) is passed to `SinkAudioMixer`. `SinkAudioMixer` then instantiates the correct `INetworkSender` (`ScreamSender`, `RTPSender`, `WebRTCSender`).
    *   **Option 2 (New specific methods - less likely if `SinkConfig` is comprehensive):**
        *   `AudioManager::add_rtp_source(const SourceConfig& config)`
        *   This might be overly complex if `SourceConfig` itself can fully describe the source.

    *For this sub-task, we assume Option 1: `AudioManager`'s existing `add_source`/`add_sink` (and update methods) will internally switch based on `protocol_type` within the passed `SourceConfig`/`SinkConfig`.*

3.  **Update `AudioEngineConfigApplier::apply_state`:**
    *   The core logic of comparing current state with desired state remains.
    *   When a new source/sink is detected in the desired state:
        *   The `SourceConfig` / `SinkConfig` passed to `audio_manager_->add_source(source_config)` or `audio_manager_->add_sink(sink_config)` will now naturally contain the `protocol_type` and the relevant nested config (`rtp_config`, `webrtc_config`).
    *   When an existing source/sink needs an update:
        *   `audio_manager_->update_source(source_config)` or `audio_manager_->update_sink(sink_config)` will be called.
        *   `AudioManager` (and `SinkAudioMixer` for sinks) must handle potential changes in `protocol_type` or its parameters. This could be complex (e.g., tearing down an old sender/receiver and creating a new one). Initially, updates to protocol-specific parameters for an *existing* device of the *same* protocol type should be prioritized. Changing `protocol_type` itself might be treated as a remove + add.

    ```cpp
    // In src/configuration/audio_engine_config_applier.cpp
    // void AudioEngineConfigApplier::apply_state(
    //     const std::map<std::string, SourceConfig>& desired_sources,
    //     const std::map<std::string, SinkConfig>& desired_sinks,
    //     /* ... other params ... */) {

    //     // ... (logic for finding current sources/sinks) ...

    //     // Add new sources
    //     for (const auto& pair : desired_sources) {
    //         const std::string& id = pair.first;
    //         const SourceConfig& config = pair.second;
    //         if (current_sources.find(id) == current_sources.end()) {
    //             screamrouter_logger::info("ConfigApplier: Adding new source {} with protocol_type {}", id, static_cast<int>(config.protocol_type));
    //             audio_manager_->add_source(config); // AudioManager::add_source handles protocol_type
    //         }
    //     }

    //     // Update existing sources
    //     for (const auto& pair : desired_sources) {
    //         const std::string& id = pair.first;
    //         const SourceConfig& config = pair.second;
    //         auto it = current_sources.find(id);
    //         if (it != current_sources.end()) {
    //             // TODO: Implement robust update logic in AudioManager
    //             // For now, a simple update call. AudioManager needs to diff and act.
    //             // If protocol_type changes, it's effectively a remove + add.
    //             screamrouter_logger::info("ConfigApplier: Updating source {} with protocol_type {}", id, static_cast<int>(config.protocol_type));
    //             audio_manager_->update_source(config); 
    //         }
    //     }
        
    //     // Remove old sources
    //     // ...

    //     // Similar logic for Sinks:
    //     // Add new sinks
    //     for (const auto& pair : desired_sinks) {
    //         const std::string& id = pair.first;
    //         const SinkConfig& config = pair.second;
    //         if (current_sinks.find(id) == current_sinks.end()) {
    //             screamrouter_logger::info("ConfigApplier: Adding new sink {} with protocol_type {}", id, static_cast<int>(config.protocol_type));
    //             audio_manager_->add_sink(config); // AudioManager passes full config to SinkAudioMixer
    //         }
    //     }
        
    //     // Update existing sinks
    //     // ...
        
    //     // Remove old sinks
    //     // ...
    // }
    ```

## Code Alterations:

*   **`src/configuration/audio_engine_config_applier.cpp`:**
    *   No major structural changes might be needed if `AudioManager::add_source/sink` and `update_source/sink` are designed to take the full `SourceConfig`/`SinkConfig` and handle the `protocol_type` internally.
    *   The main change is ensuring that the `SourceConfig` and `SinkConfig` objects being passed *contain* the correctly translated `protocol_type` and nested `rtp_config`/`webrtc_config` (which was handled in Sub-Task 4.3).
*   **`src/audio_engine/audio_manager.h` & `.cpp` (Crucial Implementation):**
    *   The bulk of the logic change resides here. `AudioManager::add_source` must now have a switch or if-else based on `config.protocol_type` to instantiate the correct receiver type (`RtpReceiver`, `RawScreamReceiver`, etc.).
    *   `AudioManager::add_sink` will pass the full `SinkConfig` to `SinkAudioMixer`, which then uses `config.protocol_type` to choose and initialize the correct `INetworkSender`.
    *   `update_source` and `update_sink` methods need to be robust. If `protocol_type` changes, it's simplest to treat it as a remove-then-add operation. If only parameters within a protocol change (e.g., RTP port), the existing component might be reconfigurable, or it might also need to be recreated.

## Recommendations:

*   **Focus on `AudioManager`:** The primary implementation effort for this sub-task lies in making `AudioManager` (and `SinkAudioMixer` for senders) protocol-aware. `AudioEngineConfigApplier`'s role is more about ensuring the *correctly populated* `SourceConfig`/`SinkConfig` (with protocol details) reaches `AudioManager`.
*   **Update Logic:** Handling updates where `protocol_type` changes can be complex. A remove-and-add strategy is often the most straightforward and robust way to handle such fundamental changes. Updates to parameters *within* the same protocol (e.g., changing an RTP port) might be handled by re-initializing the existing component if the underlying libraries support it, or by recreating the component.
*   **Logging:** Add clear logs in `AudioEngineConfigApplier` and `AudioManager` indicating which protocol is being set up for a source/sink.

## Acceptance Criteria:

*   `AudioEngineConfigApplier::apply_state` correctly passes `SourceConfig` and `SinkConfig` (containing `protocol_type` and protocol-specific configs) to `AudioManager`.
*   `AudioManager` (and `SinkAudioMixer` for sinks) demonstrates the ability to differentiate setup logic based on `protocol_type`. (Full implementation of this differentiation is part of other tasks like Task 1 for RTP, Task 2 for WebRTC).
*   The system can apply configurations specifying different protocol types for sources and sinks without crashing, and logs indicate the correct protocol path is being initiated in `AudioManager`.
