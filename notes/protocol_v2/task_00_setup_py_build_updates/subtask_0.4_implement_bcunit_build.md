# Sub-Task 0.4: Implement `bcunit` Build Logic in Custom Command

**Objective:** Implement the specific logic within the custom `build_ext` command in `setup.py` to clean, configure, build, and install the `bcunit` library. This follows the successful build and patching of `bctoolbox`.

**Parent Task:** [Update setup.py for oRTP and Dependencies Build](../task_00_setup_py_build_updates.md)
**Previous Sub-Task:** [Sub-Task 0.3: Implement `BCToolboxConfig.cmake` Patching Logic](./subtask_0.3_patch_bctoolbox_cmake.md)

## Key Steps & Considerations:

1.  **Define Helper Method `_build_bcunit`:**
    *   This method will be part of the `CustomBuildExtCommand` class in `setup.py`.
    *   It will encapsulate all steps required to build `bcunit`.
    ```python
    # Within CustomBuildExtCommand class in setup.py

    def _build_bcunit(self):
        print("Building bcunit...")
        # Path definitions (e.g., self.BCUNIT_SRC_DIR, self.BCUNIT_BUILD_DIR, self.BCUNIT_INSTALL_DIR)
        # are assumed to be defined as class attributes or initialized earlier.

        # Ensure source directory exists
        if not os.path.exists(self.BCUNIT_SRC_DIR):
            raise RuntimeError(f"bcunit source directory not found: {self.BCUNIT_SRC_DIR}")

        # 1. Clean previous build artifacts
        self._clean_cmake_artifacts(self.BCUNIT_SRC_DIR, self.BCUNIT_BUILD_DIR, self.BCUNIT_INSTALL_DIR)

        # 2. Create build and install directories
        os.makedirs(self.BCUNIT_BUILD_DIR, exist_ok=True)
        os.makedirs(self.BCUNIT_INSTALL_DIR, exist_ok=True)

        # 3. Run CMake configuration
        # Note: bcunit's CMake doesn't require pointing to bctoolbox or other complex dependencies
        # in the same way ortp will. It's a simpler build.
        cmake_args = [
            "cmake",
            f"-DCMAKE_INSTALL_PREFIX={self.BCUNIT_INSTALL_DIR}",
            "-DCMAKE_BUILD_TYPE=Release",
            # "-DBCUNIT_DOCUMENTATION=OFF", # Optional: if you want to disable docs
            # "-DBCUNIT_TESTING=OFF",       # Optional: if you want to disable tests
            ".."  # Points to BCUNIT_SRC_DIR from BCUNIT_BUILD_DIR
        ]
        self._run_subprocess(cmake_args, cwd=self.BCUNIT_BUILD_DIR, description="bcunit CMake configuration")

        # 4. Run Make (Build)
        make_args = ["make", "-j", str(os.cpu_count())]
        self._run_subprocess(make_args, cwd=self.BCUNIT_BUILD_DIR, description="bcunit build")

        # 5. Run Make Install (Install)
        make_install_args = ["make", "install"]
        self._run_subprocess(make_install_args, cwd=self.BCUNIT_BUILD_DIR, description="bcunit install")
        print("bcunit build and install complete.")
    ```

2.  **Integration into Build Orchestration:**
    *   Call `self._build_bcunit()` in the `_build_dependencies` method after `_patch_bctoolbox_cmake()`.
    ```python
    # In CustomBuildExtCommand._build_dependencies(self):
    # ...
    # self.BCUNIT_SRC_DIR = os.path.join(self.AUDIO_ENGINE_SUBMODULES_DIR, "bcunit")
    # self.BCUNIT_BUILD_DIR = os.path.join(self.BCUNIT_SRC_DIR, "_build")
    # self.BCUNIT_INSTALL_DIR = os.path.join(self.BCUNIT_SRC_DIR, "_install")
    # ...

    self._build_bctoolbox()
    self._patch_bctoolbox_cmake()
    self._build_bcunit() # Call the bcunit build method
    # self._build_ortp()
    # ...
    ```

## Code Alterations:

*   **File:** `setup.py` (within the `CustomBuildExtCommand` class)
*   **Changes:**
    *   Add the `_build_bcunit` method.
    *   Ensure path constants for `bcunit` (e.g., `self.BCUNIT_SRC_DIR`, `self.BCUNIT_BUILD_DIR`, `self.BCUNIT_INSTALL_DIR`) are correctly defined and accessible. These should be initialized similarly to the `bctoolbox` paths.
    *   The `_clean_cmake_artifacts` and `_run_subprocess` helper methods (defined in Sub-Task 0.2) will be reused.
    *   Modify `_build_dependencies` to call `_build_bcunit` in the correct sequence.

## Recommendations:

*   **Simplicity of `bcunit` Build:** The `build_ortp.sh` script shows a straightforward CMake configuration for `bcunit` without complex inter-dependencies for its own build (unlike `ortp` which depends on `bctoolbox` and `bcunit`).
*   **CMake Options:** The `build_ortp.sh` script doesn't pass many specific options to `bcunit`'s CMake. If there are options to disable documentation or tests (e.g., `-DBCUNIT_DOCUMENTATION=OFF`, `-DBCUNIT_TESTING=OFF`), they could be added to reduce build time or complexity if not needed. These are typically found by inspecting `bcunit`'s `CMakeLists.txt`.
*   **Error Handling:** Relies on the robustness of `_run_subprocess` for error reporting.
*   **Path Consistency:** Ensure the `_install` directory structure created by `bcunit`'s `make install` is as expected and will be correctly picked up by `ortp`'s build process later (via `BCUnit_DIR` CMake variable).
