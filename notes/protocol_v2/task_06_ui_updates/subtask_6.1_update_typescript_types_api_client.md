# Sub-Task 6.1: Update TypeScript Types and API Client for UI

**Objective:** Update TypeScript interfaces in the React frontend to match the new Pydantic models for Protocol v2 configurations. Modify API client functions (`api.ts`) to correctly serialize and send these new structures to the backend.

**Parent Task:** [UI Updates for Protocol v2 (Enhanced)](../task_06_ui_updates.md)

## Key Steps & Considerations:

1.  **Define/Update Core TypeScript Interfaces (`screamrouter-react/src/types/index.ts` or similar):**
    *   These interfaces should mirror the Python Pydantic models defined in `task_04_python_config_updates/subtask_4.1_finalize_pydantic_models.md`.
    *   **`ProtocolType` Literal Union:**
        ```typescript
        export type ProtocolType = "scream" | "rtp" | "webrtc" | "sip";
        ```
    *   **`RTPConfigUI` Interface:**
        ```typescript
        export interface RTPConfigUI {
            destination_port?: number | null;
            source_listening_port?: number | null;
            payload_type_pcm?: number;
            payload_type_mp3?: number;
            codec_preferences?: string[];
            srtp_enabled?: boolean;
            srtp_key?: string | null;
        }
        ```
    *   **`TURNServerConfigUI` Interface:**
        ```typescript
        export interface TURNServerConfigUI {
            urls: string[];
            username?: string | null;
            credential?: string | null;
        }
        ```
    *   **`WebRTCConfigUI` Interface:**
        ```typescript
        export interface WebRTCConfigUI {
            stun_servers?: string[];
            turn_servers?: TURNServerConfigUI[];
        }
        ```
    *   **Update `Source` and `Sink` Interfaces (extending existing ones):**
        ```typescript
        // Assuming existing BaseSourceSink interface or similar
        export interface BaseSourceSink {
            id: string;
            name: string;
            enabled: boolean;
            // ... other common fields like volume, eq_config, etc.
            
            // New Protocol v2 fields
            uuid?: string | null;
            protocol_type?: ProtocolType; // Default to 'scream' if loading old config
            sip_contact_uri?: string | null;
            last_seen_timestamp?: string | null; // Or Date object, handle conversion

            rtp_config?: RTPConfigUI | null;
            webrtc_config?: WebRTCConfigUI | null; // Typically for Sinks
        }

        export interface Source extends BaseSourceSink {
            // ... existing source-specific fields like process_id, is_default_mic etc.
            // rtp_config might be more fleshed out here if sources can be complex RTP endpoints
        }

        export interface Sink extends BaseSourceSink {
            // ... existing sink-specific fields like ip, port (for legacy), codec, mp3_bitrate etc.
            // ip and port might become less relevant for 'rtp', 'webrtc', 'sip' types
        }
        ```
        *   Ensure optional fields are marked with `?` and can be `null` to align with Pydantic's `Optional` and `exclude_none=True` during serialization.
        *   Default values for new fields (like `protocol_type` defaulting to "scream" if not present) will be handled by Pydantic on the backend when loading older configs. The UI should gracefully handle potentially missing fields from older data structures before they are re-saved.

2.  **Define `SipDeviceStatus` Interface (for the new SIP devices page):**
    ```typescript
    // In screamrouter-react/src/types/index.ts
    export interface SipDeviceStatus {
        uuid: string;
        name: string;
        id: string; // The source/sink ID in ScreamRouter if it was created
        role: 'source' | 'sink' | 'unknown';
        protocol_type?: ProtocolType; // Should be 'sip' or derived
        sip_contact_uri: string;
        online: boolean;
        last_seen?: string | null; // ISO date string or similar
        // Add any other relevant status info from backend, e.g., SDP capabilities summary
        sdp_capabilities_summary?: string; 
    }
    ```

3.  **Update API Client Functions (`screamrouter-react/src/api/api.ts`):**
    *   **Review `addSource`, `updateSource`, `addSink`, `updateSink`:**
        *   These functions take a `Partial<Source>` or `Partial<Sink>` (or the full object) as payload.
        *   Ensure the payload sent to the backend correctly includes the new fields: `uuid`, `protocol_type`, `sip_contact_uri`, and the nested `rtp_config` and `webrtc_config` objects.
        *   No special serialization might be needed if the TypeScript interfaces directly map to the Pydantic models and `axios` (or `fetch`) sends them as JSON. The backend Pydantic models will handle parsing.
        *   Pay attention to how `null` vs. `undefined` is handled for optional fields if `exclude_none=True` is used on the backend. Usually, sending `null` is fine. If a field is `undefined` in JS, it might not be sent, which `exclude_none` also handles.
    *   **Create New API Function `getSipRegisteredDevices`:**
        ```typescript
        // In screamrouter-react/src/api/api.ts
        // import { SipDeviceStatus } from '../types'; // Adjust path as needed

        // export const getSipRegisteredDevices = async (): Promise<SipDeviceStatus[]> => {
        //     const response = await apiClient.get<SipDeviceStatus[]>('/api/sip/registered_devices');
        //     return response.data;
        // };
        ```
        (Assuming `apiClient` is an `axios` instance or similar).

## Code Alterations:

*   **File:** `screamrouter-react/src/types/index.ts` (or equivalent type definition files)
    *   Define `ProtocolType` literal union.
    *   Define/update `RTPConfigUI`, `TURNServerConfigUI`, `WebRTCConfigUI`.
    *   Update `Source` and `Sink` interfaces to include all new Protocol v2 fields and nested config objects.
    *   Define `SipDeviceStatus` interface.
*   **File:** `screamrouter-react/src/api/api.ts`
    *   Review and ensure `add/update` functions for sources/sinks correctly handle sending the new data structures.
    *   Implement `getSipRegisteredDevices` function.

## Recommendations:

*   **Consistency with Backend:** Keep TypeScript interfaces as closely aligned with Python Pydantic models as possible to minimize transformation logic and potential for errors.
*   **Optional Fields:** Use `?` for optional fields in TypeScript and ensure the backend Pydantic models handle `None` values appropriately (e.g., with `Optional[Type] = None` or `Optional[Type] = Field(default=None)`).
*   **Date/Time Handling:** For `last_seen_timestamp`, if it's a datetime object in Python, it will likely be serialized as an ISO string. The frontend might need to parse this into a `Date` object or display it as is.
*   **API Endpoint Naming:** Ensure the new API endpoint `/api/sip/registered_devices` matches what will be implemented on the backend.

## Acceptance Criteria:

*   TypeScript interfaces for `Source`, `Sink`, `RTPConfigUI`, `WebRTCConfigUI`, `TURNServerConfigUI`, and `SipDeviceStatus` are correctly defined and reflect the backend Pydantic models.
*   API client functions for adding/updating sources and sinks are capable of sending the new configuration fields, including nested objects, to the backend.
*   A new API client function `getSipRegisteredDevices` is implemented to fetch the list of SIP devices.
*   The React project compiles successfully with these type changes.
