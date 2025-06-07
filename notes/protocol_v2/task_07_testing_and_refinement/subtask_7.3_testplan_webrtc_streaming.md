# Sub-Task 7.3: Test Plan for WebRTC Streaming (MP3 over Data Channels)

**Objective:** Define detailed test cases for verifying WebRTC audio streaming, focusing on MP3 delivery over data channels, including the signaling handshake (SDP/ICE via SIP) and data transmission.

**Parent Task:** [Protocol v2 Testing and Refinement Strategy](../task_07_testing_and_refinement.md)
**Previous Sub-Task:** [Sub-Task 7.2: Test Plan for RTP Streaming (PCM & MP3)](./subtask_7.2_testplan_rtp_streaming.md)

## Tools:

*   ScreamRouter instance running (with SIP server and WebRTC components active).
*   Web browser (Chrome, Firefox) with developer tools.
*   A simple HTML/JavaScript WebRTC test client page (as developed/used in Sub-Task 2.7).
    *   This client should be able to:
        *   Initiate a WebRTC connection.
        *   Generate an SDP offer.
        *   Send/receive signaling messages (SDP, ICE candidates) to/from ScreamRouter's Python signaling layer (e.g., via WebSocket or HTTP to the FastAPI app that then interacts with `SipManager`).
        *   Establish a data channel.
        *   Receive MP3 frames on the data channel.
        *   Decode and play MP3 audio using a JavaScript library (e.g., `js-mp3`).
*   Wireshark for inspecting network traffic (SIP, STUN/TURN, DTLS, SCTP).
*   Access to ScreamRouter C++ and Python logs.

## Test Cases:

1.  **TC-WEBRTC-SIG-001: Successful Signaling Handshake (Offer/Answer)**
    *   **Preconditions:** ScreamRouter running. A Sink configured with `protocol_type: "webrtc"`. An audio source routed to this sink.
    *   **Steps:**
        1.  Open the WebRTC test client in a browser.
        2.  Initiate connection from the web client. Client generates SDP offer.
        3.  Client sends SDP offer to ScreamRouter's signaling endpoint.
        4.  ScreamRouter (`SipManager` -> `AudioManager` -> `WebRTCSender`) processes the offer and generates an SDP answer.
        5.  ScreamRouter sends SDP answer back to the web client via the signaling channel.
    *   **Verification:**
        *   Web client successfully receives and processes the SDP answer.
        *   ScreamRouter logs show correct reception of offer and generation/sending of answer.
        *   Browser's WebRTC internals (`chrome://webrtc-internals` or `about:webrtc`) show `setRemoteDescription` (offer) and `setLocalDescription` (answer) succeeding on the client, and corresponding states on the server side (logged by `WebRTCSender`).
    *   **Pass/Fail.**

2.  **TC-WEBRTC-SIG-002: Successful ICE Candidate Exchange**
    *   **Preconditions:** TC-WEBRTC-SIG-001 partially complete (offer/answer exchanged or in progress).
    *   **Steps:**
        1.  Both client and ScreamRouter (`WebRTCSender`) gather ICE candidates.
        2.  Candidates are exchanged via the signaling channel.
    *   **Verification:**
        *   Web client and ScreamRouter logs show local candidates being generated and sent.
        *   Web client and ScreamRouter logs show remote candidates being received and added (`addIceCandidate`).
        *   WebRTC internals show ICE gathering state progressing and candidate pairs being formed.
        *   Connection state eventually transitions to "connected".
    *   **Pass/Fail.**

3.  **TC-WEBRTC-DC-001: Data Channel Establishment**
    *   **Preconditions:** TC-WEBRTC-SIG-002 passed (ICE connection successful).
    *   **Steps:**
        1.  `WebRTCSender` creates a data channel (e.g., named "mp3").
        2.  Web client handles the `ondatachannel` event from `RTCPeerConnection` and gets a reference to the data channel.
    *   **Verification:**
        *   `WebRTCSender` logs successful data channel creation.
        *   Web client's `ondatachannel` event fires.
        *   Both `WebRTCSender`'s data channel `onOpen` callback and the web client's data channel `onopen` event fire.
        *   `WebRTCSender.data_channel_open_` becomes true.
    *   **Pass/Fail.**

4.  **TC-WEBRTC-MP3-001: MP3 Frame Transmission and Playback**
    *   **Preconditions:** TC-WEBRTC-DC-001 passed (data channel open). Audio is being routed to the WebRTC sink in ScreamRouter.
    *   **Steps:**
        1.  ScreamRouter's `SinkAudioMixer` encodes audio to MP3.
        2.  `SinkAudioMixer` calls `WebRTCSender::send_mp3_frame()`.
        3.  `WebRTCSender` sends MP3 data over the data channel.
        4.  Web client's data channel `onmessage` event receives MP3 data.
        5.  Web client feeds MP3 data to a JavaScript MP3 decoder and audio output.
    *   **Verification:**
        *   Clear audio playback in the web browser.
        *   ScreamRouter logs show MP3 frames being sent by `WebRTCSender`.
        *   Web client console logs show MP3 data being received and passed to the decoder.
        *   No significant errors from the JS MP3 decoder.
        *   Data channel statistics (if available in WebRTC internals) show bytes sent/received.
    *   **Pass/Fail.**

5.  **TC-WEBRTC-NAT-001: Connection with STUN**
    *   **Preconditions:** ScreamRouter and web client are on different private networks behind NATs that STUN can typically handle (e.g., Full Cone, Restricted Cone, Port Restricted Cone NATs). STUN servers configured in `SinkConfig.webrtc_config`.
    *   **Steps:** Execute TC-WEBRTC-SIG-001 through TC-WEBRTC-MP3-001.
    *   **Verification:** Connection establishes and audio plays. WebRTC internals show server reflexive candidates being used.
    *   **Pass/Fail.**

6.  **TC-WEBRTC-NAT-002: Connection with TURN (UDP/TCP - if configured)**
    *   **Preconditions:** ScreamRouter and/or web client are behind symmetric NATs or firewalls that block UDP, requiring TURN. TURN servers (with credentials if needed) configured.
    *   **Steps:** Execute TC-WEBRTC-SIG-001 through TC-WEBRTC-MP3-001.
    *   **Verification:** Connection establishes and audio plays. WebRTC internals show relay candidates being used.
    *   **Pass/Fail.**

7.  **TC-WEBRTC-ERR-001: Signaling Failure (e.g., Invalid SDP)**
    *   **Steps:** Manually send a malformed SDP offer from the client.
    *   **Verification:** ScreamRouter logs an error during SDP processing. The connection does not establish. Client may show an error.
    *   **Pass/Fail.**

8.  **TC-WEBRTC-ERR-002: Data Channel Closure**
    *   **Steps:** Close the browser tab/window or manually close the `RTCPeerConnection` on the client.
    *   **Verification:**
        *   `WebRTCSender`'s data channel `onClosed` callback fires.
        *   `WebRTCSender`'s peer connection state changes to disconnected/closed.
        *   ScreamRouter logs these events and cleans up resources for that sink connection.
    *   **Pass/Fail.**

## Debugging:
*   Browser DevTools (Console, Network, `chrome://webrtc-internals`, `about:webrtc`).
*   ScreamRouter C++ logs (especially `WebRTCSender`, `AudioManager`).
*   ScreamRouter Python logs (`SipManager`, signaling endpoints).
*   Wireshark for low-level network analysis.
*   `libdatachannel`'s internal logging (via `plog`).
