# Sub-Task 6.5: Create New "SIP Registered Devices" Page

**Objective:** Develop a new React page (`SipDevicesPage.tsx`) to display a list or table of SIP-registered devices, fetching data from the `/api/sip/registered_devices` backend endpoint.

**Parent Task:** [UI Updates for Protocol v2 (Enhanced)](../task_06_ui_updates.md)
**Previous Sub-Task:** [Sub-Task 6.4: Update Item Display Components for Protocol Info](./subtask_6.4_update_item_display_components.md)

## Key Steps & Considerations:

1.  **Create New Component File:**
    *   **File:** `screamrouter-react/src/components/pages/SipDevicesPage.tsx`

2.  **Component Structure (`SipDevicesPage.tsx`):**
    *   Import React, Material-UI components (e.g., `Table`, `TableBody`, `TableCell`, `TableContainer`, `TableHead`, `TableRow`, `Paper`, `Typography`, `Button`, `CircularProgress`, `Alert`), and API functions/types.
    *   Use `useState` for storing the list of `SipDeviceStatus[]`, loading state, and error state.
    *   Use `useEffect` to fetch data when the component mounts.
    *   Implement a function to fetch data using `api.getSipRegisteredDevices()`.
    *   Include a manual "Refresh" button to re-fetch the data.

3.  **Data Fetching and State Management:**
    ```typescript
    // In screamrouter-react/src/components/pages/SipDevicesPage.tsx
    // import React, { useEffect, useState, useCallback } from 'react';
    // import { 
    //   Table, TableBody, TableCell, TableContainer, TableHead, TableRow, Paper, 
    //   Typography, Button, CircularProgress, Alert, Box 
    // } from '@mui/material';
    // import RefreshIcon from '@mui/icons-material/Refresh';
    // import { SipDeviceStatus } from '../../types'; // Adjust path
    // import { getSipRegisteredDevices } from '../../api/api'; // Adjust path

    // const SipDevicesPage: React.FC = () => {
    //   const [devices, setDevices] = useState<SipDeviceStatus[]>([]);
    //   const [loading, setLoading] = useState<boolean>(true);
    //   const [error, setError] = useState<string | null>(null);

    //   const fetchDevices = useCallback(async () => {
    //     setLoading(true);
    //     setError(null);
    //     try {
    //       const data = await getSipRegisteredDevices();
    //       setDevices(data);
    //     } catch (err) {
    //       logger.error("Failed to fetch SIP devices:", err);
    //       setError('Failed to load SIP registered devices. Please try again.');
    //     } finally {
    //       setLoading(false);
    //     }
    //   }, []);

    //   useEffect(() => {
    //     fetchDevices();
    //   }, [fetchDevices]);

    //   // ... JSX for rendering table, loading, error, refresh button ...
    // };
    // export default SipDevicesPage;
    ```

4.  **Displaying Data in a Table:**
    *   Use Material-UI `Table` components for a structured display.
    *   **Columns:**
        *   Name (Device Name from `Source/SinkDescription`)
        *   UUID
        *   Role (Source/Sink)
        *   Protocol (`protocol_type`, likely "sip" or derived actual media protocol)
        *   SIP Contact URI
        *   Online Status (Boolean, display as "Online"/"Offline" or with an icon)
        *   Last Seen (Timestamp, formatted nicely)
        *   (Optional) Associated ScreamRouter ID (if the device is linked to an existing Source/Sink config entry)
        *   (Optional) Summary of SDP Capabilities

5.  **Rendering Logic:**
    ```tsx
    // Inside SipDevicesPage component's return statement:
    // <Box sx={{ p: 2 }}>
    //   <Box sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', mb: 2 }}>
    //     <Typography variant="h5" gutterBottom>SIP Registered Devices</Typography>
    //     <Button variant="outlined" onClick={fetchDevices} startIcon={<RefreshIcon />} disabled={loading}>
    //       Refresh
    //     </Button>
    //   </Box>

    //   {loading && <CircularProgress />}
    //   {error && <Alert severity="error">{error}</Alert>}
    //   {!loading && !error && devices.length === 0 && (
    //     <Typography>No SIP devices are currently registered.</Typography>
    //   )}
    //   {!loading && !error && devices.length > 0 && (
    //     <TableContainer component={Paper}>
    //       <Table sx={{ minWidth: 650 }} aria-label="sip devices table">
    //         <TableHead>
    //           <TableRow>
    //             <TableCell>Name</TableCell>
    //             <TableCell>UUID</TableCell>
    //             <TableCell>Role</TableCell>
    //             <TableCell>Contact URI</TableCell>
    //             <TableCell>Status</TableCell>
    //             <TableCell>Last Seen</TableCell>
    //             {/* <TableCell>ScreamRouter ID</TableCell> */}
    //           </TableRow>
    //         </TableHead>
    //         <TableBody>
    //           {devices.map((device) => (
    //             <TableRow key={device.uuid}>
    //               <TableCell>{device.name}</TableCell>
    //               <TableCell><Typography variant="caption">{device.uuid}</Typography></TableCell>
    //               <TableCell>{device.role}</TableCell>
    //               <TableCell><Typography variant="caption">{device.sip_contact_uri}</Typography></TableCell>
    //               <TableCell>{device.online ? 'Online' : 'Offline'}</TableCell>
    //               <TableCell>{device.last_seen ? new Date(device.last_seen).toLocaleString() : 'N/A'}</TableCell>
    //               {/* <TableCell>{device.id || 'N/A'}</TableCell> */}
    //             </TableRow>
    //           ))}
    //         </TableBody>
    //       </Table>
    //     </TableContainer>
    //   )}
    // </Box>
    ```

6.  **Add Page to Navigation:**
    *   Update the main application layout/navigation component (e.g., `App.tsx`, a sidebar component) to include a link to `/sip-devices` (or similar route).
    *   Add the route definition in the React Router setup.
    ```tsx
    // Example in App.tsx (React Router v6)
    // import SipDevicesPage from './components/pages/SipDevicesPage';
    // ...
    // <Routes>
    //   {/* ... other routes ... */}
    //   <Route path="/sip-devices" element={<SipDevicesPage />} />
    // </Routes>
    // ...
    // And add a NavLink or ListItem in your menu component.
    ```

7.  **Styling:**
    *   Apply appropriate Material-UI styling or custom CSS for readability and consistency with the rest of the application.

## Code Alterations:

*   **New File:** `screamrouter-react/src/components/pages/SipDevicesPage.tsx` - Implement the component.
*   **Modified File:** `screamrouter-react/src/App.tsx` (or main router configuration) - Add the route for the new page.
*   **Modified File:** Navigation component (e.g., `Sidebar.tsx`, `Header.tsx`) - Add a link to the new page.
*   **Modified File:** `screamrouter-react/src/api/api.ts` - Ensure `getSipRegisteredDevices` is implemented (from Sub-Task 6.1).
*   **Modified File:** `screamrouter-react/src/types/index.ts` - Ensure `SipDeviceStatus` interface is defined (from Sub-Task 6.1).

## Recommendations:

*   **Polling/Auto-Refresh (Optional):** For a more dynamic view, consider implementing polling (e.g., fetching data every 5-10 seconds) or using WebSockets if the backend supports pushing updates for SIP device status. For now, a manual refresh button is sufficient.
*   **Data Formatting:** Format timestamps (`last_seen`) in a user-friendly way. Truncate long strings like UUIDs or `sip_contact_uri` if necessary in the table display, possibly showing the full value in a tooltip.
*   **Error States and Empty States:** Provide clear messages to the user if data fetching fails or if no devices are registered.
*   **Accessibility:** Ensure the table is accessible (e.g., proper ARIA attributes if using a complex table structure, though Material-UI handles much of this).

## Acceptance Criteria:

*   A new "SIP Registered Devices" page is created in the React application.
*   The page fetches data from the `/api/sip/registered_devices` endpoint.
*   A table displays the list of registered SIP devices with relevant columns (Name, UUID, Role, Contact URI, Online Status, Last Seen).
*   Loading and error states are handled appropriately.
*   A manual "Refresh" button allows re-fetching the device list.
*   The page is accessible via the application's navigation.
