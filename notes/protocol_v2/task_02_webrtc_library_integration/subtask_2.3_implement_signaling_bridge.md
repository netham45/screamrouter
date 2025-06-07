# Sub-Task 2.3: Implement Signaling Bridge in `AudioManager` and `WebRTCSender`

**Objective:** Implement the mechanisms for relaying WebRTC signaling messages (SDP offers/answers, ICE candidates) between the Python layer (SIP server) and the C++ `WebRTCSender` instances, via `AudioManager`.

**Parent Task:** [WebRTC Library (libdatachannel) Integration](../task_02_webrtc_library_integration.md)
**Previous Sub-Task:** [Sub-Task 2.2: Implement `WebRTCSender` Class Structure](./subtask_2.2_implement_webrtc_sender_class.md)

## Key Steps & Considerations:

### 1. `WebRTCSender` Signaling Callbacks:
   *   As defined in Sub-Task 2.2, `WebRTCSender`'s `setup_peer_connection_callbacks()` method configures `libdatachannel` to invoke lambdas when local descriptions (SDP) or ICE candidates are generated.
   *   These lambdas will use the `signaling_callback_` (passed during `WebRTCSender` construction) to send these messages upwards.
   ```cpp
   // In src/audio_engine/webrtc_sender.cpp - (setup_peer_connection_callbacks)

   // Assuming self_ptr is std::weak_ptr<WebRTCSender> to handle lifetime safely in callbacks
   auto self_ptr = weak_from_this(); 

   peer_connection_->onLocalDescription([this, self_ptr](rtc::Description sdp) {
       if (auto sender = self_ptr.lock()) { // Ensure WebRTCSender instance still exists
           std::string sdp_type_str = (sdp.type() == rtc::Description::Type::Offer) ? "offer" : "answer";
           screamrouter_logger::info("WebRTCSender ({}): Generated local SDP {}:\n{}", sink_id_, sdp_type_str, std::string(sdp));
           if (signaling_callback_) {
               signaling_callback_(sink_id_, sdp_type_str, std::string(sdp));
           }
       }
   });

   peer_connection_->onLocalCandidate([this, self_ptr](rtc::Candidate candidate) {
       if (auto sender = self_ptr.lock()) {
           screamrouter_logger::info("WebRTCSender ({}): Generated local ICE candidate: {}", sink_id_, std::string(candidate));
           if (signaling_callback_) {
               // libdatachannel candidate to JSON string
               signaling_callback_(sink_id_, "candidate", std::string(candidate)); 
           }
       }
   });
   // ... other callbacks (onStateChange, onGatheringStateChange, etc. for logging)
   ```

### 2. `AudioManager` as the Bridge:
   *   **Store `WebRTCSender` Instances:** `AudioManager` needs a way to access specific `WebRTCSender` instances, likely through `SinkAudioMixer`. When a sink is created with `protocol_type: WEBRTC`, the `SinkAudioMixer` will create a `WebRTCSender`. `AudioManager` might keep a map of `sink_id` to `SinkAudioMixer*` or `WebRTCSender*` (if `SinkAudioMixer` exposes it).
   *   **Callback from C++ to Python:**
      *   `AudioManager` will provide the `signaling_callback_` function to `WebRTCSender`. This function, when invoked by `WebRTCSender`, needs to queue the signaling message or directly call a Python-registered callback.
      *   A Python-registered callback is cleaner. `AudioManager` will have a method like `set_python_webrtc_signaling_callback(std::function<void(const std::string& sink_id, const std::string& type, const std::string& message)> py_cb)`.
      *   The `signaling_callback_` given to `WebRTCSender` will then invoke this `py_cb`.
      ```cpp
      // In src/audio_engine/audio_manager.h
      // std::function<void(const std::string& sink_id, const std::string& type, const std::string& message)> python_webrtc_signaling_cb_;

      // In src/audio_engine/audio_manager.cpp
      // void AudioManager::set_python_webrtc_signaling_callback(
      //     std::function<void(const std::string& sink_id, const std::string& type, const std::string& message)> py_cb) {
      //     python_webrtc_signaling_cb_ = py_cb;
      // }

      // When creating WebRTCSender (e.g., in SinkAudioMixer, which gets this callback from AudioManager):
      // auto am_signaling_cb = [this](const std::string& cb_sink_id, const std::string& cb_type, const std::string& cb_message) {
      //     if (audio_manager_instance_ && audio_manager_instance_->python_webrtc_signaling_cb_) {
      //         audio_manager_instance_->python_webrtc_signaling_cb_(cb_sink_id, cb_type, cb_message);
      //     }
      // };
      // webrtc_sender_ = std::make_shared<WebRTCSender>(sink_id, am_signaling_cb);
      ```
   *   **Method to Receive Messages from Python:**
      *   `AudioManager` needs a public method callable from Python (via pybind11) to pass incoming signaling messages *to* the correct `WebRTCSender`.
      ```cpp
      // In src/audio_engine/audio_manager.h
      // void handle_incoming_webrtc_signaling_message(const std::string& sink_id, const std::string& type, const std::string& message);

      // In src/audio_engine/audio_manager.cpp
      // void AudioManager::handle_incoming_webrtc_signaling_message(const std::string& sink_id, const std::string& type, const std::string& message) {
      //     screamrouter_logger::info("AudioManager: Received signaling for sink {}: type={}, msg_len={}", sink_id, type, message.length());
      //     auto mixer_it = sink_mixers_.find(sink_id);
      //     if (mixer_it != sink_mixers_.end()) {
      //         std::shared_ptr<SinkAudioMixer> mixer = mixer_it->second;
      //         // SinkAudioMixer needs to expose its WebRTCSender or a method to forward this.
      //         if (mixer && mixer->get_network_sender_type() == ProtocolType::WEBRTC) { // Assuming a way to check type
      //             auto webrtc_sender = std::dynamic_pointer_cast<WebRTCSender>(mixer->get_network_sender()); // Assuming INetworkSender can be cast
      //             if (webrtc_sender) {
      //                 webrtc_sender->process_signaling_message(type, message);
      //             } else {
      //                 screamrouter_logger::error("AudioManager: Could not get WebRTCSender for sink {}", sink_id);
      //             }
      //         }
      //     } else {
      //         screamrouter_logger::error("AudioManager: Sink {} not found for WebRTC signaling.", sink_id);
      //     }
      // }
      ```

### 3. `WebRTCSender::process_signaling_message()`:
   *   This method is called by `AudioManager` when a message (SDP offer/answer or ICE candidate) arrives from the Python/SIP layer.
   ```cpp
   // In src/audio_engine/webrtc_sender.cpp
   // void WebRTCSender::process_signaling_message(const std::string& type, const std::string& message) {
   //     if (!peer_connection_) {
   //         screamrouter_logger::error("WebRTCSender ({}): PeerConnection not initialized, cannot process signaling.", sink_id_);
   //         return;
   //     }
   //     screamrouter_logger::info("WebRTCSender ({}): Processing signaling: type={}, msg_len={}", sink_id_, type, message.length());

   //     if (type == "offer") {
   //         rtc::Description offer(message, type);
   //         peer_connection_->setRemoteDescription(offer); 
   //         // After setting remote offer, libdatachannel will trigger onLocalDescription with an answer.
   //     } else if (type == "answer") {
   //         rtc::Description answer(message, type);
   //         peer_connection_->setRemoteDescription(answer);
   //     } else if (type == "candidate") {
   //         rtc::Candidate candidate(message, "0"); // "0" is a common mid for candidates from browsers
   //         peer_connection_->addRemoteCandidate(candidate);
   //     } else {
   //         screamrouter_logger::warn("WebRTCSender ({}): Unknown signaling message type: {}", sink_id_, type);
   //     }
   // }
   ```

### 4. Pybind11 Bindings (`src/audio_engine/bindings.cpp`):
   *   Expose `AudioManager::set_python_webrtc_signaling_callback`.
   *   Expose `AudioManager::handle_incoming_webrtc_signaling_message`.
   ```cpp
    // In bindings.cpp
    // .def("set_python_webrtc_signaling_callback", &AudioManager::set_python_webrtc_signaling_callback,
    //      py::arg("callback"), "Sets the callback for WebRTC signaling messages from C++ to Python")
    // .def("handle_incoming_webrtc_signaling_message", &AudioManager::handle_incoming_webrtc_signaling_message,
    //      py::arg("sink_id"), py::arg("type"), py::arg("message"), "Handles WebRTC signaling messages from Python to C++")
   ```

## Code Alterations:

*   **`src/audio_engine/webrtc_sender.h` & `.cpp`:**
    *   Implement `process_signaling_message()`.
    *   Ensure constructor takes `signaling_callback_`.
    *   Implement `setup_peer_connection_callbacks()` to use `signaling_callback_`.
*   **`src/audio_engine/audio_manager.h` & `.cpp`:**
    *   Add `python_webrtc_signaling_cb_` member.
    *   Implement `set_python_webrtc_signaling_callback()`.
    *   Implement `handle_incoming_webrtc_signaling_message()`.
    *   Ensure `SinkAudioMixer` can provide access to its `WebRTCSender` or forward signaling messages to it.
    *   The callback passed to `WebRTCSender` should correctly invoke `python_webrtc_signaling_cb_`.
*   **`src/audio_engine/sink_audio_mixer.h` & `.cpp`:**
    *   Modify constructor/initializer to accept the `AudioManager`'s signaling callback function and pass it to `WebRTCSender`.
    *   Provide a way for `AudioManager` to get the `WebRTCSender` instance associated with a `SinkAudioMixer` (e.g., `std::shared_ptr<INetworkSender> get_network_sender()`).
*   **`src/audio_engine/bindings.cpp`:** Add pybind11 bindings for the new `AudioManager` methods.

## Recommendations:

*   **Thread Safety for Python Callback:** The `python_webrtc_signaling_cb_` will be called from a `libdatachannel` thread. When this callback invokes Python code (via pybind11), the Python Global Interpreter Lock (GIL) must be acquired. Pybind11 usually handles this automatically for calls from C++ to Python, but it's critical to be aware of. Consider using a thread-safe queue in `AudioManager` to pass messages to a Python thread if direct callbacks become problematic.
*   **Message Format:** Standardize the `type` strings ("offer", "answer", "candidate") and the format of the `message` string (SDP string, JSON string for candidate).
*   **Error Handling:** Robustly handle errors in parsing signaling messages or if `peer_connection_` is not in a valid state.
*   **Lifetime Management:** Use `std::weak_ptr` and `shared_from_this()` in `WebRTCSender` callbacks to prevent issues if the `WebRTCSender` is destroyed while callbacks are pending.

## Acceptance Criteria:

*   `WebRTCSender` can send locally generated SDP and ICE candidates to `AudioManager` via a callback.
*   `AudioManager` can relay these messages to a Python-registered callback.
*   `AudioManager` can receive signaling messages from Python (via pybind11) and forward them to the correct `WebRTCSender` instance.
*   `WebRTCSender` can process incoming SDP offers/answers and ICE candidates using `libdatachannel` APIs.
*   The signaling bridge is thread-safe, particularly concerning calls into Python.
