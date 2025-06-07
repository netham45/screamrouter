# Sub-Task 5.6: Document New mDNS Setup

**Objective:** Update all relevant project documentation (`protocol_spec_v2.md`, `README.md`, developer guides) to reflect the new mDNS SRV record publishing mechanism for SIP service discovery and the status of any old mDNS utilities.

**Parent Task:** [mDNS SRV Record Publishing for SIP Server](../task_05_zeroconf_publishing.md)
**Previous Sub-Task:** [Sub-Task 5.5: Test mDNS SRV Record Publication](./subtask_5.5_test_mdns_srv_publication.md)

## Key Documentation Areas:

1.  **`notes/protocol_v2/protocol_spec_v2.md`:**
    *   **Section 2: Device Discovery (Zeroconf):**
        *   Ensure this section accurately describes the published SRV record:
            *   **Service Type:** Confirm if it's `_screamrouter-sip._udp.local.` (as per original spec) or if a standard `_sip._udp.local.` was chosen with specific instance naming. The sub-tasks leaned towards `_screamrouter-sip._udp.local.` for specificity.
            *   **Service Name:** Clarify the format, e.g., `<User-Configurable-Instance-Name>._screamrouter-sip._udp.local.`.
            *   **Port:** The SIP port.
            *   **Target Hostname:** How it's determined (e.g., `hostname.local.`).
            *   **TXT Records:** List all published TXT records and their meanings:
                *   `srv_version` (e.g., "2.0")
                *   `router_uuid` (The persistent UUID of the ScreamRouter instance)
                *   `transport` (e.g., "udp")
                *   (Any other relevant TXT records added)
        *   Remove or update any references to old mDNS discovery methods if they are deprecated for Protocol v2 clients.

2.  **`README.md` (or User Guide / Installation Guide):**
    *   **For Users:**
        *   Briefly explain that Protocol v2 devices will discover ScreamRouter automatically on the local network using Zeroconf/Bonjour.
        *   Mention that no special client-side configuration for IP/port should be needed if their client supports Zeroconf discovery for the specified SIP service type.
    *   **For Developers/Advanced Users Building from Source:**
        *   List `python-zeroconf` as a dependency (already in `requirements.txt`).
        *   Mention any OS-level dependencies for mDNS to function correctly (e.g., Avahi daemon on Linux, Bonjour service on Windows/macOS). This is usually handled by the OS or `python-zeroconf`'s own dependencies.

3.  **Developer Documentation (if separate from README):**
    *   **`SipMdnsPublisher` Class:** Briefly describe its role and how it's used by `SipManager`.
    *   **Configuration:** Explain how `router_uuid` and `zeroconf_instance_name` are managed by `ConfigurationManager` and used in mDNS publishing.
    *   **Legacy mDNS Utilities:**
        *   Clearly state the status of `mdns_responder.py`, `mdns_pinger.py`, etc.
        *   If they are deprecated for Protocol v2, explain why (superseded by SIP/Zeroconf).
        *   If parts are retained for legacy client support or other specific purposes (e.g., web UI discovery), document their exact function, the service type they advertise/browse for, and under what conditions they are active.
        *   If there's a configuration flag to enable/disable legacy mDNS, document it.

## Code Alterations (Documentation as Code):

*   Add comments within `src/sip_server/mdns_publisher.py` and relevant sections of `src/sip_server/sip_manager.py` and `src/configuration/configuration_manager.py` explaining the mDNS logic and configuration points.

## Example Documentation Snippets:

**For `protocol_spec_v2.md` (Section 2):**

> ## 2. Device Discovery (Zeroconf)
>
> Protocol v2 compliant devices discover the ScreamRouter SIP Presence Server using Zeroconf (DNS-SD). ScreamRouter publishes an SRV record with the following characteristics:
>
> *   **Service Type:** `_screamrouter-sip._udp.local.`
> *   **Service Instance Name:** A user-configurable name, defaulting to "ScreamRouter". The full registered name will be, for example, `"My ScreamRouter._screamrouter-sip._udp.local."`.
> *   **Target Hostname:** The mDNS hostname of the machine running ScreamRouter (e.g., `hostname.local.`).
> *   **Port:** The UDP port on which the ScreamRouter SIP Presence Server is listening (configurable, defaults to 5060).
> *   **TXT Records:**
>     *   `srv_version=2.0`: Indicates the version of this SRV record format or related protocol aspects.
>     *   `router_uuid=<UUID>`: The persistent, unique identifier of this ScreamRouter instance.
>     *   `transport=udp`: Indicates the transport protocol for the SIP service.
>
> Clients query for the `_screamrouter-sip._udp.local.` service type to find active ScreamRouter instances, their target hostnames, ports, and additional capabilities via TXT records.

**For `README.md` (Developer Build Section):**

> ### mDNS/Zeroconf for Service Discovery
>
> ScreamRouter uses mDNS (Zeroconf/Bonjour) to advertise its Protocol v2 SIP service. This allows compatible clients to automatically discover the router on the local network.
>
> *   The Python `zeroconf` library is used for this.
> *   The SIP service is advertised as `_screamrouter-sip._udp.local.`.
> *   Legacy mDNS utilities for older Scream protocol discovery (`src/utils/mdns_responder.py`) may be active for backward compatibility if enabled, but Protocol v2 clients should use the SIP SRV record.

## Acceptance Criteria:

*   `protocol_spec_v2.md` is updated to accurately reflect the mDNS SRV record details for SIP service discovery.
*   `README.md` (or other user/developer guides) contains relevant information about mDNS dependencies and the new discovery mechanism.
*   The status and purpose of old mDNS utilities are clearly documented, including any deprecation notices.
*   Code comments in relevant Python modules clarify the mDNS publishing logic.
