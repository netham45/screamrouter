# Task: mDNS SRV Record Publishing for SIP Server (Replaces Zeroconf for this specific task)

**Objective:** Implement mDNS SRV record publishing within ScreamRouter so that clients can automatically discover the SIP Presence Server. This will involve creating a new, clean implementation for publishing the specific SRV record for the SIP service, deprecating the use of the old `mdns_responder.py` for this purpose.

**Parent Plan Section:** V. Device Discovery

**Files to Modify/Create:**

*   **New Python File: `src/sip_server/mdns_publisher.py` (or integrate into `src/sip_server/sip_manager.py` if simple enough):**
    *   This module/class will be responsible for publishing the mDNS SRV record for the SIP service.
    *   **`SipMdnsPublisher` Class:**
        *   Uses the `zeroconf` Python library.
        *   `__init__(self, zeroconf_instance=None)`: Optionally takes an existing `Zeroconf` instance or creates its own.
        *   `start_publishing(self, service_name: str, sip_port: int, router_uuid: str, server_hostname: str)`:
            *   Constructs a `ServiceInfo` object.
            *   **Service Type:** `"_sip._udp.local."` (Standard SIP service type via UDP).
            *   **Service Name:** `f"{service_name}._sip._udp.local."` (e.g., `"ScreamRouterLivingRoom._sip._udp.local."`). The `service_name` part should be user-configurable or derived (e.g., from system hostname).
            *   **Server (SRV target):** `f"{server_hostname}.local."` (The hostname of the machine running ScreamRouter, needs to be resolvable via mDNS as well, or use IP).
            *   **Port:** The `sip_port` the SIP server is listening on.
            *   **Priority, Weight:** Standard defaults (e.g., 0, 0).
            *   **Properties (TXT Records):**
                *   `srv_version=2.0` (Protocol version for this SRV record, distinct from general app version)
                *   `router_uuid=<router_uuid>`
                *   Potentially `transport=udp`
            *   Registers the `ServiceInfo` with the `Zeroconf` instance: `self.zeroconf.register_service(info, allow_name_change=True)`.
        *   `stop_publishing(self)`:
            *   Unregisters the service using `self.zeroconf.unregister_service(info)`.
            *   Closes its `Zeroconf` instance if it created it.
*   **`src/sip_server/sip_manager.py`:**
    *   Instantiates `SipMdnsPublisher`.
    *   When `SipManager` starts listening: Calls `mdns_publisher.start_publishing()` with its listen port, the router's UUID (obtained from `ConfigurationManager` or global config), a configurable service name part, and the system's hostname.
    *   When `SipManager` stops: Calls `mdns_publisher.stop_publishing()`.
*   **`src/configuration/configuration_manager.py`:**
    *   Needs to provide a persistent `router_uuid` for the ScreamRouter instance. If not in `config.yaml`, generate and save one.
    *   May need to provide a way to get/configure the `service_name` part for mDNS and the system `hostname` if not easily discoverable by `SipMdnsPublisher`.
*   **`src/utils/mdns_responder.py`, `src/utils/mdns_pinger.py`, `src/utils/mdns_settings_pinger.py`:**
    *   These files will **no longer be responsible for advertising the SIP service**.
    *   Review their existing functionalities. If they are solely for device discovery *by* ScreamRouter (pinging for sources/sinks) or settings sync, those parts might remain or be refactored/deprecated if SIP handles all discovery and configuration updates.
    *   The specific logic for responding to `_screamrouter._udp.local` or similar generic ScreamRouter service queries (if any) should be re-evaluated. The primary discovery mechanism for Protocol v2 clients will be the SIP SRV record.
*   **`requirements.txt` (or `pyproject.toml`):**
    *   Ensure `zeroconf` is listed.

**Detailed Steps:**

1.  **Ensure `zeroconf` Dependency:** Verify `zeroconf` is in project dependencies.
2.  **Implement `SipMdnsPublisher` (`src/sip_server/mdns_publisher.py`):**
    *   Define the class with `start_publishing` and `stop_publishing` methods.
    *   Correctly construct `ServiceInfo` for an SRV record pointing to the SIP service, including TXT records.
    *   Handle `Zeroconf` instance lifecycle.
3.  **Integrate with `SipManager`:**
    *   `SipManager` creates and controls the `SipMdnsPublisher`.
    *   Pass necessary data (port, UUID, service name, hostname) to `start_publishing`.
4.  **Router UUID and Service Name Configuration:**
    *   Ensure `ConfigurationManager` can provide a stable `router_uuid`.
    *   Determine how the `service_name` part (e.g., "ScreamRouterLivingRoom") and `server_hostname` are obtained or configured. `socket.gethostname()` can get the hostname.
5.  **Refactor/Deprecate Old mDNS Utilities:**
    *   Remove any SIP service advertising logic from `src/utils/mdns_responder.py`.
    *   Clearly document that `_sip._udp.local.` is the new discovery method for Protocol v2 clients.
6.  **Testing:**
    *   Run ScreamRouter.
    *   Use mDNS/DNS-SD browsing tools to query for `_sip._udp.local.` SRV records.
    *   Verify the SRV record points to the correct hostname and SIP port of the ScreamRouter instance.
    *   Verify the associated TXT records are present and correct.
    *   Verify service unregistration on shutdown.

**Considerations:**

*   **Hostname Resolution:** The `server_hostname` in the SRV record must be resolvable by clients on the mDNS network (usually `hostname.local.`). The `zeroconf` library often handles publishing the A/AAAA records for the hostname automatically if it's the local machine's hostname.
*   **Standard Compliance:** Adhere to DNS-SD and SRV record standards. The service type `_sip._udp.local.` is standard for SIP over UDP.
*   **Deprecation Path:** If old clients relied on `_screamrouter._udp.local.`, consider if a transitional period or dual publishing is needed, though the goal is to move to the standard SIP SRV record.

**Acceptance Criteria:**

*   ScreamRouter correctly publishes an SRV record for `_sip._udp.local.` via mDNS.
*   The SRV record contains the correct target hostname and port for the SIP server.
*   Associated TXT records (version, router_uuid) are correctly published.
*   Clients can discover the SIP server using this SRV record.
*   The mDNS record is properly unregistered on shutdown.
*   Old mDNS utilities are refactored or clearly marked regarding their new scope.
