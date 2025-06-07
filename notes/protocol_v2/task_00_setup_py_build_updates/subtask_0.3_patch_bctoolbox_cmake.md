# Sub-Task 0.3: Implement `BCToolboxConfig.cmake` Patching Logic

**Objective:** Implement the logic within the custom `build_ext` command in `setup.py` to programmatically patch the `BCToolboxConfig.cmake` file after `bctoolbox` has been built and installed. This patch is necessary to comment out the `find_dependency(BCUnit)` line.

**Parent Task:** [Update setup.py for oRTP and Dependencies Build](../task_00_setup_py_build_updates.md)
**Previous Sub-Task:** [Sub-Task 0.2: Implement `bctoolbox` Build Logic](./subtask_0.2_implement_bctoolbox_build.md)

## Key Steps & Considerations:

1.  **Define Helper Method `_patch_bctoolbox_cmake`:**
    *   This method will be part of the `CustomBuildExtCommand` class in `setup.py`.
    *   It will be called after `_build_bctoolbox` successfully completes.
    ```python
    # Within CustomBuildExtCommand class in setup.py
    import re # Ensure re is imported at the top of setup.py

    def _patch_bctoolbox_cmake(self):
        print("Patching BCToolboxConfig.cmake...")
        # Path to the BCToolboxConfig.cmake file
        # self.BCTOOLBOX_INSTALL_DIR is assumed to be defined (e.g., src/audio_engine/bctoolbox/_install)
        bctoolbox_config_cmake_path = os.path.join(
            self.BCTOOLBOX_INSTALL_DIR, "share", "BCToolbox", "cmake", "BCToolboxConfig.cmake"
        )

        if not os.path.exists(bctoolbox_config_cmake_path):
            raise RuntimeError(f"ERROR: Cannot find {bctoolbox_config_cmake_path} to patch.")

        try:
            with open(bctoolbox_config_cmake_path, "r") as f:
                content = f.read()

            # Use regex to find and comment out the line: find_dependency(BCUnit)
            # This regex looks for optional leading whitespace, 'find_dependency',
            # optional whitespace, '(BCUnit)', optional whitespace, and then comments out the whole line.
            # It handles cases where BCUnit might have different casing or spacing.
            # The '&' in r"#&" refers to the entire matched string.
            patched_content, num_replacements = re.subn(
                r"^\s*find_dependency\s*\(\s*BCUnit\s*\)", 
                r"#&", 
                content, 
                flags=re.MULTILINE | re.IGNORECASE
            )

            if num_replacements == 0:
                # Check if already patched
                if re.search(r"^\s*#\s*find_dependency\s*\(\s*BCUnit\s*\)", content, flags=re.MULTILINE | re.IGNORECASE):
                    print("BCToolboxConfig.cmake already appears to be patched. Skipping.")
                    return
                else:
                    # This is a more critical error if the line isn't found and isn't patched
                    raise RuntimeError(f"Could not find 'find_dependency(BCUnit)' line in {bctoolbox_config_cmake_path} to patch.")
            
            if num_replacements > 0:
                 with open(bctoolbox_config_cmake_path, "w") as f:
                    f.write(patched_content)
                 print(f"Successfully patched {bctoolbox_config_cmake_path} ({num_replacements} replacement(s) made).")

        except IOError as e:
            raise RuntimeError(f"Error reading or writing {bctoolbox_config_cmake_path}: {e}")
        except Exception as e:
            raise RuntimeError(f"An unexpected error occurred during patching: {e}")
    ```

2.  **Integration into Build Orchestration:**
    *   Call `self._patch_bctoolbox_cmake()` in the `_build_dependencies` method immediately after `self._build_bctoolbox()`.
    ```python
    # In CustomBuildExtCommand._build_dependencies(self):
    # ...
    self._build_bctoolbox()
    self._patch_bctoolbox_cmake() # Call the patching method
    # self._build_bcunit()
    # self._build_ortp()
    # ...
    ```

## Code Alterations:

*   **File:** `setup.py` (within the `CustomBuildExtCommand` class)
*   **Changes:**
    *   Ensure `import re` is present at the top of the file.
    *   Add the `_patch_bctoolbox_cmake` method as defined above.
    *   Ensure `self.BCTOOLBOX_INSTALL_DIR` is correctly defined and accessible.
    *   Modify `_build_dependencies` to call `_patch_bctoolbox_cmake` in the correct sequence.

## Recommendations:

*   **Robustness of Regex:** The provided regex `r"^\s*find_dependency\s*\(\s*BCUnit\s*\)"` with `re.MULTILINE | re.IGNORECASE` should be fairly robust to variations in whitespace and casing.
*   **Idempotency Check:** The patch logic includes a check to see if the line is already commented out. This prevents re-patching or errors if the script is run multiple times.
*   **Error Handling:** Clear error messages are crucial if the file is not found or the specific line to be patched is missing (and not already patched).
*   **File Encoding:** Assume UTF-8 for reading/writing the CMake file, which is standard. If issues arise, explicit encoding might be needed (e.g., `open(..., encoding='utf-8')`).
*   **Alternative to Regex:** For very simple line replacements, `str.replace()` could be used, but regex is more flexible for handling variations in whitespace or structure if the target line isn't perfectly fixed. Given the `build_ortp.sh` uses `sed`, regex is a closer equivalent.
