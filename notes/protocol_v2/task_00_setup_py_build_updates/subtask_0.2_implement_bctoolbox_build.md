# Sub-Task 0.2: Implement `bctoolbox` Build Logic in Custom Command

**Objective:** Implement the specific logic within the custom `build_ext` command in `setup.py` to clean, configure, build, and install the `bctoolbox` library.

**Parent Task:** [Update setup.py for oRTP and Dependencies Build](../task_00_setup_py_build_updates.md)
**Previous Sub-Task:** [Sub-Task 0.1: Design Custom `build_ext` Command](./subtask_0.1_design_custom_build_ext.md)

## Key Steps & Considerations:

1.  **Define Helper Method `_build_bctoolbox`:**
    *   This method will be part of the `CustomBuildExtCommand` class in `setup.py`.
    *   It will encapsulate all steps required to build `bctoolbox`.
    ```python
    # Within CustomBuildExtCommand class in setup.py

    def _build_bctoolbox(self):
        print("Building bctoolbox...")
        # Path definitions (can be class members or passed as arguments if preferred)
        # SETUP_PY_DIR = os.path.abspath(os.path.dirname(__file__))
        # AUDIO_ENGINE_SUBMODULES_DIR = os.path.join(SETUP_PY_DIR, "src", "audio_engine")
        # BCTOOLBOX_SRC_DIR = os.path.join(AUDIO_ENGINE_SUBMODULES_DIR, "bctoolbox")
        # BCTOOLBOX_BUILD_DIR = os.path.join(BCTOOLBOX_SRC_DIR, "_build")
        # BCTOOLBOX_INSTALL_DIR = os.path.join(BCTOOLBOX_SRC_DIR, "_install")

        # Ensure source directory exists
        if not os.path.exists(self.BCTOOLBOX_SRC_DIR):
            raise RuntimeError(f"bctoolbox source directory not found: {self.BCTOOLBOX_SRC_DIR}")

        # 1. Clean previous build artifacts (optional but recommended)
        self._clean_cmake_artifacts(self.BCTOOLBOX_SRC_DIR, self.BCTOOLBOX_BUILD_DIR, self.BCTOOLBOX_INSTALL_DIR)

        # 2. Create build and install directories
        os.makedirs(self.BCTOOLBOX_BUILD_DIR, exist_ok=True)
        os.makedirs(self.BCTOOLBOX_INSTALL_DIR, exist_ok=True)

        # 3. Run CMake configuration
        cmake_args = [
            "cmake",
            f"-DCMAKE_INSTALL_PREFIX={self.BCTOOLBOX_INSTALL_DIR}",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DENABLE_TESTS_COMPONENT=OFF",
            "-DENABLE_UNIT_TESTS=OFF",
            ".." # Points to BCTOOLBOX_SRC_DIR from BCTOOLBOX_BUILD_DIR
        ]
        self._run_subprocess(cmake_args, cwd=self.BCTOOLBOX_BUILD_DIR, description="bctoolbox CMake configuration")

        # 4. Run Make (Build)
        make_args = ["make", "-j", str(os.cpu_count())]
        self._run_subprocess(make_args, cwd=self.BCTOOLBOX_BUILD_DIR, description="bctoolbox build")

        # 5. Run Make Install (Install)
        make_install_args = ["make", "install"]
        self._run_subprocess(make_install_args, cwd=self.BCTOOLBOX_BUILD_DIR, description="bctoolbox install")
        print("bctoolbox build and install complete.")
    ```

2.  **Helper Method for Cleaning Artifacts `_clean_cmake_artifacts`:**
    *   A generic helper to remove common CMake and build artifacts.
    ```python
    # Within CustomBuildExtCommand class in setup.py

    def _clean_cmake_artifacts(self, src_dir, build_dir, install_dir):
        print(f"Cleaning artifacts for {os.path.basename(src_dir)}...")
        if os.path.exists(build_dir):
            shutil.rmtree(build_dir)
        if os.path.exists(install_dir):
            shutil.rmtree(install_dir)
        
        # Remove specific files from source directory if they exist
        files_to_remove = [
            "CMakeCache.txt", "Makefile", "cmake_install.cmake", 
            "install_manifest.txt", "config.h"
        ]
        for filename in files_to_remove:
            file_path = os.path.join(src_dir, filename)
            if os.path.exists(file_path):
                os.remove(file_path)
        
        # Remove CMakeFiles directory
        cmake_files_dir = os.path.join(src_dir, "CMakeFiles")
        if os.path.exists(cmake_files_dir):
            shutil.rmtree(cmake_files_dir)
        
        # Remove .pc files if any (pkg-config files)
        for item in os.listdir(src_dir):
            if item.endswith(".pc"):
                os.remove(os.path.join(src_dir, item))
    ```
    *   This method should be called before configuring each CMake project.

3.  **Helper Method for Running Subprocesses `_run_subprocess`:**
    *   A robust way to call external commands like `cmake` and `make`.
    ```python
    # Within CustomBuildExtCommand class in setup.py

    def _run_subprocess(self, args, cwd, description):
        print(f"Running: {' '.join(args)} in {cwd} ({description})")
        try:
            process = subprocess.Popen(args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            stdout, stderr = process.communicate()
            if process.returncode != 0:
                print(f"ERROR: {description} failed.")
                print(f"Stdout:\n{stdout.decode('utf-8', errors='replace')}")
                print(f"Stderr:\n{stderr.decode('utf-8', errors='replace')}")
                raise subprocess.CalledProcessError(process.returncode, args)
            # print(stdout.decode('utf-8', errors='replace')) # Optional: print stdout on success
        except FileNotFoundError:
            print(f"ERROR: Command '{args[0]}' not found. Ensure CMake and Make are installed and in PATH.")
            raise
        except Exception as e:
            print(f"An unexpected error occurred during {description}: {e}")
            raise
    ```
    *   This helper should handle errors, print output for debugging, and raise exceptions on failure. Using `subprocess.Popen` and `communicate()` allows capturing stdout/stderr.

4.  **Path Definitions:**
    *   The path constants (`BCTOOLBOX_SRC_DIR`, `BCTOOLBOX_BUILD_DIR`, `BCTOOLBOX_INSTALL_DIR`, etc.) should be defined as class attributes or instance attributes initialized in the `CustomBuildExtCommand`'s `__init__` or `run` method, as shown in Sub-Task 0.1. For clarity, they are referenced here as `self.BCTOOLBOX_SRC_DIR`, etc.

## Code Alterations:

*   **File:** `setup.py` (within the `CustomBuildExtCommand` class)
*   **Changes:**
    *   Add the `_build_bctoolbox` method.
    *   Add the `_clean_cmake_artifacts` helper method.
    *   Add the `_run_subprocess` helper method.
    *   Ensure path constants (e.g., `self.BCTOOLBOX_SRC_DIR`) are correctly defined and accessible within these methods.
    *   Call `self._build_bctoolbox()` from the `_build_dependencies` method.

    ```python
    # In CustomBuildExtCommand._build_dependencies(self):
    # ...
    # self.AUDIO_ENGINE_SUBMODULES_DIR = os.path.join(self.SETUP_PY_DIR, "src", "audio_engine")
    # self.BCTOOLBOX_SRC_DIR = os.path.join(self.AUDIO_ENGINE_SUBMODULES_DIR, "bctoolbox")
    # self.BCTOOLBOX_BUILD_DIR = os.path.join(self.BCTOOLBOX_SRC_DIR, "_build")
    # self.BCTOOLBOX_INSTALL_DIR = os.path.join(self.BCTOOLBOX_SRC_DIR, "_install")
    # ... (similarly for BCUNIT and ORTP)

    self._build_bctoolbox()
    # self._patch_bctoolbox_cmake() # Next sub-task
    # self._build_bcunit()
    # self._build_ortp()
    # ...
    ```

## Recommendations:

*   **Error Handling:** The `_run_subprocess` method should clearly indicate which command failed and provide its output. This is critical for diagnosing build issues.
*   **Idempotency:** The cleaning step helps make the build process more idempotent. If a build fails, re-running it should start from a clean state for the failed component.
*   **Configuration:** The CMake arguments (`-DCMAKE_BUILD_TYPE=Release`, `-DENABLE_TESTS_COMPONENT=OFF`, etc.) are taken directly from `build_ortp.sh`. Ensure these are appropriate for the Python extension build.
*   **Parallelism:** `make -j $(nproc)` is used in the shell script. `os.cpu_count()` is the Python equivalent for getting the number of CPUs.
