# Sub-Task 8.5: Refactor `SinkAudioMixer` to Use `INetworkSender`

**Objective:** Refactor `SinkAudioMixer` to remove direct network sending logic and instead use the `INetworkSender` interface. `SinkAudioMixer` will instantiate the appropriate concrete sender (`ScreamSender`, `RTPSender`, `WebRTCSender`) based on the `SinkConfig::protocol_type`.

**Parent Task:** [C++ Modular Network Sender Implementation](../task_08_cpp_modular_sender.md)
**Previous Sub-Task:** [Sub-Task 8.4: Implement `WebRTCSender` using `libdatachannel`](./subtask_8.4_implement_webrtc_sender.md)

## Key Steps & Considerations:

1.  **Modify `SinkAudioMixer` Header (`sink_audio_mixer.h`):**
    *   Include `network_sender.h`.
    *   Remove members related to direct socket management (e.g., `udp_socket_`, `dest_address_`) if they were solely for legacy Scream sending.
    *   Add `std::shared_ptr<INetworkSender> network_sender_;`.
    *   The constructor will need to accept parameters required for instantiating any of the sender types, or `AudioManager` will pass a pre-configured `SinkConfig`. The latter is cleaner.
    *   If `WebRTCSender` requires a signaling callback, `SinkAudioMixer`'s constructor will need to accept this callback from `AudioManager` and pass it to `WebRTCSender`.

    ```cpp
    // In src/audio_engine/sink_audio_mixer.h
    // #include "network_sender.h" // Already included if following previous tasks
    // #include <memory> // For std::shared_ptr
    // #include <functional> // For std::function if passing WebRTC signaling callback

    // class SinkAudioMixer {
    // public:
    //     SinkAudioMixer(
    //         const SinkConfig& config, // Contains protocol_type, rtp_config, webrtc_config etc.
    //         std::shared_ptr<LameEncoderManager> lame_encoder_manager,
    //         // Callback for WebRTC signaling, passed from AudioManager
    //         std::function<void(const std::string& sink_id, const std::string& type, const std::string& message)> webrtc_signaling_cb 
    //     );
    //     // ... other methods ...
    //     std::shared_ptr<INetworkSender> get_network_sender() const { return network_sender_; }
    //     ProtocolType get_protocol_type() const { return config_.protocol_type; }


    // private:
    //     // ... existing members like mixed_buffer_, resampler_, lame_encoder_manager_ ...
    //     const SinkConfig& config_; // Store a reference or copy
    //     std::shared_ptr<INetworkSender> network_sender_;
    //     std::function<void(const std::string& sink_id, const std::string& type, const std::string& message)> webrtc_signaling_callback_for_sender_;
        
    //     // Remove old network members like:
    //     // Socket_simple socket_;
    //     // Address_simple dest_address_;
    //     // uint8_t scream_header_[5];
    // };
    ```

2.  **Modify `SinkAudioMixer` Constructor/Initializer (`sink_audio_mixer.cpp`):**
    *   Store the `SinkConfig` and the `webrtc_signaling_callback_for_sender_`.
    *   Based on `config_.protocol_type`:
        *   If `ProtocolType::LEGACY_SCREAM`: `network_sender_ = std::make_shared<ScreamSender>(config_.id);`
        *   If `ProtocolType::RTP`: `network_sender_ = std::make_shared<RTPSender>(config_.id);`
        *   If `ProtocolType::WEBRTC`: `network_sender_ = std::make_shared<WebRTCSender>(config_.id, webrtc_signaling_callback_for_sender_);`
    *   Call `network_sender_->initialize(config_, initial_format_details);`.
        *   `initial_format_details` needs to be constructed. For PCM-based senders (Scream, RTP PCM), this would come from the mixer's output format. For MP3 (RTP MP3, WebRTC), it might be less critical for `initialize` but important for `send_mp3_frame` calls later.
    *   Handle initialization failure of `network_sender_`.

    ```cpp
    // In src/audio_engine/sink_audio_mixer.cpp
    // SinkAudioMixer::SinkAudioMixer(
    //     const SinkConfig& config,
    //     std::shared_ptr<LameEncoderManager> lame_encoder_manager,
    //     std::function<void(const std::string& sink_id, const std::string& type, const std::string& message)> webrtc_sig_cb
    // ) : config_(config), /* ... other initializers ... */ 
    //     lame_encoder_manager_(lame_encoder_manager),
    //     webrtc_signaling_callback_for_sender_(webrtc_sig_cb) 
    // {
    //     // ...
    //     screamrouter_logger::info("SinkAudioMixer for sink {} creating sender for protocol_type: {}", config_.id, static_cast<int>(config_.protocol_type));

    //     switch (config_.protocol_type) {
    //         case ProtocolType::RTP:
    //             network_sender_ = std::make_shared<RTPSender>(config_.id);
    //             break;
    //         case ProtocolType::WEBRTC:
    //             network_sender_ = std::make_shared<WebRTCSender>(config_.id, webrtc_signaling_callback_for_sender_);
    //             break;
    //         case ProtocolType::LEGACY_SCREAM:
    //         default: // Default to legacy scream
    //             network_sender_ = std::make_shared<ScreamSender>(config_.id);
    //             break;
    //     }

    //     if (network_sender_) {
    //         AudioFormatDetails initial_format; // Populate based on mixer's output or default config
    //         initial_format.sample_rate = config_.sample_rate; // Or mixer's fixed output rate
    //         initial_format.channels = config_.channels;     // Or mixer's fixed output channels
    //         initial_format.bit_depth = 16; // Assuming 16-bit output from mixer before encoding/packetizing
    //         // Populate chlayout1, chlayout2, rtp_payload_type, rtp_marker_bit as applicable from config_ or defaults
    //         initial_format.rtp_payload_type = config_.rtp_config.payload_type_pcm; // Example
            
    //         if (!network_sender_->initialize(config_, initial_format)) {
    //             screamrouter_logger::error("SinkAudioMixer ({}): Failed to initialize network sender.", config_.id);
    //             network_sender_.reset(); // Critical failure
    //         }
    //     } else {
    //         screamrouter_logger::error("SinkAudioMixer ({}): Failed to create network sender.", config_.id);
    //     }
    //     // ...
    // }
    ```

3.  **Update Sending Logic in `SinkAudioMixer::run()` (or equivalent processing method):**
    *   Remove old direct socket `sendto` calls and Scream header construction.
    *   After audio is mixed (and resampled to sink format for PCM, or encoded to MP3):
        *   If PCM output (for Scream or RTP PCM):
            *   Populate `AudioFormatDetails current_packet_format` with the actual format of the mixed PCM data (sample rate, channels, bit depth, channel layout). For RTP, also set `rtp_payload_type` and `rtp_marker_bit`.
            *   Calculate `rtp_timestamp`.
            *   Call `network_sender_->send_audio_packet(pcm_data_ptr, pcm_data_len, current_packet_format, rtp_timestamp);`.
        *   If MP3 output (for RTP MP3 or WebRTC):
            *   After LAME encoding gives `mp3_buffer` and `mp3_bytes`.
            *   Calculate `rtp_timestamp` for the MP3 frame(s).
            *   Call `network_sender_->send_mp3_frame(mp3_buffer_ptr, mp3_bytes, rtp_timestamp);`.
    *   Ensure `network_sender_` is valid before calling its methods.

4.  **Update `SinkAudioMixer::stop_processing_and_join()` (or similar shutdown method):**
    *   Call `network_sender_->stop()` to allow the sender to clean up its resources (close sockets, destroy oRTP/WebRTC sessions).
    *   Then `network_sender_.reset();`.

## Code Alterations:

*   **`src/audio_engine/sink_audio_mixer.h`:**
    *   Add `std::shared_ptr<INetworkSender> network_sender_;`.
    *   Remove old networking members.
    *   Update constructor signature if `webrtc_signaling_callback` is passed.
    *   Add `get_network_sender()` and `get_protocol_type()` accessors.
*   **`src/audio_engine/sink_audio_mixer.cpp`:**
    *   Implement constructor logic to instantiate the correct sender based on `config_.protocol_type` and initialize it.
    *   Replace old sending code in `run()` (or wherever audio data is dispatched) with calls to `network_sender_->send_audio_packet()` or `network_sender_->send_mp3_frame()`.
    *   Call `network_sender_->stop()` in the shutdown/cleanup path.
*   **`src/audio_engine/audio_manager.cpp`:**
    *   When `AudioManager::add_sink` creates a `SinkAudioMixer`, it must now pass the `SinkConfig` and the WebRTC signaling callback (if applicable) to the `SinkAudioMixer` constructor.

## Recommendations:

*   **`AudioFormatDetails` Population:** `SinkAudioMixer` needs to accurately populate `AudioFormatDetails` for each call to `send_audio_packet`, especially `rtp_payload_type` and `rtp_marker_bit` for RTP senders. These might come from `SinkConfig` or be determined dynamically.
*   **RTP Timestamp Calculation:** `SinkAudioMixer` is responsible for calculating the correct `rtp_timestamp` based on its internal audio processing clock and the amount of audio data represented by each outgoing packet/frame.
*   **Error Handling:** Check the return value of `network_sender_->initialize()` and handle failures appropriately (e.g., log and prevent the mixer from running).

## Acceptance Criteria:

*   `SinkAudioMixer` no longer contains direct UDP/TCP sending logic for legacy Scream.
*   `SinkAudioMixer` holds a `std::shared_ptr<INetworkSender>`.
*   Based on `SinkConfig::protocol_type`, `SinkAudioMixer` correctly instantiates `ScreamSender`, `RTPSender`, or `WebRTCSender`.
*   The chosen sender is initialized with the `SinkConfig`.
*   Mixed PCM audio or encoded MP3 frames are correctly passed to the appropriate methods of the active `INetworkSender` instance.
*   `SinkAudioMixer::stop_processing_and_join()` calls `network_sender_->stop()`.
