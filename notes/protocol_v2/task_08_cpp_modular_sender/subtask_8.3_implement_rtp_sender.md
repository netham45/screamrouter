# Sub-Task 8.3: Implement `RTPSender` using oRTP

**Objective:** Implement the `RTPSender` class, inheriting from `INetworkSender`. This class will use the oRTP library for RTP packetization and sending of both PCM and MP3 audio. Much of this logic was previously detailed in `task_01_rtp_library_integration` (Sub-Tasks 1.5 and 1.6).

**Parent Task:** [C++ Modular Network Sender Implementation](../task_08_cpp_modular_sender.md)
**Previous Sub-Task:** [Sub-Task 8.2: Implement `ScreamSender` for Legacy Protocol](./subtask_8.2_implement_scream_sender.md)
**Related Task:** [RTP Library (oRTP) Integration](../task_01_rtp_library_integration.md)

## Key Steps & Considerations:

1.  **Create/Update `rtp_sender.h` and `rtp_sender.cpp`:**
    *   Files: `src/audio_engine/rtp_sender.h` and `src/audio_engine/rtp_sender.cpp`.
    *   These files were outlined in `task_01_rtp_library_integration/subtask_1.5_define_rtp_sender_class_structure.md`. This sub-task focuses on completing their implementation.

2.  **`RTPSender` Class Definition (`rtp_sender.h` - Review/Finalize):**
    *   Ensure it inherits `INetworkSender`.
    *   Include members for `RtpSession*`, sink ID, remote address/ports, SSRC, configured payload types for PCM and MP3, and an initialization flag.
    ```cpp
    // In src/audio_engine/rtp_sender.h (ensure it matches or refines Sub-Task 1.5)
    #ifndef RTP_SENDER_H
    #define RTP_SENDER_H

    #include "network_sender.h"
    #include "audio_types.h"
    #include "configuration/audio_engine_config_types.h"
    #include "screamrouter_logger/screamrouter_logger.h"
    #include <ortp/ortp.h>
    #include <string>
    #include <vector> // For potential internal buffering if needed

    class RTPSender : public INetworkSender {
    public:
        RTPSender(const std::string& sink_id);
        ~RTPSender() override;

        bool initialize(const SinkConfig& config, const AudioFormatDetails& initial_format_details) override;
        void send_audio_packet(const uint8_t* payload, size_t payload_len, const AudioFormatDetails& format, uint32_t rtp_timestamp) override;
        void send_mp3_frame(const uint8_t* mp3_data, size_t data_len, uint32_t rtp_timestamp) override;
        void stop() override;

    private:
        bool create_ortp_session(const SinkConfig& config); // Simplified, format details can be passed to send methods
        void destroy_ortp_session();
        void send_rtp_data(const uint8_t* data, size_t len, uint8_t payload_type, bool marker_bit, uint32_t rtp_timestamp);

        std::string sink_id_;
        RtpSession* rtp_session_;
        std::string remote_ip_;
        int remote_rtp_port_;
        int remote_rtcp_port_;
        uint32_t ssrc_;
        
        uint8_t configured_pcm_payload_type_;
        uint8_t configured_mp3_payload_type_;

        bool initialized_;
    };

    #endif // RTP_SENDER_H
    ```

3.  **Implement Constructor and Destructor (`rtp_sender.cpp`):**
    *   **Constructor:** `sink_id_ = sink_id; rtp_session_ = nullptr; initialized_ = false; ssrc_ = ortp_random_get_uint32();` (Generate SSRC).
    *   **Destructor:** Call `stop()`.

4.  **Implement `initialize()` (`rtp_sender.cpp`):**
    *   Store `remote_ip_` from `config.ip`.
    *   Store `remote_rtp_port_` from `config.rtp_config.destination_port`.
    *   `remote_rtcp_port_ = remote_rtp_port_ + 1;` (Common practice, oRTP can handle this).
    *   Store `configured_pcm_payload_type_` from `config.rtp_config.payload_type_pcm`.
    *   Store `configured_mp3_payload_type_` from `config.rtp_config.payload_type_mp3`.
    *   Call `create_ortp_session(config)`.
    *   Set `initialized_ = true` on success.

5.  **Implement `create_ortp_session()` (`rtp_sender.cpp`):**
    *   `rtp_session_ = rtp_session_new(RTP_SESSION_SENDONLY);`
    *   Error check.
    *   `rtp_session_set_remote_addr_and_port(rtp_session_, remote_ip_.c_str(), remote_rtp_port_, remote_rtcp_port_);`
    *   `rtp_session_set_ssrc(rtp_session_, ssrc_);`
    *   `rtp_session_set_payload_type(rtp_session_, configured_pcm_payload_type_);` // Set a default, can be overridden per packet.
    *   `rtp_session_set_scheduling_mode(rtp_session_, false);` // We control sending rate.
    *   `rtp_session_set_blocking_mode(rtp_session_, false);` // Non-blocking send.
    *   Log success/failure.

6.  **Implement `destroy_ortp_session()` (`rtp_sender.cpp`):**
    *   If `rtp_session_`, `rtp_session_destroy(rtp_session_); rtp_session_ = nullptr;`.

7.  **Implement `stop()` (`rtp_sender.cpp`):**
    *   Call `destroy_ortp_session()`.
    *   `initialized_ = false;`.

8.  **Implement `send_rtp_data()` Helper (`rtp_sender.cpp`):**
    *   This private method was detailed in `task_01_rtp_library_integration/subtask_1.6_implement_rtp_packet_sending.md`.
    *   It creates an `mblk_t`, sets RTP headers (PT, timestamp, marker), copies payload, and uses `rtp_session_sendm_with_ts()`.
    *   **Crucial:** `rtp_session_sendm_with_ts` frees the `mblk_t`.

9.  **Implement `send_audio_packet()` (`rtp_sender.cpp`):**
    *   If not `initialized_`, return.
    *   Determine payload type: `uint8_t pt_to_use = format.rtp_payload_type != 0 ? format.rtp_payload_type : configured_pcm_payload_type_;`
    *   Call `send_rtp_data(payload, payload_len, pt_to_use, format.rtp_marker_bit, rtp_timestamp);`.

10. **Implement `send_mp3_frame()` (`rtp_sender.cpp`):**
    *   If not `initialized_`, return.
    *   Determine marker bit (e.g., always true for MP3 frames, or from `AudioFormatDetails` if that struct is extended/passed differently). For simplicity, assume `true` or a configured default for now.
    *   Call `send_rtp_data(mp3_data, data_len, configured_mp3_payload_type_, true /*marker_bit*/, rtp_timestamp);`.

## Code Alterations:

*   **`src/audio_engine/rtp_sender.h` & `src/audio_engine/rtp_sender.cpp`:** Implement all methods as described, reusing logic from Task 1 where applicable.
*   **`src/audio_engine/CMakeLists.txt`:** Ensure `rtp_sender.cpp` is listed.
*   **`src/audio_engine/audio_types.h` (`AudioFormatDetails`):**
    *   Must contain `uint8_t rtp_payload_type;`
    *   Must contain `bool rtp_marker_bit;`
*   **`src/configuration/audio_engine_config_types.h` (`SinkConfig`, `RTPConfigCpp`):**
    *   `SinkConfig` needs `std::string ip;`
    *   `RTPConfigCpp` needs `int destination_port;`, `uint8_t payload_type_pcm;`, `uint8_t payload_type_mp3;`.

## Recommendations:

*   **Timestamp Source:** The `rtp_timestamp` parameter for send methods should be provided by `SinkAudioMixer`, calculated based on the audio sample rate and the amount of audio data processed.
*   **Marker Bit for MP3:** The policy for setting the marker bit on MP3 packets should be clearly defined (e.g., always true for the first packet of a new MP3 frame or based on other criteria). The current `INetworkSender::send_mp3_frame` interface doesn't pass `AudioFormatDetails`, so the marker bit logic must be internal to `RTPSender` for MP3s or the interface needs adjustment. For now, a simple default (e.g., `true`) is used in the sketch.
*   **Error Handling:** Robustly check return values of oRTP functions and log errors.

## Acceptance Criteria:

*   `RTPSender` class is fully implemented and inherits from `INetworkSender`.
*   `initialize()` correctly sets up an oRTP session for sending based on `SinkConfig`.
*   `send_audio_packet()` sends PCM data as RTP packets with correct headers.
*   `send_mp3_frame()` sends MP3 data as RTP packets with correct headers.
*   `stop()` cleans up oRTP resources.
*   The implementation correctly uses oRTP APIs for packet creation and transmission.
