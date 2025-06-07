# Sub-Task 7.4: Test Plan for UI Configuration and Control

**Objective:** Define test cases for verifying that users can correctly configure Protocol v2 related settings through the ScreamRouter web UI, and that these configurations are properly applied and reflected in the system's behavior.

**Parent Task:** [Protocol v2 Testing and Refinement Strategy](../task_07_testing_and_refinement.md)
**Previous Sub-Task:** [Sub-Task 7.3: Test Plan for WebRTC Streaming (MP3 over Data Channels)](./subtask_7.3_testplan_webrtc_streaming.md)
**Related Task:** [UI Updates for Protocol v2 (Enhanced)](../task_06_ui_updates.md) (covers the UI implementation itself)

## Tools:

*   ScreamRouter instance running with all Protocol v2 components.
*   Web browser (Chrome, Firefox) with developer tools.
*   Access to ScreamRouter `config.yaml` and logs.
*   RTP/SIP/WebRTC client tools for verifying backend changes.

## Test Cases:

1.  **TC-UI-CONF-001: Create Source/Sink with "scream" Protocol**
    *   **Steps:**
        1.  Navigate to the UI section for adding a new Source/Sink.
        2.  Select "scream" as the `protocol_type`.
        3.  Fill in required legacy fields (Name, IP, Port).
        4.  Save the new Source/Sink.
    *   **Verification:**
        *   The device appears in the UI list with "SCREAM" protocol.
        *   `config.yaml` shows the new device with `protocol_type: "scream"` and correct legacy parameters.
        *   The audio engine reflects this (e.g., a `RawScreamReceiver` or `ScreamSender` is active).
    *   **Pass/Fail.**

2.  **TC-UI-CONF-002: Create Source with "rtp" Protocol**
    *   **Steps:**
        1.  Add a new Source. Select `protocol_type: "rtp"`.
        2.  Fill in RTP-specific fields: Name, `rtp_config.source_listening_port`, `rtp_config.payload_type_pcm`.
        3.  Save.
    *   **Verification:**
        *   Device appears in UI with "RTP" protocol and correct port.
        *   `config.yaml` shows the new source with `protocol_type: "rtp"` and populated `rtp_config`.
        *   ScreamRouter starts listening on the specified RTP port (verify with logs or `netstat`).
    *   **Pass/Fail.**

3.  **TC-UI-CONF-003: Create Sink with "rtp" Protocol**
    *   **Steps:**
        1.  Add a new Sink. Select `protocol_type: "rtp"`.
        2.  Fill in: Name, Destination IP, `rtp_config.destination_port`, `rtp_config.payload_type_pcm`.
        3.  Save.
    *   **Verification:**
        *   Device appears in UI with "RTP" protocol and correct destination.
        *   `config.yaml` shows the new sink with `protocol_type: "rtp"` and populated `rtp_config`.
        *   (If audio is routed) ScreamRouter attempts to send RTP to the destination.
    *   **Pass/Fail.**

4.  **TC-UI-CONF-004: Create Sink with "webrtc" Protocol**
    *   **Steps:**
        1.  Add a new Sink. Select `protocol_type: "webrtc"`.
        2.  Fill in: Name.
        3.  Configure STUN servers in `webrtc_config.stun_servers` (e.g., add `stun:stun.l.google.com:19302`).
        4.  (Optional) Configure TURN servers in `webrtc_config.turn_servers`.
        5.  Save.
    *   **Verification:**
        *   Device appears in UI with "WEBRTC" protocol.
        *   `config.yaml` shows the new sink with `protocol_type: "webrtc"` and populated `webrtc_config` (STUN/TURN details).
    *   **Pass/Fail.**

5.  **TC-UI-CONF-005: Edit Existing Device - Change Protocol Parameters**
    *   **Steps:**
        1.  Edit an existing RTP sink.
        2.  Change `rtp_config.destination_port` and `rtp_config.payload_type_pcm`.
        3.  Save.
    *   **Verification:**
        *   UI reflects the new port and payload type.
        *   `config.yaml` is updated.
        *   Audio engine uses the new parameters (verify with logs or by sending/receiving RTP).
    *   **Pass/Fail.**

6.  **TC-UI-CONF-006: Edit Existing Device - Change `protocol_type`**
    *   **Steps:**
        1.  Edit an existing "scream" sink.
        2.  Change `protocol_type` to "rtp".
        3.  The UI should now show RTP configuration fields. Fill them in.
        4.  Save.
    *   **Verification:**
        *   UI reflects the new protocol and its settings.
        *   `config.yaml` is updated.
        *   The audio engine correctly tears down the old "scream" sender and sets up a new "rtp" sender.
    *   **Pass/Fail.**

7.  **TC-UI-CONF-007: View "SIP Registered Devices" Page**
    *   **Preconditions:** Some devices have registered via SIP.
    *   **Steps:**
        1.  Navigate to the "SIP Registered Devices" page.
        2.  Click the "Refresh" button.
    *   **Verification:**
        *   The page displays a table with correct information for registered devices (UUID, Name, Role, Contact, Status, Last Seen).
        *   Data matches the backend state.
    *   **Pass/Fail.**

8.  **TC-UI-CONF-008: Display of Protocol Information in Lists**
    *   **Steps:**
        1.  View the main Source and Sink lists (e.g., in DesktopMenu or full menu).
    *   **Verification:**
        *   Each item correctly displays its `protocol_type`.
        *   Relevant protocol-specific info (e.g., SIP contact URI snippet, RTP port) is shown or available via tooltip.
        *   UUID is accessible (e.g., via tooltip).
    *   **Pass/Fail.**

9.  **TC-UI-CONF-009: Handling of "SIP Managed" Devices in Settings**
    *   **Preconditions:** A device has registered via SIP and appears in `config.yaml` with `protocol_type: "sip"`.
    *   **Steps:**
        1.  Attempt to edit this SIP-managed device via the UI.
    *   **Verification:**
        *   Fields like IP, port, core protocol parameters (if derived from SDP) should be read-only or clearly indicate they are SIP-managed.
        *   User might be able to change local settings like Name, Volume, EQ.
        *   `protocol_type` should likely be read-only ("sip").
    *   **Pass/Fail.**

## General UI Testing Considerations:

*   **Responsiveness:** Ensure new forms and tables are responsive on different screen sizes.
*   **Error Handling:** Test API error scenarios (e.g., backend down, invalid data causing 4xx errors) and ensure the UI displays user-friendly error messages.
*   **Loading States:** Verify loading indicators are shown during data fetching or saving.
*   **Browser Compatibility:** Test on latest versions of Chrome and Firefox at a minimum.
*   **Console Errors:** Ensure no JavaScript errors or React warnings in the browser console during UI interaction.
