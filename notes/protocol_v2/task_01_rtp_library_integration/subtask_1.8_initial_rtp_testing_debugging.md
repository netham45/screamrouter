# Sub-Task 1.8: Initial Testing and Debugging for Basic RTP I/O

**Objective:** Perform initial end-to-end testing of the basic RTP input (via `RtpReceiver`) and output (via `RTPSender`) functionality using external RTP tools. Debug any issues related to oRTP session management, packet processing, and configuration.

**Parent Task:** [RTP Library (oRTP) Integration](../task_01_rtp_library_integration.md)
**Previous Sub-Task:** [Sub-Task 1.7: Update Configuration for RTP Parameters (Python & C++)](./subtask_1.7_update_configuration_for_rtp.md)

## Key Testing Scenarios:

1.  **RTP Output Testing (ScreamRouter as Sender):**
    *   **Setup:**
        *   Configure a legacy Scream source (e.g., using a test tone generator or existing input).
        *   In `config.yaml` (or via UI if basic RTP config is added), create a new Sink with `protocol_type: "rtp"`.
        *   Set `ip` to `127.0.0.1` (or the IP of the machine running the RTP receiver tool).
        *   Set `rtp_config.destination_port` to a known port (e.g., 1234).
        *   Set `rtp_config.payload_type_pcm` to a dynamic value (e.g., 96 or 127).
        *   Route the legacy Scream source to this new RTP sink.
    *   **Execution:**
        *   Start ScreamRouter.
        *   Use an external RTP receiving tool (e.g., VLC, GStreamer, `ortp-recv` command-line tool) configured to listen on `127.0.0.1:1234` for PCM audio with the specified payload type.
            *   **VLC:** Open Network Stream -> `rtp://@:1234`. May need to provide an SDP file if VLC requires it for dynamic payload types, or configure payload type mapping if possible.
            *   **GStreamer:** `gst-launch-1.0 udpsrc port=1234 ! "application/x-rtp,media=(string)audio,clock-rate=(int)48000,encoding-name=(string)L16,payload=(int)96,channels=(int)2" ! rtpL16depay ! audioconvert ! audioresample ! autoaudiosink` (Adjust clock-rate, channels, payload type as per default PCM format).
            *   **`ortp-recv`:** `ortprecv 1234 96` (Listens on port 1234 for payload type 96). Output might be raw, needs further processing to verify.
    *   **Verification:**
        *   Audio from the Scream source is played out by the RTP receiving tool.
        *   Check ScreamRouter logs for `RTPSender` activity, errors, or warnings.
        *   Use Wireshark to inspect outgoing RTP packets: verify SSRC, payload type, timestamps, sequence numbers, and payload content.

2.  **RTP Input Testing (ScreamRouter as Receiver):**
    *   **Setup:**
        *   In `config.yaml`, create a new Source with `protocol_type: "rtp"`.
        *   Set `rtp_config.source_listening_port` to a known port (e.g., 5678).
        *   Set `rtp_config.payload_type_pcm` to match what the sending tool will use (e.g., 96).
        *   Configure a legacy Scream sink (or an RTP sink from test 1 if it worked) to output the audio.
        *   Route this new RTP source to the chosen sink.
    *   **Execution:**
        *   Start ScreamRouter.
        *   Use an external RTP sending tool (e.g., VLC, GStreamer, `ortp-send`) to send PCM audio to ScreamRouter's listening IP and port (`<ScreamRouter_IP>:5678`).
            *   **VLC:** Stream a file using RTP, configure destination IP/port and payload type.
            *   **GStreamer:** `gst-launch-1.0 audiotestsrc wave=sine ! audioconvert ! audioresample ! "audio/x-raw,format=S16BE,rate=48000,channels=2" ! rtpL16pay pt=96 ! udpsink host=<ScreamRouter_IP> port=5678`
            *   **`ortp-send`:** `ortpsend <ScreamRouter_IP> 5678 96 48000` (Sends silence or requires input).
    *   **Verification:**
        *   Audio sent by the RTP tool is played out by the configured ScreamRouter sink.
        *   Check ScreamRouter logs for `RtpReceiver` activity, packet processing details, errors, or warnings.
        *   Use Wireshark to inspect incoming RTP packets to ensure they are what `RtpReceiver` expects.

3.  **MP3 RTP Testing (if `RTPSender` and `RtpReceiver` support it initially):**
    *   Similar to PCM tests, but configure tools and ScreamRouter for MP3 payload types (e.g., 14).
    *   Requires an MP3 source for sending from ScreamRouter, or an MP3 RTP stream for receiving.
    *   This might be deferred if initial focus is PCM only.

## Debugging Steps:

*   **Logging:**
    *   Add extensive `screamrouter_logger::debug` or `info` messages in `RtpReceiver` (packet reception, header parsing, format determination, pushing to timeshift) and `RTPSender` (packet creation, header setting, sending).
    *   Log oRTP session creation parameters and success/failure.
*   **Wireshark:**
    *   Capture traffic on the relevant ports.
    *   Filter by `rtp` or `rtcp`.
    *   Analyze RTP headers: SSRC, payload type, sequence numbers, timestamps, marker bit.
    *   Analyze payload content: Does it look like valid PCM or MP3 data?
*   **oRTP Debugging:**
    *   oRTP has internal logging. It might be possible to increase its verbosity: `ortp_set_log_level_mask(ORTP_DEBUG|ORTP_MESSAGE|ORTP_WARNING|ORTP_ERROR);`
*   **Configuration Verification:**
    *   Double-check `config.yaml` and ensure parameters (ports, IPs, payload types) match between ScreamRouter and the test tools.
    *   Verify that `ConfigurationManager` and `AudioEngineConfigApplier` are correctly passing these settings to `AudioManager` and subsequently to `RtpReceiver`/`RTPSender`.
*   **Small Steps:**
    *   If output fails, first ensure `RTPSender::initialize` is called and `rtp_session_` is created. Then check if `send_audio_packet` is being called.
    *   If input fails, ensure `RtpReceiver::initialize_receiver` and `create_ortp_session` succeed. Then check if `run()` loop is active and if `process_received_packet` is hit.

## Code Alterations:

*   Primarily involves adding detailed logging to:
    *   `src/audio_engine/rtp_receiver.cpp`
    *   `src/audio_engine/rtp_sender.cpp`
    *   `src/audio_engine/audio_manager.cpp` (related to RTP source/sink setup).
*   Potentially minor adjustments to `AudioFormatDetails` or config structs based on testing needs (e.g., ensuring default payload types are sensible).

## Recommendations:

*   **Start with PCM:** PCM is simpler to debug than compressed formats like MP3.
*   **Loopback Testing:** Initially test with RTP sender and receiver tools on the same machine as ScreamRouter (`127.0.0.1`) to eliminate network issues.
*   **Known Good Tools:** Use well-known RTP tools like VLC or GStreamer, as they are generally compliant with RTP standards.
*   **Incremental Complexity:** Start with basic, unencrypted RTP. SRTP and more complex SDP negotiations will be tested later.

## Acceptance Criteria:

*   ScreamRouter can send a basic PCM RTP stream that can be received and played by an external tool.
*   ScreamRouter can receive a basic PCM RTP stream from an external tool, process it, and route it to a sink.
*   Logs provide sufficient information to trace the flow of RTP setup and packet handling.
*   Obvious issues like crashes, memory leaks (monitor system resources), or incorrect RTP header information are identified and fixed.
