# Sub-Task 0.1: Design Custom `build_ext` Command for oRTP Dependencies

**Objective:** Define and outline the structure for a custom `build_ext` command in `setup.py` to manage the compilation of oRTP and its dependencies (bctoolbox, bcunit) before the main C++ extension is built.

**Parent Task:** [Update setup.py for oRTP and Dependencies Build](../task_00_setup_py_build_updates.md)

## Key Steps & Considerations:

1.  **Subclassing `build_ext`:**
    *   The custom command will inherit from `setuptools.command.build_ext.build_ext`.
    ```python
    import os
    import subprocess
    import shutil
    import re
    from setuptools import setup, Extension
    from setuptools.command.build_ext import build_ext as _build_ext

    class CustomBuildExtCommand(_build_ext):
        def run(self):
            # Custom build logic for dependencies will go here
            print("Running custom build steps for oRTP dependencies...")
            self._build_dependencies()
            # Call the original build_ext run method
            _build_ext.run(self)

        def _build_dependencies(self):
            # This method will orchestrate the build of bctoolbox, bcunit, ortp
            # Placeholder for actual build calls
            print("Orchestrating dependency builds...")
            # build_bctoolbox()
            # patch_bctoolbox_cmake()
            # build_bcunit()
            # build_ortp()
            pass
    ```

2.  **Integrating the Custom Command:**
    *   The custom command needs to be registered in `setup()` within `setup.py`.
    ```python
    # In setup.py
    # setup(
    #     # ... other setup arguments
    #     cmdclass={
    #         'build_ext': CustomBuildExtCommand,
    #     },
    #     # ...
    # )
    ```
    *   This ensures that when `python setup.py build_ext` (or commands that trigger it like `build` or `install`) is run, our `CustomBuildExtCommand` is used.

3.  **Orchestration Logic in `_build_dependencies`:**
    *   This method will be responsible for:
        *   Defining paths to source, build, and install directories for each dependency. These directories are typically within `src/audio_engine/`.
        *   Calling individual helper methods to build each dependency in the correct order:
            1.  bctoolbox
            2.  Patch bctoolbox's CMake configuration file.
            3.  bcunit
            4.  ortp
    *   It should handle errors from each build step and halt if a dependency fails to build, providing informative messages.

4.  **Path Management:**
    *   Use `os.path.join` and `os.path.abspath` for robust path construction.
    *   Define base directories clearly, e.g., `SETUP_PY_DIR = os.path.abspath(os.path.dirname(__file__))`.
    *   The oRTP related libraries (bctoolbox, bcunit, ortp) are expected to be subdirectories within `src/audio_engine/`.
    ```python
    # Example path definitions within CustomBuildExtCommand or its helpers
    SETUP_PY_DIR = os.path.abspath(os.path.dirname(__file__)) # Assuming setup.py is in root
    AUDIO_ENGINE_SUBMODULES_DIR = os.path.join(SETUP_PY_DIR, "src", "audio_engine")

    BCTOOLBOX_SRC_DIR = os.path.join(AUDIO_ENGINE_SUBMODULES_DIR, "bctoolbox")
    BCTOOLBOX_BUILD_DIR = os.path.join(BCTOOLBOX_SRC_DIR, "_build")
    BCTOOLBOX_INSTALL_DIR = os.path.join(BCTOOLBOX_SRC_DIR, "_install")

    BCUNIT_SRC_DIR = os.path.join(AUDIO_ENGINE_SUBMODULES_DIR, "bcunit")
    BCUNIT_BUILD_DIR = os.path.join(BCUNIT_SRC_DIR, "_build")
    BCUNIT_INSTALL_DIR = os.path.join(BCUNIT_SRC_DIR, "_install")

    ORTP_SRC_DIR = os.path.join(AUDIO_ENGINE_SUBMODULES_DIR, "ortp")
    ORTP_BUILD_DIR = os.path.join(ORTP_SRC_DIR, "_build")
    ORTP_INSTALL_DIR = os.path.join(ORTP_SRC_DIR, "_install")
    ```

## Code Alterations:

*   **File:** `setup.py`
*   **Changes:**
    *   Import necessary modules (`os`, `subprocess`, `shutil`, `re`, `setuptools`, `setuptools.command.build_ext`).
    *   Define the `CustomBuildExtCommand` class as outlined above, including the `run` and `_build_dependencies` methods.
    *   Add helper methods within `CustomBuildExtCommand` for building each specific library (e.g., `_build_bctoolbox`, `_patch_bctoolbox_cmake`, `_build_bcunit`, `_build_ortp`). These will be detailed in subsequent sub-tasks.
    *   Update the `cmdclass` argument in the `setup()` function to use `CustomBuildExtCommand`.

## Recommendations:

*   Ensure the custom command is only executed when `build_ext` is invoked.
*   Provide clear print statements or use Python's `logging` module for output during the custom build process to inform the user about the progress and any issues. This is crucial for debugging build failures.
*   The design should allow for easy enabling/disabling of the custom dependency build, perhaps via an environment variable (e.g., `SKIP_DEPS_BUILD=1`). This is useful for development or CI scenarios where pre-built libraries might be preferred or available system-wide.
*   Consider the platform: CMake and make commands might need adjustments or checks for Windows vs. Linux/macOS if cross-platform compatibility is a strong requirement beyond the current Linux focus.
