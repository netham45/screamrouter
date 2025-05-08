# Task 3: `AudioProcessor` - Logging Improvements

**Objective:**
Replace direct `std::cout` usage with a standardized logging mechanism consistent with other `screamrouter::audio` components, and remove the dedicated buffer monitoring thread.

**Details:**

1.  **Remove Monitor Thread and `monitorBuffers` Method:**
    *   The `monitor_thread` member in `AudioProcessor` and its associated `monitorBuffers` method (which uses `std::cout` for logging buffer fill rates) will be removed.
    *   If buffer level monitoring is still desired for debugging, it should be done via conditional debug log statements within the main `processAudio` path or relevant sub-methods, rather than a separate thread.

2.  **Define and Use Logging Macros:**
    *   In `audio_processor.h` or a common logging header, define logging macros specific to `AudioProcessor` (e.g., `LOG_AP`, `LOG_ERROR_AP`, `LOG_WARN_AP`, `LOG_DEBUG_AP`).
    *   These macros should ideally take the `log_context_id_` (passed via `AudioProcessorConfig`) as a parameter to include in the log output, similar to how `LOG_SIP(config_.instance_id, ...)` works.
        ```cpp
        // Example (actual implementation might use a central logging utility)
        // In audio_processor.h or a shared logging header
        // #define LOG_AP(context_id, msg) std::cout << "[AudioProc:" << context_id << "] " << msg << std::endl
        // #define LOG_ERROR_AP(context_id, msg) std::cerr << "[AudioProc Error:" << context_id << "] " << msg << std::endl
        // #define LOG_DEBUG_AP(context_id, msg) // Potentially compiled out in release builds
        // #ifdef DEBUG_AUDIO_PROCESSOR
        //   #undef LOG_DEBUG_AP
        //   #define LOG_DEBUG_AP(context_id, msg) std::cout << "[AudioProc Debug:" << context_id << "] " << msg << std::endl
        // #endif
        ```
    *   Replace all existing `std::cout` and `std::cerr` calls within `AudioProcessor` methods with these new macros, passing `log_context_id_`.

3.  **Integrate `log_context_id_`:**
    *   Ensure the `AudioProcessorConfig` (from Task 2) includes a `std::string log_context_id;` field.
    *   The `AudioProcessor` constructor should store this `log_context_id_` as a member variable.
    *   When `AudioProcessor` is instantiated (e.g., in `SourceInputProcessor`), the parent component's identifier (like `instance_id`) should be passed as the `log_context_id`.

**Acceptance Criteria:**

*   The `monitor_thread` member and `monitorBuffers` method are removed from `AudioProcessor`.
*   `std::atomic<bool> monitor_running` member is removed.
*   Standardized logging macros (`LOG_AP`, `LOG_ERROR_AP`, etc.) are defined and used within `AudioProcessor`.
*   Log messages from `AudioProcessor` include a context identifier (e.g., the ID of the `SourceInputProcessor` or `SinkAudioMixer` instance that owns it).
*   No direct `std::cout` or `std::cerr` calls remain in `audio_processor.cpp`.
