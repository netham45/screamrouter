# Sub-Task 1.1: Global oRTP Initialization and Shutdown in AudioManager

**Objective:** Integrate the global initialization and deinitialization calls for the oRTP library (`ortp_init()`, `ortp_scheduler_init()`, `ortp_exit()`) into the `AudioManager` class lifecycle.

**Parent Task:** [RTP Library (oRTP) Integration](../task_01_rtp_library_integration.md)

## Key Steps & Considerations:

1.  **Include oRTP Header:**
    *   Ensure that `audio_manager.cpp` (and potentially `audio_manager.h` if oRTP types are exposed, though likely not needed for just init/exit) includes the main oRTP header.
    ```cpp
    // In src/audio_engine/audio_manager.cpp
    #include <ortp/ortp.h> 
    ```
    *   The `setup.py` changes from `task_00_setup_py_build_updates` should make `ortp/ortp.h` findable by the compiler.

2.  **Modify `AudioManager::initialize()`:**
    *   Call `ortp_init()` to initialize the oRTP library. This should be one of the first things done in `initialize()`.
    *   Call `ortp_scheduler_init()` to initialize the oRTP scheduler. This is crucial for oRTP's internal timing and packet handling.
    *   Check return codes for these functions if they provide them, and log errors or throw exceptions on failure.
    ```cpp
    // In src/audio_engine/audio_manager.cpp
    // bool AudioManager::initialize(const ScreamRouterConfig& config, std::shared_ptr<NotificationQueue> notification_queue) {
    //     // ... existing initializations ...

    //     screamrouter_logger::info("Initializing oRTP library...");
    //     if (ortp_init() != 0) { // oRTP's ortp_init() returns void, but good practice to check if a lib has error codes
    //         screamrouter_logger::error("Failed to initialize oRTP library.");
    //         // Consider how to handle this failure - throw, or return false?
    //         // For now, assume it doesn't return an error or we log and continue if it's non-critical for other parts.
    //         // However, for RTP functionality, this is critical.
    //     }
        
    //     screamrouter_logger::info("Initializing oRTP scheduler...");
    //     ortp_scheduler_init(); // This also returns void

    //     // ... rest of initialization ...
    //     return true;
    // }
    ```
    *   **Note on `ortp_init()` return:** The oRTP documentation indicates `ortp_init()` returns `void`. Error handling would rely on subsequent oRTP calls failing if initialization was problematic. `bctoolbox` (a dependency) logging might indicate issues.

3.  **Modify `AudioManager::shutdown()` (or Destructor):**
    *   Call `ortp_exit()` to clean up oRTP resources. This should be done when `AudioManager` is being shut down or destroyed.
    *   The oRTP documentation suggests `ortp_exit()` should be called, and then `bctoolbox_exit()` (from bctoolbox, an oRTP dependency). If `bctoolbox` is only used by `ortp`, `ortp_exit()` might handle its dependencies. However, if `bctoolbox` were used directly elsewhere, its lifecycle would need separate management. For now, focus on `ortp_exit()`.
    ```cpp
    // In src/audio_engine/audio_manager.cpp
    // void AudioManager::shutdown() {
    //     screamrouter_logger::info("Shutting down AudioManager...");
    //     // ... existing shutdown procedures for sources, sinks, mixers ...
        
    //     screamrouter_logger::info("Deinitializing oRTP library...");
    //     ortp_exit(); // This also returns void

    //     // ... other cleanup ...
    // }
    ```
    *   If `AudioManager` uses a destructor for cleanup, `ortp_exit()` could be called there as well, ensuring it's invoked if `shutdown()` isn't explicitly called.

4.  **Thread Safety and Call Order:**
    *   `ortp_init()` should be called once before any other oRTP functions.
    *   `ortp_exit()` should be called once after all oRTP operations are complete.
    *   `AudioManager`'s singleton-like nature or controlled lifecycle should ensure these are called appropriately.

## Code Alterations:

*   **File:** `src/audio_engine/audio_manager.cpp`
    *   Add `#include <ortp/ortp.h>`.
    *   Modify `AudioManager::initialize()` to include `ortp_init()` and `ortp_scheduler_init()`.
    *   Modify `AudioManager::shutdown()` (or its destructor) to include `ortp_exit()`.
*   **File:** `src/audio_engine/CMakeLists.txt` (or `setup.py` if not already handled)
    *   Ensure that the oRTP library is correctly linked so that these functions are available. This should have been covered by `task_00_setup_py_build_updates` where the `Extension` object in `setup.py` is configured with `ortp` library and include paths.

## Recommendations:

*   **Logging:** Add log messages before and after these calls to confirm they are being executed during startup and shutdown.
*   **Error Handling:** Although `ortp_init()` and `ortp_exit()` are `void`, be mindful that failures in these underlying library initializations can lead to crashes or undefined behavior later. Robust applications might have ways to detect such states, perhaps by checking versions or capabilities after init.
*   **Dependency Initialization:** oRTP depends on `bctoolbox`. `bctoolbox` has its own `bct_init()` and `bct_exit()`. Typically, `ortp_init()` should handle initializing its necessary dependencies like `bctoolbox`. If explicit control over `bctoolbox` init/exit is needed (e.g., if `bctoolbox` is used by other parts of the application independently of oRTP), then `bct_init()` would need to be called before `ortp_init()`, and `bct_exit()` after `ortp_exit()`. For now, assume `ortp_init/exit` manage this.

## Acceptance Criteria:

*   `ortp_init()` and `ortp_scheduler_init()` are called successfully when `AudioManager` initializes.
*   `ortp_exit()` is called successfully when `AudioManager` shuts down or is destroyed.
*   The application compiles and links correctly with these new calls.
*   No crashes occur during startup or shutdown related to oRTP initialization/deinitialization.
