# Sub-Task 3.6: Initial Testing of SIP Functionalities

**Objective:** Perform initial tests of the SIP server functionalities, including device registration (REGISTER), keep-alives (OPTIONS), and basic WebRTC call signaling (INVITE offer/answer relay).

**Parent Task:** [SIP Library (pjproject) Integration](../task_03_sip_library_integration.md)
**Previous Sub-Task:** [Sub-Task 3.5: Integrate `SipManager` with `ConfigurationManager` and Zeroconf](./subtask_3.5_integrate_sipmanager_configmanager_zeroconf.md)

## Key Testing Scenarios:

1.  **Zeroconf SIP Service Discovery:**
    *   **Test:** Start ScreamRouter. Use an mDNS/DNS-SD browsing tool (e.g., `avahi-browse -r _sip._udp` on Linux, "Discovery" app on macOS) to verify that the `_sip._udp.local.` service (or `_screamrouter-sip._udp.local.` as per spec) is advertised correctly.
    *   **Verification:** The SRV record should point to the correct hostname/IP and SIP port of the ScreamRouter instance. TXT records (version, router_uuid) should be present and correct.

2.  **Device Registration (SIP REGISTER):**
    *   **Setup:** Use a SIP client tool (e.g., Linphone, MicroSIP, `pjsua` CLI tool, or a custom Python script using `pjsua2`).
    *   **Execution:**
        1.  Configure the SIP client to send a `REGISTER` request to ScreamRouter's SIP address (discovered via Zeroconf or manually configured).
        2.  The `REGISTER` request should include:
            *   `From`: `sip:<device_uuid>@<client_domain_or_ip>` (e.g., `sip:testdevice-uuid-123@192.168.1.100`)
            *   `To`: `sip:screamrouter@<screamrouter_domain_or_ip>`
            *   `Contact`: `<sip:<device_uuid>@<client_ip>:<client_sip_port>>`
            *   `Expires`: e.g., 3600
            *   (Optional) SDP body describing client capabilities (role, supported formats, custom attributes like `a=x-screamrouter-uuid:testdevice-uuid-123`, `a=x-screamrouter-role:source`).
    *   **Verification:**
        *   ScreamRouter's `SipAccount.onRxRequest` (or specific REGISTER handler) should receive the request.
        *   Logs should show parsing of headers and SDP.
        *   `ConfigurationManager.handle_sip_registration` should be called.
        *   `config.yaml` should be updated with a new/updated device entry reflecting the registration details (UUID, IP, port, protocol_type="sip", role, capabilities).
        *   The SIP client should receive a `200 OK` response from ScreamRouter.

3.  **Keep-Alives (SIP OPTIONS):**
    *   **Setup:** Configure the SIP client to send periodic `OPTIONS` requests to ScreamRouter.
    *   **Execution:** Let the client send `OPTIONS` requests.
    *   **Verification:**
        *   ScreamRouter's `SipAccount.onRxRequest` (or specific OPTIONS handler) should receive the requests.
        *   `ConfigurationManager.update_sip_device_last_seen` (or similar) should be called.
        *   The device should remain marked as "online" or "active" in `ConfigurationManager`.
        *   The SIP client should receive a `200 OK` response (possibly with `Allow`/`Accept` headers).
        *   Test device timeout: Stop the client from sending OPTIONS. After a configured timeout, verify `ConfigurationManager.handle_sip_device_offline` is triggered and the device is marked offline.

4.  **WebRTC Signaling (INVITE Offer/Answer Relay):**
    *   **Setup:**
        *   A WebRTC sink configured in ScreamRouter.
        *   A WebRTC test client (browser page from Sub-Task 2.7) that initiates a connection by sending an SDP offer via SIP INVITE.
    *   **Execution:**
        1.  Web client sends INVITE with SDP offer to ScreamRouter.
        2.  `SipAccount.onIncomingCall` creates a `SipCall`.
        3.  `SipCall` extracts the offer and forwards it to `ConfigurationManager` -> C++ `AudioManager` -> `WebRTCSender`.
        4.  `WebRTCSender` processes the offer, generates an SDP answer, and sends it back via the C++ -> Python signaling callback.
        5.  `ConfigurationManager` (handling the callback from C++) relays the answer to `SipManager`/`SipCall`.
        6.  `SipCall` sends the `200 OK` with the SDP answer to the web client.
    *   **Verification:**
        *   Logs at each step (Python SIP, `ConfigurationManager`, C++ `AudioManager`, `WebRTCSender`) show the SDP offer/answer flow.
        *   The web client receives the SDP answer.
        *   (ICE candidate exchange will follow a similar path and should also be logged/verified).
        *   Ultimately, the WebRTC connection should establish (as tested in Sub-Task 2.7, but here focusing on the SIP part of signaling).

## Debugging Tools & Techniques:

*   **PJSIP Logging:** Enable verbose PJSIP logging in `SipManager` (`EpConfig.logConfig.level = 5` or higher) to see detailed SIP message traces and internal PJSIP operations.
*   **ScreamRouter Application Logs:** Ensure comprehensive logging in `SipManager`, `SipAccount`, `SipCall`, `sdp_utils`, and `ConfigurationManager` related to SIP processing.
*   **SIP Client Logs:** Most SIP clients provide their own logs or console output showing sent/received messages and registration status.
*   **Wireshark:** Capture network traffic on the SIP port (default 5060 UDP/TCP) to inspect raw SIP messages and SDP content. Filter by `sip`.
*   **Manual SDP Crafting:** For initial tests of `sdp_utils.py`, manually craft simple SDP strings to pass to the parsing functions.

## Code Alterations:

*   Primarily involves adding detailed logging throughout the Python SIP stack (`sip_server/` modules and `ConfigurationManager`).
*   Refinements to `SipAccount` and `SipCall` based on PJSIP behavior observed during testing (e.g., how to correctly parse specific headers or manage call states).
*   Full implementation of `handle_register_request`, `handle_options_request`, and the WebRTC signaling relay logic in `SipCall` and `ConfigurationManager`.

## Recommendations:

*   **Test Incrementally:**
    1.  Zeroconf discovery.
    2.  Basic REGISTER with no SDP, verify 200 OK and `config.yaml` update.
    3.  REGISTER with simple SDP, verify parsing.
    4.  OPTIONS keep-alives and timeouts.
    5.  INVITE with WebRTC SDP offer, verify relay to C++ and placeholder answer relay back.
*   **Use `pjsua` CLI:** The `pjsua` command-line tool that comes with `pjproject` is excellent for sending arbitrary SIP messages and testing server responses.
*   **Refer to PJSIP Documentation:** The PJSIP Book and `pjsua2` reference are crucial for understanding how to correctly handle SIP transactions, message parsing, and state management.

## Acceptance Criteria:

*   ScreamRouter's SIP service is discoverable via Zeroconf.
*   Devices can successfully REGISTER with ScreamRouter, and their details (including basic SDP capabilities) are stored/updated in `config.yaml`.
*   OPTIONS keep-alives are processed, and device online status is maintained/timed out correctly.
*   The SIP signaling path for WebRTC (INVITE with SDP offer, relay to C++, relay SDP answer back) is functional at a basic level (messages are passed through).
*   Logs provide clear insight into SIP message handling and processing.
