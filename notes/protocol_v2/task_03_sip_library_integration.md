# Task: SIP Library (pjproject) Integration

**Objective:** Integrate the `pjproject` library (specifically PJSIP using `pjsua2` Python bindings) into ScreamRouter to create a SIP Presence Server. This server will handle device registration, presence, and basic session negotiation signaling for WebRTC and potentially RTP.

**Parent Plan Section:** I. Core Technologies & Libraries Selection/Integration

**Files to Modify/Create:**

*   **`requirements.txt` (or `pyproject.toml`):**
    *   Add the Python binding for `pjproject` (e.g., `pjproject`). Ensure this package provides the `pjsua2` module.
*   **New Python Module: `src/sip_server/`**
    *   **`src/sip_server/sip_manager.py`:**
        *   `SipManager` class:
            *   Initializes PJSIP `Endpoint`, creates SIP transport (UDP, e.g., port 5060).
            *   Implements `pj.Account` virtual methods (subclass `pj.Account`):
                *   `on_reg_state()`: Handles `REGISTER` requests. Extracts device UUID (e.g., from `From` URI user part or a custom header), client IP/port from `Contact`, and parses SDP from request body if present. Calls `ConfigurationManager.handle_sip_registration()`.
                *   `on_incoming_call()`: Handles `INVITE`. Parses SDP offer. For WebRTC, relays offer to C++ `AudioManager` via `ConfigurationManager` or a direct bridge. Receives SDP answer from C++ and sends `200 OK` with this answer. Manages call state.
                *   `on_instant_message()` (or `on_rx_request` for `OPTIONS`): Handles `OPTIONS` for keep-alives. Updates device's last-seen time in `ConfigurationManager`.
            *   Manages a list of active SIP "accounts" or sessions, mapping to device UUIDs.
            *   Periodically checks for timed-out devices and notifies `ConfigurationManager`.
            *   Provides methods for `ConfigurationManager` to initiate actions if needed (e.g., send an `OPTIONS` ping).
    *   **`src/sip_server/sdp_utils.py` (New):**
        *   Helper functions to parse SDP:
            *   Extract media descriptions (`m=` lines).
            *   Extract attributes (`a=rtpmap`, `a=fmtp`, `a=sendrecv/sendonly/recvonly`).
            *   Extract custom attributes like `a=x-screamrouter-role`, `a=x-screamrouter-uuid`.
            *   Extract ICE candidates and DTLS fingerprints for WebRTC.
        *   Helper functions to generate basic SDP answers if ScreamRouter needs to construct them.
*   **`src/configuration/configuration_manager.py`:**
    *   Instantiates and starts/stops `SipManager`.
    *   `handle_sip_registration(uuid, ip, port, role, capabilities_from_sdp)`: Creates/updates `SourceDescription`/`SinkDescription`.
    *   `handle_sip_device_offline(uuid)`: Marks device as offline/disabled.
    *   `get_sip_config_for_device(uuid)`: Provides `SipManager` with data if needed.
    *   **WebRTC Signaling Bridge:**
        *   Add method `forward_webrtc_sdp_offer_to_cpp(sink_id, sdp_offer)` which calls the pybind11 `audio_manager.handle_webrtc_signaling_message`.
        *   Add method `forward_webrtc_ice_candidate_to_cpp(sink_id, candidate_str)`.
        *   Implement the Python callback function that `AudioManager` (C++) will call to send SDP answers/ICE candidates from C++ up to `SipManager`. `SipManager` then sends these to the WebRTC client.
*   **`src/utils/mdns_publisher.py` (New, replaces parts of old mDNS utilities - see `task_05`):**
    *   This new module will be responsible for publishing the `_screamrouter-sip._udp.local.` SRV record.
    *   `SipManager` will instruct this publisher with its listening port and the router's UUID.
*   **`screamrouter.py`:** Ensures `SipManager` (via `ConfigurationManager`) is started.

**Detailed Steps:**

1.  **Dependency & Build:**
    *   Add `pjproject` to `requirements.txt`.
    *   Ensure `pjproject` C libraries are available on the system or handle their build/linking if the Python package is a wrapper around source.
2.  **`SipManager` Implementation:**
    *   Initialize PJSIP `Endpoint`, add UDP transport.
    *   Create a default `Account` or a custom subclass to handle incoming registrations and calls.
    *   Implement `on_reg_state()`:
        *   Parse `REGISTER` details (UUID, contact, SDP).
        *   Use `sdp_utils.py` to parse capabilities.
        *   Call `ConfigurationManager.handle_sip_registration()`.
    *   Implement `on_incoming_call()`:
        *   Parse SDP offer.
        *   For WebRTC: Call `ConfigurationManager.forward_webrtc_sdp_offer_to_cpp()`.
        *   When C++ provides an SDP answer (via the callback mechanism), use `call.answer()` with the SDP.
    *   Implement keep-alive logic using `OPTIONS` or re-REGISTERs.
    *   Start PJSIP event processing.
3.  **`sdp_utils.py` Implementation:** Create functions for parsing common SDP attributes relevant to ScreamRouter.
4.  **`ConfigurationManager` Integration:**
    *   Add `SipManager` lifecycle management.
    *   Implement `handle_sip_registration`, `handle_sip_device_offline`.
    *   Implement the Python-side of the WebRTC signaling bridge (methods to call C++ and the callback for C++ to call Python).
5.  **Zeroconf Integration:** `SipManager` provides its port to the new `mdns_publisher.py`.
6.  **Testing:**
    *   Use SIP clients to test `REGISTER` with and without SDP. Verify `config.yaml` updates.
    *   Test keep-alives and device timeouts.
    *   Test basic `INVITE` flow for WebRTC signaling (SDP offer/answer relay).
    *   Verify Zeroconf publication of the SIP service.

**Considerations:**

*   **PJSIP Threading:** `pjsua2` often runs its own worker threads. Ensure proper integration with Python's main thread or any asyncio event loop if used elsewhere. Callbacks from PJSIP threads into Python code that might interact with non-thread-safe Python objects need care (e.g., using thread-safe queues or scheduling calls on the main thread).
*   **Error Handling:** Robust error handling for PJSIP calls.
*   **SDP Complexity:** SDP can be complex. Start with parsing essential attributes and expand as needed.
*   **Security:** Initial implementation can focus on functionality. TLS for SIP and Digest Auth are future enhancements.

**Acceptance Criteria:**

*   `SipManager` initializes and listens for SIP requests.
*   Devices can `REGISTER` successfully; `ConfigurationManager` reflects these registrations.
*   Keep-alives maintain device online status; timeouts mark devices offline.
*   Basic SDP capabilities (role, UUID, primary audio formats) are parsed from `REGISTER`.
*   WebRTC SDP offers can be received and relayed to the C++ layer, and answers relayed back.
*   SIP service is advertised via the new mDNS publisher.
