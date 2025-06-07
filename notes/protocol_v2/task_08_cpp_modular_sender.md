# Task: C++ Modular Network Sender Implementation

**Objective:** Refactor the C++ audio sending logic to support multiple output protocols (Legacy Scream, RTP, WebRTC) in a modular way. This will likely involve a base sender interface and protocol-specific implementations.

**Parent Plan Section:** II. Audio Output Enhancements (Protocol v2)

**Files to Modify/Create:**

*   **New C++ Files:**
    *   `src/audio_engine/network_sender.h`: Defines an abstract base class or interface `INetworkSender`.
        *   `virtual ~INetworkSender() {}`
        *   `virtual bool initialize(const SinkConfig& config) = 0;` (or protocol-specific config)
        *   `virtual void send_audio_packet(const uint8_t* payload, size_t payload_len, const AudioFormatDetails& format) = 0;` (AudioFormatDetails would contain sample rate, channels, bit depth, potentially RTP timestamp info if not handled internally)
        *   `virtual void send_mp3_frame(const uint8_t* mp3_data, size_t data_len) = 0;` (for MP3-specific senders like WebRTC/RTP MP3)
        *   `virtual void stop() = 0;`
    *   `src/audio_engine/scream_sender.h` / `src/audio_engine/scream_sender.cpp`: Implements `INetworkSender` for the legacy Scream protocol.
        *   Reuses much of the existing UDP sending logic from `SinkAudioMixer::send_network_buffer()`.
        *   Constructs the 5-byte Scream header.
    *   `src/audio_engine/rtp_sender.h` / `src/audio_engine/rtp_sender.cpp`: Implements `INetworkSender` for RTP (using oRTP).
        *   Manages an oRTP session.
        *   Handles RTP packetization for PCM and MP3.
        *   Sets RTP timestamps, SSRC, payload types (based on `AudioFormatDetails` and SDP info).
    *   `src/audio_engine/webrtc_sender.h` / `src/audio_engine/webrtc_sender.cpp`: Implements `INetworkSender` for WebRTC (using libdatachannel).
        *   Manages a `libdatachannel` PeerConnection and DataChannel.
        *   Handles signaling via `AudioManager`.
        *   Sends MP3 frames over the data channel.
*   **`src/audio_engine/sink_audio_mixer.h` / `src/audio_engine/sink_audio_mixer.cpp`:**
    *   Remove direct UDP/TCP sending logic.
    *   `SinkAudioMixer` will hold a `std::unique_ptr<INetworkSender>` (or a map of senders if a sink can output multiple protocols, though current design implies one protocol per sink).
    *   When `SinkAudioMixer` is configured (based on `SinkConfig::protocol_type` passed from `AudioManager`):
        *   It instantiates the appropriate sender implementation (e.g., `ScreamSender`, `RTPSender`, `WebRTCSender`).
    *   The `run()` loop, after `downscale_buffer()` (for PCM) or `encode_and_push_mp3()` (for MP3 to WebRTC/RTP MP3), will call methods on its `INetworkSender` instance:
        *   `sender_->send_audio_packet(...)` for PCM data.
        *   `sender_->send_mp3_frame(...)` for MP3 data.
*   **`src/audio_engine/audio_manager.h` / `src/audio_engine/audio_manager.cpp`:**
    *   When `AudioManager::add_sink(const SinkConfig& config)` is called:
        *   It will still create `SinkAudioMixer`.
        *   The `SinkConfig` passed to `SinkAudioMixer` will now contain the `protocol_type`.
        *   `SinkAudioMixer` itself will be responsible for creating its specific `INetworkSender` based on this `protocol_type`.
        *   If `WebRTCSender` needs to communicate signaling messages, `SinkAudioMixer` might expose methods for this, or `WebRTCSender` might be given a callback/interface to `AudioManager` for signaling.
*   **`src/audio_engine/audio_types.h` / `src/configuration/audio_engine_config_types.h`:**
    *   Ensure `SinkConfig` contains `protocol_type` (enum: `LEGACY_SCREAM`, `RTP`, `WEBRTC`).
    *   Define `AudioFormatDetails` struct if needed for `INetworkSender::send_audio_packet`.
        ```cpp
        // Example in audio_types.h
        struct AudioFormatDetails {
            int sample_rate;
            int channels;
            int bit_depth;
            uint8_t chlayout1; // For Scream header or RTP custom payload
            uint8_t chlayout2; // For Scream header or RTP custom payload
            // For RTP:
            uint32_t rtp_timestamp_offset; // Base timestamp for this chunk
            uint8_t rtp_payload_type;
            bool rtp_marker_bit;
        };
        ```
*   **`src/audio_engine/CMakeLists.txt`:** Add new `.h` and `.cpp` files for the senders.

**Detailed Steps:**

1.  **Define `INetworkSender` Interface (`network_sender.h`):**
    *   Declare pure virtual methods for initialization, sending PCM, sending MP3 (if distinct), and stopping.
2.  **Implement `ScreamSender` (`scream_sender.h/.cpp`):**
    *   Inherit from `INetworkSender`.
    *   Move existing UDP socket creation, Scream header construction, and `sendto` logic from `SinkAudioMixer` into this class.
    *   `initialize()` sets up the UDP socket and destination address from `SinkConfig`.
    *   `send_audio_packet()` prepends the Scream header and sends the UDP packet.
    *   `send_mp3_frame()` would likely not be implemented or would error out for Scream protocol.
3.  **Implement `RTPSender` (`rtp_sender.h/.cpp`):**
    *   Inherit from `INetworkSender`.
    *   Integrate oRTP for sending (as detailed in `task_01_rtp_library_integration.md`).
    *   `initialize()` sets up the oRTP session, remote target, SSRC, etc., based on `SinkConfig` and potentially SDP info (passed via `AudioFormatDetails` or another mechanism).
    *   `send_audio_packet()` packetizes PCM data into RTP, sets timestamps/payload type/marker, and sends via oRTP.
    *   `send_mp3_frame()` packetizes MP3 frames into RTP, sets timestamps/payload type/marker, and sends via oRTP.
4.  **Implement `WebRTCSender` (`webrtc_sender.h/.cpp`):**
    *   Inherit from `INetworkSender`.
    *   Integrate `libdatachannel` (as detailed in `task_02_webrtc_library_integration.md`).
    *   `initialize()` sets up the `PeerConnection`, data channel, and initiates signaling (passing signaling events to `AudioManager`/Python).
    *   `send_audio_packet()` would likely error or be a no-op, as WebRTC path is for MP3.
    *   `send_mp3_frame()` sends MP3 data over the established WebRTC data channel.
5.  **Refactor `SinkAudioMixer`:**
    *   Add `std::unique_ptr<INetworkSender> sender_;` member.
    *   In constructor or a dedicated setup method, based on `config_.protocol_type`:
        *   `sender_ = std::make_unique<ScreamSender>();`
        *   `sender_ = std::make_unique<RTPSender>();` (passing necessary oRTP config)
        *   `sender_ = std::make_unique<WebRTCSender>();` (passing `AudioManager` interface for signaling)
        *   Call `sender_->initialize(config_);`
    *   Modify `SinkAudioMixer::run()` (specifically after `downscale_buffer` or `encode_and_push_mp3`):
        *   If PCM output: Call `sender_->send_audio_packet(payload_ptr, payload_len, format_details);`
        *   If MP3 output (for WebRTC/RTP MP3): Call `sender_->send_mp3_frame(mp3_ptr, mp3_len);`
    *   Ensure `SinkAudioMixer::stop()` calls `sender_->stop()`.
6.  **Update `AudioManager`:**
    *   Ensure `SinkConfig` passed to `SinkAudioMixer` includes `protocol_type`.
    *   If `WebRTCSender` requires signaling callbacks to Python via `AudioManager`, implement the necessary bridging functions and pybind11 bindings.
7.  **Testing:**
    *   Test each sender implementation individually by configuring sinks with different `protocol_type` values.
    *   Verify that legacy Scream output still works via `ScreamSender`.
    *   Verify RTP output works via `RTPSender`.
    *   Verify WebRTC output works via `WebRTCSender`.

**Considerations:**

*   **Configuration Complexity:** `SinkConfig` and the initialization of senders will need to handle protocol-specific parameters.
*   **Signaling for WebRTC:** The interaction between `WebRTCSender`, `SinkAudioMixer`, `AudioManager`, and the Python SIP/signaling layer needs careful design.
*   **AudioFormatDetails:** The structure and content of `AudioFormatDetails` needs to be well-defined to provide senders with all necessary information (especially for RTP).

**Acceptance Criteria:**

*   `SinkAudioMixer` uses a modular `INetworkSender` interface.
*   Separate sender implementations for Scream, RTP, and WebRTC exist and are functional.
*   `SinkAudioMixer` correctly instantiates and uses the appropriate sender based on `SinkConfig::protocol_type`.
*   Audio is correctly packetized and transmitted according to the selected protocol.
