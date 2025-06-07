# Sub-Task 6.2: Implement Backend API for SIP Registered Devices

**Objective:** Create a new FastAPI endpoint (e.g., `GET /api/sip/registered_devices`) that retrieves and returns a list of currently SIP-registered devices with their status information from `ConfigurationManager`.

**Parent Task:** [UI Updates for Protocol v2 (Enhanced)](../task_06_ui_updates.md)
**Previous Sub-Task:** [Sub-Task 6.1: Update TypeScript Types and API Client for UI](./subtask_6.1_update_typescript_types_api_client.md)

## Key Steps & Considerations:

1.  **Define Pydantic Response Model (Python Backend):**
    *   **File:** `src/screamrouter_types/api_types.py` (or a new `src/screamrouter_types/sip_api_types.py`)
    *   Create a Pydantic model for the data to be returned for each SIP device. This should align with the `SipDeviceStatus` TypeScript interface.
    ```python
    # In src/screamrouter_types/api_types.py (or new sip_api_types.py)
    from pydantic import BaseModel, Field
    from typing import Optional, Literal
    from datetime import datetime # If using datetime objects for last_seen

    from .configuration import ProtocolType # Assuming ProtocolType is defined in configuration.py

    class SipDeviceStatusAPI(BaseModel):
        uuid: str
        name: str
        id: str # Source/Sink ID in ScreamRouter
        role: Literal["source", "sink", "unknown"]
        protocol_type: Optional[ProtocolType] = None # Should be 'sip' or derived actual protocol
        sip_contact_uri: str
        online: bool # Derived from 'enabled' or a dedicated status field
        last_seen: Optional[datetime] = None # Or str if sending ISO strings
        # sdp_capabilities_summary: Optional[str] = None # Optional: summary of key capabilities
    ```

2.  **Add Method to `ConfigurationManager`:**
    *   **File:** `src/configuration/configuration_manager.py`
    *   Implement a method like `get_sip_devices_status() -> List[SipDeviceStatusAPI]`.
    *   This method iterates through `self.source_descriptions.values()` and `self.sink_descriptions.values()`.
    *   It filters for devices that have a `uuid` and a `sip_contact_uri` (and/or `protocol_type == "sip"`).
    *   It compiles the necessary information into `SipDeviceStatusAPI` objects.
    ```python
    # In src/configuration/configuration_manager.py
    # from ..screamrouter_types.api_types import SipDeviceStatusAPI # Adjust import
    # from typing import List, Union # For type hinting

    # def get_sip_devices_status(self) -> List[SipDeviceStatusAPI]:
    #     sip_devices: List[SipDeviceStatusAPI] = []
    #     
    #     all_devices: List[Union[SourceDescription, SinkDescription]] = \
    #         list(self.source_descriptions.values()) + list(self.sink_descriptions.values())

    #     for device_desc in all_devices:
    #         if device_desc.uuid and device_desc.sip_contact_uri: # Key indicators of a SIP-managed device
    #             # Determine role
    #             role_str: Literal["source", "sink", "unknown"] = "unknown"
    #             if isinstance(device_desc, SourceDescription):
    #                 role_str = "source"
    #             elif isinstance(device_desc, SinkDescription):
    #                 role_str = "sink"

    #             device_status = SipDeviceStatusAPI(
    #                 uuid=device_desc.uuid,
    #                 name=device_desc.name,
    #                 id=device_desc.id, # Assuming BaseDescription has an 'id' field
    #                 role=role_str,
    #                 protocol_type=device_desc.protocol_type,
    #                 sip_contact_uri=device_desc.sip_contact_uri,
    #                 online=device_desc.enabled, # Or a dedicated online_status field
    #                 last_seen=getattr(device_desc, 'last_seen_timestamp', None) # If field exists
    #                 # sdp_capabilities_summary=self._summarize_sdp_caps(device_desc) # Optional helper
    #             )
    #             sip_devices.append(device_status)
    #     return sip_devices
    ```

3.  **Create FastAPI Endpoint:**
    *   **File:** `src/api/api_sip.py` (new file) or extend an existing API router file (e.g., `src/api/api_configuration.py`).
    *   Import `APIRouter` from `fastapi`, `List` from `typing`, `SipDeviceStatusAPI` model, and `ConfigurationManager`.
    *   Define the `GET /api/sip/registered_devices` endpoint.
    *   Use FastAPI dependency injection to get the `ConfigurationManager` instance.
    ```python
    # In src/api/api_sip.py (new file)
    # from fastapi import APIRouter, Depends
    # from typing import List
    # from ..screamrouter_types.api_types import SipDeviceStatusAPI
    # from ..configuration.configuration_manager import ConfigurationManager, get_config_manager # Assuming get_config_manager dependency

    # router = APIRouter(
    #     prefix="/api/sip",
    #     tags=["sip"],
    # )

    # @router.get("/registered_devices", response_model=List[SipDeviceStatusAPI])
    # async def get_sip_registered_devices_list(
    #     config_manager: ConfigurationManager = Depends(get_config_manager)
    # ):
    #     """
    #     Get a list of all SIP registered devices and their status.
    #     """
    #     return config_manager.get_sip_devices_status()
    ```
    *   Ensure this new router is included in the main FastAPI application in `screamrouter.py`.

4.  **Update Main FastAPI App (`screamrouter.py`):**
    *   Import the new SIP API router and include it in the FastAPI app instance.
    ```python
    # In screamrouter.py
    # from src.api import api_sip # Assuming the new file
    # ...
    # app.include_router(api_sip.router)
    # ...
    ```

## Code Alterations:

*   **New File:** `src/screamrouter_types/api_types.py` (or similar) - Define `SipDeviceStatusAPI`.
*   **Modified File:** `src/configuration/configuration_manager.py` - Implement `get_sip_devices_status()`.
*   **New File:** `src/api/api_sip.py` (or modify existing API file) - Create the FastAPI endpoint.
*   **Modified File:** `screamrouter.py` - Include the new API router.
*   **Modified File:** `src/screamrouter_types/configuration.py` - Ensure `SourceDescription`/`SinkDescription` have `id`, `uuid`, `sip_contact_uri`, `protocol_type`, `enabled`, and potentially `last_seen_timestamp`.

## Recommendations:

*   **Data Consistency:** Ensure the data returned by `get_sip_devices_status()` is consistent with the actual state managed by `SipManager` and `ConfigurationManager`. The `enabled` field (or a new `online` field) should accurately reflect if the device is considered active by the SIP server.
*   **`last_seen` Timestamp:** If implementing `last_seen_timestamp`, ensure it's updated by the SIP keep-alive mechanism and correctly serialized (e.g., to ISO 8601 string format if sending JSON).
*   **Error Handling:** The FastAPI endpoint should handle potential errors gracefully, though `get_sip_devices_status()` is mostly reading data.
*   **Security/Auth:** If API authentication is in place, ensure this new endpoint is appropriately protected if it exposes sensitive information.

## Acceptance Criteria:

*   A Pydantic model `SipDeviceStatusAPI` is defined for the API response.
*   `ConfigurationManager` has a method `get_sip_devices_status()` that correctly retrieves and formats data for SIP-managed devices.
*   A FastAPI endpoint `GET /api/sip/registered_devices` is implemented and returns a list of `SipDeviceStatusAPI` objects.
*   The endpoint is accessible and returns correct data when SIP devices are registered with ScreamRouter.
*   The main FastAPI application includes the new SIP API router.
