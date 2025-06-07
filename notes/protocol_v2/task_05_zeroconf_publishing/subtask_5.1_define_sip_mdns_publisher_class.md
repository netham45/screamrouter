# Sub-Task 5.1: Define `SipMdnsPublisher` Class Structure

**Objective:** Define the Python class structure for `SipMdnsPublisher` in a new file (`src/sip_server/mdns_publisher.py`). This class will be responsible for publishing and unpublishing the mDNS SRV record for the SIP service using the `zeroconf` library.

**Parent Task:** [mDNS SRV Record Publishing for SIP Server](../task_05_zeroconf_publishing.md)

## Key Steps & Considerations:

1.  **Create `src/sip_server/mdns_publisher.py`:**
    *   This new file will contain the `SipMdnsPublisher` class.
    *   Import necessary modules: `zeroconf`, `socket`, `logging`.

2.  **`SipMdnsPublisher` Class Definition:**
    ```python
    # In src/sip_server/mdns_publisher.py
    import zeroconf
    import socket
    import logging
    import uuid # For potentially generating a default instance name if needed

    logger = logging.getLogger(__name__)

    class SipMdnsPublisher:
        def __init__(self, zc_instance: zeroconf.Zeroconf = None):
            """
            Initializes the SIP mDNS Publisher.
            Can optionally take an existing Zeroconf instance.
            """
            if zc_instance:
                self.zc = zc_instance
                self.owns_zc_instance = False
            else:
                # Create a new Zeroconf instance. 
                # Consider making it listen on specific interfaces if needed,
                # otherwise, it defaults to all.
                self.zc = zeroconf.Zeroconf()
                self.owns_zc_instance = True
            
            self.service_info = None
            self.is_publishing = False

        def _get_local_ip_addresses(self) -> list[str]:
            """Helper to get local IP addresses, preferring non-loopback."""
            ips = []
            try:
                # This gets all addresses, including loopback, IPv6, etc.
                # We might need to filter for usable IPv4 addresses.
                # For simplicity, socket.gethostbyname(socket.gethostname()) is often used,
                # but can be unreliable in complex network setups.
                # zeroconf's internal mechanisms for IP selection are usually robust.
                # For ServiceInfo, we often provide the hostname, and Zeroconf handles A/AAAA records.
                # If specific IPs are needed for the SRV record target, more complex logic is required.
                # For now, we'll rely on hostname.
                pass # Rely on Zeroconf to publish A/AAAA for the hostname
            except socket.gaierror:
                logger.warning("Could not determine all local IP addresses for mDNS.")
            return ips # Or return None and let Zeroconf handle it

        def start_publishing(self, service_instance_name: str, sip_port: int, 
                             router_uuid: str, server_hostname: str = None,
                             service_type: str = "_sip._udp.local."):
            """
            Starts publishing the SIP SRV record.

            :param service_instance_name: User-friendly instance name (e.g., "ScreamRouterLivingRoom")
            :param sip_port: The port the SIP server is listening on.
            :param router_uuid: The unique ID of this ScreamRouter instance.
            :param server_hostname: The hostname of this server (e.g., "myrouter.local."). 
                                     If None, socket.gethostname() + ".local." will be attempted.
            :param service_type: The SIP service type string.
            """
            if self.is_publishing:
                logger.info("SIP mDNS service already publishing.")
                return

            if not server_hostname:
                try:
                    server_hostname = f"{socket.gethostname()}.local."
                except Exception as e:
                    logger.error(f"Could not determine hostname for mDNS: {e}. Using default 'screamrouter.local.'.")
                    server_hostname = "screamrouter.local." # Fallback

            # Construct the full service name for registration
            # e.g., "ScreamRouter Living Room._sip._udp.local."
            full_service_name = f"{service_instance_name}.{service_type}"

            properties = {
                "srv_version": "2.0", # Protocol version for this SRV record
                "router_uuid": router_uuid,
                "transport": service_type.split('.')[1] # "udp" or "tcp"
            }
            # Convert properties to bytes for Zeroconf
            encoded_properties = {k: v.encode('utf-8') for k, v in properties.items()}

            try:
                # For SRV records, the server field is the target host.
                # Addresses are usually published as separate A/AAAA records by Zeroconf for that host.
                # If specific IPs are needed, `addresses` param can be used, but usually not for SRV target.
                self.service_info = zeroconf.ServiceInfo(
                    type_=service_type,
                    name=full_service_name,
                    port=sip_port,
                    properties=encoded_properties,
                    server=server_hostname, # Target host for the SRV record
                    # addresses=[socket.inet_aton("192.168.1.X")] # Only if specific IP needed and hostname not reliable
                    weight=0, # Standard SRV fields
                    priority=0 # Standard SRV fields
                )
                
                logger.info(f"Registering SIP mDNS service: {full_service_name} -> {server_hostname}:{sip_port}")
                logger.debug(f"  Type: {self.service_info.type}")
                logger.debug(f"  Name: {self.service_info.name}")
                logger.debug(f"  Server: {self.service_info.server}")
                logger.debug(f"  Port: {self.service_info.port}")
                logger.debug(f"  Properties: {properties}")

                self.zc.register_service(self.service_info, allow_name_change=True)
                self.is_publishing = True
                logger.info("SIP mDNS service registered successfully.")
            except Exception as e:
                logger.error(f"Error registering SIP mDNS service: {e}", exc_info=True)
                self.service_info = None # Clear on failure

        def stop_publishing(self):
            """
            Stops publishing the SIP SRV record.
            """
            if not self.is_publishing or not self.service_info:
                logger.info("SIP mDNS service not currently publishing or no service info.")
                return

            try:
                logger.info(f"Unregistering SIP mDNS service: {self.service_info.name}")
                self.zc.unregister_service(self.service_info)
                self.is_publishing = False
                self.service_info = None
                logger.info("SIP mDNS service unregistered successfully.")
            except Exception as e:
                logger.error(f"Error unregistering SIP mDNS service: {e}", exc_info=True)

        def close(self):
            """
            Stops publishing and closes the Zeroconf instance if owned by this publisher.
            """
            self.stop_publishing()
            if self.owns_zc_instance and self.zc:
                try:
                    logger.info("Closing owned Zeroconf instance.")
                    self.zc.close()
                except Exception as e:
                    logger.error(f"Error closing Zeroconf instance: {e}", exc_info=True)
            self.zc = None
    ```

3.  **Constructor (`__init__`):**
    *   Initializes `zeroconf.Zeroconf()` instance. It can optionally accept an existing instance if Zeroconf is managed globally.
    *   Initializes `service_info` to `None` and `is_publishing` to `False`.

4.  **`start_publishing()` Method:**
    *   Takes parameters: `service_instance_name` (e.g., "ScreamRouter Office"), `sip_port`, `router_uuid`, and optionally `server_hostname` and `service_type`.
    *   Constructs the full service name (e.g., `MyScreamRouter._sip._udp.local.`).
    *   Creates TXT record properties (e.g., `srv_version`, `router_uuid`).
    *   Creates a `zeroconf.ServiceInfo` object.
        *   `type_`: `_sip._udp.local.` (standard SIP UDP) or `_screamrouter-sip._udp.local.` (custom, as per spec). The spec uses `_screamrouter-sip._udp.local`, so this should be the default.
        *   `name`: The full service name.
        *   `server`: The hostname of the machine (e.g., `my-pc.local.`). `socket.gethostname() + ".local."` is a common way to form this.
        *   `port`: The SIP listening port.
        *   `properties`: The TXT records.
        *   `weight`, `priority`: Standard SRV defaults (e.g., 0).
    *   Calls `self.zc.register_service(self.service_info, allow_name_change=True)`. `allow_name_change` handles conflicts if the instance name is already taken.
    *   Sets `self.is_publishing = True`.

5.  **`stop_publishing()` Method:**
    *   If `is_publishing` is true and `service_info` exists, calls `self.zc.unregister_service(self.service_info)`.
    *   Resets `is_publishing` and `service_info`.

6.  **`close()` Method:**
    *   Calls `stop_publishing()`.
    *   If this class instance owns the `Zeroconf` object (i.e., it created it), call `self.zc.close()`.

## Code Alterations:

*   **New File:** `src/sip_server/mdns_publisher.py` - Implement the `SipMdnsPublisher` class.
*   **File:** `src/sip_server/__init__.py` - Ensure it exists.
*   **File:** `requirements.txt` - Ensure `zeroconf` is listed (already a dependency for old mDNS).

## Recommendations:

*   **Service Type:** The specification document uses `_screamrouter-sip._udp.local.`. This custom type should be used to avoid potential conflicts if a standard SIP client is also running on the network and to specifically target ScreamRouter clients.
*   **Hostname vs. IP Addresses:** For `ServiceInfo.server`, providing a hostname (e.g., `mycomputer.local.`) is generally preferred. The `zeroconf` library itself, when registering this service, should also publish A/AAAA records for this hostname, making it resolvable by mDNS clients. Explicitly listing IP addresses in `ServiceInfo(addresses=...)` is usually for when the `server` field cannot be resolved or for more direct control, but can be more complex with multiple interfaces.
*   **Error Handling:** Add `try...except` blocks around `zeroconf` calls for robustness.
*   **Instance Name Uniqueness:** `allow_name_change=True` in `register_service` is good. The `service_instance_name` should ideally be user-configurable.

## Acceptance Criteria:

*   `SipMdnsPublisher` class is defined in `src/sip_server/mdns_publisher.py`.
*   The class can initialize a `Zeroconf` instance or use a provided one.
*   `start_publishing()` correctly constructs and registers a `ServiceInfo` object for the SIP service with appropriate SRV and TXT records.
*   `stop_publishing()` unregisters the service.
*   `close()` cleans up resources, including the `Zeroconf` instance if owned.
*   The project compiles/runs with the new module.
