# Task 03_02: Create Pybind11 Bindings for `AudioEngineConfigApplier` Class

**Objective:** Modify the Pybind11 bindings file (`src/audio_engine/bindings.cpp`) to expose the C++ `AudioEngineConfigApplier` class to Python. This will allow `ConfigurationManager.py` to instantiate the applier and call its `apply_state` method.

**File to Modify:** `src/audio_engine/bindings.cpp`

**Details:**

1.  **Include Necessary Headers:**
    *   Ensure the header for the class itself is included:
        ```cpp
        #include "src/configuration/audio_engine_config_applier.h" 
        ```
    *   Ensure the header for `AudioManager` is included (likely already is):
        ```cpp
        #include "src/audio_engine/audio_manager.h"
        ```
    *   Ensure the header for the config types is included (added in previous task):
        ```cpp
        #include "src/configuration/audio_engine_config_types.h"
        ```

2.  **Import Namespaces:**
    *   Ensure `using namespace screamrouter::config;` and `using namespace screamrouter::audio;` are present or used explicitly.

3.  **Bind `AudioEngineConfigApplier` Class:**
    *   Inside the `PYBIND11_MODULE` block, use `py::class_` to bind the class.
    *   **Bind Constructor:**
        *   The constructor takes `audio::AudioManager&`. When binding, we need to tell Pybind11 how to handle this reference. Since the Python `ConfigurationManager` will likely own the `AudioManager` instance and the `AudioEngineConfigApplier` instance, and ensure the `AudioManager` outlives the `AudioEngineConfigApplier`, we can bind the constructor directly. Pybind11 typically handles references passed during construction correctly in this scenario.
        ```cpp
        py::class_<AudioEngineConfigApplier>(m, "AudioEngineConfigApplier", "Applies desired configuration state to the C++ AudioManager")
            // Bind the constructor. It expects an existing AudioManager instance.
            // Python side will pass the AudioManager object created earlier.
            .def(py::init<AudioManager&>(), 
                 py::arg("audio_manager"), 
                 "Constructor, takes an AudioManager instance")
            // Add other methods below...
        ```
    *   **Bind `apply_state` Method:**
        *   This method takes `const DesiredEngineState&` and returns `bool`. Pybind11 handles const references automatically.
        ```cpp
            .def("apply_state", &AudioEngineConfigApplier::apply_state,
                 py::arg("desired_state"), // Name the argument for clarity in Python
                 "Applies the provided DesiredEngineState to the AudioManager, returns true on success (basic check).")
        ```
    *   **Consider Lifetime Management (Keep Alive - Optional for now):**
        *   If there was a risk that the Python `AudioManager` object could be garbage collected while the C++ `AudioEngineConfigApplier` still held a reference to it, we might use `py::keep_alive`. However, given that `ConfigurationManager` likely owns both, this shouldn't be necessary initially. We bind the constructor assuming the caller manages lifetimes correctly.
        *   Example (if needed later): `.def(py::init<AudioManager&>(), py::keep_alive<1, 2>())` // Keeps argument 2 (AudioManager) alive as long as object 1 (Applier) is alive.

**Complete Binding Example:**

```cpp
// Within PYBIND11_MODULE block in src/audio_engine/bindings.cpp

    // --- Bind AudioEngineConfigApplier Class ---
    py::class_<AudioEngineConfigApplier>(m, "AudioEngineConfigApplier", "Applies desired configuration state to the C++ AudioManager")
        .def(py::init<AudioManager&>(), 
             py::arg("audio_manager"), 
             "Constructor, takes an AudioManager instance")
        
        .def("apply_state", &AudioEngineConfigApplier::apply_state,
             py::arg("desired_state"), 
             "Applies the provided DesiredEngineState to the AudioManager, returns true on success (basic check).");

    // Ensure AudioManager itself is bound before this if not already done.
    // Ensure DesiredEngineState and its nested types are bound (Task 03_01).
```

**Acceptance Criteria:**

*   The `src/audio_engine/bindings.cpp` file is modified.
*   The `AudioEngineConfigApplier` class is bound using `py::class_`.
*   The constructor taking `AudioManager&` is bound correctly.
*   The `apply_state` method taking `const DesiredEngineState&` is bound correctly.
*   The code compiles successfully as part of the Python extension build process.
*   Python code can now `import screamrouter_audio_engine`, create an `AudioManager` instance, create an `AudioEngineConfigApplier` instance passing the manager, create a `DesiredEngineState` object, and call the `apply_state` method on the applier instance.
