# Task 04_02: Implement Python to C++ State Translation Logic

**Objective:** Develop the Python logic within `ConfigurationManager.py` to translate the solved configuration (output of `ConfigurationSolver`) into the C++ `DesiredEngineState` object, which can then be passed to the `AudioEngineConfigApplier`.

**File to Modify:** `src/configuration/configuration_manager.py`

**Details:**

1.  **Create a Translation Method:**
    *   Define a new private method within the `ConfigurationManager` class, for example, `_translate_config_to_cpp_desired_state(self) -> Optional[screamrouter_audio_engine.DesiredEngineState]:`.
    *   This method will encapsulate the translation logic. It should return the C++ object or `None` if translation fails or the C++ engine isn't available.

2.  **Import C++ Types:**
    *   Ensure the necessary bound C++ types are accessible, either via the main `screamrouter_audio_engine` import or specific imports if structured differently. We need:
        *   `screamrouter_audio_engine.DesiredEngineState`
        *   `screamrouter_audio_engine.AppliedSinkParams`
        *   `screamrouter_audio_engine.AppliedSourcePathParams`
        *   `screamrouter_audio_engine.SinkConfig` (the C++ version, needed for `AppliedSinkParams.sink_engine_config`)
        *   `screamrouter_audio_engine.SourceConfig` (the C++ version, potentially useful for reference but not directly part of `DesiredEngineState`)
        *   `screamrouter_audio_engine.EQ_BANDS` (constant, if needed for validation)

3.  **Implement Translation Logic inside `_translate_config_to_cpp_desired_state`:**
    *   Check if `self.cpp_audio_manager` and `self.cpp_config_applier` exist. If not, return `None`.
    *   Get the solved configuration: `solved_config: dict[SinkDescription, List[SourceDescription]] = self.active_configuration.real_sinks_to_real_sources` (assuming `self.active_configuration` holds the latest solved state).
    *   Create an instance of the C++ `DesiredEngineState`: `cpp_desired_state = screamrouter_audio_engine.DesiredEngineState()`
    *   Create helper containers to track processed items:
        *   `processed_source_paths: dict[str, screamrouter_audio_engine.AppliedSourcePathParams] = {}` (Use `path_id` as key for uniqueness)
    *   Iterate through the `solved_config` dictionary (`items()`):
        ```python
        for py_sink_desc, py_source_desc_list in solved_config.items():
            # py_sink_desc is a Python SinkDescription
            # py_source_desc_list is a List[Python SourceDescription] for this sink

            # A. Create C++ AppliedSinkParams for this sink
            cpp_applied_sink = screamrouter_audio_engine.AppliedSinkParams()
            cpp_applied_sink.sink_id = py_sink_desc.name # Or use py_sink_desc.config_id if preferred and stable

            # B. Create and populate the nested C++ SinkConfig
            cpp_sink_engine_config = screamrouter_audio_engine.SinkConfig()
            cpp_sink_engine_config.id = py_sink_desc.name # Or config_id
            cpp_sink_engine_config.output_ip = str(py_sink_desc.ip) if py_sink_desc.ip else ""
            cpp_sink_engine_config.output_port = py_sink_desc.port if py_sink_desc.port else 0
            cpp_sink_engine_config.bitdepth = py_sink_desc.bit_depth
            cpp_sink_engine_config.samplerate = py_sink_desc.sample_rate
            cpp_sink_engine_config.channels = py_sink_desc.channels
            # Translate channel_layout string to chlayout1/chlayout2 bytes if needed, 
            # or assume SinkConfig takes the string if bindings were adapted.
            # Assuming direct mapping for now based on current SinkConfig C++ struct:
            cpp_sink_engine_config.chlayout1 = py_sink_desc.scream_header_chlayout1 # Assuming these exist on Python SinkDescription
            cpp_sink_engine_config.chlayout2 = py_sink_desc.scream_header_chlayout2 # Assuming these exist on Python SinkDescription
            cpp_sink_engine_config.use_tcp = py_sink_desc.use_tcp
            cpp_sink_engine_config.enable_mp3 = py_sink_desc.enable_mp3 # Assuming this exists

            cpp_applied_sink.sink_engine_config = cpp_sink_engine_config

            # C. Process each source connected to this sink
            connected_path_ids_for_this_sink = []
            for py_source_desc in py_source_desc_list:
                # Create a unique ID for this specific source-to-sink path
                # Using source tag and sink name/id is a reasonable approach
                path_id = f"{py_source_desc.tag}_to_{cpp_applied_sink.sink_id}" 

                # Add path_id to the list for the current sink
                connected_path_ids_for_this_sink.append(path_id)

                # D. Create or retrieve C++ AppliedSourcePathParams for this path
                # Check if we already created params for this path_id (can happen if multiple routes lead to same source/sink pair)
                if path_id not in processed_source_paths:
                    cpp_source_path = screamrouter_audio_engine.AppliedSourcePathParams()
                    cpp_source_path.path_id = path_id
                    cpp_source_path.source_tag = py_source_desc.tag # The original tag (e.g., IP)
                    cpp_source_path.target_sink_id = cpp_applied_sink.sink_id
                    
                    # Volume/EQ/Delay/Timeshift are already adjusted by ConfigurationSolver for this path
                    cpp_source_path.volume = py_source_desc.volume 
                    cpp_source_path.eq_values = py_source_desc.equalizer.bands # Assuming Equalizer has a 'bands' list/vector
                    cpp_source_path.delay_ms = py_source_desc.delay
                    cpp_source_path.timeshift_sec = py_source_desc.timeshift
                    
                    # Get target format from the *sink* description
                    cpp_source_path.target_output_channels = py_sink_desc.channels
                    cpp_source_path.target_output_samplerate = py_sink_desc.sample_rate
                    
                    # generated_instance_id remains empty - C++ will fill it
                    
                    processed_source_paths[path_id] = cpp_source_path
                # else: # Path already processed, just ensure connection is noted (handled by appending path_id above)
                    # pass 

            # E. Finalize AppliedSinkParams
            cpp_applied_sink.connected_source_path_ids = connected_path_ids_for_this_sink
            cpp_desired_state.sinks.append(cpp_applied_sink)

        # F. Populate the source_paths list in DesiredEngineState from our processed dict
        cpp_desired_state.source_paths = list(processed_source_paths.values())

        return cpp_desired_state
        ```
    *   **Refinements/Checks:**
        *   **EQ Bands:** Ensure the Python `Equalizer` object has a property (e.g., `.bands`) that returns a list/tuple of floats compatible with `std::vector<float>`.
        *   **Sink Channel Layout:** The Python `SinkDescription` has `channel_layout: str`. The C++ `SinkConfig` has `chlayout1: uint8_t`, `chlayout2: uint8_t`. A translation function might be needed here based on how the C++ side interprets these bytes (e.g., mapping "stereo" to `0x03`, `0x00`). *Self-correction: The provided C++ `SinkConfig` seems to directly use the bytes, so Python `SinkDescription` needs corresponding fields like `scream_header_chlayout1`, `scream_header_chlayout2`.*
        *   **Path ID Uniqueness:** Ensure the chosen method for generating `path_id` (e.g., `f"{source.tag}_to_{sink.name}"`) is sufficiently unique and consistent. Using config IDs might be more robust if available and stable.
        *   **Data Types:** Double-check that types match between Python attributes and C++ struct members (e.g., `int`, `float`, `str`/`std::string`, `list`/`std::vector`).

**Acceptance Criteria:**

*   A new private method (e.g., `_translate_config_to_cpp_desired_state`) exists in `ConfigurationManager.py`.
*   The method correctly imports and uses the bound C++ types (`DesiredEngineState`, `AppliedSinkParams`, `AppliedSourcePathParams`, `SinkConfig`).
*   The method iterates through the solved configuration (`self.active_configuration.real_sinks_to_real_sources`).
*   For each sink and its connected sources, it correctly populates corresponding C++ `AppliedSinkParams` and `AppliedSourcePathParams` objects.
*   Crucially, `AppliedSourcePathParams.target_output_channels` and `.target_output_samplerate` are populated from the target sink's description.
*   Path IDs are generated and used to link sinks to source paths.
*   The method handles potential type mismatches (e.g., Python list to `std::vector<float>` for EQ, string to bytes for channel layout if needed).
*   The method returns a fully populated `DesiredEngineState` object ready to be passed to C++, or `None` if the C++ engine is unavailable.
