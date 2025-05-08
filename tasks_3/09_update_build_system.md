# Task 9: Update Build System

**Goal**: Ensure the new `RawScreamReceiver` source file is compiled and linked into the Python extension module.

**Files to Modify**:
*   `setup.py` (or `CMakeLists.txt`, or other build configuration file used for the project)

**Steps**:

1.  **Identify Build Configuration**: Determine how the `screamrouter_audio_engine` C++ extension is currently built. This is likely managed by `setup.py` using `pybind11.setup_helpers.Pybind11Extension` and `setuptools`, or potentially a `CMake` configuration.

2.  **Add New Source File**:
    *   **If using `setup.py`**: Locate the `ext_modules` list within `setup()`. Find the `Pybind11Extension` definition for `screamrouter_audio_engine`. Add the path to the new source file (`src/audio_engine/raw_scream_receiver.cpp`) to the list of source files for that extension.
      ```python
      # Example setup.py snippet
      from setuptools import setup
      from pybind11.setup_helpers import Pybind11Extension, build_ext

      ext_modules = [
          Pybind11Extension(
              "screamrouter_audio_engine",
              [
                  "src/audio_engine/bindings.cpp",
                  "src/audio_engine/audio_manager.cpp",
                  "src/audio_engine/rtp_receiver.cpp",
                  "src/audio_engine/source_input_processor.cpp",
                  "src/audio_engine/sink_audio_mixer.cpp",
                  "src/audio_engine/raw_scream_receiver.cpp", # <-- ADD THIS LINE
                  # Add other necessary C++ files like audio_processor.cpp if not linked separately
                  "c_utils/audio_processor.cpp",
                  "c_utils/biquad/biquad.cpp",
                  "c_utils/speaker_mix.cpp",
                  # ... potentially others ...
              ],
              # ... other extension options like include_dirs, library_dirs, libraries ...
              extra_compile_args=["-O3", "-std=c++17", "-Wall", "-Wextra"], # Example flags
              # Ensure LAME and samplerate are linked if needed by AudioProcessor
              libraries=["samplerate", "mp3lame"],
          ),
      ]

      setup(
          # ... other setup args ...
          ext_modules=ext_modules,
          cmdclass={"build_ext": build_ext},
          # ...
      )
      ```
    *   **If using `CMake`**: Locate the `CMakeLists.txt` file responsible for building the audio engine module. Find the `add_library` or `pybind11_add_module` command for `screamrouter_audio_engine`. Add `src/audio_engine/raw_scream_receiver.cpp` to the list of source files in that command. Ensure dependencies (like LAME, samplerate) are correctly linked via `target_link_libraries`.

3.  **Verify Dependencies**: Double-check that all necessary source files (`.cpp`) from both `src/audio_engine` and potentially `c_utils` (like `audio_processor.cpp`, `biquad.cpp`, `speaker_mix.cpp` if used directly) are included in the build for the extension module. Ensure required libraries (like `libsamplerate`, `libmp3lame`) are correctly linked.

4.  **Test Build**: After modifying the build configuration, attempt to rebuild the project (e.g., using `pip install .` or `python setup.py build_ext --inplace`) to ensure the new file compiles without errors and the extension module links successfully.
