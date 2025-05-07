# Task 05_01: Update Build System (`setup.py`)

**Objective:** Modify the `setup.py` script to include the new C++ source files (`audio_engine_config_applier.cpp`) and header files (`audio_engine_config_types.h`, `audio_engine_config_applier.h`) in the compilation process for the `screamrouter_audio_engine` Python extension module.

**File to Modify:** `setup.py`

**Details:**

1.  **Identify Extension Module Definition:**
    *   Locate the `Extension` object definition within `setup.py` that corresponds to the `screamrouter_audio_engine` module. It will look something like this:
    ```python
    screamrouter_audio_engine = Extension(
        'screamrouter_audio_engine',
        sources=[
            'src/audio_engine/bindings.cpp',
            'src/audio_engine/audio_manager.cpp',
            # ... other existing .cpp files ...
            'src/audio_engine/rtp_receiver.cpp', 
            'src/audio_engine/source_input_processor.cpp',
            'src/audio_engine/sink_audio_mixer.cpp',
            'c_utils/audio_processor.cpp', 
            # ... potentially others from c_utils ...
        ],
        include_dirs=[
            'pybind11/include', 
            'src/audio_engine', 
            'c_utils',
            # ... other include dirs ...
        ],
        # ... libraries, extra_compile_args, etc. ...
    )
    ```

2.  **Add New Source File:**
    *   Add the path to the new C++ implementation file to the `sources` list within the `Extension` definition.
    ```python
        sources=[
            # ... existing sources ...
            'src/configuration/audio_engine_config_applier.cpp', # <-- ADD THIS LINE
        ],
    ```

3.  **Add New Include Directory:**
    *   Add the directory containing the new header files (`src/configuration/`) to the `include_dirs` list. This ensures the compiler can find `#include "src/configuration/audio_engine_config_types.h"` and `#include "src/configuration/audio_engine_config_applier.h"`.
    ```python
        include_dirs=[
            # ... existing include dirs ...
            'src/configuration', # <-- ADD THIS LINE
        ],
    ```

4.  **Verify Dependencies:**
    *   Ensure that all necessary libraries (like `lame` if `SinkAudioMixer` uses it) are correctly listed in the `libraries` argument of the `Extension`. No changes should be needed here specifically for the config applier itself, but it's good practice to review.
    *   Ensure `extra_compile_args` includes flags necessary for C++11/14/17 features if used (e.g., `-std=c++17`) and any other required flags (like `-fPIC`).

**Example Modified `Extension` Definition:**

```python
# Inside setup.py

# Define the C++ extension module
screamrouter_audio_engine = Extension(
    'screamrouter_audio_engine',
    sources=[
        'src/audio_engine/bindings.cpp',
        'src/audio_engine/audio_manager.cpp',
        'src/audio_engine/rtp_receiver.cpp',
        'src/audio_engine/source_input_processor.cpp',
        'src/audio_engine/sink_audio_mixer.cpp',
        'src/configuration/audio_engine_config_applier.cpp', # Added
        # Add other necessary .cpp files from src/audio_engine and c_utils
        'c_utils/audio_processor.cpp', 
        # ... ensure all required .cpp are listed
    ],
    include_dirs=[
        'pybind11/include', # Path to pybind11 headers
        'src/audio_engine', # For audio_manager.h etc.
        'src/configuration', # Added - For audio_engine_config_*.h
        'c_utils',          # For audio_processor.h etc.
        # Add other necessary include paths (e.g., for LAME headers if needed)
        '/usr/include/lame', # Example if LAME headers are here
    ],
    libraries=[
        'lame', # Example if linking against liblame
        # Add other necessary libraries
        ], 
    language='c++',
    extra_compile_args=[
        '-std=c++17', 
        '-fPIC', 
        '-O3', # Example optimization flag
        # Add other flags like -DNDEBUG for release builds
        ],
    # extra_link_args=[ ... ] # If needed
)

setup(
    # ... other setup args ...
    ext_modules=[screamrouter_audio_engine],
    # ...
)
```
*Self-correction: Added example `libraries` and `extra_compile_args` for completeness, although they might already exist in the user's `setup.py`.*

**Acceptance Criteria:**

*   The `setup.py` script is modified.
*   The `sources` list for the `screamrouter_audio_engine` extension includes `'src/configuration/audio_engine_config_applier.cpp'`.
*   The `include_dirs` list for the extension includes `'src/configuration'`.
*   Running the build command (e.g., `python setup.py build` or `pip install .`) successfully compiles the extension module without errors related to finding the new source or header files.
