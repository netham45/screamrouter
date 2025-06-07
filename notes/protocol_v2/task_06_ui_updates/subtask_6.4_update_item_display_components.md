# Sub-Task 6.4: Update Item Display Components for Protocol Info

**Objective:** Modify existing React components that display individual Source and Sink items (e.g., in lists like `SourceList.tsx`, `SinkList.tsx`, or in the `DesktopMenu`) to show new Protocol v2 related information such as `protocol_type`, `uuid`, and `sip_contact_uri`.

**Parent Task:** [UI Updates for Protocol v2 (Enhanced)](../task_06_ui_updates.md)
**Previous Sub-Task:** [Sub-Task 6.3: Refactor Settings Modals/Pages for Protocol Configuration](./subtask_6.3_refactor_settings_modals.md)

## Key Steps & Considerations:

1.  **Identify Target Item Components:**
    *   `screamrouter-react/src/components/desktopMenu/item/SourceItem.tsx`
    *   `screamrouter-react/src/components/desktopMenu/item/SinkItem.tsx`
    *   Any other components that render a summary view of a source or sink (e.g., in the full menu, route display, etc.).

2.  **Access New Props:**
    *   Ensure these components receive the full `Source` or `Sink` object (as defined by the updated TypeScript interfaces in Sub-Task 6.1) which now include `protocol_type`, `uuid`, `sip_contact_uri`, `rtp_config`, `webrtc_config`.

3.  **Display `protocol_type`:**
    *   Add a small text element or badge to display the `protocol_type` (e.g., "Protocol: RTP", "Type: SIP").
    *   This provides a quick visual cue about how the device is connected.
    ```tsx
    // Example in SourceItem.tsx or SinkItem.tsx
    // const { source } = props; // or const { sink } = props;
    // ...
    // <div>
    //   <Typography variant="caption">
    //     Protocol: {device.protocol_type ? device.protocol_type.toUpperCase() : 'SCREAM'}
    //   </Typography>
    // </div>
    ```

4.  **Display `uuid` (Optional, e.g., in Tooltip or Secondary Line):**
    *   The UUID is long and might clutter the primary display. Consider showing it:
        *   In a tooltip when hovering over the item name or protocol type.
        *   As a smaller, secondary line of text.
    ```tsx
    // Example using Material-UI Tooltip
    // import Tooltip from '@mui/material/Tooltip';
    // ...
    // <Tooltip title={`UUID: ${device.uuid || 'N/A'}`}>
    //   <Typography variant="body1">{device.name}</Typography>
    // </Tooltip>
    // ...
    // {device.uuid && (
    //   <Typography variant="caption" display="block" gutterBottom>
    //     UUID: {device.uuid}
    //   </Typography>
    // )}
    ```

5.  **Display `sip_contact_uri` (If SIP Managed):**
    *   If `device.protocol_type === 'sip'` and `device.sip_contact_uri` is present, display it. This is useful for diagnostics.
    *   This could also be in a secondary line or tooltip.
    ```tsx
    // Example
    // {device.protocol_type === 'sip' && device.sip_contact_uri && (
    //   <Tooltip title={`SIP Contact: ${device.sip_contact_uri}`}>
    //     <Typography variant="caption" sx={{ color: 'info.main' }}>
    //       (SIP Managed)
    //     </Typography>
    //   </Tooltip>
    // )}
    ```

6.  **Display Port/IP Information Conditionally:**
    *   The existing display of IP/Port is relevant for `protocol_type: "scream"`.
    *   For `protocol_type: "rtp"`:
        *   Sinks: Display destination IP and `rtp_config.destination_port`.
        *   Sources: Display listening IP (usually 0.0.0.0) and `rtp_config.source_listening_port`.
    *   For `protocol_type: "webrtc"` or `"sip"`: Direct IP/port display might be less relevant as connections are negotiated. `sip_contact_uri` or a general "WebRTC" / "SIP" status might be more appropriate.
    *   Adjust the logic that currently displays `device.ip` and `device.port`.

    ```tsx
    // Example conditional display logic
    // const getDisplayAddress = (device: Source | Sink): string => {
    //   if (device.protocol_type === 'rtp') {
    //     if (device.rtp_config) {
    //       if ('destination_port' in device.rtp_config && device.rtp_config.destination_port) { // Sink
    //         return `${device.ip}:${device.rtp_config.destination_port} (RTP)`;
    //       } else if ('source_listening_port' in device.rtp_config && device.rtp_config.source_listening_port) { // Source
    //         return `0.0.0.0:${device.rtp_config.source_listening_port} (RTP Listen)`;
    //       }
    //     }
    //     return 'RTP (Config Error)';
    //   } else if (device.protocol_type === 'webrtc') {
    //     return 'WebRTC';
    //   } else if (device.protocol_type === 'sip') {
    //     return device.sip_contact_uri || 'SIP Managed';
    //   }
    //   // Default to legacy scream
    //   return `${device.ip || 'N/A'}:${device.port || 'N/A'}`;
    // };
    // ...
    // <Typography variant="body2">{getDisplayAddress(device)}</Typography>
    ```

7.  **Visual Cues (Optional):**
    *   Consider using small icons or color-coding next to the protocol type for better visual differentiation (e.g., an RTP icon, a WebRTC icon).

## Code Alterations:

*   **`screamrouter-react/src/components/desktopMenu/item/SourceItem.tsx`**
*   **`screamrouter-react/src/components/desktopMenu/item/SinkItem.tsx`**
*   Any other components that render summary views of sources/sinks.
    *   Modify the JSX to include new `Typography`, `Tooltip`, or other Material-UI components to display `protocol_type`, `uuid` (selectively), and `sip_contact_uri`.
    *   Update logic for displaying IP/port information based on `protocol_type`.

## Recommendations:

*   **Clarity vs. Density:** Balance showing useful information with keeping the item display clean and not overly cluttered, especially for compact views like the `DesktopMenu`. Tooltips are good for less critical but useful details.
*   **Consistency:** Apply similar display logic across all components that show source/sink summaries.
*   **Default Values:** Handle cases where new fields might be `null` or `undefined` (e.g., for older configurations not yet updated, or if a field is truly optional). Display "N/A" or omit the field gracefully.

## Acceptance Criteria:

*   Source and Sink item display components correctly show the `protocol_type`.
*   `uuid` is accessible (e.g., via tooltip or secondary text) if present.
*   `sip_contact_uri` is displayed for SIP-managed devices.
*   IP/Port display is adapted based on the `protocol_type` to show relevant information (e.g., RTP ports).
*   The UI remains clean and readable with the new information.
