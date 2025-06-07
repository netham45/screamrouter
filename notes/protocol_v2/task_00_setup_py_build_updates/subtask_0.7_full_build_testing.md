# Sub-Task 0.7: Full Build Process Testing and Wheel Creation

**Objective:** Test the entire modified `setup.py` build process, including the custom command for oRTP dependencies and the linking of these dependencies into the main C++ extension. Verify that a Python wheel can be successfully built.

**Parent Task:** [Update setup.py for oRTP and Dependencies Build](../task_00_setup_py_build_updates.md)
**Previous Sub-Task:** [Sub-Task 0.6: Update `Extension` Object for Linking Dependencies](./subtask_0.6_update_extension_linking.md)

## Key Steps & Considerations:

1.  **Environment Preparation:**
    *   Ensure all system-level build tools are installed:
        *   `cmake`
        *   `make` (or appropriate build tool for the system, e.g., Ninja)
        *   A C++ compiler (e.g., `g++`, `clang++`)
        *   Python development headers (`python3-dev` or equivalent)
    *   Ensure the oRTP, bctoolbox, and bcunit source code are present in the expected locations (e.g., `src/audio_engine/ortp`, etc., as submodules or vendored code).

2.  **Perform a Clean Build:**
    *   It's crucial to start from a clean state to avoid interference from previous build artifacts.
    *   Remove existing `build/`, `dist/`, `*.egg-info/` directories.
    *   The `_clean_cmake_artifacts` method in `CustomBuildExtCommand` should handle cleaning for the dependencies themselves.
    *   Run: `python setup.py clean --all` (if `setuptools-scm` or similar doesn't handle full cleaning, manual deletion of `build/` etc. is good practice).

3.  **Execute the Build Command:**
    *   Run `python setup.py build`.
    *   This command should trigger:
        1.  `CustomBuildExtCommand.run()`
        2.  `_build_dependencies()`:
            *   `_build_bctoolbox()`
            *   `_patch_bctoolbox_cmake()`
            *   `_build_bcunit()`
            *   `_build_ortp()`
        3.  The standard `build_ext` process for `audio_engine_python`, now linking against the locally built dependencies.
    *   Monitor the console output for:
        *   Print statements from the custom build steps.
        *   CMake configuration output for each dependency.
        *   Make build output for each dependency.
        *   Compiler and linker output for the main `audio_engine_python` extension.
        *   Any error messages.

4.  **Verify Build Artifacts for Dependencies:**
    *   After the `_build_dependencies` step (or during, by inspecting the `_build` and `_install` directories for each dependency):
        *   Check that `_install/<lib>/include/` contains header files.
        *   Check that `_install/<lib>/lib/` (or `lib64`) contains the library files (e.g., `libortp.a`, `libbctoolbox.a`, `libbcunit.a` if static linking is configured, or `.so` files if shared).
        *   Verify `BCToolboxConfig.cmake` is patched.

5.  **Verify Main Extension Build:**
    *   Check the `build/lib.*/` directory for the compiled `audio_engine_python*.so` (or `.pyd` on Windows) file.
    *   Ensure there are no unresolved symbol errors during the linking phase of `audio_engine_python`. This would indicate issues with `library_dirs` or `libraries` settings, or that the dependency libraries were not found or built correctly.

6.  **Build the Wheel:**
    *   If `python setup.py build` succeeds, proceed to build the wheel:
        *   `python setup.py bdist_wheel`
    *   This command packages the built extension and other project files into a `.whl` file in the `dist/` directory.

7.  **Inspect the Wheel (Optional but Recommended):**
    *   A `.whl` file is a ZIP archive. Rename it to `.zip` and extract its contents.
    *   Verify that `audio_engine_python*.so` is present.
    *   If static linking was used for oRTP and its dependencies, their code should be part of `audio_engine_python*.so`, and no separate `libortp.so`, etc., files should be needed *within the wheel* for these specific dependencies (unless they were built as shared and RPATH was set up to find them relative to the extension, or they are expected to be system-installed). The goal of local static linking is to bundle them.

8.  **Test Installation and Import (Basic Check):**
    *   Create a new virtual environment.
    *   Install the built wheel: `pip install dist/ScreamRouter-*.whl`.
    *   Attempt to import the extension in Python:
        ```python
        try:
            import audio_engine_python
            print("Successfully imported audio_engine_python")
            # Further tests could involve calling a simple function from the module
        except ImportError as e:
            print(f"Failed to import audio_engine_python: {e}")
        ```
    *   This basic import test checks if the dynamic linker can find all necessary symbols.

## Troubleshooting Common Issues:

*   **CMake/Make Not Found:** Ensure these tools are installed and in the system `PATH`.
*   **Dependency Source Code Missing:** Verify submodules are initialized and updated, or vendored code is present.
*   **CMake Configuration Errors:** Check CMake output for missing dependencies (e.g., if `bctoolbox` or `bcunit` didn't install correctly before `ortp` build). Verify `*_DIR` paths in CMake arguments.
*   **Compiler Errors:** Standard C++ compilation issues (syntax errors, missing headers not related to the built dependencies).
*   **Linker Errors (Unresolved Symbols):**
    *   `library_dirs` might be incorrect.
    *   `libraries` names might be wrong (e.g., `ortp` vs `libortp`).
    *   Static libraries (`.a`) might not have been built, or shared libraries (`.so`) are built but not found at link time.
    *   Mismatch in C++ ABI (e.g., if dependencies were compiled with a different compiler or standard library version than the main extension).
*   **Runtime `ImportError` (e.g., `libortp.so: cannot open shared object file`):**
    *   If shared libraries were built for dependencies and RPATH was not set correctly (or not supported on the platform for wheels in this manner), the Python extension won't find them. This reinforces the preference for static linking.

## Code Alterations:

*   No direct code alterations in `setup.py` for this sub-task itself, as it's about executing and verifying the existing setup.
*   However, findings from this testing phase will likely lead to adjustments in the `CustomBuildExtCommand` methods or the `Extension` object definition based on errors encountered.

## Recommendations:

*   **Iterative Testing:** Test each dependency build step individually first if facing issues with the full sequence.
*   **Verbose Output:** Ensure `_run_subprocess` prints sufficient stdout/stderr for debugging.
*   **Static Linking Strategy:** Re-evaluate and confirm the static vs. shared linking strategy for `ortp` and its dependencies. Aim for static linking to simplify wheel distribution.
