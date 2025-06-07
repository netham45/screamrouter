# Sub-Task 2.7: Initial Testing and Debugging for WebRTC MP3 Streaming

**Objective:** Perform initial end-to-end testing of MP3 audio streaming over WebRTC data channels. This involves using a web client for signaling and data reception, and debugging the C++/Python signaling bridge and `libdatachannel` integration.

**Parent Task:** [WebRTC Library (libdatachannel) Integration](../task_02_webrtc_library_integration.md)
**Previous Sub-Task:** [Sub-Task 2.6: Update Configuration for WebRTC Parameters (Python & C++)](./subtask_2.6_update_configuration_for_webrtc.md)

## Key Testing Scenarios:

1.  **WebRTC Output Testing (ScreamRouter as Sender, Web Client as Receiver):**
    *   **Setup (ScreamRouter):**
        *   Configure an audio source in ScreamRouter (e.g., legacy Scream input, RTP input, or a test tone).
        *   In `config.yaml` (or via UI if WebRTC config is added), create a new Sink with `protocol_type: "webrtc"`.
        *   Ensure `webrtc_config` has STUN servers configured (e.g., `stun:stun.l.google.com:19302`). Add TURN server config if testing NAT traversal beyond simple STUN.
        *   The sink's `codec` should implicitly be MP3 for WebRTC as per design.
        *   Route the audio source to this new WebRTC sink.
    *   **Setup (Web Client):**
        *   Use a simple HTML/JavaScript WebRTC test page. This page will:
            *   Connect to the ScreamRouter Python backend (e.g., via WebSocket for signaling, or use a simple HTTP endpoint on the FastAPI app that will then interact with the SIP server part).
            *   Initiate a WebRTC connection by creating an `RTCPeerConnection`.
            *   Create an SDP offer.
            *   Send this offer to ScreamRouter's signaling endpoint (e.g., Python FastAPI/SIP).
            *   Be prepared to receive an SDP answer and ICE candidates from ScreamRouter.
            *   Handle `ontrack` (for MediaStream based WebRTC) or `ondatachannel` events. For this project, it's `ondatachannel` as MP3s are sent over data channels.
            *   On the data channel's `onmessage` event, receive MP3 frames.
            *   Use a JavaScript MP3 decoder (e.g., `js-mp3`, part of `mpg123.js`, or similar) to decode and play the audio.
    *   **Execution:**
        1.  Start ScreamRouter.
        2.  Open the WebRTC test page in a browser.
        3.  The web client sends an SDP offer to ScreamRouter (Python).
        4.  Python SIP/Signaling layer forwards this offer to `AudioManager::handle_incoming_webrtc_signaling_message`.
        5.  `WebRTCSender` processes the offer, generates an SDP answer, and sends it back up via `signaling_callback_`.
        6.  Python SIP/Signaling layer sends the SDP answer to the web client.
        7.  ICE candidates are exchanged similarly.
        8.  WebRTC connection establishes, data channel opens.
        9.  `SinkAudioMixer` encodes audio to MP3 and passes frames to `WebRTCSender::send_mp3_frame`.
        10. Web client receives MP3 frames on the data channel and plays audio.
    *   **Verification:**
        *   Audio from the ScreamRouter source is played in the web browser.
        *   Check ScreamRouter C++ logs for `WebRTCSender` activity (signaling, data channel events, MP3 sending).
        *   Check Python logs for SIP/signaling message relay.
        *   Use browser developer tools (Network tab for signaling, `chrome://webrtc-internals` or `about:webrtc` in Firefox) to inspect WebRTC connection status, ICE negotiation, and data channel activity.

## Debugging Steps:

*   **Signaling First:**
    *   Ensure SDP offers/answers and ICE candidates are correctly exchanged. Log messages at each step:
        *   Web client sending offer.
        *   Python (FastAPI/SIP) receiving offer and passing to C++.
        *   `AudioManager` receiving offer and passing to `WebRTCSender`.
        *   `WebRTCSender` generating answer/candidates.
        *   `WebRTCSender` sending answer/candidates via callback.
        *   `AudioManager` receiving C++ callback and invoking Python callback.
        *   Python sending answer/candidates to web client.
        *   Web client receiving and processing answer/candidates.
    *   Use `chrome://webrtc-internals` or `about:webrtc` to verify SDP and ICE states.
*   **`libdatachannel` Logging:**
    *   `libdatachannel` uses `plog`. Configure `plog` for verbose output if needed.
    *   Check `WebRTCSender` callbacks for `onStateChange`, `onGatheringStateChange`, data channel `onOpen`, `onClosed`, `onError`.
*   **Data Channel Issues:**
    *   Verify `data_channel_->onOpen()` is triggered in `WebRTCSender`.
    *   Verify `WebRTCSender::send_mp3_frame()` is called and `data_channel_->send()` is successful.
    *   In the browser, check if the data channel's `onmessage` event fires and if data is received.
*   **MP3 Decoding in Browser:**
    *   Ensure the JavaScript MP3 decoder is correctly initialized and fed with the received ArrayBuffer/Blob data.
    *   Log any errors from the JS MP3 decoder.
*   **Configuration:**
    *   Verify STUN/TURN server configuration in `config.yaml` and that it's correctly passed to `WebRTCSender` and used in `rtc::Configuration`.
    *   Firewall issues: Ensure UDP ports for ICE candidates are not blocked.

## Code Alterations:

*   Primarily involves adding detailed logging to:
    *   `src/audio_engine/webrtc_sender.cpp` (signaling processing, data channel events, sending MP3s).
    *   `src/audio_engine/audio_manager.cpp` (WebRTC signaling bridge points).
    *   Python signaling layer (`SipManager`, FastAPI endpoints).
*   Development of a basic HTML/JavaScript WebRTC test client.

## Recommendations:

*   **Simple Test Client:** Keep the initial web client very simple, focusing on establishing the connection and receiving data.
*   **`webrtc-internals`:** This browser tool is invaluable for debugging WebRTC connection issues.
*   **Isolate Signaling vs. Media:** First, ensure the signaling handshake completes and the data channel opens. Then, debug MP3 data transmission.
*   **NAT Traversal:** Test first on a local network where STUN might be sufficient. Then test with more complex NAT scenarios requiring TURN if available.

## Acceptance Criteria:

*   A WebRTC peer connection can be established between ScreamRouter (`WebRTCSender`) and a web browser client.
*   Signaling (SDP offer/answer, ICE candidates) is correctly relayed and processed.
*   The WebRTC data channel opens successfully.
*   ScreamRouter can send MP3 frames over the data channel.
*   The web browser client receives these MP3 frames and can play back audio.
*   Logs provide sufficient detail to debug the signaling and data flow.
