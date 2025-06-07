# Sub-Task 5.5: Test mDNS SRV Record Publication

**Objective:** Verify that ScreamRouter correctly publishes the SIP service SRV record via mDNS and that this record can be discovered by mDNS browsing tools.

**Parent Task:** [mDNS SRV Record Publishing for SIP Server](../task_05_zeroconf_publishing.md)
**Previous Sub-Task:** [Sub-Task 5.4: Refactor/Deprecate Old mDNS Utilities](./subtask_5.4_refactor_deprecate_old_mdns.md)

## Key Testing Steps:

1.  **Environment Setup:**
    *   Ensure ScreamRouter is running with the `SipManager` and integrated `SipMdnsPublisher` active.
    *   Ensure the machine running ScreamRouter and the testing machine (can be the same) are on the same local network segment where mDNS/Bonjour traffic is permitted.
    *   Have mDNS/DNS-SD browsing tools available on the testing machine.
        *   **Linux:** `avahi-browse` (from `avahi-utils` package).
        *   **macOS:** "Discovery" application (available from Apple's developer resources or third-party) or `dns-sd` command-line tool.
        *   **Windows:** Bonjour Browser or similar third-party mDNS diagnostic tools.

2.  **Start ScreamRouter:**
    *   Observe ScreamRouter logs for messages indicating:
        *   `SipManager` started and listening on its configured SIP port.
        *   `SipMdnsPublisher` started publishing the service (e.g., "Registering SIP mDNS service...").
        *   The details being published (service instance name, service type, target hostname, port, TXT records).

3.  **Use mDNS Browsing Tools to Discover the Service:**
    *   **Command for `avahi-browse` (Linux):**
        ```bash
        avahi-browse -r _screamrouter-sip._udp
        # OR, if using the standard SIP SRV type:
        # avahi-browse -r _sip._udp 
        ```
        The `-r` flag resolves the service, showing SRV and TXT record details.
    *   **Command for `dns-sd` (macOS):**
        ```bash
        dns-sd -B _screamrouter-sip._udp # Browse for service instances
        # Once an instance is found (e.g., "MyScreamRouter"), resolve it:
        # dns-sd -L "MyScreamRouter" _screamrouter-sip._udp local.
        ```
    *   **Using GUI Tools:** Launch Bonjour Browser or Discovery and look for the `_screamrouter-sip._udp` service type.

4.  **Verify SRV Record Details:**
    *   **Service Name:** Should match the format `"<User-Configured-Instance-Name>._screamrouter-sip._udp.local."`.
    *   **Target Hostname:** Should be the hostname of the machine running ScreamRouter (e.g., `my-pc.local.`).
    *   **Port:** Should match the SIP port `SipManager` is listening on (e.g., 5060).
    *   **Priority and Weight:** Should be standard values (e.g., 0).

5.  **Verify TXT Record Details:**
    *   The resolved service should show TXT records. Verify:
        *   `srv_version="2.0"` (or as defined)
        *   `router_uuid="<actual_router_uuid>"`
        *   `transport="udp"` (derived from service type)

6.  **Verify Hostname Resolution (A/AAAA Records):**
    *   The mDNS browsing tool (especially with `-r` for `avahi-browse` or `-L` for `dns-sd`) should also show the IP address(es) associated with the target hostname. This confirms that Zeroconf is also publishing A/AAAA records for the host.

7.  **Test Service Unregistration:**
    *   Stop ScreamRouter gracefully.
    *   **Verification:**
        *   ScreamRouter logs should indicate `SipMdnsPublisher` is unregistering the service.
        *   Re-run the mDNS browsing tool. The ScreamRouter SIP service should no longer be listed (or should disappear after a short TTL).

8.  **Test with Name Conflicts (Optional but good):**
    *   If possible, run another instance of ScreamRouter (or a dummy mDNS service with the same name) on the network to see if `allow_name_change=True` in `zeroconf.register_service()` correctly handles the conflict by appending a number to the instance name (e.g., "MyScreamRouter (2)").

## Troubleshooting:

*   **Firewall:** Ensure local firewalls on the ScreamRouter machine and testing machine are not blocking mDNS traffic (typically UDP port 5353).
*   **Network Configuration:** mDNS works best on simple flat networks. Complex network configurations (multiple subnets, VLANs without mDNS reflectors/gateways) can prevent discovery.
*   **Zeroconf Library Logs:** If `zeroconf` Python library has its own logging, enable it for more insight.
*   **Incorrect Service Type/Name:** Double-check that the `service_type` and `name` being registered exactly match what browsing tools are querying. Typos are common.
*   **Hostname Issues:** If `server_hostname` in `ServiceInfo` is not correctly resolvable via mDNS, clients won't find the target. Ensure the hostname ends with `.local.` if that's the mDNS domain.

## Code Alterations:

*   Primarily involves adding detailed logging in `SipMdnsPublisher` and `SipManager` related to the parameters being published and the success/failure of registration/unregistration.

## Acceptance Criteria:

*   ScreamRouter, when started, correctly publishes an mDNS SRV record for its SIP service (e.g., `_screamrouter-sip._udp.local.`).
*   The published SRV record contains the correct target hostname and SIP port.
*   Associated TXT records (e.g., `srv_version`, `router_uuid`) are correctly published and discoverable.
*   mDNS browsing tools on another machine (or the same machine) can successfully discover and resolve the ScreamRouter SIP service.
*   The mDNS record is properly unregistered when ScreamRouter shuts down.
