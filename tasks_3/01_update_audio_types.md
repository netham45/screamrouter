# Task 1: Update Audio Types

**Goal**: Define necessary configuration structs and enums in `src/audio_engine/audio_types.h` to support the new `RawScreamReceiver` and differentiate input protocols for `SourceInputProcessor`.

**Files to Modify**:
*   `src/audio_engine/audio_types.h`

**Steps**:

1.  **Define `RawScreamReceiverConfig`**: Add a new struct similar to `RtpReceiverConfig` but potentially simpler if only the listen port is needed.
    ```cpp
    // Configuration for RawScreamReceiver component
    struct RawScreamReceiverConfig {
        int listen_port = 4010; // Default port (adjust if needed)
        // std::string bind_ip = "0.0.0.0"; // Optional: Interface IP to bind to
    };
    ```

2.  **Define `InputProtocolType` Enum**: Add an enum to distinguish between RTP payload and raw Scream packets.
    ```cpp
    // Enum to specify the expected input data format for a SourceInputProcessor
    enum class InputProtocolType {
        RTP_SCREAM_PAYLOAD, // Expects raw PCM audio data (RTP payload)
        RAW_SCREAM_PACKET   // Expects full 5-byte header + PCM audio data
    };
    ```

3.  **Add `InputProtocolType` to `SourceProcessorConfig`**: Add a member to `SourceProcessorConfig` to store the expected protocol type. Initialize it to a default (e.g., RTP).
    ```cpp
    // Configuration for SourceInputProcessor component
    struct SourceProcessorConfig {
        // ... existing members ...
        InputProtocolType protocol_type = InputProtocolType::RTP_SCREAM_PAYLOAD; // Add this line

        // ... existing constructor ...
    };
    ```

4.  **Add Protocol Type Hint to `SourceConfig`**: Add a corresponding field to `SourceConfig` so that the configuration source (Python layer) can specify the type. This might require a simple `int` or `string` initially if binding the enum directly is complex, or bind the enum later. Let's use an int for now (0=RTP, 1=RAW).
    ```cpp
    struct SourceConfig {
        // ... existing members ...
        int protocol_type_hint = 0; // Add this line (0 for RTP_SCREAM_PAYLOAD, 1 for RAW_SCREAM_PACKET)
    };
