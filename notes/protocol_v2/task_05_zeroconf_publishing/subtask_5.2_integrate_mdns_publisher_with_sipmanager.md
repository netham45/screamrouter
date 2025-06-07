# Sub-Task 5.2: Integrate `SipMdnsPublisher` with `SipManager`

**Objective:** Modify `SipManager` to instantiate, control, and provide necessary data (SIP port, router UUID, service name) to `SipMdnsPublisher` for advertising the SIP service.

**Parent Task:** [mDNS SRV Record Publishing for SIP Server](../task_05_zeroconf_publishing.md)
**Previous Sub-Task:** [Sub-Task 5.1: Define `SipMdnsPublisher` Class Structure](./subtask_5.1_define_sip_mdns_publisher_class.md)

## Key Steps & Considerations:

1.  **Instantiate `SipMdnsPublisher` in `SipManager`:**
    *   **File:** `src/sip_server/sip_manager.py`
    *   In `SipManager.__init__()`, create an instance of `SipMdnsPublisher`.
    ```python
    # In src/sip_server/sip_manager.py
    from .mdns_publisher import SipMdnsPublisher # Assuming it's in the same directory
    import socket # For hostname

    class SipManager:
        def __init__(self, config_manager, sip_port=5060, sip_domain="screamrouter.local", zeroconf_instance=None):
            # ... (existing initializations for self.ep, self.config_manager, etc.)
            self.mdns_publisher = SipMdnsPublisher(zc_instance=zeroconf_instance) # Pass shared zc if available
            # ...
    ```
    *   Consider if `SipManager` should own the `Zeroconf` instance or if it should be shared globally (e.g., passed from `ConfigurationManager` or main app). For now, `SipMdnsPublisher` creates its own if one isn't provided.

2.  **Start Publishing in `SipManager.start()`:**
    *   After PJSIP transport is successfully created and `libStart()` is called:
        *   Retrieve the router's UUID from `ConfigurationManager`.
        *   Retrieve a user-configurable service instance name (e.g., "ScreamRouter Living Room") from `ConfigurationManager` or global settings.
        *   Determine the server's hostname.
        *   Call `self.mdns_publisher.start_publishing(...)`.
    ```python
    # In SipManager.start() method, after self.ep.libStart() and transport creation:
    # ...
    # logger.info(f"PJSIP library started. SIP UDP transport listening on port {self.sip_port}")
    # try:
    #     router_uuid = self.config_manager.get_router_uuid() 
    #     # get_router_uuid() should generate and save one if not exists.
    #     if not router_uuid:
    #         logger.error("Router UUID is not available. Cannot start mDNS SIP publishing.")
    #         # Handle this error - perhaps don't start mDNS or raise an exception
    #     else:
    #         service_instance_name = self.config_manager.get_zeroconf_service_name() 
    #         # get_zeroconf_service_name() should return a default or user-configured name.
    #         
    #         hostname = None # Let SipMdnsPublisher try to determine it
    #         # Or, be more explicit:
    #         # try:
    //         #     hostname = f"{socket.gethostname()}.local."
    //         # except Exception:
    //         #     logger.warning("Could not determine local hostname for mDNS, SipMdnsPublisher will use a fallback.")

    #         # Use the custom service type from the spec
    #         sip_service_type = "_screamrouter-sip._udp.local."

    #         self.mdns_publisher.start_publishing(
    #             service_instance_name=service_instance_name,
    #             sip_port=self.sip_port,
    #             router_uuid=router_uuid,
    #             server_hostname=hostname, # Can be None
    #             service_type=sip_service_type
    #         )
    # except Exception as e:
    #     logger.error(f"Failed to start mDNS publishing for SIP service during SipManager start: {e}", exc_info=True)
    # ...
    ```

3.  **Stop Publishing in `SipManager.stop()`:**
    *   Before PJSIP library is destroyed, call `self.mdns_publisher.stop_publishing()`.
    *   Also call `self.mdns_publisher.close()` to allow it to clean up its `Zeroconf` instance if it owns it.
    ```python
    # In SipManager.stop() method, before self.ep.libDestroy():
    # ...
    # if self.mdns_publisher:
    #     logger.info("Stopping SIP mDNS publisher...")
    #     self.mdns_publisher.stop_publishing() # Unregister first
    #     self.mdns_publisher.close()          # Then close Zeroconf instance if owned
    #     self.mdns_publisher = None           # Clear reference
    # ...
    # try:
    #    if self.lib_inited:
    #        self.ep.libDestroy()
    # ...
    ```

4.  **Configuration for Service Name and Router UUID:**
    *   `ConfigurationManager` needs to provide:
        *   `get_router_uuid() -> str`: Returns a persistent UUID for this ScreamRouter instance. If one doesn't exist in settings, it should generate, save, and return it.
        *   `get_zeroconf_service_name() -> str`: Returns the user-configurable part of the mDNS service name (e.g., "ScreamRouter Office"). Defaults to something like "ScreamRouter".
    *   These methods were outlined in `task_04_python_config_updates/subtask_4.2_configmanager_persistence_sip_logic.md`.

## Code Alterations:

*   **`src/sip_server/sip_manager.py`:**
    *   Import `SipMdnsPublisher`.
    *   Instantiate `SipMdnsPublisher` in `__init__`.
    *   Call `mdns_publisher.start_publishing()` in the `start()` method with data from `ConfigurationManager`.
    *   Call `mdns_publisher.stop_publishing()` and `mdns_publisher.close()` in the `stop()` method.
*   **`src/configuration/configuration_manager.py`:**
    *   Ensure `get_router_uuid()` and `get_zeroconf_service_name()` are implemented and provide necessary values.
    *   If a global `Zeroconf` instance is to be shared, `ConfigurationManager` might manage it and pass it to `SipMdnsPublisher` via `SipManager`.

## Recommendations:

*   **Service Type:** Use `_screamrouter-sip._udp.local.` as the `service_type` argument to `start_publishing` to align with `protocol_spec_v2.md`.
*   **Error Handling:** Robustly handle exceptions during mDNS publisher start/stop, ensuring that failures in mDNS don't prevent the SIP server itself from starting/stopping if possible.
*   **Lifecycle Management:** Ensure `SipMdnsPublisher.close()` is called when `SipManager` is definitively shutting down to release Zeroconf resources.

## Acceptance Criteria:

*   `SipManager` instantiates `SipMdnsPublisher`.
*   `SipManager.start()` calls `SipMdnsPublisher.start_publishing()` with the correct SIP port, router UUID, and service name derived from configuration.
*   `SipManager.stop()` calls `SipMdnsPublisher.stop_publishing()` and `SipMdnsPublisher.close()`.
*   The integration allows the SIP service to be advertised via mDNS when `SipManager` is active.
