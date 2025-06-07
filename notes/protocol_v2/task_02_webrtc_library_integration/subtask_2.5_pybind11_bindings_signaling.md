# Sub-Task 2.5: Create Pybind11 Bindings for Signaling Bridge

**Objective:** Expose the C++ `AudioManager` methods related to WebRTC signaling (i.e., `set_python_webrtc_signaling_callback` and `handle_incoming_webrtc_signaling_message`) to Python using pybind11.

**Parent Task:** [WebRTC Library (libdatachannel) Integration](../task_02_webrtc_library_integration.md)
**Previous Sub-Task:** [Sub-Task 2.4: Integrate `WebRTCSender` with `SinkAudioMixer`](./subtask_2.4_integrate_webrtc_sender_with_mixer.md)

## Key Steps & Considerations:

1.  **Locate Pybind11 Module Definition (`src/audio_engine/bindings.cpp`):**
    *   This is where existing C++ classes and functions are exposed to Python.
    *   The `AudioManager` class should already have some bindings.

2.  **Bind `AudioManager::set_python_webrtc_signaling_callback`:**
    *   This method allows Python to register a callback function that `AudioManager` (and subsequently `WebRTCSender`) will invoke to send signaling messages (local SDP, ICE candidates) from C++ up to Python.
    *   The C++ signature is likely `void (std::function<void(const std::string& sink_id, const std::string& type, const std::string& message)>)`.
    *   Pybind11 can automatically convert Python callables to `std::function`.

    ```cpp
    // In src/audio_engine/bindings.cpp, within PYBIND11_MODULE block for audio_engine_python:

    // py::class_<AudioManager, std::shared_ptr<AudioManager>>(m, "AudioManager")
    //     // ... existing bindings ...
    //     .def("set_python_webrtc_signaling_callback", 
    //          &AudioManager::set_python_webrtc_signaling_callback,
    //          py::arg("callback"),
    //          py::call_guard<py::gil_scoped_release>(), // Optional: if the C++ function is long-running, though unlikely for just setting a callback
    //          "Sets the callback function that C++ will use to send WebRTC signaling messages (SDP, ICE) to Python. "
    //          "The callback should accept three string arguments: sink_id, type ('offer', 'answer', 'candidate'), and message (SDP/ICE string).")
    //     // ...
    // ;
    ```
    *   **GIL Management:** `py::call_guard<py::gil_scoped_release>()` is generally for C++ functions that might block or take a long time, releasing the GIL. For simply setting a callback, it's often not strictly necessary. However, when the C++ *invokes* the Python callback from a C++ thread, pybind11 needs to acquire the GIL. This is usually handled automatically by pybind11 when wrapping `std::function` that calls back into Python.

3.  **Bind `AudioManager::handle_incoming_webrtc_signaling_message`:**
    *   This method allows Python to send signaling messages (received from a WebRTC client via SIP) down to the C++ `AudioManager`, which then routes it to the appropriate `WebRTCSender`.
    *   The C++ signature is likely `void (const std::string& sink_id, const std::string& type, const std::string& message)`.

    ```cpp
    // In src/audio_engine/bindings.cpp, within PYBIND11_MODULE block for audio_engine_python:

    // py::class_<AudioManager, std::shared_ptr<AudioManager>>(m, "AudioManager")
    //     // ... existing bindings ...
    //     .def("handle_incoming_webrtc_signaling_message",
    //          &AudioManager::handle_incoming_webrtc_signaling_message,
    //          py::arg("sink_id"), py::arg("type"), py::arg("message"),
    //          py::call_guard<py::gil_scoped_release>(), // Release GIL if C++ processing is potentially long
    //          "Handles incoming WebRTC signaling messages (SDP offer/answer, ICE candidate) from Python, "
    //          "forwarding them to the appropriate C++ WebRTCSender instance.")
    //     // ...
    // ;
    ```
    *   **GIL Management for `handle_incoming_webrtc_signaling_message`:** If the C++ processing of this message (finding the sender, calling `libdatachannel` APIs) is quick, `py::gil_scoped_release` might not be needed. If it involves potentially blocking operations or significant C++ work, releasing the GIL can be beneficial for Python's concurrency.

## Code Alterations:

*   **File:** `src/audio_engine/bindings.cpp`
    *   Add the pybind11 bindings for `AudioManager::set_python_webrtc_signaling_callback`.
    *   Add the pybind11 bindings for `AudioManager::handle_incoming_webrtc_signaling_message`.
    *   Ensure necessary headers (`audio_manager.h`, `pybind11/functional.h` for `std::function`) are included.

*   **File:** `src/audio_engine/audio_manager.h`
    *   Ensure the methods `set_python_webrtc_signaling_callback` and `handle_incoming_webrtc_signaling_message` are declared public.

## Python-Side Usage Example (Conceptual):

```python
# In Python (e.g., within SipManager or ConfigurationManager)
# import audio_engine_python 

# audio_mgr = audio_engine_python.AudioManager.get_instance() # Assuming AudioManager is a singleton

# def cpp_to_python_signaling_handler(sink_id: str, type: str, message: str):
#     print(f"Python received from C++ for sink {sink_id}: type={type}, message_len={len(message)}")
#     # Logic to forward this message to the actual WebRTC client via SIP
#     # For example, find the SIP call associated with sink_id and send appropriate SIP INFO or re-INVITE.

# audio_mgr.set_python_webrtc_signaling_callback(cpp_to_python_signaling_handler)

# # When Python SIP server receives an SDP offer from a WebRTC client:
# # sdp_offer_from_client = "..."
# # target_sink_id = "some_webrtc_sink_id"
# # audio_mgr.handle_incoming_webrtc_signaling_message(target_sink_id, "offer", sdp_offer_from_client)
```

## Recommendations:

*   **Include `pybind11/functional.h`:** This header is necessary for pybind11 to handle `std::function` conversions.
*   **Error Handling in Python Callback:** The Python callback function (`cpp_to_python_signaling_handler` in the example) should handle its own errors gracefully, as exceptions propagating back into C++ from a callback can be problematic if not handled by pybind11.
*   **Thread Safety (Reiteration):** The Python callback registered via `set_python_webrtc_signaling_callback` will be executed on a C++ thread (likely a `libdatachannel` internal thread). Any Python code it runs must be thread-safe or manage access to shared Python resources carefully. Pybind11 handles GIL acquisition for the duration of the Python callback.
*   **Testing:** After adding bindings, test by:
    1.  Calling `set_python_webrtc_signaling_callback` from Python with a simple print function.
    2.  Triggering a local description or candidate generation in `WebRTCSender` (e.g., by processing a dummy offer) and verifying the Python callback is invoked.
    3.  Calling `handle_incoming_webrtc_signaling_message` from Python with dummy data and verifying `AudioManager` and `WebRTCSender` log the reception.

## Acceptance Criteria:

*   `AudioManager::set_python_webrtc_signaling_callback` is successfully bound and callable from Python.
*   `AudioManager::handle_incoming_webrtc_signaling_message` is successfully bound and callable from Python.
*   Python can register a callback, and this callback can be invoked by the C++ layer when `WebRTCSender` generates signaling messages.
*   C++ `WebRTCSender` can receive and process signaling messages passed from Python via these bindings.
*   The project compiles, and basic Python calls to these new bindings do not crash.
