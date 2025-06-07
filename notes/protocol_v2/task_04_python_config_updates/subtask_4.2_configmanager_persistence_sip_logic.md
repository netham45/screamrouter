# Sub-Task 4.2: Update `ConfigurationManager` for Persistence and SIP Event Handling

**Objective:** Enhance `ConfigurationManager` to correctly load/save all new Protocol v2 configuration fields (including nested RTP/WebRTC configs) and implement methods to handle device state changes triggered by SIP events (registration, offline).

**Parent Task:** [Python Configuration System Updates for Protocol v2](../task_04_python_config_updates.md)
**Previous Sub-Task:** [Sub-Task 4.1: Finalize Pydantic Models for All Protocol v2 Fields](./subtask_4.1_finalize_pydantic_models.md)

## Key Steps & Considerations:

1.  **Enhance YAML Persistence (`__load_config`, `__save_config`):**
    *   **File:** `src/configuration/configuration_manager.py`
    *   **Loading:** Pydantic's model parsing should inherently handle loading the new fields (`uuid`, `protocol_type`, `sip_contact_uri`, nested `rtp_config`, `webrtc_config`) from `config.yaml`. If older configs are loaded, fields with defaults or `default_factory` will be populated correctly. No major changes might be needed here if Pydantic models are well-defined.
    *   **Saving:**
        *   When saving, use `model.model_dump(exclude_none=True, by_alias=True)` (or `model.dict(...)` in Pydantic v1) to ensure that optional fields that are `None` are not written to YAML, keeping it clean. `by_alias=True` is important if Pydantic field aliases are used.
        *   Ensure that the nested `RTPConfig` and `WebRTCConfig` objects are also serialized correctly.
        ```python
        # In ConfigurationManager.__save_config
        # config_data = {
        #     "sources": [s.model_dump(exclude_none=True) for s in self.source_descriptions.values()],
        #     "sinks": [s.model_dump(exclude_none=True) for s in self.sink_descriptions.values()],
        #     # ... other config sections ...
        # }
        # with open(self.config_file_path, "w") as f:
        #     yaml.dump(config_data, f, sort_keys=False, Dumper=NoAliasDumper) # Ensure NoAliasDumper is used if applicable
        ```

2.  **Implement SIP Integration Methods in `ConfigurationManager`:**
    *   These methods will be called by `SipAccount` (from `task_03_sip_library_integration`).
    *   **`handle_sip_registration(self, uuid: str, client_ip: str, client_port: int, sip_contact_uri: str, client_role: str, sdp_capabilities: dict) -> bool`:**
        *   Find existing source/sink by `uuid`.
        *   **If exists:**
            *   Update `ip` (if it's the primary signaling IP, media IP might differ or come from SDP), `port` (SIP port).
            *   Update `sip_contact_uri`.
            *   Set `protocol_type` to "sip" (or "rtp"/"webrtc" if SDP clearly indicates only one and it's preferred to override).
            *   Update `enabled = True`.
            *   Parse `sdp_capabilities` (the dict from `sdp_utils.get_device_capabilities_from_sdp`):
                *   Update `rtp_config` (e.g., `payload_type_pcm`, `payload_type_mp3`, `codec_preferences`, `source_listening_port` from `m=` line in SDP).
                *   Update `webrtc_config` if WebRTC details are in SDP.
                *   Update `name` if a better one can be derived (e.g., from SIP display name or hostname in SDP).
        *   **If new:**
            *   Create a new `SourceDescription` or `SinkDescription`.
            *   `name`: e.g., `f"{client_role}_{uuid[:8]}"` or from SIP display name.
            *   `uuid`: The provided `uuid`.
            *   `ip`: `client_ip` (for signaling).
            *   `port`: `client_port` (SIP port).
            *   `protocol_type`: "sip".
            *   `sip_contact_uri`: `sip_contact_uri`.
            *   `enabled = True`.
            *   Populate `rtp_config`, `webrtc_config` from `sdp_capabilities`.
            *   Add to `self.source_descriptions` or `self.sink_descriptions`.
        *   Call `self.__reload_configuration()` to apply changes to the audio engine.
        *   Call `self.__save_config()` to persist.
        *   Return `True` if successful.
    *   **`handle_sip_device_offline(self, uuid: str) -> bool`:**
        *   Find source/sink by `uuid`.
        *   If found, set `enabled = False` (or a new dedicated `online_status` field).
        *   Call `self.__reload_configuration()`.
        *   Call `self.__save_config()`.
        *   Return `True` if found and updated.
    *   **`update_sip_device_last_seen(self, uuid: str)`:**
        *   Find device by `uuid`.
        *   Update a new `last_seen_timestamp: Optional[datetime]` field on the Pydantic model.
        *   This might not trigger a full `__reload_configuration` or `__save_config` on every OPTIONS ping to avoid excessive I/O, unless `enabled` status changes due to timeout.
    *   **`get_device_config_by_uuid(self, uuid: str) -> Optional[Union[SourceDescription, SinkDescription]]`:**
        *   Helper to find a device by UUID across both sources and sinks.

3.  **Device Timeout Logic (Interaction with `SipManager`):**
    *   `SipManager` will need to track last keep-alive times for registered devices.
    *   Periodically, `SipManager` checks for devices that haven't sent a keep-alive within a configured timeout.
    *   If a device times out, `SipManager` calls `ConfigurationManager.handle_sip_device_offline(uuid)`.

4.  **Router UUID and Zeroconf Service Name:**
    *   Implement `get_router_uuid(self) -> str`:
        *   If `self.screamrouter_settings.router_uuid` is not set, generate a new UUID (`str(uuid.uuid4())`), store it in `self.screamrouter_settings`, and save the config.
        *   Return the UUID.
    *   Implement `get_zeroconf_service_name(self) -> str`:
        *   Return a user-configurable service name part from `self.screamrouter_settings` (e.g., "ScreamRouter LivingRoom"), or a default based on hostname.

## Code Alterations:

*   **`src/configuration/configuration_manager.py`:**
    *   Implement/refine `__save_config` for `exclude_none=True`.
    *   Implement `handle_sip_registration`, `handle_sip_device_offline`, `update_sip_device_last_seen`, `get_device_config_by_uuid`.
    *   Implement `get_router_uuid` and `get_zeroconf_service_name`.
    *   Ensure `ScreamRouterSettings` Pydantic model (if it exists for global settings) has fields for `router_uuid` and `zeroconf_service_name`.
*   **`src/screamrouter_types/configuration.py`:**
    *   Add `last_seen_timestamp: Optional[datetime] = None` to `SourceDescription`/`SinkDescription`.
    *   Add `router_uuid: Optional[str] = None` and `zeroconf_service_name: str = "ScreamRouter"` to a global settings Pydantic model (e.g., `ScreamRouterSettings`).

## Recommendations:

*   **Atomicity of Updates:** Operations like `handle_sip_registration` involve multiple steps (update in memory, reload C++ engine, save to disk). Consider error handling and potential rollback if one step fails. For now, logging errors is key.
*   **`enabled` vs. `online_status`:** Using the existing `enabled` field for SIP online/offline status is simple. A dedicated `online_status: bool` field might offer more clarity if `enabled` is meant for user-driven enabling/disabling versus dynamic presence.
*   **Throttling Saves:** For `update_sip_device_last_seen`, avoid saving `config.yaml` on every single keep-alive if only a timestamp is updated. Save only when a significant state change occurs (like device going offline).
*   **Configuration Reload:** `self.__reload_configuration()` should be efficient and correctly update the C++ audio engine with the new or modified device configurations.

## Acceptance Criteria:

*   `ConfigurationManager` can save and load configurations with all new Protocol v2 fields, including nested RTP/WebRTC settings, without data loss.
*   `handle_sip_registration` correctly creates new device entries or updates existing ones based on SIP REGISTER data, including SDP capabilities.
*   `handle_sip_device_offline` correctly marks devices as offline (e.g., sets `enabled=False`).
*   `update_sip_device_last_seen` updates a timestamp for SIP devices.
*   `ConfigurationManager` provides a persistent router UUID and a configurable Zeroconf service name.
*   Changes are persisted to `config.yaml` and applied to the audio engine.
