# Sub-Task 1.4: Implement RTP Packet Processing in `RtpReceiver::process_received_packet()`

**Objective:** Implement the `process_received_packet()` method in `RtpReceiver` to parse RTP header information from the `mblk_t` packet, extract the audio payload, determine audio format (initially using a default), create a `TaggedAudioPacket`, and push it to the `TimeshiftManager`.

**Parent Task:** [RTP Library (oRTP) Integration](../task_01_rtp_library_integration.md)
**Previous Sub-Task:** [Sub-Task 1.3: Implement RTP Packet Reception Loop in `RtpReceiver::run()`](./subtask_1.3_implement_rtp_reception_loop.md)

## Key Steps & Considerations:

1.  **Method Signature and Purpose (`rtp_receiver.cpp`):**
    *   `void RtpReceiver::process_received_packet(mblk_t* rtp_packet_block)`
    *   This method takes the raw message block (`mblk_t*`) received from oRTP.
    *   It is responsible for extracting RTP header fields, the payload, and then freeing the `mblk_t`.

2.  **Extracting RTP Header Information:**
    *   Use oRTP utility functions to get data from the RTP header:
        *   `uint8_t payload_type = rtp_get_payload_type(rtp_packet_block);`
        *   `uint32_t timestamp = rtp_get_timestamp(rtp_packet_block);`
        *   `uint16_t sequence_number = rtp_get_sqnum(rtp_packet_block);`
        *   `uint32_t ssrc = rtp_get_ssrc(rtp_packet_block);`
        *   `int marker_bit = rtp_get_marker(rtp_packet_block);`
    *   Log these values for debugging.

3.  **Accessing RTP Payload:**
    *   The RTP payload data starts at `rtp_packet_block->b_rptr`.
    *   The length of the payload can be calculated from the total message block length minus the RTP header length. oRTP provides `rtp_get_payload_len(rtp_packet_block)` or you can use `msgdsize(rtp_packet_block) - rtp_get_hdr_len(rtp_packet_block)`. A simpler way is `rtp_packet_block->b_wptr - rtp_packet_block->b_rptr` if the `b_rptr` points to the start of the payload after oRTP has processed the header. More directly, `rtp_get_payload(rtp_packet_block, &payload_ptr)` can be used, and `rtp_get_payload_len(rtp_packet_block)` for its length.
    *   Let's assume `payload_ptr = rtp_packet_block->b_rptr` (after oRTP has potentially advanced it past the header if `rtp_session_recvmsg` or similar was used, but `rtp_session_recv_with_ts` gives the full packet).
    *   A safer way to get payload:
        ```cpp
        // Get header length (including CSRCs if any)
        // int rtp_header_len = rtp_get_hdr_len(rtp_packet_block); // This function might not exist directly.
        // A common way:
        int rtp_header_len = RTP_FIXED_HEADER_SIZE + rtp_get_csrc_count(rtp_packet_block) * sizeof(uint32_t);
        if (rtp_get_extension_bit(rtp_packet_block)) {
            // Add extension header length if present
            // rtp_header_len += ... (logic to parse extension header length)
        }
        
        uint8_t* payload_data = rtp_packet_block->b_rptr + rtp_header_len;
        size_t payload_length = msgdsize(rtp_packet_block) - rtp_header_len;
        ```
        Alternatively, and more robustly, use `ortp_get_payload()`:
        ```cpp
        char *actual_payload = nullptr;
        int actual_payload_len = ortp_get_payload(rtp_session_, rtp_packet_block, &actual_payload);
        // actual_payload now points to the payload, and actual_payload_len is its length.
        // Note: ortp_get_payload might return a pointer into the mblk_t or a copy.
        // The documentation for ortp_get_payload should be checked.
        // For simplicity in this draft, we'll assume direct access after header.
        // A common pattern is:
        // RtpHeader *hdr = (RtpHeader*) rtp_packet_block->b_rptr;
        // uint8_t* payload_data = rtp_packet_block->b_rptr + hdr->header_length; // header_length needs to be calculated
        // size_t payload_length = msgdsize(rtp_packet_block) - hdr->header_length;
        // For now, let's use a simpler approach based on common oRTP examples:
        uint8_t* payload_data = rtp_packet_block->b_rptr + rtp_get_header_length(rtp_packet_block); // Assuming rtp_get_header_length() exists and is accurate
        size_t payload_length = rtp_get_payload_length(rtp_packet_block); // Assuming rtp_get_payload_length() exists
        ```
        **Correction/Clarification:** oRTP's `mblk_t` structure: `b_rptr` points to the start of data, `b_wptr` to the end. `msgdsize(m)` gives `m->b_wptr - m->b_rptr`.
        The payload itself is typically accessed after parsing the header. `rtp_session_get_payload(session, mblk, &payload)` is a good utility.
        If `rtp_session_recv_with_ts` returns the raw packet, then:
        `RtpHeader *header = (RtpHeader*)rtp_packet_block->b_rptr;`
        `int header_size = rtp_header_get_length(header);`
        `uint8_t* payload_data = rtp_packet_block->b_rptr + header_size;`
        `size_t payload_length = msgdsize(rtp_packet_block) - header_size;`

4.  **Determine Audio Format:**
    *   **Initial Implementation (Default Format):**
        *   Use the `default_audio_format_` member initialized in the constructor.
        *   Compare `payload_type` from the packet with `default_audio_format_.rtp_payload_type`. If they don't match, log a warning but proceed (or drop the packet if strict).
    *   **Future (SDP Integration):**
        *   Use the `ssrc` to look up the negotiated `AudioFormatDetails` from `ssrc_format_map_`.
        *   If `ssrc` is unknown, it might be a new stream or an error.
        *   The `payload_type` from the packet will be key to selecting the correct codec and parameters from the SDP information associated with that SSRC.

5.  **Create `TaggedAudioPacket`:**
    *   Allocate a `TaggedAudioPacket`.
    *   Copy `payload_data` into `TaggedAudioPacket::data`.
    *   Set `TaggedAudioPacket::format` using the determined `AudioFormatDetails`.
    *   Set `TaggedAudioPacket::rtp_timestamp = timestamp;`
    *   Set `TaggedAudioPacket::arrival_time_ms` using `ortp_time_get_ms()` or similar.
    *   The `source_id_` of the `RtpReceiver` should be part of the tag.

6.  **Push to `TimeshiftManager`:**
    *   `timeshift_manager_->push_packet(source_id_, std::move(tagged_packet));`

7.  **Free `mblk_t`:**
    *   **Crucially**, call `freemsg(rtp_packet_block);` to release the message block allocated by oRTP. This must be done whether the packet was processed successfully or not (e.g., if dropped due to unknown payload type).

8.  **`RtpReceiver::process_received_packet()` Implementation Sketch (`rtp_receiver.cpp`):**
    ```cpp
    // In src/audio_engine/rtp_receiver.cpp
    #include "screamrouter_logger/screamrouter_logger.h" // For logging

    void RtpReceiver::process_received_packet(mblk_t* rtp_packet_block) {
        if (!rtp_packet_block) {
            return;
        }

        // Extract RTP header info
        uint8_t pt = rtp_get_payload_type(rtp_packet_block);
        uint32_t ts = rtp_get_timestamp(rtp_packet_block);
        uint16_t seq_num = rtp_get_sqnum(rtp_packet_block);
        uint32_t current_ssrc = rtp_get_ssrc(rtp_packet_block);
        // int marker = rtp_get_marker(rtp_packet_block); // Useful for some codecs

        // For debugging
        // screamrouter_logger::debug("RTP Pkt: PT={}, TS={}, Seq={}, SSRC={}", pt, ts, seq_num, current_ssrc);

        // Determine audio format (initially, use default)
        // TODO: Later, use SSRC and PT to look up format from SDP negotiations
        AudioFormatDetails current_format = default_audio_format_; 

        if (pt != current_format.rtp_payload_type) {
            screamrouter_logger::warn("RtpReceiver ({}): Received packet with PT {} but expected {}. Processing with default.",
                                     source_id_, pt, current_format.rtp_payload_type);
            // Optionally, drop the packet if PT mismatch is critical:
            // freemsg(rtp_packet_block);
            // return;
        }
        
        // Get payload
        // RtpHeader is defined in ortp/rtp.h. rtp_header_get_length is also there.
        RtpHeader *header = (RtpHeader*)rtp_packet_block->b_rptr;
        int header_size = rtp_header_get_length(header); // Gets base header + CSRC list size
        // Check for RTP extension header if present and add its size
        if (rtp_header_get_extension_bit(header)){
            RtpExtHeader *ext_hdr = (RtpExtHeader*) (rtp_packet_block->b_rptr + header_size);
            header_size += rtp_ext_header_get_length(ext_hdr);
        }

        uint8_t* payload_data_ptr = rtp_packet_block->b_rptr + header_size;
        size_t payload_data_len = msgdsize(rtp_packet_block) - header_size;

        if (payload_data_len == 0) {
            screamrouter_logger::warn("RtpReceiver ({}): Received RTP packet with no payload.", source_id_);
            freemsg(rtp_packet_block);
            return;
        }

        // Create TaggedAudioPacket
        auto tagged_packet = std::make_unique<TaggedAudioPacket>();
        tagged_packet->data.resize(payload_data_len);
        memcpy(tagged_packet->data.data(), payload_data_ptr, payload_data_len);
        
        tagged_packet->format = current_format; // Assign the determined format
        tagged_packet->rtp_timestamp = ts;
        tagged_packet->arrival_time_ms = ortp_time_get_ms(); // Or use a consistent clock
        tagged_packet->source_id = source_id_;
        // tagged_packet->sequence_number = seq_num; // If TaggedAudioPacket needs it

        // Push to TimeshiftManager
        if (timeshift_manager_) {
            timeshift_manager_->push_packet(source_id_, std::move(tagged_packet));
        } else {
            screamrouter_logger::warn("RtpReceiver ({}): TimeshiftManager is null, cannot push packet.", source_id_);
        }

        // Free the mblk_t
        freemsg(rtp_packet_block);
    }
    ```

## Code Alterations:

*   **File:** `src/audio_engine/rtp_receiver.cpp`
    *   Implement the `process_received_packet()` method as sketched above.
*   **File:** `src/audio_engine/rtp_receiver.h`
    *   Ensure `default_audio_format_` of type `AudioFormatDetails` is a member and initialized.
    *   Ensure `AudioFormatDetails` (in `audio_types.h`) has an `rtp_payload_type` field.
*   **File:** `src/audio_engine/audio_types.h`
    *   Add `uint8_t rtp_payload_type;` to `AudioFormatDetails` struct.
    *   Add `uint32_t rtp_timestamp;` to `TaggedAudioPacket` struct.
    *   Potentially `uint16_t sequence_number;` to `TaggedAudioPacket` if needed for jitter buffer or diagnostics.

## Recommendations:

*   **Payload Type Matching:** Be strict or lenient with payload type matching based on requirements. If multiple payload types are expected for a single source (e.g., different codecs negotiated via SDP), the logic will need to handle this.
*   **SSRC Handling:** The current SSRC is extracted. Later, this will be vital for demultiplexing streams if multiple SSRCs arrive on the same port or for associating packets with specific SDP-negotiated parameters.
*   **Memory Management:** Correctly using `freemsg()` is critical to prevent memory leaks from oRTP's `mblk_t` allocations.
*   **`AudioFormatDetails` Population:** Ensure `default_audio_format_` is properly initialized with sensible defaults (e.g., sample rate, channels, bit depth, and the chosen default RTP payload type for PCM).

## Acceptance Criteria:

*   `process_received_packet()` correctly parses RTP header fields (payload type, timestamp, SSRC, sequence number).
*   The RTP payload is successfully extracted.
*   A `TaggedAudioPacket` is created with the audio data and relevant metadata (using default format initially).
*   The packet is pushed to the `TimeshiftManager`.
*   The `mblk_t` message block is freed using `freemsg()`.
*   The system handles basic RTP packets without crashing or leaking memory.
