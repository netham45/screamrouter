# Task 6: Implement Dynamic Reconfiguration Logic in SourceInputProcessor

**Goal**: Implement the logic within `SourceInputProcessor` to dynamically reconfigure its internal `AudioProcessor` based on the incoming packet type and header information.

**Files to Modify**:
*   `src/audio_engine/source_input_processor.h` (Add constants, maybe new private method declaration)
*   `src/audio_engine/source_input_processor.cpp` (Implement the logic)

**Steps**:

1.  **Define Default RTP Input Constants** (`source_input_processor.h` or `.cpp`):
    *   Define constants for the assumed format of RTP Scream payloads.
    ```cpp
    // Constants for assumed RTP Scream payload format
    const int DEFAULT_RTP_INPUT_CHANNELS = 2;
    const int DEFAULT_RTP_INPUT_SAMPLERATE = 48000;
    const int DEFAULT_RTP_INPUT_BITDEPTH = 16;
    // Define expected CHUNK_SIZE if not already accessible
    const size_t CHUNK_SIZE = 1152; // Expected audio data size
    const size_t SCREAM_HEADER_SIZE = 5; // Size of the raw scream header
    ```

2.  **Declare Private Helper Method** (`source_input_processor.h`):
    *   Declare a private method to handle the format check and reconfiguration. This method will return a pointer to the actual audio data to be processed and its size, or indicate failure.
    ```cpp
    private:
        // ... other private members ...

        /**
         * @brief Checks packet format, reconfigures AudioProcessor if needed, and returns audio payload.
         * @param packet The incoming tagged audio packet.
         * @param out_audio_payload_ptr Pointer to store the start of the 1152-byte audio data.
         * @param out_audio_payload_size Pointer to store the size (should be CHUNK_SIZE).
         * @return true if successful and audio payload is valid, false otherwise (e.g., bad packet size).
         */
        bool check_format_and_reconfigure(
            const TaggedAudioPacket& packet,
            const uint8_t** out_audio_payload_ptr,
            size_t* out_audio_payload_size
        );
    ```

3.  **Implement `check_format_and_reconfigure`** (`source_input_processor.cpp`):
    *   Implement the logic described in the refined plan (Plan Mode response #6).
    *   Determine `target_channels`, `target_samplerate`, `target_bitdepth` based on `m_protocol_type` (either from packet header or RTP defaults).
    *   Determine the expected start (`audio_payload_ptr`) and size (`audio_payload_size`) of the actual audio data within `packet.audio_data`. Validate packet sizes.
    *   Compare target format with `m_current_ap_input_...`.
    *   If reconfiguration is needed (format differs or `audio_processor_` is null):
        *   Acquire `audio_processor_mutex_`.
        *   Log the reconfiguration.
        *   Replace `audio_processor_` with `std::make_unique<AudioProcessor>(...)`.
        *   Apply current EQ/Volume settings to the new processor.
        *   Update `m_current_ap_input_...` members.
        *   Release mutex.
    *   Set the output parameters `*out_audio_payload_ptr` and `*out_audio_payload_size`.
    *   Return `true` if the packet was valid and processed (or ready for processing), `false` if discarded due to errors (e.g., wrong size).

4.  **Integrate into Processing Loop** (`source_input_processor.cpp`):
    *   Modify the main processing logic (likely in `output_loop` after retrieving a packet from the timeshift buffer, or potentially in `input_loop`'s `handle_new_input_packet` if reconfiguration should happen before buffering). Let's assume it's done *after* retrieving from the timeshift buffer in `output_loop`.
    *   Inside `output_loop`, after successfully retrieving a `TaggedAudioPacket` from the `timeshift_buffer_`:
        *   Call `check_format_and_reconfigure()` with the retrieved packet.
        *   If it returns `true`:
            *   Use the returned `audio_payload_ptr` and `audio_payload_size` (which should be `CHUNK_SIZE`).
            *   Pass this specific audio data to the `process_audio_chunk()` method (or directly to `audio_processor_->processAudio()` if refactoring).
        *   If it returns `false`, skip processing this packet.

    ```cpp
    // Example snippet within output_loop after getting packet from timeshift buffer

    TaggedAudioPacket current_packet = timeshift_buffer_[timeshift_buffer_read_idx_];
    timeshift_buffer_read_idx_++;
    data_retrieved = true;
    lock.unlock(); // Release timeshift mutex before potential reconfiguration

    const uint8_t* audio_payload_ptr = nullptr;
    size_t audio_payload_size = 0;
    bool packet_ok_for_processing = check_format_and_reconfigure(
        current_packet,
        &audio_payload_ptr,
        &audio_payload_size
    );

    if (packet_ok_for_processing && audio_processor_) {
        // Ensure audio_payload_ptr is valid and size is CHUNK_SIZE
        if (audio_payload_ptr && audio_payload_size == CHUNK_SIZE) {
             // Pass the actual 1152 bytes of audio data to the processing stage
             // This might involve creating a temporary vector or modifying process_audio_chunk
             std::vector<uint8_t> chunk_data(audio_payload_ptr, audio_payload_ptr + CHUNK_SIZE);
             process_audio_chunk(chunk_data); // process_audio_chunk calls audio_processor_
             push_output_chunk_if_ready();
        } else {
            LOG_ERROR("Audio payload pointer/size invalid after check_format_and_reconfigure.");
        }
    } else if (!packet_ok_for_processing) {
        LOG_WARN("Packet discarded due to format/size issues.");
    }
    // Continue loop...
    ```

5.  **Refine `process_audio_chunk`**: Ensure `process_audio_chunk` correctly handles the `std::vector<uint8_t>` containing exactly `CHUNK_SIZE` bytes and passes its `.data()` pointer to `audio_processor_->processAudio()`.
