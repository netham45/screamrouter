# Task 02_01: Define `AudioEngineConfigApplier` Class Structure

**Objective:** Create the header file `audio_engine_config_applier.h` in the `src/configuration/` directory and define the basic structure of the `AudioEngineConfigApplier` class. This includes its constructor, public methods, and private member variables for managing state and interacting with `AudioManager`.

**File to Create:** `src/configuration/audio_engine_config_applier.h`

**Namespace:** `screamrouter::config`

**Details:**

1.  **Include Guards and Necessary Headers:**
    *   Standard include guards (`#ifndef AUDIO_ENGINE_CONFIG_APPLIER_H`, etc.).
    *   Include `<string>`, `<vector>`, `<map>`, `<set>` for data structures.
    *   Include `src/audio_engine/audio_manager.h` to reference `screamrouter::audio::AudioManager`.
    *   Include `src/configuration/audio_engine_config_types.h` (created in Task `01_01`) for `DesiredEngineState`, `AppliedSourcePathParams`, etc.
    *   Include `src/audio_engine/audio_types.h` for `audio::SourceConfig` and `audio::SinkConfig` if they are used directly in the internal state representation.

2.  **`AudioEngineConfigApplier` Class Definition:**
    *   Declare the class within the `screamrouter::config` namespace.
    *   **Public Interface:**
        *   `explicit AudioEngineConfigApplier(audio::AudioManager& audio_manager);`: Constructor taking a reference to an `AudioManager` instance. Mark as `explicit` to prevent unintended conversions.
        *   `bool apply_state(const DesiredEngineState& desired_state);`: The main method to apply a new configuration.
        *   **(Future consideration, not for initial implementation):** `DesiredEngineState get_current_engine_state() const;` - A method to retrieve the current state as understood by the applier. This might be useful for debugging or if Python needs to query the C++ side's understanding.
    *   **Private Members:**
        *   `audio::AudioManager& audio_manager_;`: A reference to the `AudioManager` instance it controls.
        *   **Internal State Representation (Shadow State):** These members will store the `AudioEngineConfigApplier`'s understanding of the current state of the `AudioManager` as a result of its own operations.
            *   `struct InternalSourcePathState {`:
                *   `AppliedSourcePathParams params; // Stores all params including target_output_format, volume, eq, etc.`
                *   `// No need to store generated_instance_id separately if it's already in params.`
                *   `// bool is_active; // Or rely on presence in the map.`
            `};`
            *   `std::map<std::string, InternalSourcePathState> active_source_paths_; // Key: path_id (from AppliedSourcePathParams)`
            *   `struct InternalSinkState {`:
                *   `AppliedSinkParams params; // Stores sink_engine_config and connected_source_path_ids`
                *   `// bool is_active; // Or rely on presence in the map.`
            `};`
            *   `std::map<std::string, InternalSinkState> active_sinks_; // Key: sink_id`
    *   **Private Helper Methods (Declarations):**
        *   `void reconcile_sinks(const std::vector<AppliedSinkParams>& desired_sinks, std::vector<std::string>& sinks_to_remove, std::vector<AppliedSinkParams>& sinks_to_add, std::vector<AppliedSinkParams>& sinks_to_update);`
        *   `void reconcile_source_paths(const std::vector<AppliedSourcePathParams>& desired_source_paths, std::vector<std::string>& paths_to_remove, std::vector<AppliedSourcePathParams>& paths_to_add, std::vector<AppliedSourcePathParams>& paths_to_update);`
        *   `void process_sink_removals(const std::vector<std::string>& sink_ids_to_remove);`
        *   `void process_sink_additions(const std::vector<AppliedSinkParams>& sinks_to_add);`
        *   `void process_sink_updates(const std::vector<AppliedSinkParams>& sinks_to_update); // Primarily for connections initially`
        *   `void process_source_path_removals(const std::vector<std::string>& path_ids_to_remove);`
        *   `void process_source_path_additions(const std::vector<AppliedSourcePathParams>& paths_to_add);`
        *   `void process_source_path_updates(const std::vector<AppliedSourcePathParams>& paths_to_update);`
        *   `void reconcile_connections_for_sink(const AppliedSinkParams& sink_params); // Helper for sink additions/updates`
        *   `// Potentially a method to clear/reset internal state if needed.`

3.  **Non-Copyable/Non-Movable:**
    *   Delete the copy constructor and copy assignment operator to prevent accidental copying, as the class manages a reference to `AudioManager` and has complex internal state.
        ```cpp
        AudioEngineConfigApplier(const AudioEngineConfigApplier&) = delete;
        AudioEngineConfigApplier& operator=(const AudioEngineConfigApplier&) = delete;
        // Consider if move semantics are needed/safe. For now, deleting them is safer.
        AudioEngineConfigApplier(AudioEngineConfigApplier&&) = delete;
        AudioEngineConfigApplier& operator=(AudioEngineConfigApplier&&) = delete;
        ```

**Example Header Structure:**

```cpp
// src/configuration/audio_engine_config_applier.h
#ifndef AUDIO_ENGINE_CONFIG_APPLIER_H
#define AUDIO_ENGINE_CONFIG_APPLIER_H

#include <string>
#include <vector>
#include <map>
#include <set>

#include "src/audio_engine/audio_manager.h"
#include "src/configuration/audio_engine_config_types.h" 
// No need to include audio_types.h again if audio_engine_config_types.h includes it for SinkConfig

namespace screamrouter {
namespace audio { // Forward declare AudioManager if not fully included via audio_manager.h
    class AudioManager; 
}

namespace config {

class AudioEngineConfigApplier {
public:
    explicit AudioEngineConfigApplier(audio::AudioManager& audio_manager);
    ~AudioEngineConfigApplier(); // Add a destructor declaration

    AudioEngineConfigApplier(const AudioEngineConfigApplier&) = delete;
    AudioEngineConfigApplier& operator=(const AudioEngineConfigApplier&) = delete;
    AudioEngineConfigApplier(AudioEngineConfigApplier&&) = delete;
    AudioEngineConfigApplier& operator=(AudioEngineConfigApplier&&) = delete;

    bool apply_state(const DesiredEngineState& desired_state);

private:
    audio::AudioManager& audio_manager_;

    struct InternalSourcePathState {
        AppliedSourcePathParams params; 
        // generated_instance_id is part of params.AppliedSourcePathParams
    };
    std::map<std::string, InternalSourcePathState> active_source_paths_; // Key: path_id

    struct InternalSinkState {
        AppliedSinkParams params; 
        // connected_source_path_ids is part of params.AppliedSinkParams
    };
    std::map<std::string, InternalSinkState> active_sinks_;     // Key: sink_id

    // Reconciliation helpers
    void reconcile_sinks(
        const std::vector<AppliedSinkParams>& desired_sinks,
        std::vector<std::string>& sink_ids_to_remove,
        std::vector<AppliedSinkParams>& sinks_to_add,
        std::vector<AppliedSinkParams>& sinks_to_update);

    void reconcile_source_paths(
        const std::vector<AppliedSourcePathParams>& desired_source_paths,
        std::vector<std::string>& path_ids_to_remove,
        std::vector<AppliedSourcePathParams>& paths_to_add,
        std::vector<AppliedSourcePathParams>& paths_to_update);

    // Processing helpers
    void process_sink_removals(const std::vector<std::string>& sink_ids_to_remove);
    void process_sink_additions(const std::vector<AppliedSinkParams>& sinks_to_add);
    void process_sink_updates(const std::vector<AppliedSinkParams>& sinks_to_update);
    
    void process_source_path_removals(const std::vector<std::string>& path_ids_to_remove);
    bool process_source_path_addition(AppliedSourcePathParams& path_to_add); // Returns true on success, takes non-const ref to update generated_instance_id
    void process_source_path_updates(const std::vector<AppliedSourcePathParams>& paths_to_update);

    void reconcile_connections_for_sink(const InternalSinkState& current_sink_state, const AppliedSinkParams& desired_sink_params);
};

} // namespace config
} // namespace screamrouter

#endif // AUDIO_ENGINE_CONFIG_APPLIER_H
```
*Self-correction: Added destructor declaration. Clarified that `generated_instance_id` is within `AppliedSourcePathParams`. Refined helper method signatures for clarity and to allow modification of `AppliedSourcePathParams` (e.g., to store `generated_instance_id`). Changed `process_source_path_additions` to `process_source_path_addition` to handle one at a time and allow updating the `generated_instance_id` in the passed-in struct.*

**Acceptance Criteria:**

*   The file `src/configuration/audio_engine_config_applier.h` is created.
*   The `AudioEngineConfigApplier` class is defined within the `screamrouter::config` namespace.
*   The public constructor and `apply_state` method are declared.
*   Private member variables for `audio_manager_`, `active_source_paths_`, and `active_sinks_` are declared with the specified types.
*   Declarations for the private helper methods are included.
*   Copy and move operations are deleted.
*   A destructor is declared.
*   Necessary includes and include guards are present.
