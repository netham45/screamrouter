# Sub-Task 6.6: Update UI State Management (`AppContext.tsx`)

**Objective:** Update the global UI state management (e.g., in `AppContext.tsx` or similar Redux/Zustand store) to include the new `SipDeviceStatus[]` list and ensure that `Source` and `Sink` types in the global state reflect the updated TypeScript interfaces.

**Parent Task:** [UI Updates for Protocol v2 (Enhanced)](../task_06_ui_updates.md)
**Previous Sub-Task:** [Sub-Task 6.5: Create New "SIP Registered Devices" Page](./subtask_6.5_create_sip_devices_page.md)

## Key Steps & Considerations:

1.  **Locate Global State Management File:**
    *   Typically `screamrouter-react/src/context/AppContext.tsx` if using React Context API, or corresponding files for Redux (reducers, actions, store) or Zustand store.

2.  **Update `Source` and `Sink` Types in Global State:**
    *   The global state likely holds arrays of `Source` and `Sink` objects.
    *   Ensure the types used for these arrays in the state definition (e.g., `initialState`, context value type) are the updated `Source` and `Sink` interfaces from Sub-Task 6.1, which include `protocol_type`, `uuid`, `rtp_config`, `webrtc_config`, etc.
    ```typescript
    // Example in AppContext.tsx
    // import { Source, Sink, SipDeviceStatus } from '../types'; // Ensure correct import
    // ...
    // interface AppState {
    //   sources: Source[];
    //   sinks: Sink[];
    //   routes: Route[]; // Assuming Route type exists
    //   sipRegisteredDevices: SipDeviceStatus[]; // New state field
    //   // ... other state fields
    // }

    // interface AppContextProps extends AppState {
    //   // ... existing action dispatchers (addSource, updateSink, etc.)
    //   fetchSipRegisteredDevices: () => Promise<void>; // New action
    //   // ...
    // }

    // const initialState: AppState = {
    //   sources: [],
    //   sinks: [],
    //   routes: [],
    //   sipRegisteredDevices: [], // Initialize as empty
    //   // ...
    // };
    ```

3.  **Add State for SIP Registered Devices:**
    *   Add a new field to the global state to store the list of SIP registered devices, e.g., `sipRegisteredDevices: SipDeviceStatus[]`.
    *   Initialize it as an empty array.

4.  **Implement Actions/Reducers for SIP Devices:**
    *   Create an action (or a function within the context provider if not using Redux-style actions) to fetch and update the `sipRegisteredDevices` state.
    *   This action will call the `api.getSipRegisteredDevices()` function.
    ```typescript
    // Example within AppContextProvider in AppContext.tsx

    // const [sipRegisteredDevices, setSipRegisteredDevices] = useState<SipDeviceStatus[]>([]);
    // // Or if using useReducer:
    // // dispatch({ type: 'SET_SIP_DEVICES', payload: data });

    // const fetchSipRegisteredDevices = useCallback(async () => {
    //   try {
    //     // Optionally set a loading state for SIP devices if needed elsewhere
    //     const data = await getSipRegisteredDevices(); // From api.ts
    //     // If using useState:
    //     setSipRegisteredDevices(data); 
    //     // If using useReducer:
    //     // dispatch({ type: 'SET_SIP_DEVICES', payload: data });
    //   } catch (error) {
    //     console.error("AppContext: Failed to fetch SIP registered devices", error);
    //     // Optionally set an error state for SIP devices
    //   }
    // }, []);

    // Expose fetchSipRegisteredDevices in the context value:
    // const contextValue = {
    //   // ... other state and functions
    //   sipRegisteredDevices,
    //   fetchSipRegisteredDevices,
    // };
    ```

5.  **Update Existing Actions/Reducers (if necessary):**
    *   Review existing actions/reducers for adding, updating, or deleting sources and sinks.
    *   Ensure they correctly handle the new fields in `Source` and `Sink` objects (e.g., when receiving updated configuration from the backend via WebSockets or after an API call).
    *   If a SIP-managed device is modified or deleted via the regular Source/Sink API (which might be restricted for SIP devices), consider how this interacts with its SIP registration status. Typically, SIP-managed devices might have limited direct editability for certain fields.

## Code Alterations:

*   **`screamrouter-react/src/context/AppContext.tsx` (or equivalent state management files):**
    *   Update the type definitions for the global state (`AppState`, `AppContextProps`) to include `sipRegisteredDevices` and use the updated `Source` and `Sink` types.
    *   Initialize `sipRegisteredDevices` in the initial state.
    *   Implement the logic (e.g., `fetchSipRegisteredDevices` function and corresponding state update) to fetch and store the list of SIP devices.
    *   Provide `sipRegisteredDevices` and `fetchSipRegisteredDevices` through the context.
*   **`screamrouter-react/src/types/index.ts`:**
    *   Ensure `Source`, `Sink`, and `SipDeviceStatus` types are fully defined as per Sub-Task 6.1.

## Recommendations:

*   **Data Flow:** Ensure a clear data flow: API call -> update global state -> components re-render with new data.
*   **Initial Fetch:** The `fetchSipRegisteredDevices` action could be called once when the application loads (e.g., in a top-level component's `useEffect`) if the SIP devices page is not the only place this data might be hinted at or used. However, fetching it primarily when the `SipDevicesPage` mounts is also acceptable.
*   **Context API vs. Other Libraries:** The examples assume React Context. If Redux, Zustand, or another state management library is used, adapt the implementation accordingly (actions, reducers, selectors, store slices).

## Acceptance Criteria:

*   The global UI state (`AppContext` or equivalent) is updated to use the revised `Source` and `Sink` TypeScript interfaces.
*   The global state includes a new field for `sipRegisteredDevices` of type `SipDeviceStatus[]`.
*   An action/function is implemented to fetch SIP registered devices from the API and update the global state.
*   Components consuming source/sink data from the global state continue to function correctly with the updated types.
*   The `SipDevicesPage` can consume `sipRegisteredDevices` from the global state.
