# Task 02_04: Implement Source Path Reconciliation Logic

**Objective:** Implement the methods in `AudioEngineConfigApplier` responsible for reconciling the desired state of source paths (source-to-sink configurations) with the currently active ones. This involves identifying paths to be added, removed, or updated, and then processing these changes by calling the appropriate `AudioManager` methods.

**File to Modify:** `src/configuration/audio_engine_config_applier.cpp`

**Methods to Implement/Update:**

1.  `AudioEngineConfigApplier::reconcile_source_paths(...)`
2.  `AudioEngineConfigApplier::process_source_path_removals(...)`
3.  `AudioEngineConfigApplier::process_source_path_addition(...)` (was a shell, now full implementation)
4.  `AudioEngineConfigApplier::process_source_path_updates(...)`

**Helper: `operator==` for `AppliedSourcePathParams` (Conceptual)**

To simplify comparisons in `reconcile_source_paths`, it's useful to define what makes two `AppliedSourcePathParams` "equal" in terms of requiring an update or being considered the same. The `generated_instance_id` should be excluded from this specific comparison as it's an output of the process.

```cpp
// Helper function (can be a lambda or a free function, or a member of AppliedSourcePathParams if modified)
// Compares all fields relevant for determining if an update is needed, excluding generated_instance_id.
bool compare_applied_source_path_params(const AppliedSourcePathParams& a, const AppliedSourcePathParams& b) {
    return a.source_tag == b.source_tag &&
           a.target_sink_id == b.target_sink_id &&
           a.volume == b.volume && // Consider float comparison tolerance if necessary
           a.eq_values == b.eq_values &&
           a.delay_ms == b.delay_ms &&
           a.timeshift_sec == b.timeshift_sec && // Float comparison
           a.target_output_channels == b.target_output_channels &&
           a.target_output_samplerate == b.target_output_samplerate;
}
```

**Details:**

**1. `AudioEngineConfigApplier::reconcile_source_paths`**

*   **Signature:**
    ```cpp
    void AudioEngineConfigApplier::reconcile_source_paths(
        const std::vector<AppliedSourcePathParams>& desired_source_paths,
        std::vector<std::string>& path_ids_to_remove,
        std::vector<AppliedSourcePathParams>& paths_to_add,
        std::vector<AppliedSourcePathParams>& paths_to_update)
    ```
*   **Logic:**
    *   Clear output vectors.
    *   **Identify paths to remove:** Iterate through `active_source_paths_` (map of `path_id` to `InternalSourcePathState`). For each active path, check if its `path_id` exists in `desired_source_paths` (e.g., by checking `item.path_id`).
        *   If an active path's ID is NOT found in `desired_source_paths`, add its `path_id` to `path_ids_to_remove`.
    *   **Identify paths to add or update:** Iterate through `desired_source_paths`. For each `desired_path_param`:
        *   Check if `desired_path_param.path_id` exists as a key in `active_source_paths_`.
            *   If NOT, this is a new path. Add `desired_path_param` to `paths_to_add`.
            *   If YES, this path exists. Compare `desired_path_param` with `active_source_paths_.at(desired_path_param.path_id).params` using a helper like `compare_applied_source_path_params`.
                *   If they are different, add `desired_path_param` to `paths_to_update`.

**2. `AudioEngineConfigApplier::process_source_path_removals`**

*   **Signature:**
    ```cpp
    void AudioEngineConfigApplier::process_source_path_removals(const std::vector<std::string>& path_ids_to_remove)
    ```
*   **Logic:**
    *   Iterate through `path_ids_to_remove`.
    *   For each `path_id`:
        *   Check if `path_id` exists in `active_source_paths_`.
        *   If yes:
            *   `const std::string& instance_id = active_source_paths_.at(path_id).params.generated_instance_id;`
            *   If `instance_id` is not empty (it should have been generated when added):
                *   Log removal of path and instance.
                *   Call `audio_manager_.remove_source(instance_id)`. Handle return value (e.g., log error if `false`).
            *   Else:
                *   `LOG_APPLIER_ERROR("Path " + path_id + " marked for removal but has no generated_instance_id in active state.");`
            *   Remove from `active_source_paths_`: `active_source_paths_.erase(path_id)`.
        *   If no:
            *   `LOG_APPLIER_ERROR("Path " + path_id + " marked for removal but not found in active_source_paths_.");`

**3. `AudioEngineConfigApplier::process_source_path_addition` (Full Implementation)**

*   **Signature:**
    ```cpp
    bool AudioEngineConfigApplier::process_source_path_addition(AppliedSourcePathParams& path_param_to_add) // Note: non-const ref
    ```
*   **Logic:**
    *   Log addition attempt for `path_param_to_add.path_id`.
    *   Create `audio::SourceConfig cpp_source_config;` (from `src/audio_engine/audio_types.h`).
        *   `cpp_source_config.tag = path_param_to_add.source_tag;`
        *   `cpp_source_config.initial_volume = path_param_to_add.volume;`
        *   `cpp_source_config.initial_eq = path_param_to_add.eq_values;` (Ensure `eq_values` is correctly sized, `AppliedSourcePathParams` constructor should handle this).
        *   `cpp_source_config.initial_delay_ms = path_param_to_add.delay_ms;`
        *   `cpp_source_config.target_output_channels = path_param_to_add.target_output_channels;`
        *   `cpp_source_config.target_output_samplerate = path_param_to_add.target_output_samplerate;`
    *   Call `std::string instance_id = audio_manager_.configure_source(cpp_source_config);`
    *   If `instance_id.empty()`:
        *   `LOG_APPLIER_ERROR("AudioManager failed to configure source for path_id: " + path_param_to_add.path_id + " with source_tag: " + path_param_to_add.source_tag);`
        *   `path_param_to_add.generated_instance_id.clear();`
        *   Return `false`.
    *   Else (success):
        *   `LOG_APPLIER("Successfully configured source for path_id: " + path_param_to_add.path_id + ", got instance_id: " + instance_id);`
        *   `path_param_to_add.generated_instance_id = instance_id;`
        *   **(Important: The caller, `apply_state`, is responsible for adding this `path_param_to_add` to `active_source_paths_` map after this function returns true.)**
        *   Return `true`.

**4. `AudioEngineConfigApplier::process_source_path_updates`**

*   **Signature:**
    ```cpp
    void AudioEngineConfigApplier::process_source_path_updates(const std::vector<AppliedSourcePathParams>& paths_to_update)
    ```
*   **Logic:**
    *   Iterate through `paths_to_update`.
    *   For each `desired_path_param`:
        *   Check if `desired_path_param.path_id` exists in `active_source_paths_`. If not, log an error and continue (shouldn't happen if reconciliation is correct).
        *   `InternalSourcePathState& current_path_state = active_source_paths_.at(desired_path_param.path_id);`
        *   `const std::string& instance_id = current_path_state.params.generated_instance_id;`
        *   If `instance_id.empty()`, log error ("Cannot update path without instance_id") and continue.
        *   **Check for fundamental changes** (output format, source_tag, target_sink_id - though target_sink_id change implies connection change, not processor change itself unless output format also changes due to it):
            *   If `current_path_state.params.source_tag != desired_path_param.source_tag ||`
               `current_path_state.params.target_output_channels != desired_path_param.target_output_channels ||`
               `current_path_state.params.target_output_samplerate != desired_path_param.target_output_samplerate`:
                *   `LOG_APPLIER("Fundamental change for path " + desired_path_param.path_id + ". Re-creating SourceInputProcessor.");`
                *   `audio_manager_.remove_source(instance_id);` // Remove old one
                *   `active_source_paths_.erase(desired_path_param.path_id);` // Remove from active state
                *   `AppliedSourcePathParams temp_param_for_add = desired_path_param; // Make a mutable copy`
                *   If `process_source_path_addition(temp_param_for_add)` is successful:
                    *   `active_source_paths_[temp_param_for_add.path_id] = {temp_param_for_add};` // Add new state
                    *   `LOG_APPLIER("Path " + desired_path_param.path_id + " re-created with new instance_id: " + temp_param_for_add.generated_instance_id);`
                    *   **(Connections for this new instance_id will need to be re-established by the sink update/connection logic later in `apply_state`)**
                *   Else:
                    *   `LOG_APPLIER_ERROR("Failed to re-create source path " + desired_path_param.path_id + " after fundamental change.");`
                *   Continue to the next path in `paths_to_update`.
        *   **Process parameter updates (if not re-created):**
            *   If `current_path_state.params.volume != desired_path_param.volume`:
                *   `audio_manager_.update_source_volume(instance_id, desired_path_param.volume);`
            *   If `current_path_state.params.eq_values != desired_path_param.eq_values`:
                *   `audio_manager_.update_source_equalizer(instance_id, desired_path_param.eq_values);`
            *   If `current_path_state.params.delay_ms != desired_path_param.delay_ms`:
                *   `audio_manager_.update_source_delay(instance_id, desired_path_param.delay_ms);`
            *   If `current_path_state.params.timeshift_sec != desired_path_param.timeshift_sec`:
                *   `audio_manager_.update_source_timeshift(instance_id, desired_path_param.timeshift_sec);`
        *   After all updates (or if no updates were needed beyond fundamental ones), update the stored state:
            *   `current_path_state.params = desired_path_param; // This ensures generated_instance_id is preserved if not re-created`
            *   If it was re-created, this assignment happens when `process_source_path_addition` succeeds and updates `active_source_paths_`.

**Acceptance Criteria:**

*   `reconcile_source_paths` correctly identifies paths to add, remove, and update based on `path_id` and parameter comparison.
*   `process_source_path_removals` correctly calls `audio_manager_.remove_source()` using the stored `generated_instance_id` and updates `active_source_paths_`.
*   `process_source_path_addition` correctly:
    *   Constructs an `audio::SourceConfig` with the correct `source_tag` and target output format.
    *   Calls `audio_manager_.configure_source()`.
    *   Updates the `generated_instance_id` in the passed `AppliedSourcePathParams` reference on success.
    *   Returns `true` on success, `false` on failure.
*   `process_source_path_updates` correctly:
    *   Identifies fundamental changes (output format, source_tag) and handles them by removing the old `SourceInputProcessor` and triggering the addition of a new one.
    *   Calls `AudioManager` update methods (volume, EQ, delay, timeshift) for non-fundamental parameter changes.
    *   Updates `active_source_paths_` to reflect the changes.
*   Logging is present for key actions and errors.
*   The internal state `active_source_paths_` is consistently maintained.
