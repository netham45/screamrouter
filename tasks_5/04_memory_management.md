# Task 4: `AudioProcessor` - Memory Management (RAII)

**Objective:**
Replace raw pointer usage and manual memory management (`new`/`delete`, C-style `src_new`/`src_delete`) with modern C++ RAII principles, primarily using `std::unique_ptr` for owned resources.

**Details:**

1.  **Biquad Filters (`filters` and `dcFilters`):**
    *   Current: `Biquad* filters[MAX_CHANNELS][EQ_BANDS];` and `Biquad* dcFilters[MAX_CHANNELS];` with manual `new` in setup methods and `delete` in the destructor.
    *   **Change to:**
        *   `std::vector<std::vector<std::unique_ptr<Biquad>>> filters_;`
        *   `std::vector<std::unique_ptr<Biquad>> dc_filters_;`
    *   In `audio_processor.h`, declare these as private members.
    *   In the `AudioProcessor` constructor:
        *   Resize `filters_` to `MAX_CHANNELS_`. Each inner vector will also be resized to `EQ_BANDS_`.
        *   Resize `dc_filters_` to `MAX_CHANNELS_`.
    *   In `setupBiquad()`:
        *   Iterate through `filters_` and assign `filters_[channel][band] = std::make_unique<Biquad>(...);`.
    *   In `setupDCFilter()`:
        *   Iterate through `dc_filters_` and assign `dc_filters_[channel] = std::make_unique<Biquad>(...);`.
    *   Remove manual `delete` calls for these filters from the `AudioProcessor` destructor; `std::unique_ptr` will handle cleanup.

2.  **`libsamplerate` States (`sampler` and `downsampler`):**
    *   Current: `SRC_STATE* sampler;` and `SRC_STATE* downsampler;` with `src_new` in `initializeSampler` and `src_delete` in the destructor.
    *   **Change to:**
        *   Define a custom deleter for `SRC_STATE` in `audio_processor.h` or `audio_processor.cpp` (within an appropriate namespace if needed, or as a static struct within the class).
            ```cpp
            // In audio_processor.h or .cpp
            struct SrcStateDeleter {
                void operator()(SRC_STATE* s) const {
                    if (s) {
                        src_delete(s);
                    }
                }
            };
            ```
        *   Declare members in `audio_processor.h`:
            ```cpp
            std::unique_ptr<SRC_STATE, SrcStateDeleter> sampler_;
            std::unique_ptr<SRC_STATE, SrcStateDeleter> downsampler_;
            ```
    *   In `initializeSampler()`:
        *   Replace `sampler = src_new(...)` with `sampler_.reset(src_new(SRC_LINEAR, input_channels_, &error));`.
        *   Replace `downsampler = src_new(...)` with `downsampler_.reset(src_new(SRC_LINEAR, output_channels_, &error));`.
        *   Handle errors from `src_new` appropriately (e.g., throw an exception if `sampler_` or `downsampler_` is null after `src_new`).
    *   Remove manual `src_delete(sampler)` and `src_delete(downsampler)` calls from the `AudioProcessor` destructor.

**Acceptance Criteria:**

*   `filters_` member is changed to `std::vector<std::vector<std::unique_ptr<Biquad>>>`.
*   `dc_filters_` member is changed to `std::vector<std::unique_ptr<Biquad>>`.
*   `Biquad` objects are created using `std::make_unique` in `setupBiquad` and `setupDCFilter`.
*   `sampler_` and `downsampler_` members are changed to `std::unique_ptr<SRC_STATE, SrcStateDeleter>`.
*   `SRC_STATE` objects are managed by `sampler_.reset(src_new(...))` and `downsampler_.reset(src_new(...))`.
*   A `SrcStateDeleter` custom deleter struct is implemented and used.
*   The `AudioProcessor` destructor no longer contains manual `delete` for Biquad filters or `src_delete` for sampler states.
*   The code compiles and all existing audio processing functionality remains intact.
