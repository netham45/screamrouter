# Task: Update setup.py for oRTP and Dependencies Build

**Objective:** Modify `setup.py` to incorporate the build process for oRTP and its dependencies (bctoolbox, bcunit), mirroring the steps in `build_ortp.sh`. This will ensure these libraries are compiled and linked correctly when building the ScreamRouter Python extension.

**Parent Plan Section:** I. Core Technologies & Libraries Selection/Integration

**Key Steps based on `build_ortp.sh`:**

1.  **Custom Build Command:**
    *   Subclass `setuptools.command.build_ext.build_ext` to create a custom build step that executes before the main C++ extension compilation.
    *   This custom command will orchestrate the building of bctoolbox, bcunit, and ortp.

2.  **Dependency Build Order:**
    *   Ensure dependencies are built in the correct order:
        1.  bctoolbox
        2.  bcunit
        3.  ortp

3.  **CMake Integration for Each Library:**
    *   For each library (bctoolbox, bcunit, ortp):
        *   **Define Source and Build Directories:**
            *   `BCTOOLBOX_SRC_DIR = os.path.join(BASE_DIR, "bctoolbox")`
            *   `BCUNIT_SRC_DIR = os.path.join(BASE_DIR, "bcunit")`
            *   `ORTP_SRC_DIR = os.path.join(BASE_DIR, "ortp")`
            *   Define corresponding `_build` and `_install` directories within each source directory (e.g., `BCTOOLBOX_INSTALL_DIR`).
        *   **Clean Previous Builds (Optional but Recommended):**
            *   Remove `CMakeCache.txt`, `Makefile`, `cmake_install.cmake`, `install_manifest.txt`, `config.h`, `*.pc`, `CMakeFiles/`, `_build/`, `_install/` directories/files before starting a new build for that library. This can be done using `shutil.rmtree` and `os.remove` with error checking.
        *   **Create Build Directory:**
            *   `os.makedirs(build_dir, exist_ok=True)`
        *   **Run CMake Configuration:**
            *   Use `subprocess.run` to execute the `cmake` command.
            *   **bctoolbox:**
                *   `cmake -DCMAKE_INSTALL_PREFIX=<BCTOOLBOX_INSTALL_DIR> -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS_COMPONENT=OFF -DENABLE_UNIT_TESTS=OFF ..` (from within `_build` dir)
            *   **bcunit:**
                *   `cmake -DCMAKE_INSTALL_PREFIX=<BCUNIT_INSTALL_DIR> -DCMAKE_BUILD_TYPE=Release ..` (from within `_build` dir)
            *   **ortp:**
                *   `cmake -DBCToolbox_DIR=<BCTOOLBOX_INSTALL_DIR>/share/BCToolbox/cmake -DBCUnit_DIR=<BCUNIT_INSTALL_DIR>/share/BCUnit/cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-Wno-maybe-uninitialized" -DENABLE_UNIT_TESTS=OFF ..` (from within `_build` dir)
        *   **Run Make (Build):**
            *   `subprocess.run(["make", "-j", str(os.cpu_count())])`
        *   **Run Make Install (Install):**
            *   `subprocess.run(["make", "install"])`

4.  **Patching `BCToolboxConfig.cmake`:**
    *   After bctoolbox is installed, programmatically apply the patch:
        ```python
        bctoolbox_config_file = os.path.join(BCTOOLBOX_INSTALL_DIR, "share", "BCToolbox", "cmake", "BCToolboxConfig.cmake")
        if os.path.exists(bctoolbox_config_file):
            with open(bctoolbox_config_file, "r") as f:
                content = f.read()
            # Add '#' to the beginning of the line containing 'find_dependency(BCUnit)'
            content = re.sub(r"^\s*find_dependency\(\s*BCUnit\s*\)", r"#&", content, flags=re.MULTILINE)
            with open(bctoolbox_config_file, "w") as f:
                f.write(content)
        else:
            raise RuntimeError(f"ERROR: Cannot find {bctoolbox_config_file} to patch.")
        ```

5.  **Updating `Extension` in `setup.py`:**
    *   The main C++ extension (`audio_engine_python`) needs to be configured to find the headers and libraries from the locally built dependencies.
    *   **`include_dirs`:**
        *   Add `os.path.join(ORTP_SRC_DIR, "include")` (or the correct path to ortp headers after build, typically within `ORTP_INSTALL_DIR/include`).
        *   Add `os.path.join(BCTOOLBOX_INSTALL_DIR, "include")`.
        *   Add `os.path.join(BCUNIT_INSTALL_DIR, "include")`.
    *   **`library_dirs`:**
        *   Add `os.path.join(ORTP_SRC_DIR, "_build", "src")` (or wherever `libortp.a`/`libortp.so` is built, typically `ORTP_INSTALL_DIR/lib`).
        *   Add `os.path.join(BCTOOLBOX_INSTALL_DIR, "lib")`.
        *   Add `os.path.join(BCUNIT_INSTALL_DIR, "lib")`.
    *   **`libraries`:**
        *   Add `ortp`, `bctoolbox`, `bcunit`. The exact names might need adjustment based on the output library files (e.g., `ortp` vs `libortp`).
    *   **`extra_compile_args` / `extra_link_args`:**
        *   May need to add rpath settings for linking if libraries are not in standard system paths, although static linking or careful library path setup might avoid this.
        *   The `-Wno-maybe-uninitialized` flag for ortp is handled during its own CMake configuration.

**Implementation Details:**

*   The custom build command in `setup.py` should execute these steps.
*   Paths should be constructed using `os.path.join` for cross-platform compatibility.
*   Error handling (`subprocess.check_call` or checking `returncode`) is crucial for each external command.
*   Consider making the number of parallel jobs (`-j$(nproc)`) configurable or using `os.cpu_count()`.
*   The `BASE_DIR` in `setup.py` would typically be `os.path.abspath(os.path.dirname(__file__))`, and then `src/audio_engine` appended to it.

**Acceptance Criteria:**

*   Running `python setup.py build` or `pip install .` successfully compiles bctoolbox, bcunit, and ortp as static or shared libraries.
*   The main ScreamRouter C++ extension compiles and links against these locally built libraries.
*   The build process is robust and replicates the behavior of `build_ortp.sh`.
*   The final Python wheel includes or correctly links these dependencies.
