# Sub-Task 2.4: Integrate `WebRTCSender` with `SinkAudioMixer`

**Objective:** Modify `SinkAudioMixer` to instantiate and use `WebRTCSender` when a sink is configured for WebRTC. Ensure MP3 frames encoded by LAME are correctly passed to `WebRTCSender::send_mp3_frame()`.

**Parent Task:** [WebRTC Library (libdatachannel) Integration](../task_02_webrtc_library_integration.md)
**Previous Sub-Task:** [Sub-Task 2.3: Implement Signaling Bridge in `AudioManager` and `WebRTCSender`](./subtask_2.3_implement_signaling_bridge.md)
**Related Task:** [C++ Modular Network Sender Implementation](../task_08_cpp_modular_sender.md) (defines `INetworkSender` and `SinkAudioMixer`'s use of it)

## Key Steps & Considerations:

1.  **Modify `SinkAudioMixer` Constructor/Initializer:**
    *   The `SinkAudioMixer` constructor (or an initialization method called by `AudioManager`) needs to receive the full `SinkConfig`.
    *   Based on `SinkConfig::protocol_type`, it will instantiate the appropriate `INetworkSender` implementation.
    ```cpp
    // In src/audio_engine/sink_audio_mixer.h
    // class SinkAudioMixer {
    // private:
    //     std::shared_ptr<INetworkSender> network_sender_;
    //     // ... other members ...
    //     // The signaling callback needs to be passed down if WebRTCSender is chosen
    //     std::function<void(const std::string& sink_id, const std::string& type, const std::string& message)> webrtc_signaling_callback_; 
    // };

    // In src/audio_engine/sink_audio_mixer.cpp
    // SinkAudioMixer::SinkAudioMixer(
    //     const SinkConfig& config, 
    //     std::shared_ptr<LameEncoderManager> lame_encoder_manager,
    //     /* ... other params ... */
    //     // Add the signaling callback for WebRTC
    //     std::function<void(const std::string& sink_id, const std::string& type, const std::string& message)> webrtc_sig_cb 
    // ) : /* ... initializers ... */, 
    //     webrtc_signaling_callback_(webrtc_sig_cb), // Store it if needed for re-init, or pass directly
    //     config_(config) // Store config_
    // {
    //     // ...
    //     if (config_.protocol_type == ProtocolType::WEBRTC) {
    //         network_sender_ = std::make_shared<WebRTCSender>(config_.id, webrtc_signaling_callback_);
    //     } else if (config_.protocol_type == ProtocolType::RTP) {
    //         network_sender_ = std::make_shared<RTPSender>(config_.id);
    //     } else { // Default to legacy Scream
    //         network_sender_ = std::make_shared<ScreamSender>(config_.id);
    //     }

    //     if (network_sender_) {
    //         // initial_format_details needs to be constructed based on config_
    //         // For WebRTC, this might be less critical if it only sends MP3 and format is implicit.
    //         AudioFormatDetails initial_format; // Populate this appropriately
    //         if (!network_sender_->initialize(config_, initial_format)) {
    //             screamrouter_logger::error("Failed to initialize network sender for sink {}", config_.id);
    //             network_sender_.reset(); // Nullify if init failed
    //         }
    //     }
    //     // ...
    // }
    ```
    *   The `AudioManager` will be responsible for providing the `webrtc_signaling_callback_` to `SinkAudioMixer` if the protocol is WebRTC.

2.  **Route MP3 Data to `WebRTCSender`:**
    *   The `SinkAudioMixer::run()` loop (or a method called by it, like `encode_and_push_mp3`) is responsible for encoding audio to MP3 using LAME when the sink is configured for MP3 output (which is the case for WebRTC).
    *   After an MP3 frame is encoded:
        *   Check if `network_sender_` is valid and if its type is appropriate for MP3 (or if `WebRTCSender` is active).
        *   Call `network_sender_->send_mp3_frame(mp3_data_ptr, mp3_data_len, rtp_timestamp);`.
        *   The `rtp_timestamp` for `send_mp3_frame` should correspond to the presentation time of the MP3 frame. This needs to be generated based on the audio stream's timing.

    ```cpp
    // In src/audio_engine/sink_audio_mixer.cpp - within the run() loop or MP3 encoding part

    // If (config_.codec == CodecType::MP3 && network_sender_) { // Or similar logic
    //     // ... LAME encoding produces mp3_buffer and mp3_bytes ...
    //     if (mp3_bytes > 0) {
    //         // Generate an appropriate RTP timestamp for this MP3 frame
    //         // This timestamp should be based on the audio sampling clock rate (e.g., 48000 Hz)
    //         // and reflect the presentation time of the first sample encoded in this MP3 frame.
    //         // This logic needs careful implementation, possibly using a running sample count.
    //         uint32_t current_rtp_timestamp = calculate_rtp_timestamp_for_mp3_frame(); // Placeholder

    //         network_sender_->send_mp3_frame(
    //             reinterpret_cast<const uint8_t*>(mp3_buffer.data()), 
    //             mp3_bytes,
    //             current_rtp_timestamp 
    //         );
    //     }
    // }
    ```

3.  **Expose `WebRTCSender` for Signaling (if needed directly by `AudioManager`):**
    *   As discussed in Sub-Task 2.3, `AudioManager` needs to forward signaling messages to the correct `WebRTCSender`.
    *   `SinkAudioMixer` should provide a method to access its `INetworkSender`.
    ```cpp
    // In src/audio_engine/sink_audio_mixer.h
    // std::shared_ptr<INetworkSender> get_network_sender() const { return network_sender_; }
    // ProtocolType get_network_sender_type() const { return config_.protocol_type; } // Expose protocol type
    ```
    *   `AudioManager` can then `dynamic_pointer_cast` the `INetworkSender` to `WebRTCSender` to call `process_signaling_message`.

4.  **`AudioFormatDetails` for `initialize()`:**
    *   The `INetworkSender::initialize()` method takes an `AudioFormatDetails` argument. For `WebRTCSender`, if it exclusively handles MP3 and the format is somewhat fixed (e.g., MP3 CBR 192kbps), this initial detail might be less critical than for `RTPSender` which handles various PCM formats. However, it should still be populated with sensible defaults or information from `SinkConfig`.

## Code Alterations:

*   **`src/audio_engine/sink_audio_mixer.h` & `.cpp`:**
    *   Modify constructor to accept `webrtc_signaling_callback` (likely from `AudioManager`).
    *   Instantiate `WebRTCSender` if `config.protocol_type == ProtocolType::WEBRTC`.
    *   In the MP3 encoding path, call `network_sender_->send_mp3_frame()`.
    *   Add `get_network_sender()` and `get_network_sender_type()` methods.
*   **`src/audio_engine/audio_manager.h` & `.cpp`:**
    *   When creating `SinkAudioMixer` for a WebRTC sink, pass the `AudioManager`'s Python-bound signaling callback mechanism.
    *   `handle_incoming_webrtc_signaling_message` will use `mixer->get_network_sender()` and `dynamic_pointer_cast` to reach the `WebRTCSender`.
*   **`src/configuration/audio_engine_config_types.h`:**
    *   Ensure `SinkConfig` has `protocol_type` (enum `ProtocolType` including `WEBRTC`) and `webrtc_config` (of type `WebRTCConfigCpp`).

## Recommendations:

*   **RTP Timestamp for MP3:** Generating correct RTP timestamps for MP3 frames is important. Each MP3 frame has a duration (e.g., 1152 samples at 44.1/48kHz). The RTP timestamp should increment by the number of samples per frame, using the audio clock rate (e.g., 48000).
*   **`send_audio_packet` in `WebRTCSender`:** For `WebRTCSender`, the `send_audio_packet` method (meant for PCM) should probably be a no-op or log an error, as WebRTC is planned for MP3 streaming over data channels in this project.
*   **Error Handling:** Ensure `network_sender_` is checked for `nullptr` before use, especially if initialization might fail.

## Acceptance Criteria:

*   `SinkAudioMixer` correctly instantiates `WebRTCSender` for sinks configured with `protocol_type: WEBRTC`.
*   The `webrtc_signaling_callback` is correctly passed from `AudioManager` through `SinkAudioMixer` to `WebRTCSender`.
*   Encoded MP3 frames from LAME are passed to `WebRTCSender::send_mp3_frame()`.
*   `AudioManager` can access the `WebRTCSender` instance via `SinkAudioMixer` to relay incoming signaling messages.
