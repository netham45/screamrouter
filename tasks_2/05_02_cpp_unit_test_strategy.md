# Task 05_02: C++ Unit Test Strategy for `AudioEngineConfigApplier`

**Objective:** Define a strategy for unit testing the C++ `AudioEngineConfigApplier` class to ensure its reconciliation and state application logic works correctly in isolation.

**Testing Framework:** Google Test with Google Mock is recommended for C++ testing, allowing for easy creation of mock objects.

**Key Component to Mock:**

*   `screamrouter::audio::AudioManager`: The `AudioEngineConfigApplier` interacts heavily with `AudioManager`. To test the applier in isolation, we need a mock version of `AudioManager` that allows us to:
    *   Verify that the correct methods (`add_sink`, `remove_sink`, `configure_source`, `remove_source`, `connect_source_sink`, `disconnect_source_sink`, `update_source_volume`, etc.) are called with the expected arguments.
    *   Simulate success and failure return values from these methods.
    *   Potentially track the "state" of the mock `AudioManager` (e.g., which sinks/sources are supposedly active) to verify the applier's logic based on simulated manager state.

**Mock `AudioManager` Implementation:**

1.  **Create Mock Class:** Define `MockAudioManager` inheriting from `screamrouter::audio::AudioManager` (if `AudioManager` methods are virtual) or create a mock class with the same public interface. If methods are not virtual, testing becomes harder, potentially requiring template-based dependency injection or interface extraction for `AudioManager`. **Assumption:** `AudioManager` methods can be mocked (e.g., are virtual, or we use an interface).
    ```cpp
    #include "gmock/gmock.h"
    #include "src/audio_engine/audio_manager.h" // Include the real header

    class MockAudioManager : public screamrouter::audio::AudioManager {
    public:
        // Use MOCK_METHOD for each public method used by AudioEngineConfigApplier
        MOCK_METHOD(bool, initialize, (int), (override)); // If needed for setup
        MOCK_METHOD(void, shutdown, (), (override));     // If needed for teardown
        MOCK_METHOD(bool, add_sink, (const screamrouter::audio::SinkConfig&), (override));
        MOCK_METHOD(bool, remove_sink, (const std::string&), (override));
        MOCK_METHOD(std::string, configure_source, (const screamrouter::audio::SourceConfig&), (override));
        MOCK_METHOD(bool, remove_source, (const std::string&), (override));
        MOCK_METHOD(bool, connect_source_sink, (const std::string&, const std::string&), (override));
        MOCK_METHOD(bool, disconnect_source_sink, (const std::string&, const std::string&), (override));
        MOCK_METHOD(bool, update_source_volume, (const std::string&, float), (override));
        MOCK_METHOD(bool, update_source_equalizer, (const std::string&, const std::vector<float>&), (override));
        MOCK_METHOD(bool, update_source_delay, (const std::string&, int), (override));
        MOCK_METHOD(bool, update_source_timeshift, (const std::string&, float), (override));
        // Add mocks for any other relevant AudioManager methods
    };
    ```
    *(Self-correction: Added `(override)` assuming methods are virtual. If not, the mocking strategy needs adjustment.)*

**Test Fixture:**

*   Create a Google Test fixture (e.g., `AudioEngineConfigApplierTest`) to handle setup and teardown.
    ```cpp
    #include "gtest/gtest.h"
    #include "src/configuration/audio_engine_config_applier.h"
    #include "src/configuration/audio_engine_config_types.h"
    // Include the MockAudioManager definition

    using namespace screamrouter::config;
    using namespace screamrouter::audio;
    using ::testing::_; // For argument matchers
    using ::testing::Return;
    using ::testing::StrEq; // For string equality matching
    // Add other matchers as needed

    class AudioEngineConfigApplierTest : public ::testing::Test {
    protected:
        MockAudioManager mock_audio_manager; // Use the mock object
        AudioEngineConfigApplier applier_{mock_audio_manager}; // Instance of the class under test

        // Helper to create a basic DesiredEngineState
        DesiredEngineState create_basic_state() {
            DesiredEngineState state;
            // Populate with test data as needed
            return state;
        }
        
        // Add helper methods to create AppliedSinkParams, AppliedSourcePathParams etc.
    };
    ```

**Test Cases:**

Focus on testing the `apply_state` method by providing different `DesiredEngineState` inputs and verifying the calls made to the `MockAudioManager`.

1.  **Initial Empty State:**
    *   Input: Empty `DesiredEngineState`.
    *   Expected: No calls to `MockAudioManager` add/remove/update methods.
2.  **Add Single Sink:**
    *   Input: `DesiredEngineState` with one `AppliedSinkParams`.
    *   Expected: `EXPECT_CALL(mock_audio_manager, add_sink(_))` is called once with the correct `SinkConfig`. `EXPECT_CALL` for `connect_source_sink` if the sink has connections.
3.  **Add Single Source Path:**
    *   Input: `DesiredEngineState` with one `AppliedSourcePathParams`.
    *   Expected: `EXPECT_CALL(mock_audio_manager, configure_source(_))` is called once with the correct `SourceConfig`. Simulate a return `instance_id`.
4.  **Add Sink and Connected Source Path:**
    *   Input: State with one sink and one source path connected to it.
    *   Expected: Calls to `configure_source`, `add_sink`, and `connect_source_sink` in the correct order with correct arguments (using the simulated `instance_id`).
5.  **Remove Sink:**
    *   Setup: Call `apply_state` with a sink.
    *   Input: Empty `DesiredEngineState`.
    *   Expected: `EXPECT_CALL(mock_audio_manager, remove_sink(_))` called once with the correct `sink_id`.
6.  **Remove Source Path:**
    *   Setup: Call `apply_state` with a source path. Simulate `configure_source` returning an `instance_id`.
    *   Input: Empty `DesiredEngineState`.
    *   Expected: `EXPECT_CALL(mock_audio_manager, remove_source(_))` called once with the correct `instance_id`.
7.  **Update Source Path Parameters:**
    *   Setup: Apply state with a source path.
    *   Input: Apply state again with the same `path_id` but different volume/EQ/delay/timeshift.
    *   Expected: Calls to the corresponding `update_source_*` methods on `MockAudioManager` with the correct `instance_id` and new values. No call to `configure_source` or `remove_source`.
8.  **Update Sink Connections (Add):**
    *   Setup: Apply state with sink S1 and source path P1 (not connected).
    *   Input: Apply state again, now `S1.connected_source_path_ids` includes `P1.path_id`.
    *   Expected: `EXPECT_CALL(mock_audio_manager, connect_source_sink(P1.instance_id, S1.sink_id))`.
9.  **Update Sink Connections (Remove):**
    *   Setup: Apply state with sink S1 and source path P1 connected.
    *   Input: Apply state again, `S1.connected_source_path_ids` no longer includes `P1.path_id`.
    *   Expected: `EXPECT_CALL(mock_audio_manager, disconnect_source_sink(P1.instance_id, S1.sink_id))`.
10. **Update Sink Parameters (Requires Re-Add):**
    *   Setup: Apply state with sink S1 (e.g., 48kHz).
    *   Input: Apply state again with S1 but different parameters (e.g., 44.1kHz).
    *   Expected: `EXPECT_CALL(mock_audio_manager, remove_sink(S1.sink_id))`, followed by `EXPECT_CALL(mock_audio_manager, add_sink(_))` with the *new* config. Connection reconciliation should also occur.
11. **Fundamental Source Path Change (Requires Re-Add):**
    *   Setup: Apply state with path P1 (e.g., output 48kHz).
    *   Input: Apply state again with P1 but different fundamental params (e.g., output 44.1kHz).
    *   Expected: `EXPECT_CALL(mock_audio_manager, remove_source(P1.instance_id))`, followed by `EXPECT_CALL(mock_audio_manager, configure_source(_))` with the *new* config. Simulate new `instance_id`. Connection reconciliation should occur for the *new* instance ID.
12. **Mixed Operations:** Combine additions, removals, and updates in a single `apply_state` call and verify all expected `MockAudioManager` calls occur.
13. **Error Handling:**
    *   Simulate `AudioManager` methods returning `false` or empty strings.
    *   Verify `AudioEngineConfigApplier` handles these failures gracefully (e.g., logs errors, potentially stops processing, returns `false` from `apply_state`).

**Build Integration:**

*   Integrate Google Test/Mock into the build system (CMake or `setup.py` extensions) to compile and run these tests.

**Acceptance Criteria:**

*   A suite of C++ unit tests using Google Test/Mock is created for `AudioEngineConfigApplier`.
*   A `MockAudioManager` class is implemented to isolate the applier from the real `AudioManager`.
*   Test cases cover various scenarios including additions, removals, parameter updates, connection updates, fundamental changes requiring re-creation, and error conditions.
*   Tests verify that `AudioEngineConfigApplier` calls the correct `AudioManager` methods with the expected arguments based on the `DesiredEngineState`.
*   The tests can be executed as part of the standard build or testing process.
