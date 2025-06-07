# Sub-Task 1.2: Define `RtpReceiver` Class Structure and oRTP Session Management

**Objective:** Define the C++ class structure for `RtpReceiver` in `rtp_receiver.h`, including member variables for managing an oRTP session and method signatures for initialization, packet reception, and processing. This class will inherit from `NetworkAudioReceiver`.

**Parent Task:** [RTP Library (oRTP) Integration](../task_01_rtp_library_integration.md)
**Previous Sub-Task:** [Sub-Task 1.1: Global oRTP Initialization and Shutdown in AudioManager](./subtask_1.1_ortp_global_init_shutdown.md)

## Key Steps & Considerations:

1.  **`RtpReceiver` Class Definition (`rtp_receiver.h`):**
    *   Inherit from `NetworkAudioReceiver`.
    *   Include necessary oRTP headers (e.g., `ortp/ortp.h`).
    *   Declare member variables:
        *   `RtpSession* rtp_session_`: Pointer to the oRTP session object. (Note: `ortp_session_t` is the C-style typedef for `struct _RtpSession`, so `RtpSession*` is idiomatic in C++ if using oRTP's C API directly).
        *   `std::string source_id_`: To identify the source this receiver is for.
        *   `int local_port_`: The local port for receiving RTP.
        *   `int local_rtcp_port_`: The local port for receiving RTCP (oRTP often handles this paired with RTP port).
        *   Potentially a map or structure to hold SSRC-to-audio-format mappings once SDP is integrated.
    *   Declare constructor and destructor.
    *   Declare methods:
        *   `initialize(const SourceConfig& config, std::shared_ptr<TimeshiftManager> timeshift_manager, const std::string& source_id)`: To set up the oRTP session. (Adjust parameters as needed based on `NetworkAudioReceiver` base or specific needs).
        *   Override `run()` from `NetworkAudioReceiver` for the packet receiving loop.
        *   Override `stop_receiving()` from `NetworkAudioReceiver`.
        *   `process_received_packet(mblk_t* rtp_packet)`: Helper to process a raw RTP packet from oRTP. (This might replace/adapt `process_and_validate_payload`).

2.  **Header File Structure (`rtp_receiver.h`):**
    ```cpp
    #ifndef RTP_RECEIVER_H
    #define RTP_RECEIVER_H

    #include "network_audio_receiver.h" // Assuming this is the base class
    #include "audio_types.h" // For TaggedAudioPacket, AudioFormatDetails
    #include "configuration/audio_engine_config_types.h" // For SourceConfig
    #include <ortp/ortp.h> // oRTP main header
    #include <string>
    #include <memory> // For std::shared_ptr

    // Forward declaration if TimeshiftManager is in a different header and only pointer/ref is used
    class TimeshiftManager; 

    class RtpReceiver : public NetworkAudioReceiver {
    public:
        RtpReceiver(const SourceConfig& config, 
                      std::shared_ptr<TimeshiftManager> timeshift_manager, 
                      const std::string& source_id);
        ~RtpReceiver() override;

        // Initialization method to set up oRTP session
        bool initialize_receiver(); 

        // Override virtual methods from NetworkAudioReceiver
        void run() override;
        void stop_receiving() override;

    private:
        // oRTP session management
        RtpSession* rtp_session_; // ortp_session_t* is equivalent
        bool create_ortp_session();
        void destroy_ortp_session();

        // Packet processing
        void process_received_packet(mblk_t* rtp_packet_block);
        // Adapts/replaces process_and_validate_payload and is_valid_packet_structure

        // Configuration and state
        const SourceConfig& config_; // Store reference to config
        std::shared_ptr<TimeshiftManager> timeshift_manager_;
        std::string source_id_;
        int local_rtp_port_;
        int local_rtcp_port_; // oRTP usually handles this +1 from rtp_port

        // Default audio format if SDP is not available (for initial implementation)
        AudioFormatDetails default_audio_format_; 

        // TODO: Add SSRC to AudioFormatDetails mapping for dynamic format changes via SDP
        // std::map<uint32_t, AudioFormatDetails> ssrc_format_map_;
    };

    #endif // RTP_RECEIVER_H
    ```

3.  **Constructor (`RtpReceiver::RtpReceiver`) (`rtp_receiver.cpp`):**
    *   Initialize member variables, including `rtp_session_` to `nullptr`.
    *   Store `config`, `timeshift_manager`, `source_id`.
    *   Determine `local_rtp_port_` and `local_rtcp_port_` from `config_`. RTCP is often RTP port + 1.
    *   Initialize `default_audio_format_` (e.g., 16-bit, 48kHz, stereo, specific payload type).

4.  **Destructor (`RtpReceiver::~RtpReceiver`) (`rtp_receiver.cpp`):**
    *   Ensure `stop_receiving()` is called.
    *   Call `destroy_ortp_session()` to clean up the oRTP session and free resources.

5.  **`initialize_receiver()` Method (`rtp_receiver.cpp`):**
    *   This method will call `create_ortp_session()`.
    *   It can be called after construction.

6.  **`create_ortp_session()` Method (`rtp_receiver.cpp`):**
    *   Creates a new oRTP session: `rtp_session_ = rtp_session_new(RTP_SESSION_RECVONLY);`
    *   Error check: if `rtp_session_` is `nullptr`, log error and return `false`.
    *   Set local address: `rtp_session_set_local_addr(rtp_session_, "0.0.0.0", local_rtp_port_, local_rtcp_port_);` (Note: `local_rtcp_port_` can often be -1 for oRTP to auto-select based on RTP port).
    *   Set payload types:
        *   Initially, set a default payload type for Scream-like PCM data if SDP is not yet handled: `rtp_session_set_payload_type_number(rtp_session_, default_audio_format_.rtp_payload_type);`
        *   Later, this will be more dynamic based on SDP.
    *   Set other session parameters as needed (e.g., jitter compensation, SSRC handling). Example: `rtp_session_set_symmetric_rtp(rtp_session_, TRUE);`
    *   Log success or failure.

7.  **`destroy_ortp_session()` Method (`rtp_receiver.cpp`):**
    *   If `rtp_session_` is not `nullptr`, call `rtp_session_destroy(rtp_session_);`
    *   Set `rtp_session_` to `nullptr`.

## Code Alterations:

*   **New/Modified File:** `src/audio_engine/rtp_receiver.h`
    *   Define the `RtpReceiver` class structure as outlined above.
*   **New/Modified File:** `src/audio_engine/rtp_receiver.cpp`
    *   Implement the constructor, destructor, `initialize_receiver()`, `create_ortp_session()`, and `destroy_ortp_session()` methods. The `run()` and `process_received_packet()` methods will be detailed in subsequent sub-tasks.
*   **File:** `src/audio_engine/CMakeLists.txt`
    *   Add `rtp_receiver.cpp` to the list of sources for the `audio_engine_python` target.

## Recommendations:

*   **Error Handling:** Robust error checking after each oRTP call is crucial. Log errors clearly.
*   **Configuration:** `SourceConfig` (from `audio_engine_config_types.h`) will need to provide the necessary parameters for `RtpReceiver`, such as the expected local RTP port and potentially default payload type information if not using SDP.
*   **Base Class `NetworkAudioReceiver`:** Review the existing `NetworkAudioReceiver` interface to ensure `RtpReceiver` can correctly override and implement its functionality using oRTP. The `socket_fd_` and direct socket operations in the base class might become unused or managed differently for `RtpReceiver`.
*   **RTCP Port:** oRTP typically handles the RTCP port automatically (RTP port + 1). Passing -1 as the RTCP port to `rtp_session_set_local_addr` often enables this.

## Acceptance Criteria:

*   `RtpReceiver.h` defines the class structure, inheriting from `NetworkAudioReceiver` and including oRTP session members.
*   Constructor, destructor, and oRTP session creation/destruction methods are implemented in `RtpReceiver.cpp`.
*   The class can be instantiated, and an oRTP session can be created and configured for receiving on a specified port.
*   The project compiles with the new/modified `RtpReceiver` files.
