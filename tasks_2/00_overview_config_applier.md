# Project: Implement AudioEngineConfigApplier

**Overall Goal:** Develop a C++ class, `AudioEngineConfigApplier`, responsible for receiving a desired audio engine configuration state (derived from Python's `ConfigurationSolver`) and applying it to the C++ `AudioManager`. This involves creating, updating, and removing `SourceInputProcessor` and `SinkAudioMixer` instances, and managing their connections, based on the principle of "one `SourceInputProcessor` instance per unique source-to-sink path," where each processor is tailored to its target sink's output requirements.

**Phases and High-Level Tasks:**

1.  **Phase 1: C++ Data Structures & AudioManager Modifications**
    *   `01_01_define_cpp_config_structs.md`: Define C++ structs for desired configuration state (`DesiredEngineState`, `AppliedSourcePathParams`, `AppliedSinkParams`).
    *   `01_02_modify_audio_types_configs.md`: Modify `SourceConfig` and `SourceProcessorConfig` in `src/audio_engine/audio_types.h` to include target output format (channels, samplerate).
    *   `01_03_modify_audiomanager_configure_source.md`: Update `AudioManager::configure_source` to accept and utilize the target output format from the modified `SourceConfig`.
    *   `01_04_modify_sourceinputprocessor_init.md`: Update `SourceInputProcessor::initialize_audio_processor` to use the target output format from its `SourceProcessorConfig`.

2.  **Phase 2: `AudioEngineConfigApplier` C++ Class Implementation**
    *   `02_01_create_config_applier_header.md`: Define the `AudioEngineConfigApplier` class structure in `audio_engine_config_applier.h`.
    *   `02_02_implement_config_applier_constructor_shell.md`: Implement the constructor and a shell for the `apply_state` method in `audio_engine_config_applier.cpp`.
    *   `02_03_implement_sink_reconciliation.md`: Implement the logic within `AudioEngineConfigApplier` to add and remove `SinkAudioMixer` instances based on the desired state.
    *   `02_04_implement_source_path_reconciliation.md`: Implement logic to add, update, and remove `SourceInputProcessor` instances (representing source-to-sink paths).
    *   `02_05_implement_connection_reconciliation.md`: Implement logic to connect and disconnect `SourceInputProcessor` instances from/to `SinkAudioMixer` instances.

3.  **Phase 3: Pybind11 Bindings**
    *   `03_01_bind_cpp_config_structs.md`: Create Pybind11 bindings for the new C++ configuration state structs.
    *   `03_02_bind_config_applier_class.md`: Create Pybind11 bindings for the `AudioEngineConfigApplier` class and its methods.

4.  **Phase 4: Python Integration (`ConfigurationManager.py`)**
    *   `04_01_integrate_config_applier_instantiation.md`: Modify `ConfigurationManager.py` to instantiate and hold the `AudioEngineConfigApplier`.
    *   `04_02_python_to_cpp_state_translation.md`: Develop Python logic to translate the output of `ConfigurationSolver.py` into the C++ `DesiredEngineState` object. This includes ensuring `ConfigurationSolver` provides necessary sink-specific output format details.
    *   `04_03_call_apply_state_in_python.md`: Modify `ConfigurationManager.py` to call the `apply_state` method of the `AudioEngineConfigApplier`.
    *   `04_04_re_evaluate_python_audiocontroller.md`: Analyze and refactor or remove the Python `AudioController` class if its responsibilities are superseded by the C++ engine.

5.  **Phase 5: Build System & Testing Strategy**
    *   `05_01_update_build_system.md`: Update `setup.py` (and CMakeLists.txt if applicable) to include all new C++ files in the build process.
    *   `05_02_cpp_unit_test_strategy.md`: Outline a strategy for C++ unit tests for `AudioEngineConfigApplier`.
    *   `05_03_python_integration_test_strategy.md`: Outline a strategy for Python integration tests.

Each markdown file will contain detailed steps for the specific task.
