# Task: Protocol v2 Testing and Refinement Strategy

**Objective:** Define a strategy for testing the integrated Protocol v2 features (RTP, WebRTC, SIP, Zeroconf) and outline areas for refinement based on initial implementation and testing.

**Parent Plan Section:** This is a cross-cutting concern, but aligns with finalization.

**Key Areas for Testing:**

1.  **Zeroconf Discovery:**
    *   **Test:** Verify SIP server discovery using standard mDNS/DNS-SD browsing tools from various client platforms (Linux, Windows, macOS).
    *   **Tools:** `avahi-browse` (Linux), "Discovery" app (macOS), other mDNS diagnostic tools.
    *   **Criteria:** Service `_screamrouter-sip._udp.local.` is correctly advertised with accurate IP, port, and TXT records (version, router_uuid). Service is removed on shutdown.

2.  **SIP Registration and Presence:**
    *   **Test:** Use SIP client software (e.g., Linphone, MicroSIP, `pjsua` CLI) to `REGISTER` with ScreamRouter.
    *   **Scenarios:**
        *   New device registration (verify new `SourceDescription`/`SinkDescription` created in `config.yaml`).
        *   Re-registration of an existing device (verify existing entry updated, marked online).
        *   Registration with SDP specifying capabilities (verify these are parsed and stored).
        *   Keep-alive mechanism (test with periodic `OPTIONS` or re-REGISTERs).
        *   Device timeout (verify device marked offline in `ConfigurationManager` if keep-alives stop).
    *   **Criteria:** `ConfigurationManager` state and `config.yaml` reflect SIP registrations and presence changes accurately.

3.  **RTP Streaming (PCM & MP3):**
    *   **Test (Input to ScreamRouter):**
        *   Use an RTP sending tool (e.g., VLC, GStreamer, `ortp-send`) to send PCM audio (L16/48000/2 initially) to `RtpReceiver`.
        *   Verify `AudioManager` detects the new source (via `TimeshiftManager` and `NotificationQueue`).
        *   Verify `SourceInputProcessor` is created and processes the audio.
        *   Route this RTP source to a legacy Scream sink or an MP3 web stream and verify audio output.
        *   Repeat with MP3 RTP stream.
    *   **Test (Output from ScreamRouter):**
        *   Configure a sink in ScreamRouter with `protocol_type: "rtp"`.
        *   Route an existing source (e.g., legacy Scream input) to this RTP sink.
        *   Use an RTP receiving tool (VLC, GStreamer, `ortp-recv`) to receive and play the audio.
        *   Verify correct audio format, timestamps, and sequence numbers.
        *   Test with both PCM and MP3 output.
    *   **Criteria:** Audio flows correctly in both directions. RTP headers are valid. Audio quality is maintained.

4.  **WebRTC Streaming (MP3 over Data Channels):**
    *   **Test:**
        *   Develop a simple test web client using HTML/JavaScript and `RTCPeerConnection` API (or `libdatachannel` examples).
        *   Web client initiates connection to ScreamRouter via SIP (for signaling SDP/ICE).
        *   Configure a sink in ScreamRouter with `protocol_type: "webrtc"`.
        *   Route an audio source to this WebRTC sink.
        *   Verify WebRTC connection establishment (ICE, DTLS, SCTP Data Channel).
        *   Verify MP3 frames are received by the web client and can be decoded/played (e.g., using `js-mp3` or similar).
    *   **Criteria:** WebRTC connection succeeds. MP3 audio is streamed and playable in the browser.

5.  **Configuration and Control:**
    *   **Test:** Use the ScreamRouter web UI to:
        *   Manually add sources/sinks with new `protocol_type` settings (RTP, WebRTC).
        *   Configure RTP-specific parameters (port, payload types, presets).
        *   View SIP-registered devices and their status.
    *   **Criteria:** UI changes are functional and correctly update the backend configuration.

6.  **Backward Compatibility:**
    *   **Test:** Ensure existing legacy Scream clients (senders and receivers) continue to function correctly with ScreamRouter when configured with `protocol_type: "scream"`.
    *   **Criteria:** No regressions in legacy protocol functionality.

7.  **Stress and Stability Testing:**
    *   **Test:** Run multiple clients (SIP, RTP, WebRTC, legacy) concurrently for extended periods.
    *   Monitor for memory leaks, crashes, performance degradation, and audio glitches.
    *   **Criteria:** System remains stable and performant under load.

**Areas for Refinement (Post-Initial Implementation):**

*   **SDP Offer/Answer Model:** Refine the SDP parsing and generation logic in `SipManager` and its interaction with `ConfigurationManager` and the C++ `AudioManager` for robust capability negotiation.
*   **RTP Payload Format for Scream Data:** If the 5-byte Scream header is included in RTP payloads, ensure this custom format is well-documented and handled by clients. Otherwise, ensure all necessary format information is conveyed via SDP.
*   **Error Handling and Logging:** Enhance error reporting and logging across all new components (SIP, RTP, WebRTC) for easier debugging.
*   **Security Enhancements:**
    *   Implement TLS for SIP signaling.
    *   Implement SRTP for RTP media.
    *   Add Digest authentication for SIP `REGISTER`.
*   **Advanced RTP Features:** Consider support for RTCP for quality feedback, and more sophisticated jitter buffering if needed.
*   **WebRTC Signaling Robustness:** Improve the robustness of the WebRTC signaling bridge between Python and C++.
*   **Performance Optimization:** Profile CPU and memory usage, especially in the C++ audio engine with new RTP/WebRTC processing, and optimize as needed.
*   **User Experience (UI/UX):** Gather feedback on the new UI elements and configuration options and iterate on their design for clarity and ease of use.

**Documentation:**

*   Continuously update the `protocol_spec_v2.md` as implementation details are finalized.
*   Document new configuration options for users.
*   Provide examples for setting up Protocol v2 clients.

This testing and refinement strategy should be an iterative process throughout the development of Protocol v2.
