# Task: RTP Library (oRTP) Integration

**Objective:** Integrate the oRTP library into the C++ audio engine to handle RTP packetization for audio output and depacketization for audio input. This task focuses on the transport layer and RTP session management. Audio format information will be derived from SDP (handled in a later SIP integration task) or default to Scream-compatible formats if SDP is not yet available. Audio format *conversion* is handled by the existing `AudioProcessor` and is out of scope for this specific task.

**Parent Plan Section:** I. Core Technologies & Libraries Selection/Integration

**Files to Modify/Create:**

*   **`setup.py`:**
    *   Modify to include oRTP as a C/C++ library dependency for the Python extension.
    *   This will involve specifying oRTP's include directories and library files (or how to build oRTP if it's included as a source submodule) for the C++ compiler and linker used by `setuptools` / `distutils`.
*   **`src/audio_engine/rtp_receiver.h` / `src/audio_engine/rtp_receiver.cpp`:**
    *   This class inherits from `NetworkAudioReceiver`. It will be significantly modified to use oRTP for managing incoming RTP streams.
    *   **Key Changes:**
        *   The `run()` loop in `RtpReceiver` (or its base `NetworkAudioReceiver` if generalized for oRTP) will now interact with oRTP's event/callback system or poll oRTP sessions, replacing direct socket operations for RTP.
        *   In its constructor or a new initialization method, create and configure an oRTP `RtpSession` (`ortp_session_t*`) for receiving. Use `rtp_session_new(RTP_SESSION_RECVONLY)`.
        *   Configure the local receiving port using `rtp_session_set_local_addr(session, "0.0.0.0", local_port, local_rtcp_port)`.
        *   Set payload types if known statically, or prepare to set them dynamically based on SDP: `rtp_session_set_payload_type_number(session, payload_type_number)`.
        *   The `process_and_validate_payload` method will be adapted:
            *   It will be called from the `run()` loop after a packet is received via `rtp_session_recv_with_ts()`.
            *   The received `mblk_t *rtp_packet` will be parsed using oRTP utility functions: `rtp_get_payload_type(rtp_packet)`, `rtp_get_timestamp(rtp_packet)`, `rtp_get_sqnum(rtp_packet)`, `rtp_get_ssrc(rtp_packet)`. The payload itself is accessed via `rtp_packet->b_rptr`.
            *   **Audio Format for `TaggedAudioPacket`:** Initially, if SDP information (from a future SIP task) is not available, the format (channels, sample rate, bit depth, layout) might default to a standard (e.g., 16-bit, 48kHz, stereo, 0x03/0x00 layout, with a specific RTP payload type like 127 for this "default Scream RTP"). Once SIP/SDP is integrated, this information will be dynamically determined per stream based on SSRC and negotiated SDP.
            *   Populate `TaggedAudioPacket` and push to `TimeshiftManager`.
        *   `is_valid_packet_structure` might check `mblk_t` validity or basic RTP header fields accessible via oRTP.
*   **`src/audio_engine/sink_audio_mixer.h` / `src/audio_engine/sink_audio_mixer.cpp` (or a new `RTPSender` class - `RTPSender` is preferred for modularity, as outlined in `task_08_cpp_modular_sender.md`):**
    *   **Objective:** Implement RTP sending capabilities using oRTP.
    *   **If new `RTPSender` class (see `task_08`):**
        *   It will manage an `ortp_session_t*` for sending.
        *   `initialize(const SinkConfig& config)`: Creates session with `rtp_session_new(RTP_SESSION_SENDONLY)`, sets remote address with `rtp_session_set_remote_addr_and_port()`, sets SSRC, default payload type.
        *   `send_audio_packet(const uint8_t* payload, size_t payload_len, const AudioFormatDetails& format)`:
            *   Uses `rtp_session_create_packet(session, RTP_FIXED_HEADER_SIZE + payload_len)`.
            *   Sets marker bit (`format.rtp_marker_bit`), payload type (`format.rtp_payload_type`), and timestamp (`format.rtp_timestamp_offset` relative to an initial timestamp) using `rtp_set_marker()`, `rtp_set_payload_type()`, `rtp_set_timestamp()`.
            *   Copies `payload` into the `mblk_t`'s data region.
            *   Sends using `rtp_session_sendm_with_ts(session, packet, format.rtp_timestamp_offset)`.
    *   **If modifying `SinkAudioMixer` directly (less ideal):** Similar logic would be embedded, conditional on `protocol_type`.
*   **`src/audio_engine/audio_manager.h` / `src/audio_engine/audio_manager.cpp`:**
    *   If `RTPSender` is a new class, `AudioManager` will manage its lifecycle for sinks configured with `protocol_type: "rtp"`.
    *   `AudioManager::initialize()`: Call `ortp_init()` and `ortp_scheduler_init()`. `AudioManager::shutdown()` calls `ortp_exit()`.
    *   The existing `RtpReceiver` instance will be internally refactored to use oRTP.
*   **`src/audio_engine/audio_types.h` / `src/configuration/audio_engine_config_types.h`:**
    *   Ensure `SinkConfig` contains `protocol_type` (enum: `LEGACY_SCREAM`, `RTP`, `WEBRTC`).
    *   Add fields to `SinkConfig` for RTP parameters if not solely from SDP (e.g., `rtp_destination_ip`, `rtp_destination_port`, `default_rtp_payload_type_pcm`, `default_rtp_payload_type_mp3`).
    *   The `AudioFormatDetails` struct (defined in `task_08`) will be crucial for passing format and RTP-specific info to the `RTPSender`.
*   **`src/configuration/configuration_manager.py` (and `AudioEngineConfigApplier`):**
    *   Update Python's `SinkDescription` and `SourceDescription` to include `protocol_type` and nested `RTPConfig`.
    *   `AudioEngineConfigApplier` must pass `protocol_type` and RTP parameters to C++ `AudioManager`.

**Detailed Steps:**

1.  **Build System (`setup.py`):** Configure `setup.py` to compile C++ code against oRTP headers and link with the oRTP library. This may involve adding include paths, library paths, and library names to the `Extension` setup.
2.  **oRTP Global Init/Deinit:** Call `ortp_init()` in `AudioManager::initialize()` and `ortp_exit()` in `AudioManager::shutdown()`. Initialize the oRTP scheduler with `ortp_scheduler_init()`.
3.  **RTP Input Refactor (`RtpReceiver`):**
    *   Modify `RtpReceiver` to use an oRTP `RtpSession` for receiving.
    *   Adapt `run()` to use `rtp_session_recv_with_ts()` or oRTP's event mechanisms.
    *   Adapt `process_and_validate_payload` to parse the `mblk_t` from oRTP.
    *   Handle audio format determination for `TaggedAudioPacket` (defaulting initially, later via SDP based on SSRC).
4.  **RTP Output Implementation (Preferably new `RTPSender` class):**
    *   Create `RTPSender` class managing an oRTP `RtpSession` for sending.
    *   Implement methods for packet construction and sending using oRTP APIs.
5.  **Configuration Updates (C++ & Python):**
    *   Add `protocol_type` fields and `RTPConfig` structures.
    *   Update `AudioEngineConfigApplier` and `AudioManager` to use `protocol_type` for selecting and configuring RTP components.
6.  **Testing:**
    *   Unit tests for `RTPSender` packetization.
    *   Integration tests using RTP tools (VLC, GStreamer, `ortp-send`, `ortp-recv`) for end-to-end streaming.
    *   Verify correct SSRC, timestamps, sequence numbers, and payload types. Test with PCM data initially.

**Acceptance Criteria:**

*   ScreamRouter can receive and send basic RTP streams (PCM initially) using oRTP.
*   `setup.py` correctly builds the C++ extension with oRTP linked.
*   Configuration system supports distinguishing and configuring RTP sinks/sources.
*   oRTP sessions are managed correctly (creation, destruction, error handling).
