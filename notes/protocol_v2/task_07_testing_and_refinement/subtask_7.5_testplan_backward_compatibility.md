# Sub-Task 7.5: Test Plan for Backward Compatibility with Legacy Scream Protocol

**Objective:** Ensure that existing legacy Scream clients (both senders and receivers) continue to function correctly with ScreamRouter when configured with `protocol_type: "scream"`, alongside new Protocol v2 functionalities.

**Parent Task:** [Protocol v2 Testing and Refinement Strategy](../task_07_testing_and_refinement.md)
**Previous Sub-Task:** [Sub-Task 7.4: Test Plan for UI Configuration and Control](./subtask_7.4_testplan_ui_config_control.md)

## Tools:

*   ScreamRouter instance running with Protocol v2 features.
*   Existing legacy Scream sender clients (e.g., Windows sender application, ESP32 sender).
*   Existing legacy Scream receiver clients (e.g., Windows receiver application, ESP32 receiver, Android app).
*   Access to ScreamRouter `config.yaml` and logs.
*   Wireshark for packet inspection (UDP traffic on legacy Scream ports).

## Test Cases:

1.  **TC-LEGACY-SRC-001: Legacy Scream Source Input**
    *   **Preconditions:**
        *   ScreamRouter running.
        *   In `config.yaml` or via UI, a Source is configured with `protocol_type: "scream"` (or if `protocol_type` is absent, it defaults to "scream").
        *   The Source is configured with a specific listening port (e.g., 4010).
        *   This Scream source is routed to a known working output (e.g., a local speaker sink, or a new RTP/WebRTC sink that has been tested).
    *   **Steps:**
        1.  Start a legacy Scream sender client, configured to send audio to ScreamRouter's IP and the port specified for the "scream" source (e.g., 4010).
        2.  Send audio from the legacy client.
    *   **Verification:**
        *   Audio from the legacy Scream sender is successfully received by ScreamRouter and played out through the routed sink.
        *   ScreamRouter logs show activity for the legacy Scream receiver (`RawScreamReceiver` or `PerProcessScreamReceiver`).
        *   No errors related to processing legacy Scream packets.
        *   Wireshark shows UDP packets arriving on port 4010 with the expected 5-byte Scream header and PCM payload.
    *   **Pass/Fail.**

2.  **TC-LEGACY-SNK-001: Legacy Scream Sink Output**
    *   **Preconditions:**
        *   ScreamRouter running.
        *   An audio source is active in ScreamRouter (e.g., a test tone, an RTP input source, or a legacy Scream input source from TC-LEGACY-SRC-001).
        *   In `config.yaml` or via UI, a Sink is configured with `protocol_type: "scream"`.
        *   The Sink is configured with the IP address and port of a legacy Scream receiver client.
        *   The active audio source is routed to this legacy Scream sink.
    *   **Steps:**
        1.  Start the legacy Scream receiver client, configured to listen on the specified IP/port.
    *   **Verification:**
        *   Audio from ScreamRouter is successfully received and played by the legacy Scream receiver client.
        *   ScreamRouter logs show activity for the legacy Scream sender (`ScreamSender` instance within `SinkAudioMixer`).
        *   No errors related to sending legacy Scream packets.
        *   Wireshark shows UDP packets being sent from ScreamRouter to the client's IP/port, with the 5-byte Scream header and PCM payload.
    *   **Pass/Fail.**

3.  **TC-LEGACY-MIX-001: Coexistence of Legacy and Protocol v2 Devices**
    *   **Preconditions:** ScreamRouter running.
    *   **Steps:**
        1.  Configure and run a mix of devices:
            *   A legacy Scream source sending to ScreamRouter.
            *   A legacy Scream sink receiving from ScreamRouter.
            *   An RTP source sending to ScreamRouter.
            *   An RTP sink receiving from ScreamRouter.
            *   (If WebRTC is stable) A WebRTC sink receiving from ScreamRouter.
            *   (If SIP is stable) A SIP-registered device sending/receiving RTP.
        2.  Create routes between these various sources and sinks (e.g., legacy source to RTP sink, RTP source to legacy sink, legacy source to legacy sink, RTP source to RTP sink).
    *   **Verification:**
        *   All configured audio routes function correctly without interference.
        *   Legacy devices operate as expected.
        *   Protocol v2 devices operate as expected.
        *   ScreamRouter remains stable and responsive.
        *   Logs do not show errors related to protocol conflicts or resource mismanagement.
    *   **Pass/Fail.**

4.  **TC-LEGACY-CONF-001: Configuration Defaulting to Legacy**
    *   **Preconditions:** ScreamRouter running.
    *   **Steps:**
        1.  Add a new Source or Sink via the UI or API *without* explicitly specifying a `protocol_type`.
        2.  Provide only legacy parameters (IP, Port).
    *   **Verification:**
        *   The device is created and defaults to `protocol_type: "scream"`.
        *   The device functions correctly as a legacy Scream source/sink.
        *   `config.yaml` reflects `protocol_type: "scream"` (or the field might be omitted if "scream" is the implicit default when the field is missing, but explicit is better).
    *   **Pass/Fail.**

## Debugging:

*   ScreamRouter logs (C++ and Python).
*   Wireshark to inspect UDP packets on legacy ports, verifying the 5-byte header and payload.
*   Logs from legacy client applications, if available.

## Acceptance Criteria:

*   Existing legacy Scream sender clients can successfully send audio to ScreamRouter when a source is configured with `protocol_type: "scream"`.
*   Existing legacy Scream receiver clients can successfully receive audio from ScreamRouter when a sink is configured with `protocol_type: "scream"`.
*   Legacy Scream devices operate correctly alongside new Protocol v2 (RTP, WebRTC, SIP) devices without interference.
*   Configurations that omit `protocol_type` (if any are still possible) default to or are correctly handled as legacy "scream" protocol.
*   No regressions in functionality for existing legacy Scream protocol operations.
