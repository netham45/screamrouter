# Task 04_03: Call `apply_state` from `ConfigurationManager.py`

**Objective:** Modify the main configuration processing logic in `ConfigurationManager.py` (likely within `__process_and_apply_configuration` or the main `run` loop) to call the new translation method (`_translate_config_to_cpp_desired_state`) and then pass the resulting C++ `DesiredEngineState` object to the `apply_state` method of the C++ `AudioEngineConfigApplier`.

**File to Modify:** `src/configuration/configuration_manager.py`

**Details:**

1.  **Locate Configuration Application Point:**
    *   Find the place where `ConfigurationManager` currently applies the solved configuration. This is likely within the `__process_and_apply_configuration` method, after `self.active_configuration = ConfigurationSolver(...)` is called, or potentially within the main `run` loop where configuration reloads are triggered.
    *   Currently, this section might be comparing old/new Python configurations and directly managing Python `AudioController` instances. This logic will be replaced or significantly augmented by the call to the C++ applier.

2.  **Call Translation Method:**
    *   After solving the configuration (`self.active_configuration = ConfigurationSolver(...)`), call the translation method created in Task `04_02`:
        ```python
        cpp_desired_state = self._translate_config_to_cpp_desired_state()
        ```

3.  **Call C++ `apply_state`:**
    *   Check if the translation was successful and the C++ applier exists.
    *   If yes, call the `apply_state` method on the `self.cpp_config_applier` instance.
    *   Handle the boolean return value (log success or failure).

    ```python
    # Within __process_and_apply_configuration or similar method

        # ... (solve configuration into self.active_configuration) ...
        _logger.debug("[Configuration Manager] Configuration solved.")

        # --- NEW C++ ENGINE APPLICATION ---
        if self.cpp_config_applier: # Check if C++ applier was initialized
            _logger.info("[Configuration Manager] Translating configuration for C++ engine...")
            cpp_desired_state = self._translate_config_to_cpp_desired_state()

            if cpp_desired_state:
                try:
                    _logger.info("[Configuration Manager] Applying translated state to C++ engine...")
                    success = self.cpp_config_applier.apply_state(cpp_desired_state)
                    if success:
                        _logger.info("[Configuration Manager] C++ engine state applied successfully.")
                    else:
                        _logger.error("[Configuration Manager] C++ AudioEngineConfigApplier reported failure during apply_state.")
                        # Decide on further error handling if needed
                except Exception as e:
                    _logger.exception("[Configuration Manager] Exception calling C++ apply_state: %s", e)
            else:
                _logger.warning("[Configuration Manager] Failed to translate configuration to C++ DesiredEngineState.")
        else:
            _logger.warning("[Configuration Manager] C++ Config Applier not available. Skipping C++ engine configuration.")
        # --- END NEW C++ ENGINE APPLICATION ---

        # --- Existing Python Logic (To be reviewed/removed in Task 04_04) ---
        # The old logic comparing Python Sink/SourceDescriptions and managing 
        # Python AudioControllers might still be here. It needs to be evaluated
        # whether this is still needed or if the C++ engine now handles everything.
        # For now, we leave it but note it needs review.
        
        # Example of where old logic might be:
        # added_sinks, removed_sinks, changed_sinks = self.__find_added_removed_changed_sinks() 
        # ... (code managing self.audio_controllers based on these diffs) ...
        # ... (code managing self.scream_receiver, etc.) ...

        # --- End Existing Python Logic ---

        self.__save_config() # Keep saving the Python config representation

        # ... (plugin manager reload, etc.) ...
    ```

4.  **Consider Interaction with Existing Python Logic:**
    *   The call to `self.cpp_config_applier.apply_state()` now handles the configuration of the C++ audio engine components.
    *   The existing Python code in `__process_and_apply_configuration` that calculates differences (`__find_added_removed_changed_sinks`) and manages `self.audio_controllers` (the Python `AudioController` instances) needs careful review (Task `04_04`).
    *   If the C++ engine completely replaces the functionality previously handled by Python `AudioController`s (like managing ffmpeg/lame), then the Python diffing and `AudioController` management code should be removed.
    *   If some sinks/sources are still handled purely in Python (e.g., by plugins that don't use the C++ engine), then some Python-level management might need to remain, but it should avoid conflicting with the C++ engine's state.

**Acceptance Criteria:**

*   The `ConfigurationManager` calls the `_translate_config_to_cpp_desired_state` method after solving the configuration.
*   If the translation is successful and `self.cpp_config_applier` exists, the `apply_state` method is called on `self.cpp_config_applier` with the translated `DesiredEngineState`.
*   The boolean result from `apply_state` is checked and logged.
*   Error handling is added for potential exceptions during the C++ call.
*   The placement of this new logic relative to existing Python configuration application logic is considered (pending review in Task `04_04`).
