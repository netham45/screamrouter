# Sub-Task 8.4: Implement `WebRTCSender` using `libdatachannel`

**Objective:** Implement the `WebRTCSender` class, inheriting from `INetworkSender`. This class will use the `libdatachannel` library for WebRTC peer connection management, data channel creation, and sending MP3 frames. It will also handle signaling via a callback to `AudioManager`. Much of this logic was previously detailed in `task_02_webrtc_library_integration`.

**Parent Task:** [C++ Modular Network Sender Implementation](../task_08_cpp_modular_sender.md)
**Previous Sub-Task:** [Sub-Task 8.3: Implement `RTPSender` using oRTP](./subtask_8.3_implement_rtp_sender.md)
**Related Task:** [WebRTC Library (libdatachannel) Integration](../task_02_webrtc_library_integration.md)

## Key Steps & Considerations:

1.  **Create/Update `webrtc_sender.h` and `webrtc_sender.cpp`:**
    *   Files: `src/audio_engine/webrtc_sender.h` and `src/audio_engine/webrtc_sender.cpp`.
    *   These files were outlined in `task_02_webrtc_library_integration/subtask_2.2_implement_webrtc_sender_class.md`. This sub-task focuses on completing their implementation.

2.  **`WebRTCSender` Class Definition (`webrtc_sender.h` - Review/Finalize):**
    *   Ensure it inherits `INetworkSender` and `std::enable_shared_from_this<WebRTCSender>`.
    *   Include members for `rtc::PeerConnection`, `rtc::DataChannel`, sink ID, signaling callback, `rtc::Configuration`, and state flags (`initialized_`, `data_channel_open_`).
    ```cpp
    // In src/audio_engine/webrtc_sender.h (ensure it matches or refines Sub-Task 2.2)
    #ifndef WEBRTC_SENDER_H
    #define WEBRTC_SENDER_H

    #include "network_sender.h"
    #include "configuration/audio_engine_config_types.h"
    #include "screamrouter_logger/screamrouter_logger.h"

    #include <rtc/peerconnection.hpp>
    #include <rtc/datachannel.hpp>
    #include <rtc/configuration.hpp> // For rtc::Configuration
    #include <rtc/description.hpp>  // For rtc::Description
    #include <rtc/candidate.hpp>    // For rtc::Candidate

    #include <string>
    #include <memory>
    #include <functional>
    #include <atomic> // For std::atomic

    class WebRTCSender : public INetworkSender, public std::enable_shared_from_this<WebRTCSender> {
    public:
        WebRTCSender(const std::string& sink_id,
                     std::function<void(const std::string& sink_id, const std::string& type, const std::string& message)> signaling_cb);
        ~WebRTCSender() override;

        bool initialize(const SinkConfig& config, const AudioFormatDetails& initial_format_details) override;
        void send_audio_packet(const uint8_t* payload, size_t payload_len, const AudioFormatDetails& format, uint32_t rtp_timestamp) override;
        void send_mp3_frame(const uint8_t* mp3_data, size_t data_len, uint32_t rtp_timestamp) override;
        void stop() override;

        void process_signaling_message(const std::string& type, const std::string& message);

    private:
        void setup_peer_connection_callbacks();
        void create_data_channel(const std::string& label = "mp3"); // Default label "mp3"

        std::string sink_id_;
        std::function<void(const std::string& sink_id, const std::string& type, const std::string& message)> signaling_callback_;
        
        rtc::Configuration rtc_config_; // Holds STUN/TURN server info
        std::shared_ptr<rtc::PeerConnection> peer_connection_;
        std::shared_ptr<rtc::DataChannel> data_channel_;

        bool initialized_;
        std::atomic<bool> data_channel_open_;
        std::string data_channel_label_;
    };

    #endif // WEBRTC_SENDER_H
    ```

3.  **Implement Constructor and Destructor (`webrtc_sender.cpp`):**
    *   **Constructor:** `sink_id_ = sink_id; signaling_callback_ = signaling_cb; initialized_ = false; data_channel_open_ = false; data_channel_label_ = "mp3";`
    *   **Destructor:** Call `stop()`.

4.  **Implement `initialize()` (`webrtc_sender.cpp`):**
    *   Populate `rtc_config_` from `config.webrtc_config` (STUN/TURN servers).
    *   `peer_connection_ = std::make_shared<rtc::PeerConnection>(rtc_config_);`
    *   Call `setup_peer_connection_callbacks()`.
    *   Call `create_data_channel(data_channel_label_)`.
    *   Set `initialized_ = true;`.
    *   Log success/failure.

5.  **Implement `setup_peer_connection_callbacks()` (`webrtc_sender.cpp`):**
    *   (As detailed in Sub-Task 2.3) Set callbacks for `onLocalDescription`, `onLocalCandidate`, `onStateChange`, `onGatheringStateChange`, `onDataChannel` (if acting as callee for DC).
    *   Use `weak_from_this()` to safely capture `this` in lambdas.
    *   Invoke `signaling_callback_` with appropriate parameters.

6.  **Implement `create_data_channel()` (`webrtc_sender.cpp`):**
    *   `data_channel_ = peer_connection_->createDataChannel(label);`
    *   Set callbacks for `data_channel_`: `onOpen`, `onClosed`, `onError`. Update `data_channel_open_` state.
    *   Log data channel creation and state changes.

7.  **Implement `stop()` (`webrtc_sender.cpp`):**
    *   `data_channel_open_ = false; initialized_ = false;`
    *   If `data_channel_` is valid and open, call `data_channel_->close()`.
    *   If `peer_connection_` is valid, call `peer_connection_->close()`.
    *   `data_channel_.reset(); peer_connection_.reset();`.

8.  **Implement `process_signaling_message()` (`webrtc_sender.cpp`):**
    *   (As detailed in Sub-Task 2.3) Handle incoming "offer", "answer", "candidate" messages.
    *   Use `rtc::Description` and `rtc::Candidate` to parse messages.
    *   Call `peer_connection_->setRemoteDescription()`, `peer_connection_->addRemoteCandidate()`.
    *   If setting a remote offer, `libdatachannel` will automatically trigger `onLocalDescription` with an answer.

9.  **Implement `send_mp3_frame()` (`webrtc_sender.cpp`):**
    *   If not `initialized_` or not `data_channel_open_`, log warning and return.
    *   Check if `data_channel_` is valid and `isOpen()`.
    *   `data_channel_->send(rtc::message_variant(reinterpret_cast<const std::byte*>(mp3_data), data_len));`
        *   Note: `libdatachannel`'s `send` method takes `rtc::message_variant`, which can be constructed from `std::byte*` or `std::string`.
    *   Log success or errors (e.g., if send buffer is full).
    *   The `rtp_timestamp` parameter might be used for logging or internal sequencing if needed, but it's not part of the WebRTC data channel protocol itself.

10. **Implement `send_audio_packet()` (`webrtc_sender.cpp`):**
    *   This method is for PCM data. Since WebRTC in this design is for MP3 over data channels:
        *   Log an error: `screamrouter_logger::error("WebRTCSender ({}) does not support sending raw PCM audio packets.", sink_id_);`
        *   This method should be a no-op.

## Code Alterations:

*   **`src/audio_engine/webrtc_sender.h` & `src/audio_engine/webrtc_sender.cpp`:** Implement all methods as described.
*   **`src/audio_engine/CMakeLists.txt`:** Ensure `webrtc_sender.cpp` is listed.
*   **`src/configuration/audio_engine_config_types.h` (`SinkConfig`, `WebRTCConfigCpp`):**
    *   Ensure these structs contain fields for STUN/TURN servers as defined in Sub-Task 2.6.
*   **`src/audio_engine/audio_manager.cpp` and `src/audio_engine/sink_audio_mixer.cpp`:**
    *   Ensure `WebRTCSender` is correctly instantiated by `SinkAudioMixer` when `protocol_type` is `WEBRTC`.
    *   Ensure the `signaling_callback_` is correctly passed from `AudioManager` to `WebRTCSender`.

## Recommendations:

*   **`libdatachannel` API:** Refer to `libdatachannel` documentation and examples for correct API usage, especially for `rtc::message_variant` and callback signatures.
*   **Thread Safety:** Callbacks from `libdatachannel` execute on its internal threads. Ensure thread safety when accessing shared members or calling external functions (like the Python signaling callback, which needs GIL management via pybind11).
*   **Error Handling:** Robustly handle errors from `libdatachannel` API calls and in callbacks.
*   **Signaling Flow:** The interaction with the Python signaling layer (`SipManager` via `AudioManager`) is critical.

## Acceptance Criteria:

*   `WebRTCSender` class is fully implemented and inherits from `INetworkSender`.
*   `initialize()` correctly sets up `rtc::PeerConnection` with STUN/TURN configuration and creates a data channel.
*   Signaling callbacks (`onLocalDescription`, `onLocalCandidate`) correctly use the provided `signaling_callback_`.
*   `process_signaling_message()` correctly handles incoming SDP and ICE candidates.
*   `send_mp3_frame()` sends MP3 data over the established data channel.
*   `send_audio_packet()` is a no-op or logs an error.
*   `stop()` cleans up `libdatachannel` resources.
