# Sub-Task 8.2: Implement `ScreamSender` for Legacy Protocol

**Objective:** Implement the `ScreamSender` class, inheriting from `INetworkSender`. This class will encapsulate the existing UDP sending logic for the legacy Scream protocol, including Scream header construction.

**Parent Task:** [C++ Modular Network Sender Implementation](../task_08_cpp_modular_sender.md)
**Previous Sub-Task:** [Sub-Task 8.1: Define `INetworkSender` Interface](./subtask_8.1_define_inetworksender_interface.md)

## Key Steps & Considerations:

1.  **Create `scream_sender.h` and `scream_sender.cpp`:**
    *   **New Files:** `src/audio_engine/scream_sender.h` and `src/audio_engine/scream_sender.cpp`.
    *   `scream_sender.h` will define the `ScreamSender` class.
    *   `scream_sender.cpp` will implement its methods.

2.  **`ScreamSender` Class Definition (`scream_sender.h`):**
    ```cpp
    // In src/audio_engine/scream_sender.h
    #ifndef SCREAM_SENDER_H
    #define SCREAM_SENDER_H

    #include "network_sender.h" // INetworkSender interface
    #include "utils/network_utils.h" // For Socket, Address (or similar existing network utilities)
    #include "screamrouter_logger/screamrouter_logger.h" // For logging
    #include <vector> // For buffer

    // Forward declarations from existing code, if necessary
    // struct SinkConfig; (already forward declared in INetworkSender.h)
    // struct AudioFormatDetails; (already forward declared in INetworkSender.h)

    class ScreamSender : public INetworkSender {
    public:
        ScreamSender(const std::string& sink_id);
        ~ScreamSender() override;

        // INetworkSender interface implementation
        bool initialize(const SinkConfig& config, const AudioFormatDetails& initial_format_details) override;
        void send_audio_packet(const uint8_t* payload, 
                               size_t payload_len, 
                               const AudioFormatDetails& format, 
                               uint32_t rtp_timestamp) override; // rtp_timestamp might be ignored
        void send_mp3_frame(const uint8_t* mp3_data, 
                            size_t data_len, 
                            uint32_t rtp_timestamp) override; // Likely a no-op or error for Scream
        void stop() override;

        // const std::string& get_sink_id() const override { return sink_id_; }

    private:
        void construct_scream_header(uint8_t* header_buffer, const AudioFormatDetails& format);

        std::string sink_id_;
        Socket_simple udp_socket_; // Assuming Socket_simple or similar from network_utils.h
        Address_simple dest_address_; // Assuming Address_simple or similar
        bool initialized_;
        
        // Buffer for Scream packet (header + payload)
        std::vector<uint8_t> send_buffer_; 
        static const size_t SCREAM_HEADER_SIZE = 5;
        // static const size_t SCREAM_PAYLOAD_SIZE = 1152; // If fixed, but send_audio_packet takes variable len
    };

    #endif // SCREAM_SENDER_H
    ```

3.  **Implement Constructor and Destructor (`scream_sender.cpp`):**
    *   **Constructor:** Initialize `sink_id_`, `initialized_ = false;`.
    *   **Destructor:** Call `stop()` to ensure socket is closed.

4.  **Implement `initialize()` (`scream_sender.cpp`):**
    *   Takes `SinkConfig`.
    *   Extract destination IP and port from `config.ip` and `config.port` (legacy fields).
    *   Create and configure `udp_socket_` (e.g., `udp_socket_.Create(false)` for non-blocking or blocking based on existing logic).
    *   Set up `dest_address_` using the IP and port.
    *   Set `initialized_ = true;`.
    *   Return `true` on success, `false` on failure (e.g., socket creation error).
    *   The `initial_format_details` might be ignored if not relevant for legacy Scream setup.

5.  **Implement `construct_scream_header()` (`scream_sender.cpp`):**
    *   Private helper method.
    *   Takes `AudioFormatDetails` (containing `sample_rate`, `bit_depth`, `channels`, `chlayout1`, `chlayout2`).
    *   Populates a 5-byte buffer with the Scream header based on these details. This logic should be migrated from `SinkAudioMixer::send_network_buffer` or wherever it currently resides.
    ```cpp
    // Example logic (needs to match existing Scream header format precisely)
    // void ScreamSender::construct_scream_header(uint8_t* header_buffer, const AudioFormatDetails& format) {
    //     // Byte 0: Samplerate Divisor and 44.1kHz base flag
    //     bool is_44100_base = (format.sample_rate % 11025 == 0);
    //     uint8_t samplerate_divisor = 0;
    //     if (is_44100_base) {
    //         samplerate_divisor = static_cast<uint8_t>(format.sample_rate / 11025);
    //     } else { // Assume 48000 base
    //         samplerate_divisor = static_cast<uint8_t>(format.sample_rate / 12000); // Or /6000 if rates are lower
    //     }
    //     header_buffer[0] = (is_44100_base ? 0x80 : 0x00) | (samplerate_divisor & 0x7F);

    //     // Byte 1: Bit Depth
    //     header_buffer[1] = static_cast<uint8_t>(format.bit_depth);
    //     // Byte 2: Channels
    //     header_buffer[2] = static_cast<uint8_t>(format.channels);
    //     // Byte 3 & 4: Channel Layout (chlayout1, chlayout2 from AudioFormatDetails)
    //     header_buffer[3] = format.chlayout1;
    //     header_buffer[4] = format.chlayout2;
    // }
    ```

6.  **Implement `send_audio_packet()` (`scream_sender.cpp`):**
    *   If not `initialized_`, return.
    *   Prepare `send_buffer_`: resize to `SCREAM_HEADER_SIZE + payload_len`.
    *   Call `construct_scream_header()` to fill the first 5 bytes of `send_buffer_`.
    *   Copy `payload` into `send_buffer_` after the header.
    *   Use `udp_socket_.SendTo(dest_address_, send_buffer_.data(), send_buffer_.size())`.
    *   Log success or errors.
    *   The `rtp_timestamp` parameter is likely ignored for legacy Scream.

7.  **Implement `send_mp3_frame()` (`scream_sender.cpp`):**
    *   This method is not applicable to the legacy Scream protocol, which only sends raw PCM.
    *   Implementation should either:
        *   Log an error: `screamrouter_logger::error("ScreamSender does not support sending MP3 frames.");`
        *   Be a no-op.
        *   Throw an exception if called.

8.  **Implement `stop()` (`scream_sender.cpp`):**
    *   Close `udp_socket_` if it's open.
    *   Set `initialized_ = false;`.

## Code Alterations:

*   **New Files:** `src/audio_engine/scream_sender.h`, `src/audio_engine/scream_sender.cpp`.
*   **`src/audio_engine/CMakeLists.txt`:** Add `scream_sender.cpp` to the sources.
*   **`src/audio_engine/audio_types.h`:** Ensure `AudioFormatDetails` contains `chlayout1` and `chlayout2` if they are not already there.
*   **`src/utils/network_utils.h` (and `.cpp`):** Ensure `Socket_simple` and `Address_simple` (or equivalent existing UDP socket utilities) are suitable and available. If they don't exist, they would need to be created or adapted from existing socket code in `SinkAudioMixer`.

## Recommendations:

*   **Migrate Existing Logic:** Carefully migrate the exact Scream header creation and UDP sending logic from `SinkAudioMixer` (or its current location) to `ScreamSender` to ensure backward compatibility.
*   **Network Utilities:** Leverage existing network utility classes if possible (like `Socket_simple`). If these are not robust or suitable, this might be an opportunity to improve them or use a more standard socket wrapper.
*   **Error Handling:** Implement proper error handling for socket operations (creation, send).

## Acceptance Criteria:

*   `ScreamSender` class is implemented, inheriting from `INetworkSender`.
*   `initialize()` correctly sets up the UDP socket and destination address from `SinkConfig`.
*   `send_audio_packet()` correctly constructs the 5-byte Scream header and sends the PCM payload over UDP.
*   `send_mp3_frame()` is a no-op or logs an error, as Scream protocol doesn't support MP3.
*   `stop()` closes the socket and cleans up resources.
*   The implementation is functionally equivalent to the legacy Scream sending logic it replaces.
