# Sub-Task 4.5: Test Updated Configuration System

**Objective:** Thoroughly test the updated Python configuration system, including loading and saving `config.yaml` with new Protocol v2 fields, correct handling of SIP-driven device updates by `ConfigurationManager`, and accurate translation of configurations to the C++ audio engine.

**Parent Task:** [Python Configuration System Updates for Protocol v2](../task_04_python_config_updates.md)
**Previous Sub-Task:** [Sub-Task 4.4: Update `AudioEngineConfigApplier` for Protocol Types](./subtask_4.4_audioengineconfigapplier_protocol_types.md)

## Key Testing Scenarios:

1.  **YAML Load/Save and Backward Compatibility:**
    *   **Test A (New Config):**
        *   Manually create a `config.yaml` with new Protocol v2 fields:
            *   Sources/Sinks with `protocol_type: "rtp"` and populated `rtp_config`.
            *   Sinks with `protocol_type: "webrtc"` and populated `webrtc_config`.
            *   Sources/Sinks with `protocol_type: "sip"`, `uuid`, and `sip_contact_uri`.
        *   Start ScreamRouter.
        *   **Verification:** Check that all fields are loaded correctly into `ConfigurationManager`'s Pydantic models. Check that C++ translation via `_translate_config_to_cpp_desired_state` populates C++ structs accurately (inspect logs or use a debugger if necessary).
    *   **Test B (Old Config):**
        *   Use an existing `config.yaml` from before Protocol v2 changes (i.e., only "scream" protocol devices).
        *   Start ScreamRouter.
        *   **Verification:** Ensure it loads without errors. New fields (`protocol_type`, `uuid`, `rtp_config`, etc.) should take their Pydantic default values (e.g., `protocol_type="scream"`). The application should function as before for these legacy devices.
    *   **Test C (Save and Reload):**
        *   Start with an old config, make some changes via API/UI (if available for new fields) or programmatically modify `ConfigurationManager` state.
        *   Trigger a save (`ConfigurationManager.__save_config()`).
        *   Inspect the saved `config.yaml` to ensure new fields are present and correctly formatted (e.g., `exclude_none=True` behavior).
        *   Restart ScreamRouter and verify the saved config loads correctly.

2.  **SIP-Driven Configuration Changes:**
    *   **Test A (New Device Registration):**
        *   With an empty or minimal `config.yaml`, have a SIP client send a `REGISTER` request (with SDP capabilities).
        *   **Verification:**
            *   `ConfigurationManager.handle_sip_registration` is called.
            *   A new `SourceDescription` or `SinkDescription` is created in memory with `protocol_type: "sip"` (or "rtp"/"webrtc" if derived from SDP), `uuid`, `sip_contact_uri`, and `rtp_config`/`webrtc_config` populated from SDP.
            *   `config.yaml` is updated with the new device.
            *   The C++ audio engine is updated (e.g., a new `RtpReceiver` is created if the SDP indicated an RTP source).
    *   **Test B (Existing Device Re-registers/Updates):**
        *   Have an existing SIP device in `config.yaml` send a new `REGISTER` (e.g., with a changed Contact port or modified SDP).
        *   **Verification:** The existing entry in `ConfigurationManager` and `config.yaml` is updated (IP, port, capabilities, `enabled=True`).
    *   **Test C (Device Offline/Timeout):**
        *   A registered SIP device stops sending keep-alives.
        *   **Verification:** After the timeout period, `SipManager` calls `ConfigurationManager.handle_sip_device_offline`. The device's `enabled` status (or `online_status`) in `ConfigurationManager` and `config.yaml` is set to `False`. The C++ engine is updated (e.g., the corresponding RTP stream is stopped/removed).

3.  **C++ Translation Accuracy (`_translate_config_to_cpp_desired_state`):**
    *   **Test:** For various configurations (RTP source, RTP sink, WebRTC sink, SIP-managed device that resolves to RTP), step through or log the output of `_translate_config_to_cpp_desired_state`.
    *   **Verification:**
        *   `ProtocolType` enum is correctly mapped.
        *   `uuid`, `sip_contact_uri` are passed.
        *   `RTPConfigCpp` fields (ports, payload types) are correctly populated from Python `RTPConfig`.
        *   `WebRTCConfigCpp` fields (STUN/TURN server lists and credentials) are correctly populated.
        *   Ensure that if a Python config field is `None` (e.g., `rtp_config.destination_port`), the C++ struct gets a sensible default (e.g., 0 or as defined in C++ struct).

4.  **`AudioEngineConfigApplier` Behavior:**
    *   **Test:** Trigger configuration changes that result in adding, updating, and removing sources/sinks of different protocol types.
    *   **Verification:**
        *   Observe logs from `AudioEngineConfigApplier` and `AudioManager`.
        *   Confirm that `AudioManager` is attempting to set up the correct type of network receiver/sender based on the `protocol_type` received in `SourceConfig`/`SinkConfig`. (Full functionality of these components is tested in their respective task modules, e.g., Task 1 for RTP).

## Debugging Tools & Techniques:

*   **Logging:** Add extensive logging in `ConfigurationManager` (especially in `__load_config`, `__save_config`, `handle_sip_*` methods, and `_translate_config_to_cpp_desired_state`).
*   **Python Debugger:** Use `pdb` or an IDE debugger to step through `ConfigurationManager` logic.
*   **Inspect `config.yaml`:** Manually review the YAML file after saves.
*   **Inspect C++ State:** If possible (e.g., via debug prints in C++ or a C++ debugger attached to the Python process), verify the state of C++ `SourceConfig`/`SinkConfig` objects after `AudioEngineConfigApplier::apply_state`.

## Code Alterations:

*   Primarily involves adding comprehensive logging and potentially assertions in `ConfigurationManager` to verify state during testing.
*   Refinements to Pydantic models or C++ structs if inconsistencies or missing defaults are found.
*   Refinements to the SIP event handling methods in `ConfigurationManager` based on test outcomes.

## Recommendations:

*   **Structured Test Cases:** Create a list of specific configuration scenarios to test, covering different protocol types and combinations of fields.
*   **Automated Tests (Future):** While initial testing is manual/log-based, consider how parts of this could be covered by unit or integration tests in Python (e.g., testing `_translate_config_to_cpp_desired_state` with mock C++ objects, testing Pydantic model loading/saving).

## Acceptance Criteria:

*   `config.yaml` can be loaded and saved correctly with all new Protocol v2 fields, maintaining data integrity.
*   Loading older `config.yaml` files works, and new fields adopt appropriate default values.
*   `ConfigurationManager` methods (`handle_sip_registration`, `handle_sip_device_offline`, etc.) correctly update the in-memory configuration and persist changes.
*   The translation from Python Pydantic models to C++ configuration structs in `_translate_config_to_cpp_desired_state` is accurate for all protocol types and their specific parameters.
*   `AudioEngineConfigApplier` receives correctly populated C++ configs and initiates the appropriate actions in `AudioManager` based on `protocol_type`.
