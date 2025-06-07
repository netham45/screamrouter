# Sub-Task 0.5: Implement `ortp` Build Logic in Custom Command

**Objective:** Implement the specific logic within the custom `build_ext` command in `setup.py` to clean, configure, build, and install the `ortp` library. This is the final dependency build step.

**Parent Task:** [Update setup.py for oRTP and Dependencies Build](../task_00_setup_py_build_updates.md)
**Previous Sub-Task:** [Sub-Task 0.4: Implement `bcunit` Build Logic](./subtask_0.4_implement_bcunit_build.md)

## Key Steps & Considerations:

1.  **Define Helper Method `_build_ortp`:**
    *   This method will be part of the `CustomBuildExtCommand` class in `setup.py`.
    *   It will encapsulate all steps required to build `ortp`.
    ```python
    # Within CustomBuildExtCommand class in setup.py

    def _build_ortp(self):
        print("Building ortp...")
        # Path definitions (e.g., self.ORTP_SRC_DIR, self.ORTP_BUILD_DIR, self.ORTP_INSTALL_DIR,
        # self.BCTOOLBOX_INSTALL_DIR, self.BCUNIT_INSTALL_DIR)
        # are assumed to be defined as class attributes or initialized earlier.

        # Ensure source directory exists
        if not os.path.exists(self.ORTP_SRC_DIR):
            raise RuntimeError(f"ortp source directory not found: {self.ORTP_SRC_DIR}")
        if not os.path.exists(self.BCTOOLBOX_INSTALL_DIR):
            raise RuntimeError(f"bctoolbox install directory not found: {self.BCTOOLBOX_INSTALL_DIR}")
        if not os.path.exists(self.BCUNIT_INSTALL_DIR):
            raise RuntimeError(f"bcunit install directory not found: {self.BCUNIT_INSTALL_DIR}")

        # 1. Clean previous build artifacts
        self._clean_cmake_artifacts(self.ORTP_SRC_DIR, self.ORTP_BUILD_DIR, self.ORTP_INSTALL_DIR)

        # 2. Create build and install directories
        os.makedirs(self.ORTP_BUILD_DIR, exist_ok=True)
        os.makedirs(self.ORTP_INSTALL_DIR, exist_ok=True) # oRTP also needs an install dir for its artifacts

        # 3. Run CMake configuration
        # This requires pointing to the install locations of bctoolbox and bcunit.
        # The paths for BCToolbox_DIR and BCUnit_DIR should point to the directory
        # containing BCToolboxConfig.cmake and BCUnitConfig.cmake respectively.
        # These are typically in <install_dir>/share/<LibName>/cmake/
        bctoolbox_cmake_dir = os.path.join(self.BCTOOLBOX_INSTALL_DIR, "share", "BCToolbox", "cmake")
        bcunit_cmake_dir = os.path.join(self.BCUNIT_INSTALL_DIR, "share", "BCUnit", "cmake")

        cmake_args = [
            "cmake",
            f"-DCMAKE_INSTALL_PREFIX={self.ORTP_INSTALL_DIR}", # oRTP will be installed locally too
            f"-DBCToolbox_DIR={bctoolbox_cmake_dir}",
            f"-DBCUnit_DIR={bcunit_cmake_dir}",
            "-DCMAKE_BUILD_TYPE=Release",
            '-DCMAKE_C_FLAGS="-Wno-maybe-uninitialized"', # As per build_ortp.sh
            "-DENABLE_UNIT_TESTS=OFF", # As per build_ortp.sh
            # "-DENABLE_DOC=OFF", # Optional: if you want to disable docs
            # "-DENABLE_STATIC=ON", # Consider if static linking is preferred for the final extension
            # "-DENABLE_SHARED=OFF", # If static linking
            ".."  # Points to ORTP_SRC_DIR from ORTP_BUILD_DIR
        ]
        self._run_subprocess(cmake_args, cwd=self.ORTP_BUILD_DIR, description="ortp CMake configuration")

        # 4. Run Make (Build)
        make_args = ["make", "-j", str(os.cpu_count())]
        self._run_subprocess(make_args, cwd=self.ORTP_BUILD_DIR, description="ortp build")

        # 5. Run Make Install (Install)
        make_install_args = ["make", "install"]
        self._run_subprocess(make_install_args, cwd=self.ORTP_BUILD_DIR, description="ortp install")
        print("ortp build and install complete.")
    ```

2.  **Integration into Build Orchestration:**
    *   Call `self._build_ortp()` in the `_build_dependencies` method after `_build_bcunit()`.
    ```python
    # In CustomBuildExtCommand._build_dependencies(self):
    # ...
    # self.ORTP_SRC_DIR = os.path.join(self.AUDIO_ENGINE_SUBMODULES_DIR, "ortp")
    # self.ORTP_BUILD_DIR = os.path.join(self.ORTP_SRC_DIR, "_build")
    # self.ORTP_INSTALL_DIR = os.path.join(self.ORTP_SRC_DIR, "_install")
    # ...

    self._build_bctoolbox()
    self._patch_bctoolbox_cmake()
    self._build_bcunit()
    self._build_ortp() # Call the ortp build method
    # ...
    ```

## Code Alterations:

*   **File:** `setup.py` (within the `CustomBuildExtCommand` class)
*   **Changes:**
    *   Add the `_build_ortp` method.
    *   Ensure path constants for `ortp` (e.g., `self.ORTP_SRC_DIR`, `self.ORTP_BUILD_DIR`, `self.ORTP_INSTALL_DIR`) and its dependencies' install directories (`self.BCTOOLBOX_INSTALL_DIR`, `self.BCUNIT_INSTALL_DIR`) are correctly defined and accessible.
    *   The `_clean_cmake_artifacts` and `_run_subprocess` helper methods are reused.
    *   Modify `_build_dependencies` to call `_build_ortp` as the final step in dependency compilation.

## Recommendations:

*   **Dependency Paths (`BCToolbox_DIR`, `BCUnit_DIR`):** It's critical that these CMake variables point to the directories containing the `*Config.cmake` files generated by the installation of `bctoolbox` and `bcunit`. The typical location is `<install_dir>/share/<LibName>/cmake`.
*   **Compiler Flags (`CMAKE_C_FLAGS`):** The `-Wno-maybe-uninitialized` flag is carried over from `build_ortp.sh`.
*   **Static vs. Shared Libraries:** The `build_ortp.sh` script doesn't explicitly set `ENABLE_STATIC=ON` or `ENABLE_SHARED=OFF` for `ortp`, meaning it likely builds shared libraries by default. For embedding into a Python extension, static libraries (`.a`) are often preferred to simplify distribution. If static linking is desired, these CMake flags should be added: `-DENABLE_STATIC=ON -DENABLE_SHARED=OFF`. This will affect how the main C++ extension links to `ortp`. This sub-task assumes the default from `build_ortp.sh` for now, but it's a key consideration for the next sub-task (updating `Extension` objects).
*   **Installation of `ortp`:** `ortp` itself is also "installed" to its local `_install` directory. This makes its headers and libraries available in a predictable location for the main Python extension to link against.
