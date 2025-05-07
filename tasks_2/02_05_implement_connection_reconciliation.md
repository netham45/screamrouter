# Task 02_05: Implement Connection Reconciliation Logic

**Objective:** Implement the `AudioEngineConfigApplier::reconcile_connections_for_sink` helper method. This method is responsible for comparing the desired source path connections for a specific sink with its current connections (as tracked by the applier's internal state) and calling the appropriate `AudioManager` methods (`connect_source_sink` or `disconnect_source_sink`) to match the desired state.

**File to Modify:** `src/configuration/audio_engine_config_applier.cpp`

**Method to Implement:**

1.  `AudioEngineConfigApplier::reconcile_connections_for_sink(...)`

**Details:**

**1. `AudioEngineConfigApplier::reconcile_connections_for_sink`**

*   **Signature (Update):** Let's refine the signature slightly from the header shell. It needs the *desired* state for the sink and access to the *current* internal state map (`active_source_paths_`) to look up `instance_id`s. It should also update the *current* internal state for the sink after making changes.
    ```cpp
    // In audio_engine_config_applier.h (adjust declaration if needed)
    // void reconcile_connections_for_sink(const std::string& sink_id, const std::vector<std::string>& desired_path_ids); 
    // Let's pass the full desired sink params for context, and modify the internal state directly.
    void reconcile_connections_for_sink(const AppliedSinkParams& desired_sink_params);

    // In audio_engine_config_applier.cpp (Implementation)
    void AudioEngineConfigApplier::reconcile_connections_for_sink(const AppliedSinkParams& desired_sink_params) 
    ```
*   **Logic:**
    1.  Get the `sink_id` from `desired_sink_params.sink_id`.
    2.  Find the current internal state for this sink:
        *   `auto current_sink_state_it = active_sinks_.find(sink_id);`
        *   If `current_sink_state_it == active_sinks_.end()`, log an error ("Cannot reconcile connections for unknown sink") and return. This shouldn't happen if called correctly after sink addition/update.
        *   `InternalSinkState& current_sink_state = current_sink_state_it->second;`
    3.  Get the set of currently connected source path IDs from the internal state:
        *   `const std::vector<std::string>& current_path_ids_vec = current_sink_state.params.connected_source_path_ids;`
        *   Convert to a set for easier comparison: `std::set<std::string> current_path_ids_set(current_path_ids_vec.begin(), current_path_ids_vec.end());`
    4.  Get the set of desired source path IDs:
        *   `const std::vector<std::string>& desired_path_ids_vec = desired_sink_params.connected_source_path_ids;`
        *   Convert to a set: `std::set<std::string> desired_path_ids_set(desired_path_ids_vec.begin(), desired_path_ids_vec.end());`
    5.  **Identify Connections to Add:**
        *   Iterate through `desired_path_ids_set`.
        *   For each `desired_path_id`, check if it exists in `current_path_ids_set`.
        *   If **NOT** found in `current_path_ids_set`:
            *   This connection needs to be added.
            *   Look up the `generated_instance_id` for this `desired_path_id` in `active_source_paths_`:
                *   `auto source_path_it = active_source_paths_.find(desired_path_id);`
                *   If `source_path_it == active_source_paths_.end()` or `source_path_it->second.params.generated_instance_id.empty()`:
                    *   `LOG_APPLIER_ERROR("Cannot connect path " + desired_path_id + " to sink " + sink_id + ": Source path or its instance_id not found/generated.");`
                    *   Continue to the next `desired_path_id`.
                *   `const std::string& source_instance_id = source_path_it->second.params.generated_instance_id;`
            *   Call `audio_manager_.connect_source_sink(source_instance_id, sink_id);`. Handle return value (log error if `false`).
    6.  **Identify Connections to Remove:**
        *   Iterate through `current_path_ids_set`.
        *   For each `current_path_id`, check if it exists in `desired_path_ids_set`.
        *   If **NOT** found in `desired_path_ids_set`:
            *   This connection needs to be removed.
            *   Look up the `generated_instance_id` for this `current_path_id` in `active_source_paths_` (it *should* exist if it was previously connected).
                *   `auto source_path_it = active_source_paths_.find(current_path_id);`
                *   If `source_path_it == active_source_paths_.end()` or `source_path_it->second.params.generated_instance_id.empty()`:
                    *   `LOG_APPLIER_ERROR("Cannot disconnect path " + current_path_id + " from sink " + sink_id + ": Source path or its instance_id not found in active state (this indicates an inconsistency).");`
                    *   Continue to the next `current_path_id`.
                *   `const std::string& source_instance_id = source_path_it->second.params.generated_instance_id;`
            *   Call `audio_manager_.disconnect_source_sink(source_instance_id, sink_id);`. Handle return value.
    7.  **Update Internal State:** After processing all additions and removals for this sink, update the internal state to reflect the desired state:
        *   `current_sink_state.params.connected_source_path_ids = desired_sink_params.connected_source_path_ids;`

**Acceptance Criteria:**

*   The `reconcile_connections_for_sink` method is implemented in `audio_engine_config_applier.cpp`.
*   The method correctly identifies source paths that need to be connected to the given sink based on the difference between `desired_sink_params.connected_source_path_ids` and the current state stored in `active_sinks_`.
*   The method correctly identifies source paths that need to be disconnected.
*   For each connection/disconnection, the method correctly looks up the corresponding `generated_instance_id` from `active_source_paths_`.
*   The method calls `audio_manager_.connect_source_sink()` for additions and `audio_manager_.disconnect_source_sink()` for removals, using the correct `source_instance_id` and `sink_id`.
*   Errors during lookup or `AudioManager` calls are logged.
*   The internal state (`active_sinks_[sink_id].params.connected_source_path_ids`) is updated to match the `desired_sink_params` after the operations are performed.
