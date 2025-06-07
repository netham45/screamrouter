# Sub-Task 6.3: Refactor Settings Modals/Pages for Protocol Configuration

**Objective:** Update the React components responsible for adding/editing Sources and Sinks (e.g., `SettingsModal.tsx`, or specific pages like `SourceSettingsPage.tsx`, `SinkSettingsPage.tsx`) to include UI elements for selecting `protocol_type` and configuring protocol-specific parameters (RTP, WebRTC).

**Parent Task:** [UI Updates for Protocol v2 (Enhanced)](../task_06_ui_updates.md)
**Previous Sub-Task:** [Sub-Task 6.2: Implement Backend API for SIP Registered Devices](./subtask_6.2_backend_api_sip_devices.md)

## Key Steps & Considerations:

1.  **Identify Target Components:**
    *   Locate the React component(s) used for the "Add Source", "Edit Source", "Add Sink", "Edit Sink" dialogs or pages. This might be a generic modal or separate components. For this example, let's assume a `DeviceSettingsModal.tsx` that handles both sources and sinks.

2.  **Add `Protocol Type` Selector:**
    *   Introduce a dropdown/select input for `protocol_type`.
    *   Options: "Scream" (legacy), "RTP", "WebRTC", "SIP Managed".
    *   The "SIP Managed" option might be read-only if the device was discovered via SIP and its protocol is determined by that. Or, it could be a type a user selects if they expect a device to register and be managed by SIP.
    *   The selected `protocol_type` will control which subsequent configuration sections are visible.

3.  **Conditional Rendering for Protocol-Specific Settings:**
    *   Based on the selected `protocol_type`, conditionally render different form sections.
    *   **State Management:** The component's local state will need to hold the full device object, including `protocol_type` and the nested `rtp_config` and `webrtc_config` objects. Initialize these nested objects when a new protocol type is selected if they don't exist.

4.  **UI for "Scream" Protocol (Legacy):**
    *   Show existing fields: IP Address, Port.
    *   These might be hidden or read-only if `protocol_type` is not "scream".

5.  **UI for "RTP" Protocol:**
    *   **Input Fields (for Sinks):**
        *   `IP Address` (Destination IP for sending RTP).
        *   `RTP Destination Port` (e.g., `rtp_config.destination_port`).
    *   **Input Fields (for Sources):**
        *   `RTP Listening Port` (e.g., `rtp_config.source_listening_port`).
    *   **Common RTP Fields:**
        *   `Payload Type PCM`: Input for `rtp_config.payload_type_pcm`.
        *   `Payload Type MP3`: Input for `rtp_config.payload_type_mp3`.
        *   `Codec Preferences`: A multi-select, tag input, or reorderable list for `rtp_config.codec_preferences` (e.g., `["L16/48000/2", "MP3/192k"]`). This is more for future SDP use but can be stored.
        *   (Advanced/Future) `SRTP Enabled` (checkbox), `SRTP Key` (text input).
    *   Consider a "Presets" dropdown for common RTP configurations (e.g., "CD Quality PCM", "uLaw RTP").

6.  **UI for "WebRTC" Protocol (Primarily Sinks):**
    *   Display informational text: "WebRTC connections are typically negotiated via SIP signaling. Configure STUN/TURN servers for NAT traversal."
    *   **STUN Servers (`webrtc_config.stun_servers`):**
        *   A list of text inputs, allowing users to add/remove STUN server URLs (e.g., `stun:stun.l.google.com:19302`).
    *   **TURN Servers (`webrtc_config.turn_servers`):**
        *   A list of forms, where each form allows input for:
            *   `URLs` (comma-separated string or multiple input fields).
            *   `Username` (optional text input).
            *   `Credential` (optional password input).
        *   Allow adding/removing TURN server configurations.

7.  **UI for "SIP Managed" Protocol:**
    *   Many fields (IP, port, specific RTP/WebRTC params) might become read-only, as they are derived from SIP registration and SDP.
    *   Display `UUID` (read-only).
    *   Display `SIP Contact URI` (read-only).
    *   The UI might show a summary of capabilities learned via SDP.
    *   The user might still be able to configure local aspects like `name`, `volume`, `eq_config`.

8.  **Data Handling and State Management:**
    *   Use React state (e.g., `useState` with a device object) to manage form data.
    *   When `protocol_type` changes, ensure that the corresponding config object (e.g., `rtp_config`, `webrtc_config`) is initialized in the state if it's null/undefined.
        ```typescript
        // const [device, setDevice] = useState<Partial<Source | Sink>>(initialDeviceData);

        // const handleProtocolChange = (newProtocol: ProtocolType) => {
        //   setDevice(prev => {
        //     const updatedDevice = { ...prev, protocol_type: newProtocol };
        //     if (newProtocol === 'rtp' && !updatedDevice.rtp_config) {
        //       updatedDevice.rtp_config = { payload_type_pcm: 127, payload_type_mp3: 14 }; // Default values
        //     } else if (newProtocol !== 'rtp') {
        //       // updatedDevice.rtp_config = null; // Or keep for potential switch back
        //     }
        //     if (newProtocol === 'webrtc' && !updatedDevice.webrtc_config) {
        //       updatedDevice.webrtc_config = { stun_servers: ["stun:stun.l.google.com:19302"], turn_servers: [] };
        //     } else if (newProtocol !== 'webrtc') {
        //       // updatedDevice.webrtc_config = null;
        //     }
        //     return updatedDevice;
        //   });
        // };
        ```
    *   On form submission, ensure the `device` object with its nested `rtp_config` or `webrtc_config` is correctly passed to the API update/add functions.

9.  **Preserve Existing Controls:**
    *   Ensure common controls like Volume, EQ, Delay, Timeshift, Speaker Layouts are still accessible and functional regardless of the selected `protocol_type`. These are generally local processing settings.

## Code Alterations:

*   **Target Component(s) (e.g., `screamrouter-react/src/components/shared/SettingsModal.tsx`, or specific source/sink setting pages):**
    *   Add state management for `protocol_type` and nested config objects (`rtp_config`, `webrtc_config`).
    *   Implement the `Protocol Type` dropdown.
    *   Use conditional rendering (`{device.protocol_type === 'rtp' && (...) }`) to show/hide protocol-specific form sections.
    *   Create new input components for lists of STUN/TURN servers if needed.
    *   Update form submission logic to send the complete, structured device object.
*   **`screamrouter-react/src/context/AppContext.tsx` (if applicable):**
    *   Ensure the `Source` and `Sink` types used in the context state match the updated interfaces from Sub-Task 6.1.

## Recommendations:

*   **Component Reusability:** Create reusable form input components (e.g., for a list of STUN servers, or a single TURN server entry) if these patterns are complex.
*   **User Experience:**
    *   Clearly label all new fields.
    *   Provide tooltips or helper text for complex settings like payload types or STUN/TURN.
    *   When switching `protocol_type`, preserve user input in common fields if possible, but reset or hide irrelevant protocol-specific fields.
*   **Validation:** Implement basic client-side validation for new fields (e.g., port numbers, URL formats for STUN/TURN).
*   **Default Values:** Populate new configuration sections with sensible defaults when a protocol type is first selected.

## Acceptance Criteria:

*   The UI for adding/editing sources and sinks includes a "Protocol Type" selector.
*   Conditional UI sections for "Scream", "RTP", and "WebRTC" configurations are displayed based on the selected protocol type.
*   Users can input and modify RTP-specific settings (ports, payload types).
*   Users can input and modify WebRTC-specific settings (STUN/TURN server lists).
*   For "SIP Managed" devices, relevant fields are displayed (possibly read-only).
*   The form state correctly manages the nested `rtp_config` and `webrtc_config` objects.
*   On submission, the complete and correct device configuration is sent to the backend API.
*   Existing UI controls (volume, EQ, etc.) remain functional.
