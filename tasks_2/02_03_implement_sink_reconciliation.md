# Task 02_03: Implement Sink Reconciliation Logic

**Objective:** Implement the methods in `AudioEngineConfigApplier` responsible for reconciling the desired state of sinks with the current active sinks. This includes identifying sinks to be added, removed, or updated, and then processing these changes by calling the appropriate `AudioManager` methods.

**File to Modify:** `src/configuration/audio_engine_config_applier.cpp`

**Methods to Implement:**

1.  `AudioEngineConfigApplier::reconcile_sinks(...)`
2.  `AudioEngineConfigApplier::process_sink_removals(...)`
3.  `AudioEngineConfigApplier::process_sink_additions(...)`
4.  `AudioEngineConfigApplier::process_sink_updates(...)`

**Details:**

**1. `AudioEngineConfigApplier::reconcile_sinks`**

*   **Signature:**
    ```cpp
    void AudioEngineConfigApplier::reconcile_sinks(
        const std::vector<AppliedSinkParams>& desired_sinks,
        std::vector<std::string>& sink_ids_to_remove,
        std::vector<AppliedSinkParams>& sinks_to_add,
        std::vector<AppliedSinkParams>& sinks_to_update)
    ```
*   **Logic:**
    *   Clear the output vectors (`sink_ids_to_remove`, `sinks_to_add`, `sinks_to_update`).
    *   **Identify sinks to remove:** Iterate through `active_sinks_` (the internal map `std::map<std::string, InternalSinkState>`). For each active sink, check if its `sink_id` exists in the `desired_sinks` list.
        *   If an active sink's ID is NOT found in `desired_sinks`, add its `sink_id` to `sink_ids_to_remove`.
    *   **Identify sinks to add or update:** Iterate through `desired_sinks`. For each `desired_sink_param`:
        *   Check if `desired_sink_param.sink_id` exists as a key in `active_sinks_`.
            *   If NOT, this is a new sink. Add `desired_sink_param` to `sinks_to_add`.
            *   If YES, this sink exists. Compare `desired_sink_param` with `active_sinks_.at(desired_sink_param.sink_id).params`.
                *   Comparison should check:
                    *   `sink_engine_config` members (IP, port, bit_depth, samplerate, channels, chlayout1, chlayout2, use_tcp).
                    *   `connected_source_path_ids` (compare the sets of connected path IDs).
                *   If any difference is found, add `desired_sink_param` to `sinks_to_update`.
                *   *Note on comparing `audio::SinkConfig`*: You might need to implement an `operator==` for `audio::SinkConfig` or do a field-by-field comparison.
                *   *Note on comparing `connected_source_path_ids`*: Convert both vectors to `std::set<std::string>` for efficient comparison if order doesn't matter, or sort and compare if order matters (though for connections, set comparison is usually sufficient).

**2. `AudioEngineConfigApplier::process_sink_removals`**

*   **Signature:**
    ```cpp
    void AudioEngineConfigApplier::process_sink_removals(const std::vector<std::string>& sink_ids_to_remove)
    ```
*   **Logic:**
    *   Iterate through `sink_ids_to_remove`.
    *   For each `sink_id`:
        *   Log the removal.
        *   Call `audio_manager_.remove_sink(sink_id)`.
        *   Remove the entry from `active_sinks_` map: `active_sinks_.erase(sink_id)`.
        *   Handle potential errors from `remove_sink` (e.g., if it returns `false`).

**3. `AudioEngineConfigApplier::process_sink_additions`**

*   **Signature:**
    ```cpp
    void AudioEngineConfigApplier::process_sink_additions(const std::vector<AppliedSinkParams>& sinks_to_add)
    ```
*   **Logic:**
    *   Iterate through `sinks_to_add`.
    *   For each `sink_param_to_add`:
        *   Log the addition.
        *   Call `audio_manager_.add_sink(sink_param_to_add.sink_engine_config)`.
        *   If `add_sink` is successful (returns `true`):
            *   Create an `InternalSinkState` entry:
                *   `InternalSinkState new_internal_sink_state;`
                *   `new_internal_sink_state.params = sink_param_to_add;` // Store the full desired params
                *   `new_internal_sink_state.params.connected_source_path_ids.clear();` // Connections will be handled by reconcile_connections_for_sink
            *   Add to `active_sinks_`: `active_sinks_[sink_param_to_add.sink_id] = new_internal_sink_state;`
            *   **Crucially, after adding the sink to `AudioManager` and `active_sinks_`, immediately reconcile its connections:**
                *   Call `reconcile_connections_for_sink(active_sinks_.at(sink_param_to_add.sink_id), sink_param_to_add);`
                *   (The first argument is the current state (empty connections), second is desired state from `sink_param_to_add`).
        *   Else (if `add_sink` fails):
            *   Log an error.
            *   Decide on error handling (e.g., stop processing, mark overall `apply_state` as failed).

**4. `AudioEngineConfigApplier::process_sink_updates`**

*   **Signature:**
    ```cpp
    void AudioEngineConfigApplier::process_sink_updates(const std::vector<AppliedSinkParams>& sinks_to_update)
    ```
*   **Logic:**
    *   Iterate through `sinks_to_update`.
    *   For each `desired_sink_param`:
        *   Log the update for `desired_sink_param.sink_id`.
        *   Retrieve the current internal state: `InternalSinkState& current_internal_state = active_sinks_.at(desired_sink_param.sink_id);`
        *   **Parameter Change Handling:**
            *   Compare `desired_sink_param.sink_engine_config` with `current_internal_state.params.sink_engine_config`.
            *   If any core parameters of `sink_engine_config` (like IP, port, format) have changed:
                *   `AudioManager` currently lacks a granular `update_sink` method for these parameters.
                *   **Strategy:** The most straightforward (though disruptive) way is to remove and re-add the sink.
                    *   `LOG_APPLIER("Core sink parameters changed for " + desired_sink_param.sink_id + ". Re-adding sink.");`
                    *   `audio_manager_.remove_sink(desired_sink_param.sink_id);`
                    *   `audio_manager_.add_sink(desired_sink_param.sink_engine_config);`
                    *   `current_internal_state.params.sink_engine_config = desired_sink_param.sink_engine_config;`
                    *   `current_internal_state.params.connected_source_path_ids.clear(); // Connections will be re-established by reconcile_connections_for_sink`
                *   If `add_sink` fails, log error.
        *   **Connection Reconciliation (always do this for updated sinks, as connections might be the only change, or parameters changed requiring re-connection):**
            *   Call `reconcile_connections_for_sink(current_internal_state, desired_sink_param);`
            *   After `reconcile_connections_for_sink` updates `AudioManager`, ensure `current_internal_state.params.connected_source_path_ids` is updated to match `desired_sink_param.connected_source_path_ids`. This will be handled inside `reconcile_connections_for_sink` or immediately after.
        *   Update the rest of the `current_internal_state.params` if other non-`sink_engine_config` parts of `AppliedSinkParams` could change (though currently it's mostly `sink_engine_config` and connections).

**Helper: `operator==` for `audio::SinkConfig` (Suggestion)**

*   To simplify comparisons in `reconcile_sinks` and `process_sink_updates`, consider adding an `operator==` to the `audio::SinkConfig` struct in `src/audio_engine/audio_types.h`.
    ```cpp
    // In src/audio_engine/audio_types.h, inside struct SinkConfig
    bool operator==(const SinkConfig& other) const {
        return id == other.id && // Though id might not be part of sink_engine_config directly
               output_ip == other.output_ip &&
               output_port == other.output_port &&
               bitdepth == other.bitdepth &&
               samplerate == other.samplerate &&
               channels == other.channels &&
               chlayout1 == other.chlayout1 &&
               chlayout2 == other.chlayout2 &&
               use_tcp == other.use_tcp &&
               enable_mp3 == other.enable_mp3; // Add other fields if any
    }
    ```

**Acceptance Criteria:**

*   The `reconcile_sinks` method correctly identifies sinks to be added, removed, or updated by comparing `desired_sinks` with `active_sinks_`.
*   The `process_sink_removals` method correctly calls `audio_manager_.remove_sink()` and updates `active_sinks_`.
*   The `process_sink_additions` method correctly calls `audio_manager_.add_sink()`, updates `active_sinks_`, and triggers connection reconciliation for the new sink.
*   The `process_sink_updates` method handles changes to sink parameters (potentially by re-adding the sink) and triggers connection reconciliation.
*   Logging is added for significant actions and errors.
*   The internal state `active_sinks_` is consistently maintained.
