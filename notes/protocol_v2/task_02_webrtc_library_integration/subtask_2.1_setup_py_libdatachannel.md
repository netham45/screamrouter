# Sub-Task 2.1: `setup.py` Integration for `libdatachannel` and Dependencies

**Objective:** Modify `setup.py` to incorporate the build process for `libdatachannel` and its core dependencies (e.g., OpenSSL, usrsctp, libjuice, plog). This is a complex step due to the number of dependencies and their own build systems. The strategy will likely involve using CMake if `libdatachannel` provides a comprehensive CMake build system for itself and its vendored dependencies, or potentially relying on system-installed versions for some dependencies if local compilation is too complex.

**Parent Task:** [WebRTC Library (libdatachannel) Integration](../task_02_webrtc_library_integration.md)

## Key Steps & Considerations:

1.  **Dependency Analysis for `libdatachannel`:**
    *   Identify all direct and transitive dependencies of `libdatachannel`. Common ones include:
        *   `OpenSSL` (for DTLS)
        *   `usrsctp` (for SCTP, data channels)
        *   `libjuice` (for ICE)
        *   `plog` (for logging, often header-only or simple to integrate)
        *   Potentially others like `cjson`.
    *   Determine how `libdatachannel`'s build system (likely CMake) handles these:
        *   Does it vendor them (include their source code)?
        *   Does it expect them to be system-installed and findable via `pkg-config` or CMake's `find_package`?
        *   Does it allow choosing between vendored and system versions?

2.  **Strategy for Building Dependencies:**
    *   **Option A: Leverage `libdatachannel`'s CMake (Preferred if comprehensive):**
        *   If `libdatachannel`'s main `CMakeLists.txt` can build its key dependencies (like `usrsctp`, `libjuice`) when they are vendored or included as submodules, then the `CustomBuildExtCommand` in `setup.py` can be extended to build `libdatachannel` similarly to how `ortp` was built.
        *   This would involve:
            *   Adding a `_build_libdatachannel` method to `CustomBuildExtCommand`.
            *   This method would run CMake for `libdatachannel`, configuring it to build its dependencies and install them to a local `_install` directory (e.g., `src/audio_engine/libdatachannel/_install`).
            *   CMake flags for `libdatachannel` might include options to enable/disable features or specify paths to dependencies if not automatically found.
    *   **Option B: System Dependencies + Local `libdatachannel` Build:**
        *   For complex dependencies like OpenSSL, it might be more practical to require them to be pre-installed on the system. `setup.py` would then only build `libdatachannel` itself, configuring it to find these system libraries.
        *   This simplifies `setup.py` but adds prerequisites for the build environment.
    *   **Option C: Build each dependency individually (Most Complex):**
        *   Similar to `ortp` and its dependencies, add separate build methods in `CustomBuildExtCommand` for `OpenSSL`, `usrsctp`, `libjuice`, and then `libdatachannel`. This offers maximum control but is the most work.

    *   **Initial Approach:** Assume `libdatachannel` vendors or can easily build `usrsctp` and `libjuice`. `OpenSSL` is often a system dependency. `plog` is often header-only.

3.  **Modify `CustomBuildExtCommand` in `setup.py`:**
    *   Add path definitions for `libdatachannel` (e.g., `LIBDATACHANNEL_SRC_DIR`, `LIBDATACHANNEL_BUILD_DIR`, `LIBDATACHANNEL_INSTALL_DIR`).
    *   Implement `_build_libdatachannel()`:
        *   Call `_clean_cmake_artifacts()` for `libdatachannel`.
        *   Run CMake for `libdatachannel`:
            *   `cmake -DCMAKE_INSTALL_PREFIX=<LIBDATACHANNEL_INSTALL_DIR> -DCMAKE_BUILD_TYPE=Release ... <LIBDATACHANNEL_SRC_DIR>`
            *   Crucial CMake flags for `libdatachannel` might include:
                *   `-DBUILD_SHARED_LIBS=OFF` (if static linking is preferred for easier wheel distribution).
                *   `-DOPENSSL_ROOT_DIR=<path_to_system_openssl>` (if OpenSSL is system-provided and not found automatically).
                *   Flags to enable/disable examples, tests.
                *   Flags related to its own dependencies (e.g., `-DUSE_SYSTEM_USRSCTP=OFF` if using vendored).
        *   Run `make` and `make install`.
    *   Call `_build_libdatachannel()` in the `_build_dependencies()` sequence.

4.  **Update `Extension` Object in `setup.py`:**
    *   Add `libdatachannel`'s include directory (from its `_install/include`) to `include_dirs`.
    *   Add `libdatachannel`'s library directory (from its `_install/lib` or `lib64`) to `library_dirs`.
    *   Add `datachannel` (or the actual library name, e.g., `libdatachannel.a`) to `libraries`.
    *   Also add any libraries `libdatachannel` itself depends on that are not part of its static build and need to be linked explicitly (e.g., `ssl`, `crypto` for OpenSSL, `usrsctp`, `juice`, potentially `pthread`, `dl`). This depends heavily on how `libdatachannel` is built (static/shared) and how it links its own dependencies.
        *   If `libdatachannel` is built statically and correctly bundles its own static dependencies (like a static `usrsctp`), then only `libdatachannel.a` and system libs like OpenSSL might be needed.
        *   If `libdatachannel` is shared, or links to shared versions of `usrsctp`/`libjuice`, then RPATH or other linking strategies are needed for these too.

## Code Alterations:

*   **File:** `setup.py`
    *   Extend `CustomBuildExtCommand` with `_build_libdatachannel()`.
    *   Update path definitions.
    *   Modify the `Extension("audio_engine_python", ...)` to include `libdatachannel` headers, library paths, and library names. This will also require adding linker flags for `libdatachannel`'s dependencies like OpenSSL (`-lssl -lcrypto`), `usrsctp`, `libjuice` if they are not statically linked into `libdatachannel`.

    ```python
    # In CustomBuildExtCommand:
    # self.LIBDATACHANNEL_SRC_DIR = os.path.join(self.AUDIO_ENGINE_SUBMODULES_DIR, "libdatachannel")
    # self.LIBDATACHANNEL_BUILD_DIR = os.path.join(self.LIBDATACHANNEL_SRC_DIR, "_build")
    # self.LIBDATACHANNEL_INSTALL_DIR = os.path.join(self.LIBDATACHANNEL_SRC_DIR, "_install")
    # ...
    # def _build_libdatachannel(self):
    #     print("Building libdatachannel...")
    #     # ... CMake, make, make install logic ...
    #     # Example CMake args:
    #     cmake_args = [
    #         "cmake",
    #         f"-DCMAKE_INSTALL_PREFIX={self.LIBDATACHANNEL_INSTALL_DIR}",
    #         "-DCMAKE_BUILD_TYPE=Release",
    #         "-DBUILD_SHARED_LIBS=OFF", # Aim for static lib
    #         "-DNO_EXAMPLES=ON",
    #         "-DNO_TESTS=ON",
    #         # Potentially flags for OpenSSL location if not found by CMake
    #         self.LIBDATACHANNEL_SRC_DIR 
    #     ]
    #     self._run_subprocess(cmake_args, cwd=self.LIBDATACHANNEL_BUILD_DIR, ...)
    #     # ... make & make install ...

    # In _build_dependencies:
    # ...
    # self._build_ortp()
    # self._build_libdatachannel() # Add this call

    # In Extension object:
    # include_dirs.append(os.path.join(LIBDATACHANNEL_INSTALL_DIR, "include"))
    # library_dirs.append(os.path.join(LIBDATACHANNEL_INSTALL_DIR, "lib")) # or lib64
    # libraries.extend(["datachannel", "usrsctp", "juice", "ssl", "crypto"]) # Example, exact list depends on libdatachannel build
    ```

## Recommendations:

*   **Start with `libdatachannel`'s Documentation:** Consult `libdatachannel`'s build instructions thoroughly. It often details how to handle its dependencies.
*   **Static Linking Focus:** Prioritize building `libdatachannel` and its immediate dependencies (usrsctp, libjuice) statically if possible. This greatly simplifies wheel creation. OpenSSL is often dynamically linked from the system.
*   **Submodules:** If `libdatachannel` and its dependencies are included as Git submodules, ensure they are initialized (`git submodule update --init --recursive`).
*   **Iterative Approach:**
    1.  Try building `libdatachannel` manually outside `setup.py` first to understand its CMake options and dependency handling.
    2.  Then, integrate this process into `setup.py`.
*   **Dependency Hell:** This is the most complex part of integrating WebRTC libraries. Be prepared for troubleshooting CMake finding issues or linker errors.
*   **`pkg-config`:** If system libraries are used, `pkg-config` can help CMake find them. Ensure it's available.

## Acceptance Criteria:

*   `setup.py` can successfully configure, compile, and install `libdatachannel` (and its key bundled/vendored dependencies) to a local directory.
*   The main C++ extension `audio_engine_python` is correctly configured to find `libdatachannel` headers.
*   The main C++ extension links successfully against the compiled `libdatachannel` library and its necessary dependencies (like OpenSSL).
*   The overall build process (`python setup.py build`) completes without errors related to `libdatachannel`.
