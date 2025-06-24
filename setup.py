"""
setup script for screamrouter audio engine (cross-platform)
Attempts to build LAME, bctoolbox, bcunit, and oRTP from local git submodules
on all supported platforms.
On Windows, it tries to automatically find and use vcvarsall.bat
to set up the MSVC build environment if not already configured.
Project uses std::thread, so pthreads-win32 is not included.
"""
import os
import re  # Added for patching
import shutil
import struct
import subprocess
import sys
from pathlib import Path

from setuptools import setup
from setuptools.command.build_ext import build_ext as _build_ext

try:
    from pybind11.setup_helpers import Pybind11Extension
except ImportError:
    print("Error: pybind11 not found. Please install it using 'pip install pybind11>=2.6'",
          file=sys.stderr)
    sys.exit(1)

# Project root directory
PROJECT_ROOT = Path(__file__).parent.resolve()
# Centralized directory for built dependencies (libs and headers)
DEPS_INSTALL_DIR = PROJECT_ROOT / "build" / "deps"
DEPS_INSTALL_LIB_DIR = DEPS_INSTALL_DIR / "lib"
DEPS_INSTALL_INCLUDE_DIR = DEPS_INSTALL_DIR / "include"

# --- Dependency Definitions for Building from Submodules ---
DEPS_CONFIG = {
    "lame": {
        "src_dir": PROJECT_ROOT / "src/audio_engine/deps/lame",
        "build_system": "autotools_or_nmake",
        "nmake_makefile_rel_path_win": "Makefile.MSVC",
        "nmake_target_win": "",
        "win_lib_search_rel_paths": ["output/libmp3lame-static.lib"],
        "win_headers_rel_path": "include",
        "unix_configure_args": ["--disable-shared", "--enable-static", "--disable-frontend"],
        "unix_make_targets": ["install"],
        "lib_name_win": "mp3lame.lib",
        "lib_name_unix_static": "libmp3lame.a",
        "header_dir_name": "lame",
        "main_header_file_rel_to_header_dir": "lame.h",
        "link_name": "mp3lame",
    },
    "openssl": {
        "src_dir": PROJECT_ROOT / "src/audio_engine/deps/openssl",
        "build_system": "configure_make_openssl", # Changed from "cmake"
        "configure_script_name_unix": "Configure", # Or "config"
        "configure_args_unix": [
            "linux-x86_64", # Target OS, can be platform-dependent. OpenSSL often auto-detects.
                            # Other common targets: darwin64-x86_64-cc (macOS), mingw64 (MinGW on Win)
            "no-shared",    # Build static libraries
            "no-tests",
            "no-apps",
            "no-docs",
            "no-comp",      # Disable compression algorithms if not needed
            "no-engine",    # Disable engine support if not needed
            "no-weak-ssl-ciphers",
            "no-legacy",    # For OpenSSL 3.x, if legacy provider not needed
            # -fPIC is crucial for static libs that will be linked into a shared object (our Python extension)
            # This can be passed via CFLAGS in env, or directly if Configure supports it.
            # OpenSSL's Configure script usually respects CFLAGS/CXXFLAGS from environment.
            # We will ensure -fPIC is in CFLAGS in the _run_subprocess_in_msvc_env (which handles non-Windows too)
            # or pass it directly like: "CFLAGS=-fPIC CXXFLAGS=-fPIC" if needed as part of the command.
            # For OpenSSL 3.x, it's often better to pass options like 'no-pic' or let it handle PIC.
            # However, for linking into a Python extension, PIC is essential.
            # Let's rely on environment CFLAGS for -fPIC for now.
            # --prefix and --openssldir will be added dynamically in the build step.
        ],
        "configure_args_win": [ # For Windows, if attempting native build (complex)
            "VC-WIN64A", # Example for 64-bit MSVC
            "no-shared",
            "no-tests",
            "no-apps",
            "no-docs",
            "no-asm", # Often simplifies Windows builds if NASM isn't configured
        ],
        "make_command_unix": "make",
        "make_command_win": "nmake", # For MSVC builds after Configure
        "make_targets_unix": ["depend", "all"], # Build targets
        "install_targets_unix": ["install_sw", "install_ssldirs"], # Install targets
        "make_targets_win": [], # nmake doesn't usually have 'all' like GNU make
        "install_targets_win": ["install_sw", "install_ssldirs"],
        "lib_name_win": "libcrypto.lib",
        "lib_name_unix_static": "libcrypto.a",
        "extra_libs_unix_static": ["libssl.a"],
        "extra_libs_win": ["libssl.lib"],
        "unix_install_lib_dir_rel_to_deps_install": "lib64", # OpenSSL default
        "header_dir_name": "openssl",
        "main_header_file_rel_to_header_dir": "ssl.h",
        "link_name": "crypto",
        "extra_link_names": ["ssl"],
        "installs_cmake_config": False, # Traditional build does not install CMake config
        "clean_files_rel_to_src": [
            "Makefile", "Makefile.bak", "configdata.pm",
            "libcrypto.a", "libssl.a", "libcrypto.so", "libssl.so",
            "libcrypto.lib", "libssl.lib",
            "libcrypto.pc", "libssl.pc", "openssl.pc",
            "*.o", "*.obj", "apps", "include/openssl/opensslconf.h" # Clean generated files
        ],
        "clean_dirs_rel_to_src": ["_build", "build_cmake"] # Clean old/other build dirs
    },
    "libdatachannel": {
        "src_dir": PROJECT_ROOT / "src/audio_engine/deps/libdatachannel",
        "build_system": "cmake",
        "cmake_build_dir_name": "build", # Common CMake build directory name
        "cmake_configure_args": [
            "-DBUILD_SHARED_LIBS=OFF", # Build libdatachannel as a static library
            "-DCMAKE_BUILD_TYPE=Release",
            "-DOPENSSL_USE_STATIC_LIBS=ON", # If OpenSSL is linked, prefer static
            "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
            "-DUSE_NICE=OFF", # Assuming NICE is not needed for plain RTP
            "-DNO_EXAMPLES=ON",
            "-DNO_TESTS=ON",
            # e.g., -DPLUGINS="" to disable plugins if not used
        ],
        "lib_name_win": "datachannel.lib", # Verify actual static lib name on Windows
        "lib_name_unix_static": "libdatachannel.a",
        "unix_install_lib_dir_rel_to_deps_install": "lib64", # Changed from "lib" to "lib64" for Unix
        "header_dir_name": "rtc", # Headers are typically in include/rtc
        "main_header_file_rel_to_header_dir": "rtc.hpp", # Key header
        "link_name": "datachannel", # Link name for the library
        "extra_link_names": ["juice", "usrsctp", "srtp2"], # Add juice, usrsctp, and srtp2 as dependencies
        "installs_cmake_config": True, # If it installs a CMake config file
        "cmake_config_install_dir_rel": "lib64/cmake/LibDataChannel", # Changed from "lib" to "lib64" for Unix
        "clean_files_rel_to_src": [ # Files/dirs to clean in libdatachannel source dir
            "CMakeCache.txt", "Makefile", "cmake_install.cmake", "CMakeFiles",
            "build", # The build directory itself
            "LibDataChannelConfig.cmake", "LibDataChannelConfigVersion.cmake", # Generated config files
            "LibJuiceConfig.cmake", "LibJuiceConfigVersion.cmake" # If juice is part of it
        ]
    },
    "opus": {
        "src_dir": PROJECT_ROOT / "src/audio_engine/deps/opus",
        "build_system": "autotools_or_nmake",
        "nmake_makefile_rel_path_win": "win32/VS2015/opus.sln", # This is a solution file, requires msbuild
        "win_build_system": "msbuild",
        "win_solution_path_rel": "win32/VS2015/opus.sln",
        "win_static_lib_project_name": "opus",
        "win_static_lib_rel_path": "win32/VS2015/x64/Release/opus.lib",
        "win_headers_rel_path": "include",
        "unix_configure_args": ["--disable-shared", "--enable-static", "--with-pic", "--disable-doc", "--disable-extra-programs"],
        "unix_make_targets": ["install"],
        "lib_name_win": "opus.lib",
        "lib_name_unix_static": "libopus.a",
        "header_dir_name": "opus",
        "main_header_file_rel_to_header_dir": "opus.h",
        "link_name": "opus",
        "installs_cmake_config": False,
    }
}

class BuildExtCommand(_build_ext):
    """Custom build_ext command to build C++ dependencies from submodules."""

    _msvc_env_path_info = None

    def _find_vcvarsall(self):
        if BuildExtCommand._msvc_env_path_info is not None:
            return BuildExtCommand._msvc_env_path_info

        vswhere_path = Path(os.environ.get("ProgramFiles(x86)", Path("C:/Program Files (x86)"))) / "Microsoft Visual Studio/Installer/vswhere.exe"
        if not vswhere_path.exists():
            print("WARNING: vswhere.exe not found. Cannot automatically set up MSVC environment.", file=sys.stderr)
            print("Please run 'pip install .' from a Developer Command Prompt for Visual Studio.", file=sys.stderr)
            BuildExtCommand._msvc_env_path_info = False
            return False
        try:
            cmd = [str(vswhere_path), "-latest", "-prerelease", "-products", "*",
                   "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"]
            result = subprocess.run(cmd, capture_output=True, text=True, check=True, encoding='utf-8')
            vs_install_path = Path(result.stdout.strip())
            vcvarsall_path = vs_install_path / "VC/Auxiliary/Build/vcvarsall.bat"
            if vcvarsall_path.exists():
                BuildExtCommand._msvc_env_path_info = {"vcvarsall": str(vcvarsall_path)}
                return BuildExtCommand._msvc_env_path_info
            else:
                print(f"WARNING: Found VS at {vs_install_path} but vcvarsall.bat missing at {vcvarsall_path}.", file=sys.stderr)
        except (subprocess.CalledProcessError, FileNotFoundError, Exception) as e:
            print(f"WARNING: Error finding Visual Studio using vswhere.exe: {e}", file=sys.stderr)

        print("WARNING: Failed to automatically find vcvarsall.bat.", file=sys.stderr)
        print("Please ensure you are running 'pip install .' from a Developer Command Prompt for Visual Studio.", file=sys.stderr)
        BuildExtCommand._msvc_env_path_info = False
        return False

    def _run_subprocess_in_msvc_env(self, command_parts, cwd, dep_name="dependency"):
        if not sys.platform == "win32":
            current_env = os.environ.copy()
            cflags = current_env.get("CFLAGS", "")
            cxxflags = current_env.get("CXXFLAGS", "")
            if "-fPIC" not in cflags.split(): cflags = (cflags + " -fPIC").strip()
            if "-fPIC" not in cxxflags.split(): cxxflags = (cxxflags + " -fPIC").strip()
            current_env["CFLAGS"] = cflags
            current_env["CXXFLAGS"] = cxxflags
            
            # Removed explicit PYTHONPATH modification to avoid issues with pip's isolated build environment.
            # The default environment should be sufficient for build tools.
            # current_sys_path = os.pathsep.join(sys.path)
            # existing_python_path = current_env.get("PYTHONPATH")
            # if existing_python_path:
            #     current_env["PYTHONPATH"] = current_sys_path + os.pathsep + existing_python_path
            # else:
            #     current_env["PYTHONPATH"] = current_sys_path
            # print(f"  [DEBUG] subprocess PYTHONPATH for {dep_name}: {current_env.get('PYTHONPATH')}")

            subprocess.run(command_parts, cwd=cwd, check=True, env=current_env)
            return

        if os.environ.get("VCINSTALLDIR") and os.environ.get("VCToolsInstallDir"):
            print(f"MSVC environment seems active for {dep_name}. Running command directly: {' '.join(command_parts)}")
            subprocess.run(command_parts, cwd=cwd, check=True, shell=False)
            return

        print(f"Attempting to set up MSVC environment for {dep_name} command: {' '.join(command_parts)}")
        vcvars_info = self._find_vcvarsall()
        if not vcvars_info:
            raise RuntimeError(
                f"Failed to find vcvarsall.bat. Cannot configure MSVC environment for {dep_name}.\n"
                "Please run 'pip install .' from a Developer Command Prompt for Visual Studio."
            )

        vcvarsall_script = vcvars_info["vcvarsall"]
        is_64bit_python = struct.calcsize("P") * 8 == 64
        vcvars_arch = "x64" if is_64bit_python else "x86"
        quoted_command_parts = [f'"{part}"' if ' ' in part and not (part.startswith('"') and part.endswith('"')) else part for part in command_parts]
        inner_command = " ".join(quoted_command_parts)
        full_command_str = f'"{vcvarsall_script}" {vcvars_arch} && {inner_command}'
        print(f"Executing in MSVC env ({vcvars_arch}): {full_command_str}")
        try:
            subprocess.run(full_command_str, cwd=cwd, check=True, shell=True)
        except subprocess.CalledProcessError as e:
            print(f"ERROR: Command failed while running in MSVC environment for {dep_name}.", file=sys.stderr)
            print(f"Command was: {full_command_str}", file=sys.stderr)
            print("Ensure Visual Studio C++ tools are correctly installed and try running from a Developer Command Prompt.", file=sys.stderr)
            raise e

    def get_expected_header_path(self, config, src_dir_override=None):
        base_include_dir = DEPS_INSTALL_INCLUDE_DIR
        if config.get("is_installed_to_deps_dir", True) is False and src_dir_override:
            base_include_dir = src_dir_override 
        header_subdir = config.get("header_dir_name", "")
        main_header_file = config["main_header_file_rel_to_header_dir"]
        if header_subdir:
            return base_include_dir / header_subdir / main_header_file
        return base_include_dir / main_header_file

    def get_expected_lib_path(self, config, src_dir_override=None, build_dir_override=None):
        lib_name = config["lib_name_unix_static"] if sys.platform != "win32" else config["lib_name_win"]
        if config.get("is_installed_to_deps_dir", True) is False: # ortp case
            if src_dir_override and build_dir_override:
                lib_search_subdir = config.get("lib_search_path_rel_to_src_build", "")
                if lib_search_subdir:
                    return build_dir_override / lib_search_subdir / lib_name
                return build_dir_override / lib_name
            else:
                raise ValueError("src_dir_override and build_dir_override must be provided for non-installed libs")
        else: # bctoolbox, bcunit, lame, mbedtls
            base_lib_dir = DEPS_INSTALL_LIB_DIR
            if sys.platform != "win32" and config.get("unix_install_lib_dir_rel_to_deps_install"):
                base_lib_dir = DEPS_INSTALL_DIR / config["unix_install_lib_dir_rel_to_deps_install"]
            return base_lib_dir / lib_name

    def run(self):
        # Removed automatic 'git submodule update' call.
        # User should ensure submodules are initialized via 'git submodule update --init --recursive'
        # in their project directory before running 'pip install .'.
        # The script will check for the existence of submodule source directories.

        DEPS_INSTALL_LIB_DIR.mkdir(parents=True, exist_ok=True)
        DEPS_INSTALL_INCLUDE_DIR.mkdir(parents=True, exist_ok=True)
        all_deps_processed_successfully = True
        ordered_deps_to_build = ["lame", "opus", "openssl", "libdatachannel"]

        for dep_name in ordered_deps_to_build:
            if dep_name not in DEPS_CONFIG:
                print(f"WARNING: Dependency '{dep_name}' in build order but not in DEPS_CONFIG. Skipping.", file=sys.stderr)
                continue
            
            config = DEPS_CONFIG[dep_name]
            print(f"--- Handling dependency: {dep_name} ---")

            src_dir = config["src_dir"].resolve()
            cmake_build_dir_name_for_paths = config.get("cmake_build_dir_name", "build_cmake")
            ortp_build_dir_for_paths = src_dir / cmake_build_dir_name_for_paths if dep_name == "ortp" else None
            expected_lib_path = self.get_expected_lib_path(config, src_dir_override=src_dir if dep_name == "ortp" else None, build_dir_override=ortp_build_dir_for_paths)
            expected_header_path = self.get_expected_header_path(config, src_dir_override=src_dir if dep_name == "ortp" else None)

            if dep_name == "openssl":
                print(f"DEBUG_OPENSSL: Checking existence for src_dir: {src_dir}")
                print(f"DEBUG_OPENSSL: os.path.exists(str(src_dir)): {os.path.exists(str(src_dir))}")
                print(f"DEBUG_OPENSSL: src_dir.is_dir(): {src_dir.is_dir()}")
                # Try listing contents if it's a dir, to see if Python can access it
                if os.path.exists(str(src_dir)) and src_dir.is_dir():
                    try:
                        print(f"DEBUG_OPENSSL: Contents of {src_dir}: {os.listdir(str(src_dir))[:5]}") # Print first 5 items
                    except Exception as e_ls:
                        print(f"DEBUG_OPENSSL: Error listing {src_dir}: {e_ls}")


            if not src_dir.exists():
                print(f"ERROR: Source directory for {dep_name} not found at {src_dir}.", file=sys.stderr)
                print("Ensure git submodules are initialized ('git submodule update --init --recursive').", file=sys.stderr)
                all_deps_processed_successfully = False; continue

            # --- Enhanced Clean for the current dependency's installed artifacts ---
            # This section cleans artifacts from the DEPS_INSTALL_DIR for dependencies that are installed there.
            if config.get("is_installed_to_deps_dir", True):
                # Determine the paths for the library and main header file within DEPS_INSTALL_DIR
                # Note: self.get_expected_lib_path/header_path correctly resolve to DEPS_INSTALL_DIR for these deps.
                lib_to_clean_in_deps = self.get_expected_lib_path(config)
                if lib_to_clean_in_deps.exists():
                    print(f"DEBUG: Force cleaning existing installed library for {dep_name} from DEPS_INSTALL_DIR: {lib_to_clean_in_deps}")
                    lib_to_clean_in_deps.unlink(missing_ok=True)

                header_file_to_clean_in_deps = self.get_expected_header_path(config)
                if header_file_to_clean_in_deps.exists() and header_file_to_clean_in_deps.is_file():
                    print(f"DEBUG: Force cleaning existing installed main header file for {dep_name} from DEPS_INSTALL_DIR: {header_file_to_clean_in_deps}")
                    header_file_to_clean_in_deps.unlink(missing_ok=True)
                
                # Clean the specific header subdirectory within DEPS_INSTALL_INCLUDE_DIR
                # e.g., DEPS_INSTALL_INCLUDE_DIR / "lame" or DEPS_INSTALL_INCLUDE_DIR / "bctoolbox"
                header_subdir_name_in_deps = config.get("header_dir_name")
                # Ensure header_subdir_name_in_deps is a simple name, not a path like "include/ortp"
                if header_subdir_name_in_deps and "/" not in header_subdir_name_in_deps and "\\" not in header_subdir_name_in_deps:
                    installed_header_dir_to_clean = DEPS_INSTALL_INCLUDE_DIR / header_subdir_name_in_deps
                    if installed_header_dir_to_clean.is_dir():
                        print(f"DEBUG: Force cleaning existing installed header directory for {dep_name} from DEPS_INSTALL_DIR: {installed_header_dir_to_clean}")
                        shutil.rmtree(installed_header_dir_to_clean, ignore_errors=True)
            else:
                # For deps not installed to DEPS_INSTALL_DIR (like ortp), expected_lib_path/header_path point to source/build dirs.
                # We should not clean these source/build-specific artifact paths here as part of "installed artifact cleaning".
                # The internal build directory cleaning (cmake_build_dir_path, etc.) handles cleaning within the dep's own build space.
                print(f"DEBUG: Skipping install-dir (DEPS_INSTALL_DIR) clean for {dep_name} as is_installed_to_deps_dir is False.")

            if config["build_system"] == "cmake":
                cmake_build_dir_name = config.get("cmake_build_dir_name", "build_cmake")
                cmake_build_dir_path = src_dir / cmake_build_dir_name
                print(f"Force cleaning internal CMake build directory for {dep_name}: {cmake_build_dir_path}")
                if cmake_build_dir_path.exists():
                    shutil.rmtree(cmake_build_dir_path, ignore_errors=True)
                cmake_cache_in_src = src_dir / "CMakeCache.txt"
                if cmake_cache_in_src.exists():
                    print(f"Cleaning CMakeCache.txt from {src_dir}")
                    cmake_cache_in_src.unlink()
                for item_name in config.get("clean_files_rel_to_src", []):
                    item_path = src_dir / item_name
                    if item_path.is_file() or item_path.is_symlink():
                        print(f"Cleaning file: {item_path}")
                        item_path.unlink(missing_ok=True)
                    elif item_path.is_dir() and item_path != cmake_build_dir_path: # Avoid re-deleting build dir if listed
                        print(f"Cleaning directory: {item_path}")
                        shutil.rmtree(item_path, ignore_errors=True)
            
            # This check should now ideally fail for bctoolbox, forcing a rebuild
            if expected_lib_path.exists() and expected_header_path.exists():
                print(f"WARNING: {dep_name} artifacts still found ({expected_lib_path}, {expected_header_path}) post-install-dir-clean. Build might be skipped if these are valid.");
            # else: # This else branch would be where the build happens
            #    print(f"DEBUG: {dep_name} artifacts not found or cleaned. Proceeding with build.")


            build_successful = False
            try:
                # The original "if expected_lib_path.exists()..." check that skipped builds is now implicitly handled
                # because we delete the files. If they still exist, it's a warning.
                # The build should proceed regardless unless an outer condition prevents it.

                if config["build_system"] == "cmake": # For libdatachannel
                    cmake_build_dir = src_dir / config.get("cmake_build_dir_name", "build_cmake")
                    cmake_build_dir.mkdir(exist_ok=True)

                    configure_cmd_parts = [
                        "cmake", "-S", str(src_dir), "-B", str(cmake_build_dir),
                        f"-DCMAKE_INSTALL_PREFIX={DEPS_INSTALL_DIR}",
                        "-DCMAKE_POLICY_DEFAULT_CMP0077=NEW",
                    ]
                    if sys.platform == "win32":
                        configure_cmd_parts.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL")
                    else:
                        configure_cmd_parts.append("-DCMAKE_POSITION_INDEPENDENT_CODE=ON")
                    
                    current_cmake_configure_args = list(config.get("cmake_configure_args", []))
                    if dep_name == "libdatachannel":
                         current_cmake_configure_args.append(f"-DOPENSSL_ROOT_DIR={DEPS_INSTALL_DIR}")
                         current_cmake_configure_args.append(f"-DCMAKE_PREFIX_PATH={DEPS_INSTALL_DIR}") # Helps find OpenSSL installed in DEPS_INSTALL_DIR
                         current_cmake_configure_args.append("-DOPENSSL_USE_STATIC_LIBS=ON")


                    has_build_type = any("CMAKE_BUILD_TYPE" in arg for arg in current_cmake_configure_args)
                    if not has_build_type and not any("CMAKE_CONFIGURATION_TYPES" in arg for arg in current_cmake_configure_args):
                         current_cmake_configure_args.append("-DCMAKE_BUILD_TYPE=Release")
                    configure_cmd_parts.extend(current_cmake_configure_args)

                    print(f"Running CMake configure for {dep_name}...")
                    self._run_subprocess_in_msvc_env(configure_cmd_parts, src_dir, dep_name=f"{dep_name} CMake configure")
                    
                    build_target = "install" # libdatachannel should also be installed
                    if config.get("is_installed_to_deps_dir", True) is False: # Should not be false for libdatachannel
                        build_target = None

                    build_cmd_parts = ["cmake", "--build", str(cmake_build_dir), "--config", "Release"]
                    if build_target:
                        build_cmd_parts.extend(["--target", build_target])
                    build_cmd_parts.extend(["-j", str(os.cpu_count() or 1)])
                    
                    print(f"Running CMake build{' and ' + build_target if build_target else ''} for {dep_name}...")
                    self._run_subprocess_in_msvc_env(build_cmd_parts, src_dir, dep_name=f"{dep_name} CMake build")
                    build_successful = True

                elif config["build_system"] == "configure_make_openssl": # OpenSSL
                    if sys.platform == "win32":
                        # OpenSSL build on Windows using Configure and nmake is complex.
                        # For now, focusing on Linux/macOS. This would need specific handling.
                        print(f"WARNING: OpenSSL build via Configure/nmake on Windows is not fully implemented in this script. Expect issues.", file=sys.stderr)
                        # Example: perl Configure VC-WIN64A no-asm no-shared --prefix=... --openssldir=...
                        # Then nmake, nmake install_sw
                        # This requires Perl and nmake in PATH.
                        # For simplicity, we might assume OpenSSL is pre-installed on Windows or use vcpkg/Conan.
                        # For now, let this fail or be handled manually.
                        configure_script = "Configure" # Or path to perl.exe
                        openssl_config_args = config.get("configure_args_win", [])
                        # Add prefix and openssldir
                        full_configure_cmd = [configure_script] + openssl_config_args + [
                            f"--prefix={DEPS_INSTALL_DIR}",
                            f"--openssldir={DEPS_INSTALL_DIR / 'ssl'}" # Common practice for openssldir
                        ]
                        # This needs to be run with MSVC env
                        # self._run_subprocess_in_msvc_env(full_configure_cmd, src_dir, dep_name=f"{dep_name} Configure")
                        # self._run_subprocess_in_msvc_env(["nmake"], src_dir, dep_name=f"{dep_name} nmake build")
                        # self._run_subprocess_in_msvc_env(["nmake"] + config.get("make_targets_win", ["install_sw"]), src_dir, dep_name=f"{dep_name} nmake install")
                        print("OpenSSL build on Windows via Configure/nmake needs manual setup or a more robust script.", file=sys.stderr)
                        build_successful = False # Mark as not automatically handled for now
                    else: # Linux/macOS
                        # Clean previous Makefile if it exists from a failed run
                        if (src_dir / "Makefile").exists():
                            (src_dir / "Makefile").unlink()

                        configure_script = src_dir / "Configure"
                        if not configure_script.exists() and (src_dir / "config").exists(): # Some OpenSSL versions use 'config'
                            configure_script = src_dir / "config"
                        elif not configure_script.exists():
                             raise FileNotFoundError(f"OpenSSL Configure script not found at {src_dir / 'Configure'} or {src_dir / 'config'}")

                        openssl_config_args = config.get("configure_args_unix", [])
                        # Add CFLAGS for PIC if not already in args
                        env = os.environ.copy()
                        cflags = env.get("CFLAGS", "")
                        if "-fPIC" not in cflags: cflags = (cflags + " -fPIC").strip()
                        env["CFLAGS"] = cflags
                        
                        # For OpenSSL, it's better to pass CFLAGS via its Configure script if possible,
                        # or ensure they are in the environment. The ./Configure script often respects CFLAGS.
                        # Some OpenSSL versions might need 'CFLAGS=-fPIC ./Configure ...'

                        full_configure_cmd = [str(configure_script)]
                        full_configure_cmd += openssl_config_args + [
                            f"--prefix={DEPS_INSTALL_DIR}",
                            f"--openssldir={DEPS_INSTALL_DIR / 'ssl'}"
                        ]
                        # Remove any duplicate -fPIC if already in openssl_config_args
                        if "-fPIC" in openssl_config_args and "CFLAGS=-fPIC" not in " ".join(full_configure_cmd):
                             pass # Already handled by env CFLAGS or direct arg
                        elif "-fPIC" not in openssl_config_args and "CFLAGS" not in " ".join(full_configure_cmd):
                             # If Configure doesn't pick up CFLAGS from env, might need to prepend
                             # For now, rely on env CFLAGS
                             pass


                        print(f"Running OpenSSL Configure for {dep_name}: {' '.join(full_configure_cmd)}")
                        subprocess.run(full_configure_cmd, cwd=src_dir, check=True, env=env)
                        
                        make_cmd = ["make", "-j", str(os.cpu_count() or 1)]
                        print(f"Running OpenSSL make for {dep_name}: {' '.join(make_cmd)}")
                        subprocess.run(make_cmd, cwd=src_dir, check=True, env=env)
                        
                        install_targets = config.get("install_targets_unix", ["install_sw"]) # install_sw is common
                        for target in install_targets:
                            print(f"Running OpenSSL make {target} for {dep_name}...")
                            subprocess.run(["make", target], cwd=src_dir, check=True, env=env)
                        build_successful = True

                    # Verification for OpenSSL after configure_make_openssl
                    if build_successful and dep_name == "openssl":
                        print(f"DEBUG: Verifying OpenSSL installation in {DEPS_INSTALL_DIR} after Configure/make")
                        openssl_lib_dir_path = DEPS_INSTALL_LIB_DIR
                        if sys.platform != "win32" and config.get("unix_install_lib_dir_rel_to_deps_install"):
                            openssl_lib_dir_path = DEPS_INSTALL_DIR / config["unix_install_lib_dir_rel_to_deps_install"]
                        
                        libcrypto_path = openssl_lib_dir_path / config["lib_name_unix_static"]
                        libssl_path = openssl_lib_dir_path / config["extra_libs_unix_static"][0]
                        
                        print(f"DEBUG: Checking for {libcrypto_path}")
                        if not libcrypto_path.exists():
                            print(f"ERROR: {libcrypto_path} NOT FOUND after OpenSSL build.")
                            build_successful = False
                        else: print(f"DEBUG: Found {libcrypto_path}")

                        print(f"DEBUG: Checking for {libssl_path}")
                        if not libssl_path.exists():
                            print(f"ERROR: {libssl_path} NOT FOUND after OpenSSL build.")
                            build_successful = False
                        else: print(f"DEBUG: Found {libssl_path}")
                        
                        openssl_include_dir = DEPS_INSTALL_INCLUDE_DIR / config["header_dir_name"]
                        openssl_main_header = openssl_include_dir / config["main_header_file_rel_to_header_dir"]
                        print(f"DEBUG: Checking for OpenSSL main header {openssl_main_header}")
                        if not openssl_main_header.exists():
                             print(f"ERROR: OpenSSL main header {openssl_main_header} NOT FOUND.")
                             build_successful = False
                        else: print(f"DEBUG: Found OpenSSL main header {openssl_main_header}")


                elif config["build_system"] == "autotools_or_nmake":
                    if sys.platform == "win32":
                        if config.get("win_build_system") == "msbuild":
                            sln_path = src_dir / config["win_solution_path_rel"]
                            proj_name = config["win_static_lib_project_name"]
                            msbuild_cmd = [
                                "msbuild", str(sln_path),
                                f"/p:Configuration=Release",
                                f"/p:Platform={'x64' if struct.calcsize('P') * 8 == 64 else 'Win32'}",
                                f"/t:{proj_name}"
                            ]
                            self._run_subprocess_in_msvc_env(msbuild_cmd, src_dir, dep_name=f"{dep_name} msbuild")
                            built_lib_path = src_dir / config["win_static_lib_rel_path"]
                            if built_lib_path.exists():
                                shutil.copy2(built_lib_path, DEPS_INSTALL_LIB_DIR / config["lib_name_win"])
                                src_hdr_dir = src_dir / config["win_headers_rel_path"]
                                dest_hdr_dir = DEPS_INSTALL_INCLUDE_DIR / config["header_dir_name"]
                                if dest_hdr_dir.exists(): shutil.rmtree(dest_hdr_dir)
                                shutil.copytree(src_hdr_dir, dest_hdr_dir, dirs_exist_ok=True)
                                build_successful = True
                            else:
                                print(f"ERROR: Library not found at {built_lib_path} after msbuild.", file=sys.stderr)
                                build_successful = False
                        else:
                            makefile_rel_path = config.get("nmake_makefile_rel_path_win", "Makefile.MSVC")
                            nmake_target = config.get("nmake_target_win", "")
                            actual_makefile_path = src_dir / makefile_rel_path
                            if not actual_makefile_path.is_file():
                                print(f"ERROR: LAME Makefile '{actual_makefile_path}' not found for {dep_name}.", file=sys.stderr)
                                build_successful = False
                            else:
                                nmake_cmd = ["nmake", "/f", str(actual_makefile_path), nmake_target, "comp=msvc", "asm=no", "MACHINE=", "LN_OPTS=", "LN_DLL="]
                                self._run_subprocess_in_msvc_env(nmake_cmd, src_dir, dep_name=f"{dep_name} nmake")
                                lib_found_and_copied = False
                                for pattern in config["win_lib_search_rel_paths"]:
                                    found_libs = list(src_dir.glob(pattern))
                                    if found_libs:
                                        found_libs.sort(key=lambda p: (len(str(p)), "x64" not in str(p).lower()))
                                        shutil.copy2(found_libs[0], DEPS_INSTALL_LIB_DIR / config["lib_name_win"])
                                        lib_found_and_copied = True; break
                                if not lib_found_and_copied:
                                    print(f"ERROR: LAME library {config['lib_name_win']} not found after nmake.", file=sys.stderr)
                                    build_successful = False
                                else: build_successful = True
                                if build_successful:
                                    src_hdr_dir = src_dir / config["win_headers_rel_path"]
                                    dest_hdr_dir = DEPS_INSTALL_INCLUDE_DIR / config["header_dir_name"]
                                    if dest_hdr_dir.exists(): shutil.rmtree(dest_hdr_dir)
                                    shutil.copytree(src_hdr_dir, dest_hdr_dir, dirs_exist_ok=True)
                    else: # Autotools for Unix-like systems
                        if not (src_dir / "configure").exists() and (src_dir / "autogen.sh").exists():
                            subprocess.run(["sh", "./autogen.sh"], cwd=src_dir, check=True)
                        configure_cmd = ["./configure", f"--prefix={DEPS_INSTALL_DIR}"] + config["unix_configure_args"]
                        current_env = os.environ.copy()
                        cflags = current_env.get("CFLAGS", "")
                        if "-fPIC" not in cflags.split(): cflags = (cflags + " -fPIC").strip()
                        current_env["CFLAGS"] = cflags
                        subprocess.run(configure_cmd, cwd=src_dir, check=True, env=current_env)
                        subprocess.run(["make", "-j", str(os.cpu_count() or 1)], cwd=src_dir, check=True, env=current_env)
                        subprocess.run(["make"] + config["unix_make_targets"], cwd=src_dir, check=True, env=current_env)
                        build_successful = True

                if build_successful:
                    # --- BEGIN DEBUG: Check for libdatachannel CMake config file after build ---
                    if dep_name == "libdatachannel" and config.get("installs_cmake_config"):
                        libdatachannel_cmake_config_rel_path = config.get("cmake_config_install_dir_rel")
                        if libdatachannel_cmake_config_rel_path:
                            libdatachannel_cmake_config_dir_abs_path = DEPS_INSTALL_DIR / libdatachannel_cmake_config_rel_path
                            expected_config_file = libdatachannel_cmake_config_dir_abs_path / "LibDataChannelConfig.cmake"
                            print(f"DEBUG: Checking for libdatachannel CMake config at {expected_config_file}")
                            if expected_config_file.exists():
                                print(f"DEBUG: Found libdatachannel CMake config: {expected_config_file}")
                            else:
                                print(f"DEBUG: libdatachannel CMake config NOT FOUND in {libdatachannel_cmake_config_dir_abs_path}.")
                                if libdatachannel_cmake_config_dir_abs_path.exists() and libdatachannel_cmake_config_dir_abs_path.is_dir():
                                    print(f"DEBUG: Contents of {libdatachannel_cmake_config_dir_abs_path}: {list(libdatachannel_cmake_config_dir_abs_path.iterdir())}")
                                else:
                                    print(f"DEBUG: Directory {libdatachannel_cmake_config_dir_abs_path} does not exist or is not a directory.")
                        else:
                            print(f"DEBUG: cmake_config_install_dir_rel not defined for {dep_name} in DEPS_CONFIG.")
                    # --- END DEBUG ---

                    if not expected_lib_path.exists():
                        print(f"ERROR: Library file {expected_lib_path} for {dep_name} NOT FOUND post-build.", file=sys.stderr); build_successful = False
                    if not expected_header_path.exists():
                        print(f"ERROR: Main header {expected_header_path} for {dep_name} NOT FOUND post-build.", file=sys.stderr); build_successful = False
                if build_successful: print(f"{dep_name} processed and verified successfully.")
                else:
                    print(f"ERROR: Build or verification failed for {dep_name}.", file=sys.stderr)
                    all_deps_processed_successfully = False

            except (subprocess.CalledProcessError, FileNotFoundError, RuntimeError, Exception) as e:
                print(f"CRITICAL ERROR processing dependency {dep_name}: {e}", file=sys.stderr)
                all_deps_processed_successfully = False; continue
        
        if not all_deps_processed_successfully:
            print("ERROR: One or more dependencies failed to build or verify. Aborting setup.", file=sys.stderr); sys.exit(1)

        for ext in self.extensions:
            if str(DEPS_INSTALL_INCLUDE_DIR) not in ext.include_dirs: ext.include_dirs.insert(0, str(DEPS_INSTALL_INCLUDE_DIR))
            if str(DEPS_INSTALL_LIB_DIR) not in ext.library_dirs: ext.library_dirs.insert(0, str(DEPS_INSTALL_LIB_DIR))
        print(f"Ensured {DEPS_INSTALL_INCLUDE_DIR} and {DEPS_INSTALL_LIB_DIR} are in extension paths.")
        super().run()

        # --- Generate pybind11 stubs after build ---
        if not self.dry_run:
            print("--- Generating pybind11 stubs ---")
            try:
                # The stub generator needs to import the module, which is in the build_lib directory.
                env = os.environ.copy()
                env["PYTHONPATH"] = self.build_lib + os.pathsep + env.get("PYTHONPATH", "")

                stubgen_cmd = [
                    sys.executable,
                    "-m",
                    "pybind11_stubgen",
                    "screamrouter_audio_engine",
                    "--output-dir",
                    ".",  # Output .pyi file to the project root
                    "--no-setup-py-cmd" # Prevent recursion
                ]
                
                print(f"Running command: {' '.join(stubgen_cmd)}")
                subprocess.run(stubgen_cmd, check=True, env=env)
                print("Successfully generated stubs for screamrouter_audio_engine.")

            except (subprocess.CalledProcessError, FileNotFoundError) as e:
                print(f"WARNING: Failed to generate pybind11 stubs: {e}", file=sys.stderr)
                # We don't fail the whole build if stub generation fails.
                pass

# --- Main Extension Configuration ---
src_root = Path("src")
source_files = []
for root, dirs, files in os.walk(str(src_root)):
    # Exclude 'deps' directories from the search
    if 'deps' in dirs:
        dirs.remove('deps')
    for file in files:
        if file.endswith(".cpp"):
            source_files.append(str(Path(root) / file))

# Add r8brain source file directly
source_files.append("src/audio_engine/deps/r8brain-free-src/r8bbase.cpp")

# Dynamically generate include directories from the discovered source files
include_dirs_from_sources = {str(Path(f).parent) for f in source_files}

main_extension_include_dirs = list(include_dirs_from_sources.union({
    str(DEPS_INSTALL_INCLUDE_DIR),
    str(DEPS_INSTALL_INCLUDE_DIR / DEPS_CONFIG["libdatachannel"]["header_dir_name"]),
    str(DEPS_INSTALL_INCLUDE_DIR / DEPS_CONFIG["openssl"]["header_dir_name"]),
    # Add r8brain include directory
    str(PROJECT_ROOT / "src/audio_engine/deps/r8brain-free-src"),
    # Add other necessary non-discoverable include directories here
    str(PROJECT_ROOT / "src/audio_engine/json/include"),
    str(PROJECT_ROOT / "src/audio_engine/opus/include"),
}))
platform_extra_compile_args = []
platform_extra_link_args = []

main_extension_libraries = [
    DEPS_CONFIG["lame"]["link_name"],
    DEPS_CONFIG["libdatachannel"]["link_name"],
    DEPS_CONFIG["openssl"]["link_name"], # e.g., "crypto"
    DEPS_CONFIG["opus"]["link_name"],
]
# Add extra OpenSSL libs if defined (e.g., "ssl")
if DEPS_CONFIG["openssl"].get("extra_link_names"):
    main_extension_libraries.extend(DEPS_CONFIG["openssl"]["extra_link_names"])
if DEPS_CONFIG["libdatachannel"].get("extra_link_names"):
    main_extension_libraries.extend(DEPS_CONFIG["libdatachannel"]["extra_link_names"])

# Add usrsctp if libdatachannel requires it and doesn't bundle/build it.
# This might be system-dependent or require another submodule. For now, assume not needed explicitly here.
# main_extension_libraries.append("usrsctp")


# Removed platform-specificOons of bctoolbox, bcunit, ortp
main_extension_library_dirs = [
    str(DEPS_INSTALL_LIB_DIR), # General lib dir for deps
    # Specific lib dir for libdatachannel if it installs to a subdir like lib64
    str(DEPS_INSTALL_DIR / DEPS_CONFIG["libdatachannel"].get("unix_install_lib_dir_rel_to_deps_install", "lib")) if sys.platform != "win32" else str(DEPS_INSTALL_LIB_DIR),
    # Specific lib dir for openssl if it installs to a subdir like lib64
    str(DEPS_INSTALL_DIR / DEPS_CONFIG["openssl"].get("unix_install_lib_dir_rel_to_deps_install", "lib")) if sys.platform != "win32" else str(DEPS_INSTALL_LIB_DIR),
]
main_extension_library_dirs = [d for d in main_extension_library_dirs if d]

if sys.platform == "win32":
    print("Configuring main extension for Windows (MSVC)")
    platform_extra_compile_args.extend(["/std:c++17", "/O2", "/W3", "/EHsc", "/D_CRT_SECURE_NO_WARNINGS", "/MP", "/DLAMELIB_API=", "/DCDECL="])
    main_extension_libraries.append("ws2_32")
elif sys.platform == "darwin":
    print("Configuring main extension for macOS (Clang)")
    platform_extra_compile_args.extend(["-std=c++17", "-O2", "-Wall", "-fPIC", "-stdlib=libc++", "-mmacosx-version-min=10.14"])
else: # Linux/Unix-like systems
    print(f"Configuring main extension for Linux/Unix-like system ({sys.platform})")
    platform_extra_compile_args.extend(["-std=c++17", "-O2", "-Wall", "-fPIC"])
    # Link libdatachannel. Other dependencies like OpenSSL might be needed explicitly
    # if not handled by libdatachannel's static build or found by the system linker.
    platform_extra_link_args.extend([
        # Example if libdatachannel needs explicit linking for its deps:
        f"-l{DEPS_CONFIG['openssl']['extra_link_names'][0] if DEPS_CONFIG['openssl'].get('extra_link_names') else 'ssl'}", # Link ssl
        f"-l{DEPS_CONFIG['openssl']['link_name']}", # Link crypto
        # "-lusrsctp", # If needed
        f"-l{DEPS_CONFIG['libdatachannel']['link_name']}",
        "-lpthread", # Threads often needed with network libs
        "-ldl"       # For dynamic loading if any dep uses it
    ])
    # If libdatachannel is a static library and its dependencies (OpenSSL, usrsctp) are also static
    # and need to be linked directly, they might need to be wrapped with --whole-archive if they
    # are not automatically pulled in. For now, assuming standard linking works.

ext_modules = [
    Pybind11Extension("screamrouter_audio_engine",
        sources=sorted(source_files),
        include_dirs=main_extension_include_dirs,
        library_dirs=main_extension_library_dirs,
        libraries=main_extension_libraries,
        extra_compile_args=platform_extra_compile_args,
        extra_link_args=platform_extra_link_args,
        language='c++',
        cxx_std=17)
]

setup(
    name="screamrouter_audio_engine",
    version="0.2.2",
    author="Netham45",
    description="C++ audio engine for ScreamRouter (builds LAME, oRTP stack from submodules, auto MSVC env setup)",
    long_description=(PROJECT_ROOT / "README.md").read_text(encoding="utf-8") if (PROJECT_ROOT / "README.md").exists() else \
                     "Builds LAME, bctoolbox, bcunit, oRTP from submodules. Tries to auto-setup MSVC env on Windows.",
    long_description_content_type="text/markdown",
    ext_modules=ext_modules,
    cmdclass={"build_ext": BuildExtCommand},
    zip_safe=False,
    python_requires=">=3.7",
    classifiers=[
        "Development Status :: 3 - Alpha", "Intended Audience :: Developers",
        "Programming Language :: Python :: 3", "Programming Language :: C++",
        "Operating System :: Microsoft :: Windows", "Operating System :: MacOS :: MacOS X",
        "Operating System :: POSIX :: Linux", "Topic :: Multimedia :: Sound/Audio",
    ],
)
