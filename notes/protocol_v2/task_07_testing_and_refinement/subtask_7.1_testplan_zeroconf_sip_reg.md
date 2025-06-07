# Sub-Task 7.1: Test Plan for Zeroconf Discovery and SIP Registration

**Objective:** Define detailed test cases and procedures for verifying Zeroconf-based SIP server discovery and the complete SIP device registration process, including SDP capability exchange and keep-alives.

**Parent Task:** [Protocol v2 Testing and Refinement Strategy](../task_07_testing_and_refinement.md)

## I. Zeroconf SIP Service Discovery Testing

**Tools:**
*   ScreamRouter instance running.
*   mDNS/DNS-SD browsing tool (e.g., `avahi-browse` on Linux, `dns-sd` on macOS, Bonjour Browser on Windows).
*   Wireshark (optional, for deep inspection of mDNS packets).

**Test Cases:**

1.  **TC-ZC-001: Service Publication Verification**
    *   **Preconditions:** ScreamRouter started, `SipManager` and `SipMdnsPublisher` active.
    *   **Steps:**
        1.  On a client machine on the same network, use an mDNS browser to look for service type `_screamrouter-sip._udp.local.` (or the final chosen type like `_sip._udp.local.` if standard is adopted).
    *   **Expected Results:**
        *   The ScreamRouter instance is discovered.
        *   The service instance name matches the configured `zeroconf_instance_name` (e.g., "MyScreamRouter._screamrouter-sip._udp.local.").
    *   **Pass/Fail.**

2.  **TC-ZC-002: SRV Record Content Verification**
    *   **Preconditions:** TC-ZC-001 passed. Service instance discovered.
    *   **Steps:**
        1.  Resolve the discovered service instance to get SRV record details (e.g., `avahi-browse -r <instance_name>`).
    *   **Expected Results:**
        *   SRV record target hostname is correct (e.g., `screamrouter-hostname.local.`).
        *   SRV record port matches the actual SIP listening port of `SipManager`.
        *   Priority and Weight are standard values (e.g., 0).
    *   **Pass/Fail.**

3.  **TC-ZC-003: TXT Record Content Verification**
    *   **Preconditions:** TC-ZC-001 passed. Service instance discovered.
    *   **Steps:**
        1.  Inspect the TXT records associated with the discovered service.
    *   **Expected Results:**
        *   `srv_version` TXT record is present and correct (e.g., "2.0").
        *   `router_uuid` TXT record is present and matches ScreamRouter's persistent UUID.
        *   `transport` TXT record is present (e.g., "udp").
    *   **Pass/Fail.**

4.  **TC-ZC-004: Hostname Resolution (A/AAAA Record)**
    *   **Preconditions:** TC-ZC-002 passed. SRV record resolved.
    *   **Steps:**
        1.  Verify that the target hostname from the SRV record is resolvable to an IP address via mDNS (browsing tools often show this).
    *   **Expected Results:** The hostname resolves to the correct IP address(es) of the ScreamRouter machine.
    *   **Pass/Fail.**

5.  **TC-ZC-005: Service Unregistration on Shutdown**
    *   **Preconditions:** TC-ZC-001 passed. Service is currently published.
    *   **Steps:**
        1.  Gracefully shut down ScreamRouter.
        2.  Monitor mDNS advertisements using the browsing tool.
    *   **Expected Results:** The ScreamRouter SIP service is no longer advertised after a short period (respecting mDNS TTLs for "goodbye" packets).
    *   **Pass/Fail.**

## II. SIP Registration and Presence Testing

**Tools:**
*   ScreamRouter instance running.
*   SIP client software (e.g., Linphone, MicroSIP, Bria, `pjsua` CLI) or custom Python test script using `pjsua2`.
*   Wireshark for inspecting SIP messages.
*   Access to ScreamRouter's `config.yaml` and logs.

**Test Cases:**

1.  **TC-SIP-REG-001: New Device Registration (No SDP)**
    *   **Preconditions:** ScreamRouter running. No existing configuration for the test device UUID.
    *   **Steps:**
        1.  Configure SIP client with a unique UUID (e.g., `client-uuid-001`).
        2.  Send a `REGISTER` request to ScreamRouter's SIP address. Do not include an SDP body.
        3.  Set `Expires` header to e.g., 300.
    *   **Expected Results:**
        *   Client receives `200 OK` from ScreamRouter.
        *   ScreamRouter logs show successful REGISTER processing.
        *   `config.yaml` is updated: a new Source/Sink (based on a default role or lack of SDP) is created with `uuid="client-uuid-001"`, `protocol_type="sip"`, correct `sip_contact_uri`, and `enabled=true`.
    *   **Pass/Fail.**

2.  **TC-SIP-REG-002: New Device Registration (With SDP - Source)**
    *   **Preconditions:** ScreamRouter running. No existing configuration for `client-uuid-002`.
    *   **Steps:**
        1.  Configure SIP client with `uuid="client-uuid-002"`.
        2.  Send `REGISTER` with an SDP body indicating it's an audio source (e.g., `a=sendonly`, `a=x-screamrouter-role:source`, RTP map for L16/48000/2).
    *   **Expected Results:**
        *   Client receives `200 OK`.
        *   `config.yaml` creates a new `SourceDescription` for `client-uuid-002`, `protocol_type="sip"`, `role="source"`.
        *   `rtp_config` within the new source description reflects capabilities from SDP (e.g., payload type for L16, listening port if specified in `m=` line).
    *   **Pass/Fail.**

3.  **TC-SIP-REG-003: New Device Registration (With SDP - Sink)**
    *   **Preconditions:** Similar to TC-SIP-REG-002, but for `client-uuid-003`.
    *   **Steps:**
        1.  Send `REGISTER` with SDP indicating an audio sink (e.g., `a=recvonly`, `a=x-screamrouter-role:sink`).
    *   **Expected Results:**
        *   Client receives `200 OK`.
        *   `config.yaml` creates a new `SinkDescription` for `client-uuid-003`, `protocol_type="sip"`, `role="sink"`.
    *   **Pass/Fail.**

4.  **TC-SIP-REG-004: Device Re-Registration (Update Contact/Capabilities)**
    *   **Preconditions:** Device `client-uuid-001` is already registered.
    *   **Steps:**
        1.  Client `client-uuid-001` sends a new `REGISTER` with a different Contact port or modified SDP (e.g., adding a new `rtpmap`).
    *   **Expected Results:**
        *   Client receives `200 OK`.
        *   The existing entry for `client-uuid-001` in `config.yaml` is updated with the new `sip_contact_uri` and/or capabilities from SDP. `enabled` remains `true`.
    *   **Pass/Fail.**

5.  **TC-SIP-REG-005: Registration Expiry (De-registration)**
    *   **Preconditions:** Device `client-uuid-001` registered with `Expires=60`.
    *   **Steps:**
        1.  Client `client-uuid-001` sends `REGISTER` with `Expires=0`.
    *   **Expected Results:**
        *   Client receives `200 OK`.
        *   Device `client-uuid-001` in `config.yaml` is marked as `enabled=false` (or `online_status=false`). Its configuration details (UUID, capabilities) should remain.
    *   **Pass/Fail.**

6.  **TC-SIP-PRES-001: Keep-Alive (OPTIONS)**
    *   **Preconditions:** Device `client-uuid-001` is registered.
    *   **Steps:**
        1.  Client `client-uuid-001` sends an `OPTIONS` request to ScreamRouter.
    *   **Expected Results:**
        *   Client receives `200 OK` (possibly with `Allow`/`Accept` headers).
        *   ScreamRouter logs show OPTIONS processing.
        *   `ConfigurationManager` updates the `last_seen_timestamp` for `client-uuid-001`. Device remains `enabled=true`.
    *   **Pass/Fail.**

7.  **TC-SIP-PRES-002: Keep-Alive Timeout**
    *   **Preconditions:** Device `client-uuid-001` registered. Keep-alive timeout in ScreamRouter is set to a testable value (e.g., 30 seconds).
    *   **Steps:**
        1.  Client `client-uuid-001` stops sending any SIP messages (REGISTER or OPTIONS).
        2.  Wait for longer than the keep-alive timeout.
    *   **Expected Results:**
        *   ScreamRouter's `SipManager` detects the timeout.
        *   `ConfigurationManager.handle_sip_device_offline` is called for `client-uuid-001`.
        *   Device `client-uuid-001` in `config.yaml` is marked `enabled=false`.
    *   **Pass/Fail.**

## III. General Considerations

*   **Logging:** All test cases rely on detailed logging from `SipMdnsPublisher`, `SipManager`, `SipAccount`, and `ConfigurationManager`.
*   **Wireshark:** Use Wireshark to capture and verify the actual mDNS and SIP messages being exchanged, especially if discrepancies are found between client/server behavior and logs.
*   **Configuration:** Ensure ScreamRouter's SIP port, domain, and Zeroconf settings are correctly configured for the tests.
