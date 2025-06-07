# Sub-Task 7.2: Test Plan for RTP Streaming (PCM & MP3)

**Objective:** Define detailed test cases for verifying RTP audio streaming functionality, covering both PCM and MP3 codecs, for input to and output from ScreamRouter.

**Parent Task:** [Protocol v2 Testing and Refinement Strategy](../task_07_testing_and_refinement.md)
**Previous Sub-Task:** [Sub-Task 7.1: Test Plan for Zeroconf Discovery and SIP Registration](./subtask_7.1_testplan_zeroconf_sip_reg.md)

## Tools:

*   ScreamRouter instance running.
*   RTP sending/receiving tools:
    *   VLC Media Player
    *   GStreamer (`gst-launch-1.0`, `gst-discoverer-1.0`)
    *   `ortp-send` / `ortp-recv` (from oRTP library, if available as command-line tools)
    *   Other SIP clients or RTP test tools capable of sending/receiving specific RTP streams.
*   Wireshark for packet inspection.
*   Audio test files (PCM WAV, MP3).
*   Access to ScreamRouter `config.yaml` and logs.

## I. RTP Output Testing (ScreamRouter as Sender)

**Common Setup:**
*   Configure an audio source in ScreamRouter (e.g., a test tone generator plugin, a file input plugin playing a WAV/MP3, or a legacy Scream input).
*   In `config.yaml` or via UI, create a Sink with `protocol_type: "rtp"`.
*   Set `ip` to the IP address of the machine running the RTP receiver tool (e.g., `127.0.0.1` for local testing).
*   Configure `rtp_config` for the sink:
    *   `destination_port` (e.g., 1234 for PCM, 1236 for MP3).
    *   `payload_type_pcm` (e.g., 96).
    *   `payload_type_mp3` (e.g., 14).
*   Route the chosen audio source to this RTP sink.
*   Start ScreamRouter.

**Test Cases (PCM Output):**

1.  **TC-RTP-OUT-PCM-001: Basic PCM Stream (L16/48kHz/Stereo)**
    *   **Source:** Test tone (440Hz sine wave) or simple WAV file (16-bit, 48kHz, stereo).
    *   **Sink `rtp_config`:** `payload_type_pcm: 96`.
    *   **Receiver Tool:** Configure to receive L16 audio, 48000Hz, 2 channels, payload type 96, on the specified IP/port.
    *   **Verification:**
        *   Clear audio received and played by the tool.
        *   ScreamRouter logs show `RTPSender` activity for PCM.
        *   Wireshark: RTP packets have PT=96, SSRC is consistent, timestamps increment correctly (e.g., by 1152 for 1152-sample chunks if that's the packetization interval, at 48kHz clock rate). Payload is valid L16 stereo data.
    *   **Pass/Fail.**

2.  **TC-RTP-OUT-PCM-002: Different PCM Parameters (e.g., L16/44.1kHz/Mono)**
    *   **Source:** WAV file (16-bit, 44.1kHz, mono).
    *   **Sink `rtp_config`:** `payload_type_pcm: 97` (or another dynamic PT).
    *   **Receiver Tool:** Configure for L16, 44100Hz, 1 channel, PT 97.
    *   **Verification:** As above, with parameters matching the source. `AudioProcessor` in ScreamRouter should handle any necessary resampling/rechanneling if the internal format differs.
    *   **Pass/Fail.**

**Test Cases (MP3 Output):**

1.  **TC-RTP-OUT-MP3-001: Basic MP3 Stream**
    *   **Source:** MP3 file input or a source transcoded to MP3 by ScreamRouter (LAME encoder).
    *   **Sink `rtp_config`:** `payload_type_mp3: 14` (or other dynamic PT for MP3). Sink codec must be set to MP3.
    *   **Receiver Tool:** Configure to receive MP3 RTP stream (PT 14) on the specified IP/port. (VLC often handles MP3 RTP well).
    *   **Verification:**
        *   Clear MP3 audio received and played.
        *   ScreamRouter logs show `RTPSender` activity for MP3.
        *   Wireshark: RTP packets have PT=14. Timestamps increment based on MP3 frame duration (e.g., 1152 samples per frame for MPEG-1 Layer III). Marker bit usage is as expected (e.g., per frame or talkspurt). Payload contains MP3 frames.
    *   **Pass/Fail.**

## II. RTP Input Testing (ScreamRouter as Receiver)

**Common Setup:**
*   In `config.yaml` or via UI, create a Source with `protocol_type: "rtp"`.
*   Configure `rtp_config` for the source:
    *   `source_listening_port` (e.g., 5678 for PCM, 5680 for MP3).
    *   `payload_type_pcm` (e.g., 96).
    *   `payload_type_mp3` (e.g., 14).
    *   `default_audio_format` in `RtpReceiver` should align with expected input if SDP isn't used yet.
*   Configure a Sink in ScreamRouter to output the received audio (e.g., a legacy Scream sink, or a working RTP/WebRTC sink).
*   Route the new RTP source to this output sink.
*   Start ScreamRouter.

**Test Cases (PCM Input):**

1.  **TC-RTP-IN-PCM-001: Basic PCM Stream (L16/48kHz/Stereo)**
    *   **Sender Tool:** Configure to send L16 audio, 48000Hz, 2 channels, payload type 96, to ScreamRouter's listening IP/port.
    *   **Verification:**
        *   Audio is played out by ScreamRouter's configured output sink.
        *   ScreamRouter logs show `RtpReceiver` activity, correct parsing of PT, TS, SSRC. `TaggedAudioPacket` pushed to `TimeshiftManager`.
        *   Wireshark: Verify incoming packets match sender's configuration.
    *   **Pass/Fail.**

2.  **TC-RTP-IN-PCM-002: Different PCM Parameters (e.g., L16/44.1kHz/Mono)**
    *   **Sender Tool:** Configure to send L16, 44.1kHz, 1 channel, PT 97.
    *   **ScreamRouter Source `rtp_config`:** `payload_type_pcm: 97`. `default_audio_format_` in `RtpReceiver` should be set to expect this or adapt.
    *   **Verification:** As above. `AudioProcessor` should handle format conversion if needed for the output sink.
    *   **Pass/Fail.**

3.  **TC-RTP-IN-PCM-003: Unknown Payload Type**
    *   **Sender Tool:** Send PCM with a payload type *not* configured in ScreamRouter's RTP source.
    *   **Verification:**
        *   ScreamRouter logs a warning about the unknown/unexpected payload type.
        *   The packet is either dropped or processed using a default format (depending on implementation). No audio or garbled audio might result at the output sink.
    *   **Pass/Fail.**

**Test Cases (MP3 Input - if supported by `RtpReceiver` initially):**

1.  **TC-RTP-IN-MP3-001: Basic MP3 Stream**
    *   **Sender Tool:** Configure to send an MP3 RTP stream (PT 14) to ScreamRouter's listening IP/port.
    *   **ScreamRouter Source `rtp_config`:** `payload_type_mp3: 14`.
    *   **Verification:**
        *   MP3 audio is received, (conceptually) decoded by an MP3 decoder within ScreamRouter's pipeline (if direct MP3 processing is planned, or converted to PCM), and played out.
        *   ScreamRouter logs show `RtpReceiver` processing MP3 RTP packets.
    *   **Pass/Fail.** (This depends on whether `RtpReceiver` and subsequent pipeline can handle raw MP3 input directly or expect PCM).

## III. General RTP Considerations

1.  **TC-RTP-GEN-001: SSRC Handling**
    *   **Output:** Verify a consistent SSRC is used for outgoing streams from a given `RTPSender`.
    *   **Input:** Verify `RtpReceiver` correctly identifies and logs the SSRC of incoming streams. (Later, SSRC will be key for mapping to SDP-negotiated formats).
    *   **Pass/Fail.**

2.  **TC-RTP-GEN-002: Timestamp and Sequence Number Integrity**
    *   **Output & Input:** Use Wireshark to monitor streams.
    *   **Verification:** Sequence numbers increment by 1 for each packet. Timestamps increment according to the clock rate and amount of audio data per packet. No large jumps or resets unless expected (e.g., SSRC change).
    *   **Pass/Fail.**

3.  **TC-RTP-GEN-003: RTCP (Basic Check - if oRTP sends/receives by default)**
    *   **Verification:** Use Wireshark to check if basic RTCP Sender Reports (SR) or Receiver Reports (RR) are being exchanged on the RTCP port (RTP port + 1). oRTP usually handles this.
    *   **Pass/Fail.** (Full RTCP QOS testing is out of initial scope).

## Debugging:
*   Extensive logging in `RtpReceiver`, `RTPSender`, `AudioManager`, `SinkAudioMixer`.
*   Wireshark is critical.
*   Use `ortp_set_log_level_mask()` for oRTP's internal logs.
