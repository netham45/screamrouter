# Sub-Task 1.5: Define `RTPSender` Class Structure for RTP Output

**Objective:** Define the C++ class structure for `RTPSender` in `rtp_sender.h` (a new file). This class will implement the `INetworkSender` interface (defined in `task_08_cpp_modular_sender.md`) and manage an oRTP session for sending RTP packets.

**Parent Task:** [RTP Library (oRTP) Integration](../task_01_rtp_library_integration.md)
**Related Task:** [C++ Modular Network Sender Implementation](../task_08_cpp_modular_sender.md) (defines `INetworkSender`)

## Key Steps & Considerations:

1.  **`INetworkSender` Interface (from `task_08_cpp_modular_sender.md`):**
    *   Ensure `network_sender.h` defining `INetworkSender` is created or planned.
    ```cpp
    // src/audio_engine/network_sender.h (Example from task_08)
    #ifndef INETWORK_SENDER_H
    #define INETWORK_SENDER_H

    #include "configuration/audio_engine_config_types.h" // For SinkConfig
    #include "audio_types.h" // For AudioFormatDetails
    #include <cstdint>
    #include <cstddef>

    class INetworkSender {
    public:
        virtual ~INetworkSender() = default;
        virtual bool initialize(const SinkConfig& config, const AudioFormatDetails& initial_format_details) = 0;
        virtual void send_audio_packet(const uint8_t* payload, size_t payload_len, const AudioFormatDetails& format, uint32_t rtp_timestamp) = 0;
        virtual void send_mp3_frame(const uint8_t* mp3_data, size_t data_len, uint32_t rtp_timestamp) = 0;
        virtual void stop() = 0;
        // virtual void update_format(const AudioFormatDetails& new_format_details) = 0; // Consider for dynamic changes
    };

    #endif // INETWORK_SENDER_H
    ```
    *   Note: `initialize` might take more specific config or format details. `send_audio_packet` and `send_mp3_frame` now include `rtp_timestamp`.

2.  **`RTPSender` Class Definition (`rtp_sender.h`):**
    *   Create a new header file `src/audio_engine/rtp_sender.h`.
    *   The class `RTPSender` will inherit from `INetworkSender`.
    *   Include necessary headers: `network_sender.h`, `ortp/ortp.h`, `audio_types.h`, `configuration/audio_engine_config_types.h`.
    *   Declare member variables:
        *   `RtpSession* rtp_session_`: Pointer to the oRTP session.
        *   `std::string sink_id_`: To identify the sink this sender is for.
        *   `std::string remote_ip_`: Destination IP address.
        *   `int remote_rtp_port_`: Destination RTP port.
        *   `int remote_rtcp_port_`: Destination RTCP port.
        *   `uint32_t ssrc_`: SSRC for the outgoing stream.
        *   `AudioFormatDetails current_pcm_format_`: To store current PCM format details (payload type, etc.).
        *   `AudioFormatDetails current_mp3_format_`: To store current MP3 format details.
        *   `uint32_t rtp_ts_offset_`: An initial RTP timestamp offset, or manage timestamps per call.

3.  **Header File Structure (`rtp_sender.h`):**
    ```cpp
    #ifndef RTP_SENDER_H
    #define RTP_SENDER_H

    #include "network_sender.h"
    #include "audio_types.h"
    #include "configuration/audio_engine_config_types.h"
    #include <ortp/ortp.h>
    #include <string>

    class RTPSender : public INetworkSender {
    public:
        RTPSender(const std::string& sink_id);
        ~RTPSender() override;

        // INetworkSender interface implementation
        bool initialize(const SinkConfig& config, const AudioFormatDetails& initial_format_details) override;
        void send_audio_packet(const uint8_t* payload, size_t payload_len, const AudioFormatDetails& format, uint32_t rtp_timestamp) override;
        void send_mp3_frame(const uint8_t* mp3_data, size_t data_len, uint32_t rtp_timestamp) override;
        void stop() override;
        // void update_format(const AudioFormatDetails& new_format_details) override; // If added to interface

    private:
        bool create_ortp_session(const SinkConfig& config, const AudioFormatDetails& initial_format_details);
        void destroy_ortp_session();
        void send_rtp_data(const uint8_t* data, size_t len, uint8_t payload_type, bool marker_bit, uint32_t rtp_timestamp);

        RtpSession* rtp_session_;
        std::string sink_id_;
        std::string remote_ip_;
        int remote_rtp_port_;
        int remote_rtcp_port_; // Often RTP port + 1
        uint32_t ssrc_;
        
        // Store configured payload types for quick access
        uint8_t pcm_payload_type_;
        uint8_t mp3_payload_type_;

        bool initialized_;
    };

    #endif // RTP_SENDER_H
    ```

4.  **Constructor (`RTPSender::RTPSender`) (`rtp_sender.cpp` - new file):**
    *   Initialize members: `rtp_session_ = nullptr;`, `initialized_ = false;`, `sink_id_ = sink_id;`.
    *   `ssrc_` can be generated randomly or taken from config. `ortp_get_time()` can be a source for randomness.

5.  **Destructor (`RTPSender::~RTPSender`) (`rtp_sender.cpp`):**
    *   Call `stop()` to ensure resources are released.

6.  **`initialize()` Method (`rtp_sender.cpp`):**
    *   Store `remote_ip_`, `remote_rtp_port_` from `config.ip` and `config.rtp_config.destination_port`.
    *   `remote_rtcp_port_` is typically `remote_rtp_port_ + 1`.
    *   Store `pcm_payload_type_` and `mp3_payload_type_` from `config.rtp_config` or `initial_format_details`.
    *   Call `create_ortp_session(config, initial_format_details)`.
    *   Set `initialized_ = true` on success.

7.  **`create_ortp_session()` Method (`rtp_sender.cpp`):**
    *   `rtp_session_ = rtp_session_new(RTP_SESSION_SENDONLY);`
    *   Error check.
    *   `rtp_session_set_remote_addr_and_port(rtp_session_, remote_ip_.c_str(), remote_rtp_port_, remote_rtcp_port_);`
    *   `rtp_session_set_ssrc(rtp_session_, ssrc_);`
    *   `rtp_session_set_payload_type(rtp_session_, initial_format_details.rtp_payload_type);` // Set initial/default PT
    *   `rtp_session_set_scheduling_mode(rtp_session_, 0);` // Not using internal scheduler for sending
    *   `rtp_session_set_blocking_mode(rtp_session_, 0);` // Not using blocking send
    *   Set other parameters like jitter compensation (though less critical for send-only).

8.  **`destroy_ortp_session()` Method (`rtp_sender.cpp`):**
    *   If `rtp_session_`, call `rtp_session_destroy(rtp_session_);` and set to `nullptr`.

9.  **`stop()` Method (`rtp_sender.cpp`):**
    *   Call `destroy_ortp_session()`.
    *   Set `initialized_ = false;`.

## Code Alterations:

*   **New File:** `src/audio_engine/rtp_sender.h` - Define the `RTPSender` class.
*   **New File:** `src/audio_engine/rtp_sender.cpp` - Implement constructor, destructor, `initialize`, `stop`, `create_ortp_session`, `destroy_ortp_session`. (Sending methods in next sub-task).
*   **File:** `src/audio_engine/network_sender.h` - Create or ensure this file exists with the `INetworkSender` interface.
*   **File:** `src/audio_engine/CMakeLists.txt` - Add `rtp_sender.cpp` and `network_sender.cpp` (if `INetworkSender` has an implementation file) to sources.
*   **File:** `src/configuration/audio_engine_config_types.h` (`SinkConfig` and `RTPConfig` within it)
    *   Ensure `SinkConfig` has `std::string ip;` (for destination).
    *   Ensure `RTPConfig` (nested in `SinkConfig`) has `int destination_port;`, `int payload_type_pcm;`, `int payload_type_mp3;`.
*   **File:** `src/audio_engine/audio_types.h` (`AudioFormatDetails`)
    *   Ensure `AudioFormatDetails` has `uint8_t rtp_payload_type;` and `bool rtp_marker_bit;`.

## Recommendations:

*   **SSRC Generation:** Generate a unique SSRC for each `RTPSender` instance. `ortp_random_get_uint32()` is a utility for this.
*   **Payload Types:** The sender needs to know which RTP payload type to use for PCM and which for MP3. This should come from `SinkConfig.rtp_config` or be negotiated via SDP and then updated.
*   **Timestamp Management:** RTP timestamps are crucial. The `send_audio_packet` and `send_mp3_frame` methods will receive an `rtp_timestamp` parameter. This timestamp should directly correspond to the sampling instant of the audio data.
*   **Modularity:** This structure prepares for `SinkAudioMixer` to instantiate an `RTPSender` when a sink is configured for RTP.

## Acceptance Criteria:

*   `rtp_sender.h` defines the `RTPSender` class inheriting `INetworkSender`.
*   Constructor, destructor, `initialize()`, `stop()`, and oRTP session management methods are implemented.
*   `RTPSender` can be initialized with `SinkConfig` data, creating and configuring an oRTP session for sending.
*   The project compiles with the new `RTPSender` files and updated interface.
