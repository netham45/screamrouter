# Sub-Task 1.6: Implement RTP Packet Sending Logic in `RTPSender`

**Objective:** Implement the `send_audio_packet()` and `send_mp3_frame()` methods in `RTPSender` to construct and send RTP packets using the oRTP session. This includes setting RTP headers (payload type, timestamp, marker bit) and sending the payload.

**Parent Task:** [RTP Library (oRTP) Integration](../task_01_rtp_library_integration.md)
**Previous Sub-Task:** [Sub-Task 1.5: Define `RTPSender` Class Structure for RTP Output](./subtask_1.5_define_rtp_sender_class_structure.md)

## Key Steps & Considerations:

1.  **Helper Method `send_rtp_data()` (`rtp_sender.cpp`):**
    *   Create a private helper method to encapsulate the common logic for creating and sending an RTP packet.
    ```cpp
    // In src/audio_engine/rtp_sender.cpp
    void RTPSender::send_rtp_data(const uint8_t* data, size_t len, 
                                  uint8_t payload_type, bool marker_bit, 
                                  uint32_t rtp_timestamp) {
        if (!initialized_ || !rtp_session_) {
            screamrouter_logger::warn("RTPSender ({}): Not initialized or session closed, cannot send RTP data.", sink_id_);
            return;
        }

        // oRTP recommends using rtp_session_create_packet_with_data for simplicity if available,
        // or manual creation and then rtp_session_sendm_with_ts.
        // Let's use the manual approach for more control as shown in some oRTP examples.

        // 1. Create an mblk_t for the RTP packet
        // The size should be RTP_FIXED_HEADER_SIZE + payload length.
        // oRTP's rtp_session_create_packet will allocate an mblk_t with space for the header.
        mblk_t* rtp_packet = rtp_session_create_packet(rtp_session_, RTP_FIXED_HEADER_SIZE + len);
        if (!rtp_packet) {
            screamrouter_logger::error("RTPSender ({}): Failed to create RTP packet (mblk_t).", sink_id_);
            return;
        }

        // 2. Set RTP header fields
        rtp_set_payload_type(rtp_packet, payload_type);
        rtp_set_timestamp(rtp_packet, rtp_timestamp); 
        rtp_set_marker(rtp_packet, marker_bit ? 1 : 0);
        // Sequence number is handled internally by oRTP for the session.
        // SSRC is set on the session.

        // 3. Copy payload data into the mblk_t
        // The rtp_packet->b_wptr initially points to where payload should start after header.
        memcpy(rtp_packet->b_wptr, data, len);
        rtp_packet->b_wptr += len; // Advance write pointer

        // 4. Send the packet
        // rtp_session_sendm_with_ts sends the mblk_t and also handles RTCP reporting based on the timestamp.
        int send_status = rtp_session_sendm_with_ts(rtp_session_, rtp_packet, rtp_timestamp);
        // Note: rtp_session_sendm_with_ts frees the mblk_t (rtp_packet) itself,
        // so we don't call freemsg() here. This is important.

        if (send_status < 0) {
            screamrouter_logger::error("RTPSender ({}): Failed to send RTP packet. Error code: {}", sink_id_, send_status);
            // oRTP errors can be checked with ortp_get_last_error() or similar if needed.
        }
        // else {
        //    screamrouter_logger::debug("RTPSender ({}): Sent RTP packet, len {}, PT {}, TS {}", sink_id_, len, payload_type, rtp_timestamp);
        // }
    }
    ```

2.  **Implement `send_audio_packet()` (`rtp_sender.cpp`):**
    *   This method is for sending PCM audio data.
    *   It calls `send_rtp_data()` with the appropriate payload type for PCM (e.g., `pcm_payload_type_` stored from config/initialization).
    *   The marker bit logic might depend on the PCM packetization scheme (e.g., if it marks the start of a talkspurt). For continuous PCM, it might often be false.
    ```cpp
    // In src/audio_engine/rtp_sender.cpp
    void RTPSender::send_audio_packet(const uint8_t* payload, size_t payload_len, 
                                      const AudioFormatDetails& format, uint32_t rtp_timestamp) {
        if (!initialized_) return;

        // Use format.rtp_payload_type if it's dynamically provided per call,
        // or use the stored pcm_payload_type_ if it's fixed for the session.
        // For now, assume format.rtp_payload_type is the one to use, or fallback to stored.
        uint8_t pt_to_use = (format.rtp_payload_type != 0) ? format.rtp_payload_type : pcm_payload_type_;
        
        send_rtp_data(payload, payload_len, pt_to_use, format.rtp_marker_bit, rtp_timestamp);
    }
    ```

3.  **Implement `send_mp3_frame()` (`rtp_sender.cpp`):**
    *   This method is for sending MP3 audio data.
    *   It calls `send_rtp_data()` with the payload type for MP3 (e.g., `mp3_payload_type_`).
    *   The marker bit for MP3 frames often indicates the first packet of a frame or a significant boundary. RFC 2250 (for MPEG audio/video) suggests setting M bit for packets containing the end of a video frame. For audio, it might be set for the first packet of an independently decodable audio unit or talkspurt.
    ```cpp
    // In src/audio_engine/rtp_sender.cpp
    void RTPSender::send_mp3_frame(const uint8_t* mp3_data, size_t data_len, uint32_t rtp_timestamp) {
        if (!initialized_) return;

        // MP3 frames are often sent with marker bit set, e.g. for first packet of a talkspurt or file.
        // This might need more sophisticated logic based on MP3 framing.
        // For now, let's assume the caller of send_mp3_frame provides this via a field in AudioFormatDetails
        // if it were passed, or we use a default (e.g. true for every frame, or false).
        // The INetworkSender interface for send_mp3_frame doesn't take AudioFormatDetails.
        // We'll use a default marker bit (e.g., true, assuming each frame is significant).
        // Or, better, the AudioFormatDetails should be part of the call if marker bit is dynamic.
        // For now, using a fixed marker bit or deriving from a simple rule.
        // Let's assume true for now, or it should be part of the SinkConfig/AudioFormatDetails for MP3.
        bool marker = true; // Placeholder: This needs proper determination.
                            // The `AudioFormatDetails` passed to `initialize` might set a default marker policy.

        send_rtp_data(mp3_data, data_len, mp3_payload_type_, marker, rtp_timestamp);
    }
    ```

## Code Alterations:

*   **File:** `src/audio_engine/rtp_sender.cpp`
    *   Implement the private `send_rtp_data()` helper method.
    *   Implement the public `send_audio_packet()` method.
    *   Implement the public `send_mp3_frame()` method.
*   **File:** `src/audio_engine/rtp_sender.h`
    *   Declare the private `send_rtp_data()` method.
    *   Ensure `pcm_payload_type_` and `mp3_payload_type_` members are declared and initialized (e.g., in `initialize()`).
*   **File:** `src/audio_engine/audio_types.h` (`AudioFormatDetails`)
    *   Ensure `rtp_marker_bit` is part of `AudioFormatDetails` if it's to be controlled per packet for PCM.
    *   The `send_mp3_frame` interface might need to be revisited if dynamic marker bits are needed for MP3 and not derivable from `mp3_payload_type_` or a session-wide setting.

## Recommendations:

*   **`rtp_session_sendm_with_ts` and `freemsg`:** Double-check oRTP documentation: `rtp_session_sendm_with_ts` *does* free the `mblk_t` it is given. Do not call `freemsg()` on the `mblk_t` after `rtp_session_sendm_with_ts`.
*   **Timestamp Accuracy:** The `rtp_timestamp` passed to these functions must be accurate and derived from the audio sampling clock. `SinkAudioMixer` will be responsible for generating these timestamps based on the audio data it processes.
*   **Marker Bit (`M`):** The logic for setting the marker bit needs to be appropriate for the codec and packetization scheme.
    *   For PCM, it often marks the beginning of a talkspurt.
    *   For MP3, it might mark the first packet of an MP3 frame or a set of frames. RFC 2250 (RTP Payload for MPEG1/MPEG2 Video) is often referenced for MPEG audio too.
    *   The current `INetworkSender` interface might need adjustment if `AudioFormatDetails` (which contains `rtp_marker_bit`) is not passed to `send_mp3_frame`. For now, a default or configured behavior is assumed.
*   **Payload Types:** Ensure `pcm_payload_type_` and `mp3_payload_type_` are correctly initialized from `SinkConfig` (which in turn gets them from Python configuration, potentially from SDP negotiation in the future).
*   **Error Handling:** `rtp_session_sendm_with_ts` returns `< 0` on error. Log these errors. `ortp_get_last_error(rtp_session_)` can provide more detailed error information.

## Acceptance Criteria:

*   `RTPSender` can successfully construct and send RTP packets for PCM data via `send_audio_packet()`.
*   `RTPSender` can successfully construct and send RTP packets for MP3 data via `send_mp3_frame()`.
*   RTP headers (payload type, timestamp, marker bit, SSRC, sequence number) are set correctly.
*   The oRTP session is used correctly for sending, and `mblk_t` are managed properly (created, and freed by oRTP on send).
*   The methods handle cases where the sender is not initialized.
