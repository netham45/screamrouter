# Sub-Task 0.6: Update `Extension` Object for Linking Dependencies

**Objective:** Modify the `setuptools.Extension` object for the main C++ audio engine (`audio_engine_python`) in `setup.py` to correctly include paths and library names for the locally built `ortp`, `bctoolbox`, and `bcunit`.

**Parent Task:** [Update setup.py for oRTP and Dependencies Build](../task_00_setup_py_build_updates.md)
**Previous Sub-Task:** [Sub-Task 0.5: Implement `ortp` Build Logic](./subtask_0.5_implement_ortp_build.md)

## Key Steps & Considerations:

1.  **Locate the `Extension` Definition:**
    *   In `setup.py`, find the `Extension` instance for `audio_engine_python`. It will look something like this:
    ```python
    # Existing setup.py (simplified)
    # audio_engine_module = Extension(
    #     "audio_engine_python",
    #     sources=[...], # List of .cpp files
    #     include_dirs=[...],
    #     library_dirs=[...],
    #     libraries=[...],
    #     extra_compile_args=[...],
    #     extra_link_args=[...]
    # )
    ```

2.  **Update `include_dirs`:**
    *   Add the `include` directories from the `_install` locations of `ortp`, `bctoolbox`, and `bcunit`.
    *   The `CustomBuildExtCommand` should make these paths available, perhaps by storing them as instance attributes after the dependencies are built.
    ```python
    # Within setup.py, when defining or modifying the Extension object
    # Assuming self.ORTP_INSTALL_DIR, self.BCTOOLBOX_INSTALL_DIR, self.BCUNIT_INSTALL_DIR
    # are attributes of the CustomBuildExtCommand instance, or accessible globally after build.
    # For example, if these paths are set in CustomBuildExtCommand.run()

    # Example:
    # new_include_dirs = [
    #     os.path.join(CustomBuildExtCommand.ORTP_INSTALL_DIR, "include"),
    #     os.path.join(CustomBuildExtCommand.BCTOOLBOX_INSTALL_DIR, "include"),
    #     os.path.join(CustomBuildExtCommand.BCUNIT_INSTALL_DIR, "include"),
    # ]
    # audio_engine_module.include_dirs.extend(new_include_dirs)
    ```
    *   Alternatively, these paths can be directly added to the `include_dirs` list when the `Extension` is defined, using the same path constants defined in `CustomBuildExtCommand`.

3.  **Update `library_dirs`:**
    *   Add the `lib` (or `lib64`) directories from the `_install` locations.
    ```python
    # Example:
    # new_library_dirs = [
    #     os.path.join(CustomBuildExtCommand.ORTP_INSTALL_DIR, "lib"), # or "lib64"
    #     os.path.join(CustomBuildExtCommand.BCTOOLBOX_INSTALL_DIR, "lib"), # or "lib64"
    #     os.path.join(CustomBuildExtCommand.BCUNIT_INSTALL_DIR, "lib"), # or "lib64"
    # ]
    # audio_engine_module.library_dirs.extend(new_library_dirs)
    ```
    *   The exact subdirectory (`lib` or `lib64`) might depend on the system or CMake configuration. It's important to verify this. For consistency, the CMake `CMAKE_INSTALL_LIBDIR` could be set to `lib` for all dependencies.

4.  **Update `libraries`:**
    *   Add the names of the libraries to link against. These are typically the names without the `lib` prefix or `.a`/`.so` suffix.
    ```python
    # Example:
    # new_libraries = ["ortp", "bctoolbox", "bcunit"]
    # audio_engine_module.libraries.extend(new_libraries)
    ```
    *   If static linking was enforced (e.g., `ENABLE_STATIC=ON` for `ortp`), the linker will look for `.a` files. If shared (`.so`), it will look for those. `setuptools` usually handles this correctly based on what it finds in `library_dirs`.

5.  **Consider `extra_link_args` for RPATH (if using shared libraries and not installing system-wide):**
    *   If the dependencies are built as shared libraries (`.so`) and are *not* installed into a standard system library path, the Python extension might not find them at runtime.
    *   To solve this, `RPATH` can be embedded into the extension.
    ```python
    # Example for Linux, if shared libraries are in their respective _install/lib directories:
    # rpath_args = [
    #     f"-Wl,-rpath,{os.path.join(CustomBuildExtCommand.ORTP_INSTALL_DIR, 'lib')}",
    #     f"-Wl,-rpath,{os.path.join(CustomBuildExtCommand.BCTOOLBOX_INSTALL_DIR, 'lib')}",
    #     f"-Wl,-rpath,{os.path.join(CustomBuildExtCommand.BCUNIT_INSTALL_DIR, 'lib')}",
    # ]
    # audio_engine_module.extra_link_args.extend(rpath_args)
    ```
    *   **Static Linking Preference:** For easier distribution of a Python wheel, building `ortp`, `bctoolbox`, and `bcunit` as **static libraries** (`.a`) is generally preferred. If they are static, RPATH is not needed as their code is linked directly into `audio_engine_python.so`.
        *   To enforce static linking for `ortp` (and potentially its deps if they also offer the option), CMake flags like `-DENABLE_STATIC=ON -DENABLE_SHARED=OFF` should be used during their build (Sub-Tasks 0.2, 0.4, 0.5).
        *   If static linking is used, ensure the `.a` files are what's being produced and placed in the `lib` directories.

## Code Alterations:

*   **File:** `setup.py`
*   **Changes:**
    *   Modify the `Extension("audio_engine_python", ...)` definition.
    *   The paths used (`ORTP_INSTALL_DIR`, etc.) must correspond to the actual installation directories configured in the `_build_ortp`, `_build_bctoolbox`, and `_build_bcunit` methods. It's best to use the same path constants defined in `CustomBuildExtCommand`.

    ```python
    # In setup.py, defining the audio_engine_module Extension
    
    # Assuming these paths are defined globally or accessible from where Extension is defined
    # These should match the *_INSTALL_DIR paths used in CustomBuildExtCommand
    SETUP_PY_DIR = os.path.abspath(os.path.dirname(__file__))
    AUDIO_ENGINE_SUBMODULES_DIR = os.path.join(SETUP_PY_DIR, "src", "audio_engine")
    
    BCTOOLBOX_INSTALL_DIR = os.path.join(AUDIO_ENGINE_SUBMODULES_DIR, "bctoolbox", "_install")
    BCUNIT_INSTALL_DIR = os.path.join(AUDIO_ENGINE_SUBMODULES_DIR, "bcunit", "_install")
    ORTP_INSTALL_DIR = os.path.join(AUDIO_ENGINE_SUBMODULES_DIR, "ortp", "_install")

    audio_engine_module = Extension(
        "audio_engine_python",
        sources=[...], # Keep existing sources
        include_dirs=[
            # ... existing include_dirs ...
            os.path.join(ORTP_INSTALL_DIR, "include"),
            os.path.join(BCTOOLBOX_INSTALL_DIR, "include"),
            os.path.join(BCUNIT_INSTALL_DIR, "include"),
        ],
        library_dirs=[
            # ... existing library_dirs ...
            os.path.join(ORTP_INSTALL_DIR, "lib"), # or "lib64"
            os.path.join(BCTOOLBOX_INSTALL_DIR, "lib"), # or "lib64"
            os.path.join(BCUNIT_INSTALL_DIR, "lib"), # or "lib64"
        ],
        libraries=[
            # ... existing libraries ...
            "ortp", 
            "bctoolbox", 
            "bcunit"
        ],
        # ... other args like extra_compile_args ...
        extra_link_args=[
            # ... existing extra_link_args ...
            # Add RPATH args here if using shared libraries and they are not in a standard path
            # Example for shared libs (prefer static if possible):
            # f"-Wl,-rpath,{os.path.join(ORTP_INSTALL_DIR, 'lib')}",
            # f"-Wl,-rpath,{os.path.join(BCTOOLBOX_INSTALL_DIR, 'lib')}",
            # f"-Wl,-rpath,{os.path.join(BCUNIT_INSTALL_DIR, 'lib')}",
        ]
    )
    ```

## Recommendations:

*   **Static Linking:** Strongly consider configuring `ortp`, `bctoolbox`, and `bcunit` to build as static libraries (`-DENABLE_STATIC=ON -DENABLE_SHARED=OFF` in their CMake configurations). This simplifies the Python wheel distribution as the dependencies are bundled into the main extension.
*   **Path Verification:** Double-check the exact output paths for headers (`include`) and libraries (`lib` or `lib64`) from the dependency build process.
*   **Order of Libraries:** The order in `libraries` list usually doesn't matter for modern linkers, but sometimes it can. Typically, list the direct dependencies first.
*   **Clean Builds:** When testing changes to these linking flags, always perform a clean build (`python setup.py clean --all` and remove the `build` directory) to ensure old artifacts don't interfere.
*   **Platform Specifics:** `RPATH` is a Linux/Unix concept. For macOS, it's `-Wl,-rpath,@loader_path/...` or similar. For Windows, DLLs need to be in the PATH or alongside the executable/PYD. Static linking avoids these platform-specific linking complexities for distribution.
