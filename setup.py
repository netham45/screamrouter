"""
setup script for screamrouter audio engine (cross-platform)
Attempts to build LAME, bctoolbox, bcunit, and oRTP from local git submodules
on all supported platforms.
On Windows, it tries to automatically find and use vcvarsall.bat
to set up the MSVC build environment if not already configured.
Project uses std::thread, so pthreads-win32 is not included.
"""
import sys
import os
import subprocess
import shutil
from pathlib import Path
import struct
import re # Added for patching

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
        "src_dir": PROJECT_ROOT / "src/audio_engine/lame",
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
    "bctoolbox": {
        "src_dir": PROJECT_ROOT / "src/audio_engine/bctoolbox",
        "build_system": "cmake",
        "cmake_build_dir_name": "_build",
        "cmake_configure_args": [
            "-DCMAKE_BUILD_TYPE=Release",
            "-DENABLE_TESTS_COMPONENT=OFF",
            "-DENABLE_UNIT_TESTS=OFF",
        ],
        "lib_name_win": "bctoolbox.lib", # Assuming standard name, verify if different
        "lib_name_unix_static": "libbctoolbox.a", # Actual name of the .a file
        "unix_install_lib_dir_rel_to_deps_install": "lib64", # Installed to deps/lib64
        "header_dir_name": "bctoolbox", # Headers installed to include/bctoolbox
        "main_header_file_rel_to_header_dir": "defs.h", # A key header in bctoolbox
        "link_name": "bctoolbox",
        "clean_files_rel_to_src": [
            "CMakeCache.txt", "Makefile", "cmake_install.cmake",
            "install_manifest.txt", "config.h", "bctoolbox.pc",
            "bctoolbox-tester.pc", "CMakeFiles", "_build", "_install"
        ]
    },
    "bcunit": {
        "src_dir": PROJECT_ROOT / "src/audio_engine/bcunit",
        "build_system": "cmake",
        "cmake_build_dir_name": "_build",
        "cmake_configure_args": [
            "-DCMAKE_BUILD_TYPE=Release",
        ],
        "lib_name_win": "bcunit.lib", # Assuming standard name
        "lib_name_unix_static": "libbcunit.a", # Actual name of the .a file
        "unix_install_lib_dir_rel_to_deps_install": "lib64", # Installed to deps/lib64
        "header_dir_name": "BCUnit", # Headers installed to include/bcunit
        "main_header_file_rel_to_header_dir": "BCUnit.h",
        "link_name": "bcunit",
        "clean_files_rel_to_src": [
            "CMakeCache.txt", "Makefile", "cmake_install.cmake",
            "install_manifest.txt", "config.h", "bcunit.pc",
            "CMakeFiles", "BCUnitConfig.cmake", "BCUnitConfigVersion.cmake",
            "_build", "_install"
        ]
    },
    "ortp": {
        "src_dir": PROJECT_ROOT / "src/audio_engine/ortp",
        "build_system": "cmake",
        "cmake_build_dir_name": "_build",
        "cmake_configure_args": [ # BCToolbox_DIR and BCUnit_DIR will be added dynamically
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_C_FLAGS=-Wno-maybe-uninitialized", # For GCC/Clang, MSVC will ignore if not applicable
            "-DENABLE_UNIT_TESTS=OFF",
        ],
        "lib_name_win": "ortp.lib", # Assuming standard name
        "lib_name_unix_static": "libortp.a",
        "header_dir_name": "include/ortp",
        "main_header_file_rel_to_header_dir": "ortp.h", # e.g. ortp/include/ortp.h
        "is_installed_to_deps_dir": False, # Special flag for ortp
        "lib_search_path_rel_to_src_build": "src", # e.g. ortp/_build/src/libortp.a
        "link_name": "ortp",
        "clean_files_rel_to_src": [
            "CMakeCache.txt", "Makefile", "cmake_install.cmake", "CMakeFiles",
            "_build"
        ]
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
            # ortp headers are in ortp/include/ortp
            base_include_dir = src_dir_override 

        header_subdir = config.get("header_dir_name", "") # e.g., "bctoolbox", "BCUnit", "include/ortp"
        main_header_file = config["main_header_file_rel_to_header_dir"] # e.g., "defs.h", "BCUnit.h", "ortp.h"
        
        # For ortp, header_subdir is "include/ortp", so base_include_dir is src_dir ("ortp_project_root")
        # Path becomes: ortp_project_root / "include/ortp" / "ortp.h"
        # For bctoolbox, header_subdir is "bctoolbox", base_include_dir is DEPS_INSTALL_INCLUDE_DIR
        # Path becomes: DEPS_INSTALL_INCLUDE_DIR / "bctoolbox" / "defs.h"
        if header_subdir:
            return base_include_dir / header_subdir / main_header_file
        return base_include_dir / main_header_file

    def get_expected_lib_path(self, config, src_dir_override=None, build_dir_override=None):
        lib_name = config["lib_name_unix_static"] if sys.platform != "win32" else config["lib_name_win"]

        if config.get("is_installed_to_deps_dir", True) is False: # ortp case
            if src_dir_override and build_dir_override:
                # Library is in ortp's own build directory, e.g., ortp/_build/src/libortp.a
                lib_search_subdir = config.get("lib_search_path_rel_to_src_build", "")
                if lib_search_subdir:
                    return build_dir_override / lib_search_subdir / lib_name
                return build_dir_override / lib_name
            else: # Should not happen if overrides are correctly passed for ortp
                raise ValueError("src_dir_override and build_dir_override must be provided for non-installed libs")
        else: # bctoolbox, bcunit, lame
            base_lib_dir = DEPS_INSTALL_LIB_DIR
            if sys.platform != "win32" and config.get("unix_install_lib_dir_rel_to_deps_install"):
                # For bctoolbox/bcunit on Unix, they install to deps/lib64
                base_lib_dir = DEPS_INSTALL_DIR / config["unix_install_lib_dir_rel_to_deps_install"]
            return base_lib_dir / lib_name

    def run(self):
        DEPS_INSTALL_LIB_DIR.mkdir(parents=True, exist_ok=True)
        DEPS_INSTALL_INCLUDE_DIR.mkdir(parents=True, exist_ok=True)
        all_deps_processed_successfully = True

        # Define build order
        ordered_deps_to_build = ["lame", "bctoolbox", "bcunit", "ortp"]

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


            if not src_dir.exists():
                print(f"ERROR: Source directory for {dep_name} not found at {src_dir}.", file=sys.stderr)
                print("Ensure git submodules are initialized ('git submodule update --init --recursive').", file=sys.stderr)
                all_deps_processed_successfully = False; continue

            # Perform cleaning before checking existence or building
            # This mirrors the `rm -rf` in build_ortp.sh more closely
            if config["build_system"] == "cmake": # Specific cleaning for CMake projects
                cmake_build_dir_name = config.get("cmake_build_dir_name", "build_cmake") # Default if not specified
                cmake_build_dir_path = src_dir / cmake_build_dir_name
                if cmake_build_dir_path.exists():
                    print(f"Cleaning CMake build directory: {cmake_build_dir_path}")
                    shutil.rmtree(cmake_build_dir_path, ignore_errors=True)
                
                for item_name in config.get("clean_files_rel_to_src", []):
                    item_path = src_dir / item_name
                    if item_path.is_file() or item_path.is_symlink():
                        print(f"Cleaning file: {item_path}")
                        item_path.unlink(missing_ok=True)
                    elif item_path.is_dir() and item_path != cmake_build_dir_path: # Avoid double delete if _build is in list
                        print(f"Cleaning directory: {item_path}")
                        shutil.rmtree(item_path, ignore_errors=True)
            
            # Check if already built (after cleaning, this effectively means we always rebuild if clean_files were present)
            # To truly skip, cleaning should be conditional or a separate step.
            # For now, this matches the task's implication of replicating the shell script's clean-then-build.
            if expected_lib_path.exists() and expected_header_path.exists():
                # This check might be less effective if cleaning always happens before.
                # Consider a "force" flag or make cleaning more targeted if rebuilds are too frequent.
                print(f"{dep_name} artifacts already found ({expected_lib_path}, {expected_header_path}). Assuming up-to-date post-clean attempt."); # continue

            build_successful = False
            try:
                if config["build_system"] == "cmake":
                    cmake_build_dir = src_dir / config.get("cmake_build_dir_name", "build_cmake")
                    # No: if cmake_build_dir.exists(): shutil.rmtree(cmake_build_dir) # Cleaning is done above
                    cmake_build_dir.mkdir(exist_ok=True)

                    configure_cmd_parts = [
                        "cmake", "-S", str(src_dir), "-B", str(cmake_build_dir),
                        f"-DCMAKE_INSTALL_PREFIX={DEPS_INSTALL_DIR}",
                        "-DCMAKE_POLICY_DEFAULT_CMP0077=NEW",
                        # CMAKE_CONFIGURATION_TYPES is for multi-config generators like VS.
                        # For single-config (Makefiles), CMAKE_BUILD_TYPE is used at configure time.
                        # Let's ensure CMAKE_BUILD_TYPE is part of cmake_configure_args if needed.
                    ]
                    if sys.platform == "win32":
                        configure_cmd_parts.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL")
                    else:
                        configure_cmd_parts.append("-DCMAKE_POSITION_INDEPENDENT_CODE=ON")
                    
                    current_cmake_configure_args = list(config.get("cmake_configure_args", []))
                    if dep_name == "ortp":
                        current_cmake_configure_args.extend([
                            f"-DBCToolbox_DIR={DEPS_INSTALL_DIR / 'share/BCToolbox/cmake'}",
                            f"-DBCUnit_DIR={DEPS_INSTALL_DIR / 'share/BCUnit/cmake'}",
                        ])
                    
                    # Ensure CMAKE_BUILD_TYPE=Release is present if not already
                    has_build_type = any("CMAKE_BUILD_TYPE" in arg for arg in current_cmake_configure_args)
                    if not has_build_type and not any("CMAKE_CONFIGURATION_TYPES" in arg for arg in current_cmake_configure_args):
                         current_cmake_configure_args.append("-DCMAKE_BUILD_TYPE=Release")


                    configure_cmd_parts.extend(current_cmake_configure_args)

                    print(f"Running CMake configure for {dep_name}...")
                    self._run_subprocess_in_msvc_env(configure_cmd_parts, src_dir, dep_name=f"{dep_name} CMake configure")

                    # For single-config generators (like Unix Makefiles), --config Release is used here.
                    # For multi-config (like Visual Studio), it's specified during build.
                    build_target = "install"
                    if dep_name == "ortp" and config.get("is_installed_to_deps_dir", True) is False:
                        build_target = None # ortp is not installed, just built

                    build_cmd_parts = ["cmake", "--build", str(cmake_build_dir), "--config", "Release"]
                    if build_target:
                        build_cmd_parts.extend(["--target", build_target])
                    build_cmd_parts.extend(["-j", str(os.cpu_count() or 1)])
                    
                    print(f"Running CMake build{' and ' + build_target if build_target else ''} for {dep_name}...")
                    self._run_subprocess_in_msvc_env(build_cmd_parts, src_dir, dep_name=f"{dep_name} CMake build")
                    build_successful = True

                    if dep_name == "bctoolbox" and build_successful:
                        print("Patching BCToolboxConfig.cmake...")
                        bctoolbox_config_file = DEPS_INSTALL_DIR / "share" / "BCToolbox" / "cmake" / "BCToolboxConfig.cmake"
                        if bctoolbox_config_file.exists():
                            with open(bctoolbox_config_file, "r+", encoding='utf-8') as f:
                                content = f.read()
                            new_content = re.sub(r"^\s*find_dependency\(\s*BCUnit\s*\)", r"#\g<0>", content, flags=re.MULTILINE)
                            if new_content != content:
                                with open(bctoolbox_config_file, "w", encoding='utf-8') as f:
                                    f.write(new_content)
                                print(f"Patched {bctoolbox_config_file}")
                            else:
                                print(f"Patch pattern not found in {bctoolbox_config_file}. Already patched or file changed?")
                        else:
                            print(f"ERROR: Cannot find {bctoolbox_config_file} to patch.", file=sys.stderr)
                            # This should be a failure for ortp build
                            all_deps_processed_successfully = False # Mark as error

                elif config["build_system"] == "autotools_or_nmake": # LAME
                    if sys.platform == "win32":
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
                    else: # LAME autotools
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

# --- Main Extension Configuration ---
source_files = [
    "src/audio_engine/bindings.cpp", "src/audio_engine/audio_manager.cpp",
    "src/audio_engine/network_audio_receiver.cpp",
    "src/audio_engine/rtp_receiver.cpp", "src/audio_engine/raw_scream_receiver.cpp", "src/audio_engine/per_process_scream_receiver.cpp",
    "src/audio_engine/source_input_processor.cpp", "src/audio_engine/sink_audio_mixer.cpp",
    "src/audio_engine/audio_processor.cpp", "src/audio_engine/layout_mixer.cpp",
    "src/audio_engine/biquad/biquad.cpp", "src/configuration/audio_engine_config_applier.cpp",
    "src/audio_engine/r8brain-free-src/r8bbase.cpp", "src/audio_engine/r8brain-free-src/pffft.cpp",
    "src/audio_engine/timeshift_manager.cpp",
    "src/audio_engine/cpp_logger.cpp",
]
main_extension_include_dirs = [
    str(PROJECT_ROOT / "src/audio_engine"), str(PROJECT_ROOT / "src/configuration"),
    str(PROJECT_ROOT / "src/audio_engine/r8brain-free-src"),
    str(PROJECT_ROOT / "src/audio_engine/ortp/include"), # ortp's own include
    str(PROJECT_ROOT / "src/screamrouter_logger"),      # For screamrouter_logger
    str(PROJECT_ROOT / "src/utils")                     # For network_utils
]
platform_extra_compile_args = []
platform_extra_link_args = []
main_extension_libraries = [
    DEPS_CONFIG["lame"]["link_name"],
    DEPS_CONFIG["bctoolbox"]["link_name"],
    DEPS_CONFIG["bcunit"]["link_name"],
    DEPS_CONFIG["ortp"]["link_name"],
]
main_extension_library_dirs = [
    # Add ortp's own library build directory (ortp_src/_build/src)
    str(PROJECT_ROOT / "src/audio_engine/ortp" / DEPS_CONFIG["ortp"].get("cmake_build_dir_name", "_build") / DEPS_CONFIG["ortp"].get("lib_search_path_rel_to_src_build", "src")),
    # Add deps/lib64 for bctoolbox and bcunit on Unix
    str(DEPS_INSTALL_DIR / "lib64") if sys.platform != "win32" else "",
]
# Filter out empty strings from library_dirs that might occur on Windows
main_extension_library_dirs = [d for d in main_extension_library_dirs if d]


if sys.platform == "win32":
    print("Configuring main extension for Windows (MSVC)")
    platform_extra_compile_args.extend(["/std:c++17", "/O2", "/W3", "/EHsc", "/D_CRT_SECURE_NO_WARNINGS", "/MP", "/DLAMELIB_API=", "/DCDECL="])
    main_extension_libraries.append("ws2_32")
elif sys.platform == "darwin":
    print("Configuring main extension for macOS (Clang)")
    platform_extra_compile_args.extend(["-std=c++17", "-O2", "-Wall", "-fPIC", "-stdlib=libc++", "-mmacosx-version-min=10.14"])
else:
    print(f"Configuring main extension for Linux/Unix-like system ({sys.platform})")
    platform_extra_compile_args.extend(["-std=c++17", "-O2", "-Wall", "-fPIC"])

ext_modules = [
    Pybind11Extension("screamrouter_audio_engine",
        sources=sorted([str(PROJECT_ROOT / f) for f in source_files]),
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
    version="0.2.2", # Incremented version for oRTP integration
    author="Cline",
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
