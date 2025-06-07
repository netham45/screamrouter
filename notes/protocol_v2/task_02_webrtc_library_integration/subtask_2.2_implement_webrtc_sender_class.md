# Sub-Task 2.2: Implement `WebRTCSender` Class Structure

**Objective:** Define and implement the basic structure of the `WebRTCSender` C++ class, including member variables for `libdatachannel::PeerConnection` and `libdatachannel::DataChannel`, and methods for initialization, signaling, and sending MP3 frames. This class will implement the `INetworkSender` interface.

**Parent Task:** [WebRTC Library (libdatachannel) Integration](../task_02_webrtc_library_integration.md)
**Previous Sub-Task:** [Sub-Task 2.1: `setup.py` Integration for `libdatachannel` and Dependencies](./subtask_2.1_setup_py_libdatachannel.md)

## Key Steps & Considerations:

1.  **`WebRTCSender` Class Definition (`webrtc_sender.h` - new file):**
    *   Inherit from `INetworkSender`.
    *   Include necessary `libdatachannel` headers (e.g., `rtc/peerconnection.hpp`, `rtc/datachannel.hpp`, `rtc/configuration.hpp`).
    *   Declare member variables:
        *   `std::shared_ptr<rtc::PeerConnection> peer_connection_`: The main WebRTC peer connection object.
        *   `std::shared_ptr<rtc::DataChannel> data_channel_`: The data channel for sending MP3 frames.
        *   `std::string sink_id_`: Identifier for this sender.
        *   `std::function<void(const std::string& type, const std::string& sdp_or_candidate)> signaling_callback_`: Callback to send signaling messages (SDP, ICE) to `AudioManager`.
        *   `bool initialized_`: Tracks initialization state.
        *   `bool data_channel_open_`: Tracks if the data channel is ready.
        *   `rtc::Configuration rtc_config_`: Holds STUN/TURN server configurations.

2.  **Header File Structure (`webrtc_sender.h`):**
    ```cpp
    #ifndef WEBRTC_SENDER_H
    #define WEBRTC_SENDER_H

    #include "network_sender.h" // INetworkSender interface
    #include "configuration/audio_engine_config_types.h" // For SinkConfig, WebRTCConfigCpp
    #include "audio_types.h" // For AudioFormatDetails (though less used for MP3 raw send)

    #include <rtc/peerconnection.hpp>
    #include <rtc/datachannel.hpp>
    #include <rtc/configuration.hpp>

    #include <string>
    #include <memory> // For std::shared_ptr
    #include <functional> // For std::function

    class WebRTCSender : public INetworkSender, public std::enable_shared_from_this<WebRTCSender> {
    public:
        WebRTCSender(const std::string& sink_id, 
                     std::function<void(const std::string& sink_id, const std::string& type, const std::string& message)> signaling_callback);
        ~WebRTCSender() override;

        // INetworkSender interface implementation
        // initial_format_details might be less relevant here if only MP3 is sent.
        bool initialize(const SinkConfig& config, const AudioFormatDetails& initial_format_details) override; 
        
        // send_audio_packet might be a no-op or error for WebRTC if only MP3 is supported
        void send_audio_packet(const uint8_t* payload, size_t payload_len, const AudioFormatDetails& format, uint32_t rtp_timestamp) override;
        
        void send_mp3_frame(const uint8_t* mp3_data, size_t data_len, uint32_t rtp_timestamp) override; // rtp_timestamp might be unused if not embedding in data
        void stop() override;

        // Method for AudioManager to push signaling messages from Python/SIP to this sender
        void process_signaling_message(const std::string& type, const std::string& sdp_or_candidate);

    private:
        void setup_peer_connection_callbacks();
        void create_data_channel();

        std::string sink_id_;
        std::function<void(const std::string& sink_id, const std::string& type, const std::string& message)> signaling_callback_;
        
        rtc::Configuration rtc_config_;
        std::shared_ptr<rtc::PeerConnection> peer_connection_;
        std::shared_ptr<rtc::DataChannel> data_channel_;

        bool initialized_;
        std::atomic<bool> data_channel_open_; // Atomic for thread safety with callbacks
    };

    #endif // WEBRTC_SENDER_H
    ```
    *   `std::enable_shared_from_this` is useful if `libdatachannel` callbacks need a `shared_ptr` to `this`.

3.  **Constructor (`WebRTCSender::WebRTCSender`) (`webrtc_sender.cpp` - new file):**
    *   Initialize members: `sink_id_`, `signaling_callback_`, `initialized_ = false;`, `data_channel_open_ = false;`.
    *   The `signaling_callback_` is crucial for sending SDP/ICE candidates upwards.

4.  **Destructor (`WebRTCSender::~WebRTCSender`) (`webrtc_sender.cpp`):**
    *   Call `stop()` to clean up resources.

5.  **`initialize()` Method (`rtp_sender.cpp`):**
    *   Populate `rtc_config_` from `config.webrtc_config` (STUN/TURN servers).
        ```cpp
        // Example:
        // for (const auto& stun_server_url : config.webrtc_config.stun_servers) {
        //    rtc_config_.iceServers.emplace_back(stun_server_url);
        // }
        // for (const auto& turn_config : config.webrtc_config.turn_servers) {
        //    rtc::IceServer turn(turn_config.username, turn_config.credential);
        //    for (const auto& url : turn_config.urls) {
        //        turn.addUrl(url);
        //    }
        //    rtc_config_.iceServers.push_back(turn);
        // }
        ```
    *   Create `peer_connection_ = std::make_shared<rtc::PeerConnection>(rtc_config_);`.
    *   Call `setup_peer_connection_callbacks()`.
    *   Call `create_data_channel()`.
    *   Set `initialized_ = true;`.
    *   **Important:** `libdatachannel` typically expects the application to initiate the offer if it's the "caller". In this model, ScreamRouter (server-side) often acts as the callee, receiving an offer from the web client. The `initialize` method here sets up the PC, but the actual connection handshake (offer/answer, ICE) is driven by `process_signaling_message`.

6.  **`setup_peer_connection_callbacks()` Method (`webrtc_sender.cpp`):**
    *   Set up `libdatachannel` callbacks on `peer_connection_`:
        *   `peer_connection_->onLocalDescription([this](rtc::Description sdp) { ... });` -> Use `signaling_callback_` to send SDP (offer/answer) to `AudioManager`.
        *   `peer_connection_->onLocalCandidate([this](rtc::Candidate candidate) { ... });` -> Use `signaling_callback_` to send ICE candidate to `AudioManager`.
        *   `peer_connection_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) { ... });` -> If ScreamRouter is *receiving* a data channel (less common for this sender-focused class, but good to be aware of).
        *   `peer_connection_->onStateChange([](rtc::PeerConnection::State state) { ... });` -> Log state changes (Connected, Disconnected, Failed).
        *   `peer_connection_->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) { ... });` -> Log ICE gathering state.

7.  **`create_data_channel()` Method (`webrtc_sender.cpp`):**
    *   If ScreamRouter is initiating the data channel (e.g., if it always creates one named "mp3"):
        *   `data_channel_ = peer_connection_->createDataChannel("mp3");`
        *   Set up callbacks for the `data_channel_`:
            *   `data_channel_->onOpen([this]() { data_channel_open_ = true; ... });`
            *   `data_channel_->onClosed([this]() { data_channel_open_ = false; ... });`
            *   `data_channel_->onError([this](std::string err) { ... });`
            *   `data_channel_->onMessage([](auto data) { ... });` (Likely not used for sending-only channel).

8.  **`stop()` Method (`webrtc_sender.cpp`):**
    *   Close `data_channel_` if open and not null.
    *   Close `peer_connection_` if not null.
    *   Set `initialized_ = false;`, `data_channel_open_ = false;`.
    *   Clear shared pointers: `data_channel_.reset(); peer_connection_.reset();`.

## Code Alterations:

*   **New File:** `src/audio_engine/webrtc_sender.h` - Define `WebRTCSender`.
*   **New File:** `src/audio_engine/webrtc_sender.cpp` - Implement constructor, destructor, `initialize`, `stop`, `setup_peer_connection_callbacks`, `create_data_channel`. (Signaling and sending methods in next sub-tasks).
*   **File:** `src/audio_engine/CMakeLists.txt` - Add `webrtc_sender.cpp` to sources.
*   **File:** `src/configuration/audio_engine_config_types.h` (`SinkConfig` and `WebRTCConfigCpp`):
    *   Define `struct WebRTCTURNServerCpp { std::vector<std::string> urls; std::string username; std::string credential; };`
    *   Define `struct WebRTCConfigCpp { std::vector<std::string> stun_servers; std::vector<WebRTCTURNServerCpp> turn_servers; };`
    *   Add `WebRTCConfigCpp webrtc_config;` to `SinkConfig`.
*   **File:** `src/audio_engine/audio_manager.h` (and `.cpp`):
    *   The `signaling_callback_` passed to `WebRTCSender` will likely originate from or pass through `AudioManager` to bridge to Python.

## Recommendations:

*   **Thread Safety:** Callbacks from `libdatachannel` can occur on different threads. Ensure that any shared state accessed by these callbacks (like `data_channel_open_`) is protected (e.g., using `std::atomic` or mutexes if more complex state is involved) or that operations are marshaled to a specific thread if necessary.
*   **Signaling Flow:** The exact signaling flow (who makes the offer, who makes the answer) needs to be clear. Typically, the web client (browser) will create an offer, send it via SIP to ScreamRouter. ScreamRouter's `SipManager` (Python) relays it to `AudioManager` (C++), which then calls `WebRTCSender::process_signaling_message()`. The `WebRTCSender` sets the remote description, generates an answer, and sends it back up the chain.
*   **Error Handling:** Implement robust error handling for all `libdatachannel` API calls and callback events.
*   **`std::enable_shared_from_this`:** This is useful because `libdatachannel` callbacks are often lambdas that capture `this`. If these lambdas outlive the `WebRTCSender` instance or are stored by `libdatachannel` internally, using `shared_from_this()` ensures the object remains alive.

## Acceptance Criteria:

*   `webrtc_sender.h` defines the `WebRTCSender` class structure.
*   Constructor, destructor, `initialize()`, `stop()`, and basic `libdatachannel` object setup methods are implemented.
*   `WebRTCSender` can be initialized with `SinkConfig` data (STUN/TURN servers).
*   Callbacks for local SDP and ICE candidates are set up to use the provided `signaling_callback_`.
*   A data channel can be created (though not necessarily open yet).
*   The project compiles with the new `WebRTCSender` files.
