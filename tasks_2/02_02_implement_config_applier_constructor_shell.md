# Task 02_02: Implement `AudioEngineConfigApplier` Constructor and `apply_state` Shell

**Objective:** Create the C++ source file `audio_engine_config_applier.cpp` in `src/configuration/` and implement the constructor, destructor, and a basic shell for the `apply_state` method of the `AudioEngineConfigApplier` class.

**File to Create:** `src/configuration/audio_engine_config_applier.cpp`

**Details:**

1.  **Include Necessary Headers:**
    *   Include `src/configuration/audio_engine_config_applier.h`.
    *   Include `<iostream>` or a proper logging header for any diagnostic messages (e.g., `src/screamrouter_logger/screamrouter_logger.h` if adaptable for C++, or a simple `std::cout` for now).
    *   Include `<algorithm>` for `std::remove_if` or similar utilities if needed later.
    *   Include `src/audio_engine/audio_types.h` for `EQ_BANDS` if not already pulled in.

2.  **Implement Constructor `AudioEngineConfigApplier::AudioEngineConfigApplier`:**
    *   The constructor takes `audio::AudioManager& audio_manager` as a parameter.
    *   Initialize the `audio_manager_` member variable using an initializer list.
    *   Initialize `active_source_paths_` and `active_sinks_` maps to be empty. The initial state of `AudioManager` is assumed to be empty or will be reconciled on the first `apply_state` call.
    *   Add a log message indicating the applier has been created.

3.  **Implement Destructor `AudioEngineConfigApplier::~AudioEngineConfigApplier`:**
    *   For now, the destructor can be empty.
    *   Add a log message if desired (e.g., "AudioEngineConfigApplier destroyed.").
    *   It does not own `audio_manager_`, so it should not delete it.

4.  **Implement `apply_state` Method Shell:**
    *   This method takes `const DesiredEngineState& desired_state` as a parameter and returns `bool`.
    *   **Overall Logic (Shell):**
        1.  Log the start of the state application process.
        2.  Declare vectors to hold items identified by reconciliation:
            *   `std::vector<std::string> sink_ids_to_remove;`
            *   `std::vector<AppliedSinkParams> sinks_to_add;`
            *   `std::vector<AppliedSinkParams> sinks_to_update;`
            *   `std::vector<std::string> path_ids_to_remove;`
            *   `std::vector<AppliedSourcePathParams> paths_to_add;`
            *   `std::vector<AppliedSourcePathParams> paths_to_update;`
        3.  Call the (yet to be fully implemented) reconciliation helper methods:
            *   `reconcile_sinks(desired_state.sinks, sink_ids_to_remove, sinks_to_add, sinks_to_update);`
            *   `reconcile_source_paths(desired_state.source_paths, path_ids_to_remove, paths_to_add, paths_to_update);`
        4.  Call the (yet to be fully implemented) processing helper methods in a safe order (removals first, then additions, then updates/connections):
            *   `process_sink_removals(sink_ids_to_remove);`
            *   `process_source_path_removals(path_ids_to_remove);`
            *   For additions, iterate and call `process_source_path_addition` for each path in `paths_to_add`. Store the `AppliedSourcePathParams` (which now includes `generated_instance_id`) in `active_source_paths_`.
            *   `process_sink_additions(sinks_to_add);` (This will also handle initial connections for new sinks based on their `connected_source_path_ids` by looking up `generated_instance_id` from `active_source_paths_`).
            *   `process_source_path_updates(paths_to_update);`
            *   `process_sink_updates(sinks_to_update);` (This will primarily handle connection changes for existing sinks).
        5.  Log completion and return `true` (for now, assuming success). Error handling will be added later.

5.  **Implement Shells for Helper Methods:**
    *   For each private helper method declared in the header, create an empty or minimal implementation in the `.cpp` file. They will be filled in subsequent tasks.
    *   Example for `reconcile_sinks`:
        ```cpp
        void AudioEngineConfigApplier::reconcile_sinks(
            const std::vector<AppliedSinkParams>& desired_sinks,
            std::vector<std::string>& sink_ids_to_remove,
            std::vector<AppliedSinkParams>& sinks_to_add,
            std::vector<AppliedSinkParams>& sinks_to_update) {
            // TODO: Implement logic in Task 02_03
            // For now, can log that it was called.
            std::cout << "[ConfigApplier] reconcile_sinks called." << std::endl;
        }
        ```
    *   Do this for all helper methods listed in Task `02_01`.

**Example `audio_engine_config_applier.cpp` Structure (Shell):**

```cpp
// src/configuration/audio_engine_config_applier.cpp
#include "src/configuration/audio_engine_config_applier.h"
#include <iostream> // Temporary for logging
#include <algorithm> // For std::find_if, etc. later

// Define a simple logger macro for now, replace with proper logger later
#define LOG_APPLIER(msg) std::cout << "[ConfigApplier] " << msg << std::endl
#define LOG_APPLIER_ERROR(msg) std::cerr << "[ConfigApplier ERROR] " << msg << std::endl

namespace screamrouter {
namespace config {

AudioEngineConfigApplier::AudioEngineConfigApplier(audio::AudioManager& audio_manager)
    : audio_manager_(audio_manager) {
    LOG_APPLIER("AudioEngineConfigApplier created.");
    // active_source_paths_ and active_sinks_ are default initialized (empty)
}

AudioEngineConfigApplier::~AudioEngineConfigApplier() {
    LOG_APPLIER("AudioEngineConfigApplier destroyed.");
}

bool AudioEngineConfigApplier::apply_state(const DesiredEngineState& desired_state) {
    LOG_APPLIER("Applying new engine state...");

    std::vector<std::string> sink_ids_to_remove;
    std::vector<AppliedSinkParams> sinks_to_add;
    std::vector<AppliedSinkParams> sinks_to_update;

    std::vector<std::string> path_ids_to_remove;
    std::vector<AppliedSourcePathParams> paths_to_add; // Will hold paths that need new SourceInputProcessors
    std::vector<AppliedSourcePathParams> paths_to_update; // Paths whose parameters (vol, EQ) changed

    // 1. Reconcile to find differences
    reconcile_sinks(desired_state.sinks, sink_ids_to_remove, sinks_to_add, sinks_to_update);
    reconcile_source_paths(desired_state.source_paths, path_ids_to_remove, paths_to_add, paths_to_update);

    // 2. Process removals first
    // Order: Remove connections related to paths being removed, then remove paths, then remove sinks.
    // However, AudioManager handles disconnecting sources when a sink is removed,
    // and disconnecting sinks when a source is removed.
    // Simpler order: remove source paths, then remove sinks.
    LOG_APPLIER("Processing source path removals...");
    process_source_path_removals(path_ids_to_remove);

    LOG_APPLIER("Processing sink removals...");
    process_sink_removals(sink_ids_to_remove);

    // 3. Process additions
    LOG_APPLIER("Processing source path additions...");
    for (auto& path_param : paths_to_add) { // Pass by reference to update generated_instance_id
        if (process_source_path_addition(path_param)) {
            // Successfully added, update our active state
            active_source_paths_[path_param.path_id] = {path_param};
        } else {
            LOG_APPLIER_ERROR("Failed to add source path: " + path_param.path_id);
            // Handle error - potentially stop further processing or mark overall failure
        }
    }

    LOG_APPLIER("Processing sink additions...");
    process_sink_additions(sinks_to_add); // This will also handle initial connections

    // 4. Process updates (parameter changes for existing items, connection changes for existing sinks)
    LOG_APPLIER("Processing source path updates...");
    process_source_path_updates(paths_to_update);
    
    LOG_APPLIER("Processing sink updates (connections)...");
    process_sink_updates(sinks_to_update);


    LOG_APPLIER("Engine state application finished.");
    return true; // Placeholder
}

// --- Shell Implementations for Helper Methods ---

void AudioEngineConfigApplier::reconcile_sinks(
    const std::vector<AppliedSinkParams>& desired_sinks,
    std::vector<std::string>& sink_ids_to_remove,
    std::vector<AppliedSinkParams>& sinks_to_add,
    std::vector<AppliedSinkParams>& sinks_to_update) {
    LOG_APPLIER("reconcile_sinks called (shell).");
    // Implementation in Task 02_03
}

void AudioEngineConfigApplier::reconcile_source_paths(
    const std::vector<AppliedSourcePathParams>& desired_source_paths,
    std::vector<std::string>& path_ids_to_remove,
    std::vector<AppliedSourcePathParams>& paths_to_add,
    std::vector<AppliedSourcePathParams>& paths_to_update) {
    LOG_APPLIER("reconcile_source_paths called (shell).");
    // Implementation in Task 02_04
}

void AudioEngineConfigApplier::process_sink_removals(const std::vector<std::string>& sink_ids_to_remove) {
    LOG_APPLIER("process_sink_removals called (shell).");
    // Implementation in Task 02_03
}

void AudioEngineConfigApplier::process_sink_additions(const std::vector<AppliedSinkParams>& sinks_to_add) {
    LOG_APPLIER("process_sink_additions called (shell).");
    // Implementation in Task 02_03
}

void AudioEngineConfigApplier::process_sink_updates(const std::vector<AppliedSinkParams>& sinks_to_update) {
    LOG_APPLIER("process_sink_updates called (shell).");
    // Implementation in Task 02_03 / 02_05
}

void AudioEngineConfigApplier::process_source_path_removals(const std::vector<std::string>& path_ids_to_remove) {
    LOG_APPLIER("process_source_path_removals called (shell).");
    // Implementation in Task 02_04
}

// Changed from vector to single item, returns bool for success, takes non-const ref
bool AudioEngineConfigApplier::process_source_path_addition(AppliedSourcePathParams& path_to_add) {
    LOG_APPLIER("process_source_path_addition for path_id: " + path_to_add.path_id + " (shell).");
    // Implementation in Task 02_04
    // This method will call audio_manager_.configure_source()
    // and update path_to_add.generated_instance_id
    // For shell:
    // path_to_add.generated_instance_id = "temp_instance_id_for_" + path_to_add.path_id; // Placeholder
    return true; // Placeholder
}

void AudioEngineConfigApplier::process_source_path_updates(const std::vector<AppliedSourcePathParams>& paths_to_update) {
    LOG_APPLIER("process_source_path_updates called (shell).");
    // Implementation in Task 02_04
}

void AudioEngineConfigApplier::reconcile_connections_for_sink(const InternalSinkState& current_sink_state, const AppliedSinkParams& desired_sink_params) {
    LOG_APPLIER("reconcile_connections_for_sink for sink_id: " + desired_sink_params.sink_id + " (shell).");
    // Implementation in Task 02_05
}


} // namespace config
} // namespace screamrouter
```

**Acceptance Criteria:**

*   The file `src/configuration/audio_engine_config_applier.cpp` is created.
*   The constructor `AudioEngineConfigApplier::AudioEngineConfigApplier` is implemented, initializing `audio_manager_` and logging creation.
*   The destructor `AudioEngineConfigApplier::~AudioEngineConfigApplier` is implemented (can be empty with a log message).
*   A shell for the `bool AudioEngineConfigApplier::apply_state(const DesiredEngineState& desired_state)` method is implemented, including declarations for intermediate vectors and calls to (shell) helper methods in a logical order.
*   Shell implementations (e.g., just a log message) are provided for all private helper methods declared in `audio_engine_config_applier.h`.
*   The file compiles successfully when linked with the header and other necessary components (though full functionality awaits implementation of helper methods).
