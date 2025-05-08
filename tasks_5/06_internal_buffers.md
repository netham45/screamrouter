# Task 6: `AudioProcessor` - Internal Buffers (Vectorization & Pre-allocation)

**Objective:**
Convert all C-style array member buffers to `std::vector`, pre-allocating them in the constructor to their maximum required sizes to avoid runtime reallocations and improve safety.

**Details:**

1.  **Convert C-Style Arrays to `std::vector`:**
    *   In `audio_processor.h`, change the declarations of all internal processing buffers from C-style arrays to `std::vector`.
    *   Examples:
        *   `uint8_t receive_buffer[CHUNK_SIZE * 4]` -> `std::vector<uint8_t> receive_buffer_;`
        *   `int32_t scaled_buffer[CHUNK_SIZE * 32]` -> `std::vector<int32_t> scaled_buffer_;`
        *   `int32_t resampled_buffer[CHUNK_SIZE * MAX_CHANNELS * 16]` -> `std::vector<int32_t> resampled_buffer_;`
        *   `int32_t channel_buffers[MAX_CHANNELS][CHUNK_SIZE * 64]` -> `std::vector<std::vector<int32_t>> channel_buffers_;`
        *   `int32_t remixed_channel_buffers[MAX_CHANNELS][CHUNK_SIZE * 64]` -> `std::vector<std::vector<int32_t>> remixed_channel_buffers_;`
        *   `int32_t merged_buffer[CHUNK_SIZE * 64]` -> `std::vector<int32_t> merged_buffer_;`
        *   `int32_t processed_buffer[CHUNK_SIZE * 64]` -> `std::vector<int32_t> processed_buffer_;`
        *   `float resampler_data_in[CHUNK_SIZE * MAX_CHANNELS * 8]` -> `std::vector<float> resampler_data_in_;`
        *   `float resampler_data_out[CHUNK_SIZE * MAX_CHANNELS * 8]` -> `std::vector<float> resampler_data_out_;`

2.  **Remove Redundant Pointers:**
    *   Remove the `scaled_buffer_int8` member (`uint8_t *scaled_buffer_int8 = (uint8_t *)scaled_buffer;`).
    *   If byte-level access to `scaled_buffer_` is needed, use `reinterpret_cast<uint8_t*>(scaled_buffer_.data())` locally within the method requiring it.

3.  **Constructor Initialization with `resize()`:**
    *   In the `AudioProcessor` constructor (after member variables like `input_channels_`, `output_channels_`, and constants like `CHUNK_SIZE_BYTES_` are initialized or available):
        *   Call `resize()` on each vector to pre-allocate memory to its maximum required capacity. These capacities should be determined based on the original array dimensions and the processing logic.
        *   **Example Sizing (ensure these match the maximums needed by the algorithms):**
            *   `receive_buffer_.resize(CHUNK_SIZE_BYTES_);` (e.g., 1152 bytes for the input chunk)
            *   `scaled_buffer_.resize(CHUNK_SIZE_BYTES_ / (config_.input_bit_depth_ / 8));` (Max `int32_t` samples from input chunk, e.g., `1152 / (16/8) = 576` for 16-bit input)
            *   `resampler_data_in_.resize(73728);` (Based on original `CHUNK_SIZE * MAX_CHANNELS * 8` floats)
            *   `resampler_data_out_.resize(73728);`
            *   `resampled_buffer_.resize(resampler_data_out_.capacity());`
            *   `channel_buffers_.resize(MAX_CHANNELS_);`
                `size_t max_samples_per_channel_after_resample = resampled_buffer_.capacity();`
                `for (auto& buf : channel_buffers_) { buf.resize(max_samples_per_channel_after_resample); }`
            *   `remixed_channel_buffers_.resize(MAX_CHANNELS_);`
                `for (auto& buf : remixed_channel_buffers_) { buf.resize(max_samples_per_channel_after_resample); }`
            *   `merged_buffer_.resize(max_samples_per_channel_after_resample * MAX_CHANNELS_);`
            *   `processed_buffer_.resize(resampler_data_out_.capacity());`

4.  **Update Buffer Access:**
    *   Modify all code that accesses these buffers to use `std::vector` methods and iterators where appropriate (e.g., `.data()` for C-style API interop, `.size()`, iterators for loops).
    *   The `_pos` member variables (e.g., `scale_buffer_pos_`, `resample_buffer_pos_`) will continue to track the number of valid elements within these pre-allocated vectors. Ensure they are used correctly with vector operations (e.g., not exceeding `vector.size()` when writing, and using `_pos` to indicate the extent of valid data).

**Acceptance Criteria:**

*   All C-style array member buffers are converted to `std::vector`.
*   `scaled_buffer_int8` pointer is removed.
*   Vectors are `resize()`d in the constructor to appropriate maximum capacities, preventing runtime reallocations during `processAudio`.
*   Buffer access patterns are updated to use `std::vector` mechanisms.
*   `_pos` variables correctly track the amount of valid data in each vector.
*   The code compiles and all existing audio processing functionality remains intact.
*   Performance is not negatively impacted by unnecessary reallocations; pre-allocation should maintain or improve performance characteristics.
