# Sub-Task 6.7: Test All UI Changes for Protocol v2

**Objective:** Conduct thorough testing of all UI modifications related to Protocol v2, including the settings modals/pages for different protocol types, the new "SIP Registered Devices" page, and updated item displays.

**Parent Task:** [UI Updates for Protocol v2 (Enhanced)](../task_06_ui_updates.md)
**Previous Sub-Task:** [Sub-Task 6.6: Update UI State Management (`AppContext.tsx`)](./subtask_6.6_update_ui_state_management.md)

## Key Testing Scenarios:

1.  **Source/Sink Settings Modal/Page Functionality:**
    *   **Create New Source/Sink:**
        *   Test creating a new Source/Sink for each `protocol_type`: "scream", "rtp", "webrtc", "sip" (if "sip" is a selectable type for manual creation, otherwise it's auto-created).
        *   Verify that selecting a `protocol_type` shows the correct conditional form sections.
        *   Verify that all input fields for each protocol's specific configuration (RTP ports, payload types; WebRTC STUN/TURN servers) are present and accept valid input.
        *   Verify that saving a new device sends the correct data structure (including nested `rtp_config` or `webrtc_config`) to the backend API.
        *   Verify the device appears correctly in the UI lists after creation.
    *   **Edit Existing Source/Sink:**
        *   Test editing an existing device of each `protocol_type`.
        *   Verify that current settings are populated correctly in the form.
        *   Modify settings (e.g., change RTP port, add a STUN server) and save.
        *   Verify changes are persisted and reflected in the UI and backend `config.yaml`.
    *   **Switching `protocol_type` for an Existing Device:**
        *   Test changing the `protocol_type` of an existing device (e.g., from "scream" to "rtp").
        *   Verify that the UI correctly shows the new protocol's configuration section and potentially clears/resets old protocol-specific data.
        *   Verify that saving this change updates the backend correctly.
    *   **Validation and Error Handling:**
        *   Test invalid inputs (e.g., non-numeric port, malformed URLs for STUN/TURN) and verify client-side validation messages.
        *   Test scenarios where API calls might fail (e.g., backend error) and ensure UI shows appropriate error messages.
    *   **Default Values:** Ensure new protocol config sections are populated with sensible defaults when a protocol is selected for a new device.

2.  **Item Display Components (`SourceItem.tsx`, `SinkItem.tsx`, etc.):**
    *   Verify that for devices with different `protocol_type` values:
        *   The correct protocol type ("SCREAM", "RTP", "WEBRTC", "SIP") is displayed.
        *   UUID is shown (e.g., in a tooltip) if available.
        *   SIP Contact URI is shown for "sip" protocol devices.
        *   IP/Port information is displayed correctly based on the protocol (legacy IP/port, RTP destination/listen ports, or "WebRTC"/"SIP Managed" status).
    *   Test with devices that have missing optional fields (e.g., an old "scream" device loaded into the new system) to ensure graceful degradation (e.g., "N/A" or field omitted).

3.  **"SIP Registered Devices" Page (`SipDevicesPage.tsx`):**
    *   **Data Fetching and Display:**
        *   Verify the page fetches and displays a list of SIP registered devices from the `/api/sip/registered_devices` endpoint.
        *   Check that all columns (Name, UUID, Role, Contact URI, Online Status, Last Seen) are populated correctly.
        *   Verify correct formatting for timestamps (`last_seen`).
    *   **Loading and Error States:**
        *   Test the loading indicator while data is being fetched.
        *   Simulate an API error (if possible, or test with a disconnected backend) and verify an appropriate error message is shown.
        *   Verify the "No SIP devices are currently registered" message appears when the list is empty.
    *   **Refresh Button:**
        *   Test the "Refresh" button to ensure it re-fetches and updates the device list.
    *   **Dynamic Updates (if SIP registrations change on backend):**
        *   Register a new SIP device while the page is open. Click "Refresh" and verify the new device appears.
        *   Have a SIP device go offline. Click "Refresh" and verify its status changes.

4.  **Global State Integration (`AppContext.tsx`):**
    *   Verify that `Source` and `Sink` objects fetched into the global state (e.g., via initial config load or WebSocket updates) correctly include all new Protocol v2 fields.
    *   Verify that components consuming this global state (e.g., lists, modals) correctly receive and render the updated data structures.
    *   If `sipRegisteredDevices` is stored in global state, verify it's updated correctly by `fetchSipRegisteredDevices` and consumed properly by `SipDevicesPage`.

5.  **User Experience and Visual Consistency:**
    *   Ensure all new UI elements are styled consistently with the existing application theme (Material-UI).
    *   Check for readability, usability, and clarity of new forms and information displays.
    *   Ensure no console errors or warnings in the browser developer tools during interaction with new UI elements.

## Debugging Tools & Techniques:

*   **Browser Developer Tools:**
    *   **Console:** Check for React warnings, JavaScript errors, and `console.log` statements.
    *   **Network Tab:** Inspect API requests and responses for adding/editing devices and fetching SIP device lists. Verify payloads and status codes.
    *   **Components Tab (React DevTools):** Inspect component props and state to understand data flow and rendering logic.
*   **UI State Inspection:** If using Redux DevTools or similar for Zustand/Context, inspect the global state to verify `Source`, `Sink`, and `SipDeviceStatus` data.

## Code Alterations:

*   Primarily involves interacting with the UI and backend. Code changes would be fixes or refinements based on issues found during testing.
*   May involve adding more `console.log` or temporary UI elements for debugging state.

## Acceptance Criteria:

*   All UI elements for configuring "scream", "rtp", and "webrtc" protocols in Source/Sink settings are functional and save data correctly.
*   Display of `protocol_type`, `uuid`, `sip_contact_uri`, and protocol-specific address information in item lists is accurate.
*   The "SIP Registered Devices" page correctly fetches, displays, and refreshes the list of SIP devices with their status.
*   Global UI state correctly incorporates the new data structures and fields.
*   The UI is responsive, free of console errors, and provides a good user experience for configuring and viewing Protocol v2 devices.
