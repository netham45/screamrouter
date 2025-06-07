# Sub-Task 3.1: Add `pjproject` Dependency and System Setup

**Objective:** Add `pjproject` (specifically its Python bindings for `pjsua2`) to the project's dependencies and ensure that the underlying C libraries for `pjproject` are available on the system or guide the user on their installation.

**Parent Task:** [SIP Library (pjproject) Integration](../task_03_sip_library_integration.md)

## Key Steps & Considerations:

1.  **Add `pjproject` to Python Dependencies:**
    *   **File:** `requirements.txt` (or `pyproject.toml` if using Poetry/PDM).
    *   **Action:** Add the appropriate `pjproject` Python package. The exact package name can vary (e.g., `pjproject`, `pjsua2`). Research the most stable and well-maintained Python wrapper for `pjsua2`.
        *   A common one might be simply `pjproject`.
        ```
        # In requirements.txt
        # ... other dependencies ...
        pjproject
        ```
    *   **Note:** Some `pjproject` Python wrappers might be thin layers over compiled C libraries and might require `pjproject` to be compiled and installed on the system separately. Others might attempt to bundle or build `pjproject` during their own installation. This needs to be clarified by the chosen Python package's documentation.

2.  **System-Level `pjproject` C Libraries:**
    *   `pjsua2` Python bindings are wrappers around the `pjproject` C libraries. These C libraries **must** be present on the system where ScreamRouter runs.
    *   **Guidance for Users/Developers:**
        *   Update project documentation (`README.md` or a build guide) with instructions on how to install `pjproject` C libraries.
        *   **Linux (Debian/Ubuntu Example):**
            ```bash
            sudo apt-get update
            sudo apt-get install libpjproject-dev # Or a similar package providing pjproject development files
            # Sometimes specific versions or a full build from source is needed for stability/features.
            ```
        *   **Linux (Build from Source - More Control):**
            If a specific version or configuration of `pjproject` is needed, building from source is common:
            ```bash
            # Download pjproject source tarball (e.g., from https://www.pjsip.org)
            tar -jxvf pjproject-X.Y.Z.tar.bz2
            cd pjproject-X.Y.Z
            ./configure --prefix=/usr/local # Or other desired prefix
            make dep
            make
            sudo make install
            sudo ldconfig # Update dynamic linker cache
            ```
            The `./configure` step might need flags to enable/disable certain features (e.g., SRTP, TLS, specific codecs). For ScreamRouter, basic SIP, SDP, and transport (UDP/TCP) capabilities are essential.
        *   **macOS:** `brew install pjproject` might be an option.
        *   **Windows:** This is often more complex. `pjproject` provides Visual Studio solutions, or users might use MSYS2/MinGW to build it. Pre-compiled binaries for Windows might be available from some sources but are less common for direct Python integration.
    *   **ScreamRouter Build Process:** The `setup.py` for ScreamRouter's C++ extension does *not* need to build `pjproject`'s C libraries. This is a runtime dependency for the Python part of ScreamRouter that uses the `pjsua2` Python module.

3.  **Verify Python Wrapper Installation:**
    *   After adding to `requirements.txt` and installing system C libraries, test the Python wrapper installation in a virtual environment:
        ```bash
        pip install -r requirements.txt
        python -c "import pjsua2 as pj; print(f'PJSIP version: {pj.Lib.instance().version().full}')"
        ```
    *   If the import fails or `pj.Lib.instance()` causes errors, it usually means the underlying C libraries are missing, not found (linker path issues), or there's a version mismatch.

4.  **Consider `LD_LIBRARY_PATH` / Linker Cache:**
    *   If `pjproject` C libraries are installed to a non-standard location (e.g., `/opt/pjproject`), the system's dynamic linker needs to be able to find them.
    *   This might involve:
        *   Setting `LD_LIBRARY_PATH` (Linux/Unix) before running ScreamRouter.
        *   Running `sudo ldconfig` after installing to a standard system path or a path known to `ldconfig`.
        *   For development, ensuring the Python interpreter can find the `.so` files.

## Code Alterations:

*   **File:** `requirements.txt` (or `pyproject.toml`)
    *   Add the chosen `pjproject` Python package.
*   **Documentation:**
    *   Update `README.md` or a dedicated `BUILDING.md` / `INSTALL.md` with prerequisites for `pjproject` C libraries, including installation commands for common platforms or instructions for building from source.

## Recommendations:

*   **Choose a Stable Python Wrapper:** Research available `pjsua2` Python wrappers. Some might be outdated or unmaintained. The official `pjproject` source includes Python SWIG bindings that are generally reliable if built correctly.
*   **Version Pinning:** Consider pinning the version of the `pjproject` Python package in `requirements.txt` for reproducible builds, especially if it's sensitive to the version of the underlying C libraries.
*   **Docker/Containerization:** If ScreamRouter is distributed via Docker, the `pjproject` C libraries would be installed as part of the Docker image build process, simplifying setup for end-users of the Docker image.
*   **Error Messages:** If the Python wrapper fails to import, the error messages can sometimes be cryptic. Common causes are missing `.so`/`.dylib`/`.dll` files or version incompatibilities.

## Acceptance Criteria:

*   `pjproject` Python package is added to project dependencies.
*   Clear instructions are provided in project documentation for installing the required `pjproject` C libraries.
*   After following the setup instructions, `import pjsua2` succeeds in a Python environment where ScreamRouter will run.
*   Basic version information can be retrieved from the imported `pjsua2` library, confirming it's linked correctly.
