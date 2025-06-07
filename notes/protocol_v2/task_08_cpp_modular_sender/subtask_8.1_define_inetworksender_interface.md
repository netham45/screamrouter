# Sub-Task 8.1: Define `INetworkSender` Interface

**Objective:** Define the abstract base class or interface `INetworkSender` in `src/audio_engine/network_sender.h`. This interface will declare the common methods for initializing, sending audio data (PCM and MP3), and stopping network senders for different protocols.

**Parent Task:** [C++ Modular Network Sender Implementation](../task_08_cpp_modular_sender.md)

## Key Steps & Considerations:

1.  **Create `src/audio_engine/network_sender.h`:**
    *   This new header file will contain the definition of the `INetworkSender` interface.
    *   Include necessary headers for types used in the interface methods, such as `SinkConfig` and `AudioFormatDetails`.

2.  **`INetworkSender` Interface Definition:**
    ```cpp
    // In src/audio_engine/network_sender.h
    #ifndef INETWORK_SENDER_H
    #define INETWORK_SENDER_H

    #include <cstdint> // For uint8_t, uint32_t
    #include <cstddef> // For size_t
    #include <string>  // For std::string in method signatures if needed, or for IDs

    // Forward declarations to avoid circular dependencies if full definitions are heavy
    // Actual includes will be in .cpp files or if members require full type.
    struct SinkConfig;        // From "configuration/audio_engine_config_types.h"
    struct AudioFormatDetails; // From "audio_types.h"

    class INetworkSender {
    public:
        // Virtual destructor is crucial for base classes with virtual methods
        virtual ~INetworkSender() = default;

        /**
         * @brief Initializes the network sender with the given sink configuration and initial audio format.
         * @param config The configuration for the sink this sender is associated with.
         * @param initial_format_details Details about the audio format to be initially expected or configured.
         *                               This might be more relevant for RTP/WebRTC than legacy Scream.
         * @return True if initialization was successful, false otherwise.
         */
        virtual bool initialize(const SinkConfig& config, const AudioFormatDetails& initial_format_details) = 0;

        /**
         * @brief Sends a packet of PCM audio data.
         * @param payload Pointer to the raw PCM audio data.
         * @param payload_len Length of the audio data in bytes.
         * @param format Details of the current audio packet's format (sample rate, channels, bit depth, RTP marker/PT).
         * @param rtp_timestamp The RTP timestamp for this audio packet (relevant for RTP/WebRTC).
         */
        virtual void send_audio_packet(const uint8_t* payload, 
                                       size_t payload_len, 
                                       const AudioFormatDetails& format, 
                                       uint32_t rtp_timestamp) = 0;

        /**
         * @brief Sends a pre-encoded MP3 frame.
         * @param mp3_data Pointer to the MP3 frame data.
         * @param data_len Length of the MP3 data in bytes.
         * @param rtp_timestamp The RTP timestamp for this MP3 frame (relevant for RTP/WebRTC MP3 streaming).
         *                      For WebRTC data channels, this might be used for sequencing or logging if not directly in protocol.
         */
        virtual void send_mp3_frame(const uint8_t* mp3_data, 
                                    size_t data_len, 
                                    uint32_t rtp_timestamp) = 0;
        
        /**
         * @brief Stops the network sender and releases any resources.
         * This method should ensure that any ongoing sending operations are ceased
         * and network resources (sockets, sessions) are cleaned up.
         */
        virtual void stop() = 0;

        /**
         * @brief (Optional Future Enhancement) Updates the sending format dynamically.
         * Useful if, for example, SDP renegotiation changes payload types or codecs mid-stream.
         * @param new_format_details The new audio format details.
         */
        // virtual void update_format(const AudioFormatDetails& new_format_details) = 0;

        /**
         * @brief (Optional) Returns the unique ID of the sink this sender is for.
         * Can be useful for logging or management.
         * @return The sink ID string.
         */
        // virtual const std::string& get_sink_id() const = 0; 
    };

    #endif // INETWORK_SENDER_H
    ```

3.  **Method Signatures and Parameters:**
    *   **`virtual ~INetworkSender() = default;`**: Essential for proper cleanup when dealing with polymorphism.
    *   **`initialize(const SinkConfig& config, const AudioFormatDetails& initial_format_details)`**:
        *   `SinkConfig` provides all necessary configuration for the sink, including protocol type, IP/port, and nested protocol-specific configs (RTP, WebRTC).
        *   `initial_format_details` can provide baseline audio format info, especially for RTP/WebRTC payload type setup if not fully dynamic from day one.
    *   **`send_audio_packet(const uint8_t* payload, size_t payload_len, const AudioFormatDetails& format, uint32_t rtp_timestamp)`**:
        *   For sending raw PCM data.
        *   `AudioFormatDetails` provides current packet's specifics (sample rate, channels, bit depth, and for RTP: `rtp_payload_type`, `rtp_marker_bit`).
        *   `rtp_timestamp` is crucial for RTP/SRTP senders. Legacy Scream sender might ignore it or use it to derive its internal timing.
    *   **`send_mp3_frame(const uint8_t* mp3_data, size_t data_len, uint32_t rtp_timestamp)`**:
        *   Specifically for sending pre-encoded MP3 frames.
        *   `rtp_timestamp` is relevant for RTP/WebRTC MP3 streaming.
    *   **`stop()`**: For cleanup and resource release.
    *   **`update_format` (Commented out - Future):** Could be added later if dynamic mid-stream format changes (e.g., due to SDP re-INVITE) need to be supported without recreating the sender.
    *   **`get_sink_id` (Commented out - Optional):** Could be useful for logging or if `SinkAudioMixer` needs to identify its sender.

## Code Alterations:

*   **New File:** `src/audio_engine/network_sender.h` - Create this file and add the `INetworkSender` interface definition.
*   **Dependent Files:**
    *   `src/audio_engine/audio_types.h`: Ensure `AudioFormatDetails` is well-defined and includes fields like `rtp_payload_type` and `rtp_marker_bit`.
    *   `src/configuration/audio_engine_config_types.h`: Ensure `SinkConfig` is defined and includes `protocol_type` and nested configs like `RTPConfigCpp`, `WebRTCConfigCpp`.

## Recommendations:

*   **Pure Virtual Functions:** Using `= 0` makes `INetworkSender` an abstract base class, ensuring derived classes implement all interface methods.
*   **Const Correctness:** Use `const` where appropriate in method parameters (e.g., `const SinkConfig&`).
*   **Parameter Granularity:**
    *   The `AudioFormatDetails` struct passed to `send_audio_packet` allows flexibility if format details change per packet or block, though often they might be consistent for a stream segment.
    *   The `rtp_timestamp` is explicitly passed to allow the caller (e.g., `SinkAudioMixer`) to manage the precise timing based on the audio data being processed.
*   **MP3 Sending:** The `send_mp3_frame` method is distinct because MP3 data is already encoded and might have different packetization or header requirements (e.g., for RTP marker bit logic) compared to raw PCM.

## Acceptance Criteria:

*   `src/audio_engine/network_sender.h` is created and contains the `INetworkSender` interface definition.
*   The interface includes pure virtual methods for `initialize`, `send_audio_packet`, `send_mp3_frame`, and `stop`.
*   Method signatures use appropriate types from `audio_types.h` and `audio_engine_config_types.h`.
*   The interface includes a virtual destructor.
*   The project code referencing these types compiles (though concrete implementations are pending).
