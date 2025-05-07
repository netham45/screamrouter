# Task 05_03: Python Integration Test Strategy

**Objective:** Define a strategy for integration testing the Python `ConfigurationManager`'s interaction with the compiled C++ extension module (`screamrouter_audio_engine`), specifically focusing on the use of `AudioEngineConfigApplier` and the state translation process.

**Testing Framework:** `pytest` is recommended for its fixtures, parametrization, and ease of use. `unittest.mock` can be used for mocking Python-level dependencies if needed.

**Scope:** These tests bridge the Python and C++ layers. They verify that:

1.  Python correctly instantiates and manages the C++ objects (`AudioManager`, `AudioEngineConfigApplier`).
2.  The translation logic (`_translate_config_to_cpp_desired_state`) correctly converts Python configuration states into the C++ `DesiredEngineState` struct.
3.  Calling `apply_state` on the C++ `AudioEngineConfigApplier` from Python triggers the expected C++ logic (though the *detailed* C++ logic is verified by C++ unit tests, these tests ensure the call happens correctly).
4.  End-to-end configuration changes in Python lead to the correct state application via the C++ layer.

**Testing Approaches:**

1.  **Testing with the Real C++ Extension:**
    *   **Pros:** Tests the actual compiled code and bindings. Catches issues in the C++/Python boundary, bindings, and C++ implementation working together.
    *   **Cons:** Requires the C++ extension to be compiled successfully before running tests. C++ errors might be harder to debug from Python tests. The full C++ engine might have external dependencies (sockets, threads) making tests more complex or slower.
    *   **Strategy:** Use `pytest`. Create test scenarios by setting up `ConfigurationManager` with specific `SourceDescription`, `SinkDescription`, and `RouteDescription` lists. Trigger configuration reloads (`_ConfigurationManager__process_and_apply_configuration` or the main loop mechanism). Assertions might be difficult directly on the C++ state, so focus on:
        *   Verifying no exceptions occur during translation and `apply_state` calls.
        *   If C++ `AudioManager` gets methods to query its state (e.g., get active sink IDs, get source instance params), use those via bindings to verify the outcome.
        *   Mock Python dependencies of `ConfigurationManager` (like `PluginManager`, `APIWebStream`) if necessary to isolate the config application logic.

2.  **Testing with a Mocked C++ Extension Boundary:**
    *   **Pros:** Isolates Python logic (`ConfigurationManager`, `ConfigurationSolver`, translation method) from the actual C++ implementation. Faster execution. Allows simulating C++ errors.
    *   **Cons:** Doesn't test the actual bindings or C++ code. Relies on the mock accurately representing the C++ interface.
    *   **Strategy:** Use `pytest` and `unittest.mock`.
        *   Mock the `screamrouter_audio_engine` module import.
        *   Create mock classes for `AudioManager` and `AudioEngineConfigApplier` in Python.
        *   Configure the mock `AudioEngineConfigApplier.apply_state` method to allow inspection of the `DesiredEngineState` object it receives.
        *   Test `ConfigurationManager._translate_config_to_cpp_desired_state` by providing solved configurations and asserting the structure and values of the returned (mock) `DesiredEngineState`.
        *   Test `ConfigurationManager.__process_and_apply_configuration` by verifying that `_translate_config_to_cpp_desired_state` is called and its result is passed to the mock `apply_state` method.

**Recommended Strategy:** Employ a mix of both approaches.

*   Use **Mocking (Approach 2)** for detailed testing of the Python translation logic (`_translate_config_to_cpp_desired_state`) and the control flow within `ConfigurationManager` that calls the C++ layer. This ensures the Python side prepares the correct data.
*   Use **Real Extension Testing (Approach 1)** for a smaller set of key integration scenarios (e.g., adding a sink, adding a source path, removing items, basic updates) to verify the bindings work and the Python-to-C++ call chain functions without crashing. These act as smoke tests for the integration.

**Key Test Scenarios (for both approaches, adapted):**

1.  **Initialization:** Verify `ConfigurationManager` correctly instantiates C++ objects (or mocks).
2.  **Translation Accuracy:** Provide various `ConfigurationSolver` outputs and verify the generated `DesiredEngineState` (passed to the real/mock `apply_state`) has the correct structure, path IDs, sink parameters, source path parameters, and connections.
3.  **`apply_state` Call:** Verify that `apply_state` is called during configuration processing.
4.  **End-to-End Config Changes:**
    *   Start with an empty config, add a sink -> verify state/calls.
    *   Add a source path -> verify state/calls.
    *   Add another sink and connect the source path -> verify state/calls.
    *   Update source path parameters (volume, EQ) -> verify state/calls.
    *   Remove a connection -> verify state/calls.
    *   Remove a source path -> verify state/calls.
    *   Remove a sink -> verify state/calls.
5.  **Error Handling:** If using mocks, simulate errors from `apply_state` and verify `ConfigurationManager` handles them. If using the real extension, test scenarios known to cause C++ errors (if possible) or verify logs.

**Test Structure:**

*   Organize tests in a dedicated test directory (e.g., `tests/`).
*   Use `pytest` fixtures to set up `ConfigurationManager` instances, potentially with mocked dependencies or specific initial configurations.
*   Use clear test function names describing the scenario being tested.

**Acceptance Criteria:**

*   A Python integration test suite (`pytest`-based) is created.
*   Tests cover the instantiation of C++ objects within `ConfigurationManager`.
*   Tests validate the logic of `_translate_config_to_cpp_desired_state` (likely using mocks).
*   Tests verify that `AudioEngineConfigApplier.apply_state` is called correctly from Python during configuration updates.
*   A subset of tests run against the compiled C++ extension to ensure basic integration and bindings work.
*   The test suite can be executed automatically.
