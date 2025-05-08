# Task 7: Pass Protocol Type in AudioManager

**Goal**: Modify `AudioManager` methods (`configure_source`, `remove_source`) to handle the `InputProtocolType` and register/unregister `SourceInputProcessor` queues with the correct receiver type (`RtpReceiver` or `RawScreamReceiver`).

**Files to Modify**:
*   `src/audio_engine/audio_manager.h` (Potentially add helper method declaration)
*   `src/audio_engine/audio_manager.cpp`

**Steps**:

1.  **Modify `configure_source`** (`audio_manager.cpp`):
    *   Inside `configure_source`, after creating `validated_config` from the input `SourceConfig`:
    *   Determine the `InputProtocolType` based on `validated_config.protocol_type_hint`.
        ```cpp
        InputProtocolType proto_type = (validated_config.protocol_type_hint == 1) ?
                                           InputProtocolType::RAW_SCREAM_PACKET :
                                           InputProtocolType::RTP_SCREAM_PAYLOAD;
        ```
    *   Set this `proto_type` in the `SourceProcessorConfig` (`proc_config`) before creating the `SourceInputProcessor`.
        ```cpp
        // ... inside configure_source, after creating proc_config ...
        proc_config.protocol_type = proto_type;
        // ... rest of proc_config setup ...
        ```
    *   **Crucially**: After successfully creating and starting the `new_source` (`SourceInputProcessor`), register its input queue (`rtp_queue`) with the *correct* receiver.
        *   If `proto_type` is `InputProtocolType::RTP_SCREAM_PAYLOAD`:
            *   Check if `rtp_receiver_` exists.
            *   Call `rtp_receiver_->add_output_queue(...)` as is currently done.
            *   Handle the case where `rtp_receiver_` is null.
        *   If `proto_type` is `InputProtocolType::RAW_SCREAM_PACKET`:
            *   Determine which `RawScreamReceiver` instance should handle this source. This requires a lookup mechanism. **How is this determined?**
                *   **Assumption**: For now, assume there's only one `RawScreamReceiver` or that the `SourceConfig` needs to provide information (like the listen port of the target `RawScreamReceiver`) to identify it. Let's add a field `target_receiver_port` to `SourceConfig` and `SourceProcessorConfig` for this purpose. *(This requires revisiting Task 1 & 5)*.
                *   **Alternative**: If only one `RawScreamReceiver` is expected, find it in the `raw_scream_receivers_` map (e.g., if the map only ever holds one entry).
            *   Find the target `RawScreamReceiver` instance in the `raw_scream_receivers_` map using the determined identifier (e.g., `target_receiver_port`).
            *   If found, call `target_raw_receiver->add_output_queue(...)`.
            *   Handle the case where the target `RawScreamReceiver` is not found (log error, stop `new_source`, cleanup queues, return failure).
        *   If registration with the receiver fails, ensure the created `new_source` is stopped and queues are cleaned up before returning failure.

2.  **Modify `remove_source`** (`audio_manager.cpp`):
    *   Inside `remove_source`, after finding the `source_to_remove` (`SourceInputProcessor`) by `instance_id`:
    *   Get the `InputProtocolType` and the `source_tag` from the `source_to_remove` (e.g., `source_to_remove->get_config().protocol_type` and `source_to_remove->get_source_tag()`).
    *   If the type is `InputProtocolType::RTP_SCREAM_PAYLOAD`:
        *   Call `rtp_receiver_->remove_output_queue(source_tag_for_removal, instance_id)` as is currently done.
    *   If the type is `InputProtocolType::RAW_SCREAM_PACKET`:
        *   Determine which `RawScreamReceiver` this source was registered with (again, needs the identifier like `target_receiver_port` from the source's config).
        *   Find the target `RawScreamReceiver` instance in `raw_scream_receivers_`.
        *   If found, call `target_raw_receiver->remove_output_queue(source_tag_for_removal, instance_id)`.
        *   Handle cases where the receiver isn't found (log warning).

3.  **Revisit Task 1 & 5 (Self-Correction)**:
    *   Add `int target_receiver_port = -1;` to `SourceConfig` in `audio_types.h`.
    *   Add `int target_receiver_port = -1;` to `SourceProcessorConfig` in `audio_types.h`.
    *   Modify `AudioManager::configure_source` to copy this port from `validated_config` to `proc_config`.
    *   Modify `SourceInputProcessor` constructor (Task 5) to potentially store this if needed internally, although `AudioManager` primarily uses it.

**(Note: This task introduces complexity in how `AudioManager` associates a source configuration with the correct receiver instance, especially for `RawScreamReceiver`. The use of `target_receiver_port` is one way, but requires the configuration provider to know which receiver port to target.)**
