# Task: UI Updates for Protocol v2 (Enhanced)

**Objective:** Significantly update the React-based web interface to display new Protocol v2 information, allow comprehensive configuration of new parameters (especially for RTP and WebRTC), provide a dedicated view for SIP-registered devices, and ensure a clear user experience for these advanced features.

**Parent Plan Section:** VI. Configuration System & UI Updates (React UI parts)

**Files to Modify/Create (primarily in `screamrouter-react/src/`):**

*   **`screamrouter-react/src/types/index.ts` (or similar type definition files):**
    *   Update TypeScript interfaces for `Source`, `Sink` to match the new fields in Python Pydantic models: `uuid`, `protocol_type`, `sip_contact_uri`.
    *   Define `RTPConfigUI` interface:
        *   `destination_port?: number;`
        *   `payload_type_pcm?: number;`
        *   `payload_type_mp3?: number;`
        *   `codec_preferences?: string[];` (e.g., ["L16/48000/2", "MP3/192k"])
        *   `srtp_enabled?: boolean;`
        *   `srtp_key?: string;`
    *   Define `TURNServerConfigUI` interface: `urls: string[]; username?: string; credential?: string;`
    *   Define `WebRTCConfigUI` interface:
        *   `stun_servers?: string[];`
        *   `turn_servers?: TURNServerConfigUI[];`
    *   Add `rtp_config?: RTPConfigUI;` and `webrtc_config?: WebRTCConfigUI;` to `Source` and `Sink` interfaces.
*   **`screamrouter-react/src/api/api.ts`:**
    *   Update `addSource`, `updateSource`, `addSink`, `updateSink` API functions to correctly serialize and send the new `protocol_type` and nested `rtp_config` / `webrtc_config` objects.
    *   Create new API function: `getSipRegisteredDevices(): Promise<SipDeviceStatus[]>` (define `SipDeviceStatus` interface: `uuid: string; name: string; role: 'source' | 'sink'; sip_contact_uri: string; online: boolean; last_seen?: string;`).
*   **Backend API (`src/api/api_configuration.py` or new `src/api/api_sip.py`):**
    *   Create a new FastAPI endpoint `GET /api/sip/registered_devices` that:
        *   Calls a method on `ConfigurationManager` (e.g., `get_sip_devices_status()`).
        *   This `ConfigurationManager` method would iterate through its `source_descriptions` and `sink_descriptions`, filter for those with a `uuid` and `sip_contact_uri` (indicating SIP registration), and compile their status (name, uuid, role, contact, online status from `enabled` field or a new dedicated field, last_seen if tracked by `SipManager`).
        *   Returns this list.
*   **Shared Settings Components (e.g., `screamrouter-react/src/components/shared/SettingsModal.tsx` or similar if a generic modal is used for editing sources/sinks):**
    *   This component will need significant conditional rendering logic.
    *   **Display Fields:**
        *   Always display `Name`.
        *   Display `UUID` (read-only if device is SIP-managed and UUID assigned by registration).
        *   Display `Protocol Type` as a dropdown: "Scream" (legacy), "RTP", "WebRTC", "SIP Managed".
            *   "SIP Managed" could be a read-only state if the device was discovered via SIP, or a type the user can select if they expect a device to register.
        *   Display `SIP Contact URI` (read-only, populated for SIP-managed devices).
    *   **Conditional Sections based on `protocol_type`:**
        *   **If "Scream":** Show existing IP/Port fields.
        *   **If "RTP":**
            *   Show `IP Address` (for destination).
            *   Input for `RTP Destination Port`.
            *   Input for `Codec Preferences` (e.g., a multi-select or reorderable list of strings like "L16/48000/2", "MP3/192k").
            *   Advanced: Optional inputs for manual `Payload Type PCM`, `Payload Type MP3`.
            *   Advanced: Checkbox for `SRTP Enabled` and input for `SRTP Key` (for future use).
            *   Consider a "Presets" dropdown for common RTP configurations.
        *   **If "WebRTC":**
            *   Display informational text: "WebRTC connections are negotiated via SIP signaling."
            *   Input fields for `STUN Servers` (list of URLs, allow add/remove).
            *   Input fields for `TURN Servers` (list of objects, each with URLs, optional username/credential, allow add/remove).
        *   **If "SIP Managed":** Most network fields (IP, port) might be read-only as they are derived from SIP registration. Show UUID.
    *   Ensure existing controls (Volume, EQ, Delay, Timeshift, Speaker Layouts) are still accessible and function correctly regardless of protocol.
*   **Item Display Components (`SourceItem.tsx`, `SinkItem.tsx` in `desktopMenu/item/` and full menu equivalents):**
    *   Display `Protocol: {protocol_type}`.
    *   If `sip_contact_uri` exists, display it.
    *   If `uuid` exists, display it (perhaps in a tooltip or secondary line).
*   **New Page: `screamrouter-react/src/components/pages/SipDevicesPage.tsx`:**
    *   Fetches data from `/api/sip/registered_devices`.
    *   Displays a table: Name, UUID, Role (Source/Sink), Protocol, SIP Contact, Online Status, Last Seen.
    *   Include a manual "Refresh" button.
    *   Consider adding this page to the main navigation menu.
*   **`screamrouter-react/src/context/AppContext.tsx`:**
    *   Update `Source` and `Sink` types in the context state.
    *   Add state for `sipRegisteredDevices: SipDeviceStatus[]`.
    *   Add actions/reducers for fetching and updating `sipRegisteredDevices`.

**Detailed Steps:**

1.  **Update TypeScript Types & API Client:** Implement changes in `types/index.ts` and `api/api.ts`.
2.  **Implement Backend API for SIP Devices:** Create the `GET /api/sip/registered_devices` endpoint in Python/FastAPI.
3.  **Refactor Settings Modals/Pages:**
    *   Add the `Protocol Type` dropdown.
    *   Implement conditional rendering for protocol-specific settings sections (Scream, RTP, WebRTC).
    *   Ensure data for nested `rtp_config` and `webrtc_config` is correctly handled (fetched, displayed, updated, and sent to the backend).
4.  **Update Item Display Components:** Modify `SourceItem.tsx`, `SinkItem.tsx`, etc., to show new protocol-related info.
5.  **Create `SipDevicesPage.tsx`:**
    *   Implement fetching and displaying the table of SIP registered devices.
    *   Add to navigation.
6.  **Update State Management (`AppContext.tsx`):** Incorporate new types and state for SIP devices.
7.  **Testing:**
    *   Thoroughly test creating and editing Sources/Sinks with each `protocol_type`.
    *   Verify all UI fields for RTP and WebRTC configurations are present, functional, and save correctly.
    *   Test the "SIP Registered Devices" page with mock data and then with actual SIP registrations.
    *   Ensure UI gracefully handles devices with older configurations (missing new fields).
    *   Verify that read-only fields for SIP-managed devices behave as expected.

**Acceptance Criteria:**

*   UI allows selection and configuration of `protocol_type` for Sources/Sinks.
*   RTP-specific settings (port, codecs, payload types) can be configured via the UI.
*   WebRTC-specific settings (STUN/TURN) can be configured via the UI.
*   New protocol information (type, UUID, SIP contact) is displayed appropriately.
*   A dedicated page lists SIP-registered devices with their status, fetched from a new API endpoint.
*   All UI changes are correctly persisted to the backend.
