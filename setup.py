"""
setup script for screamrouter audio engine (cross-platform)
Attempts to build LAME and libsamplerate from local git submodules
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
        # For LAME on Windows (nmake)
        "nmake_makefile_rel_path_win": "Makefile.MSVC", # Path to Makefile.MSVC relative to src_dir
        "nmake_target_win": "",                      # User's nmake args are hardcoded below for now
        "win_lib_search_rel_paths": [
            "output/libmp3lame-static.lib", # User's specific path from their setup.py context
        ],
        "win_headers_rel_path": "include",
        # For LAME on Unix (autotools)
        "unix_configure_args": ["--disable-shared", "--enable-static", "--disable-frontend"],
        "unix_make_targets": ["install"],
        # Final names in DEPS_INSTALL_DIR
        "lib_name_win": "mp3lame.lib",
        "lib_name_unix_static": "libmp3lame.a",
        "header_dir_name": "lame", # Results in: build/deps/include/lame/lame.h
        "main_header_file_rel_to_header_dir": "lame.h",
        "link_name": "mp3lame",
    },
    "samplerate": {
        "src_dir": PROJECT_ROOT / "src/audio_engine/libsamplerate",
        "build_system": "cmake",
        "cmake_args": [
            "-DLIBSAMPLERATE_EXAMPLES=OFF", "-DLIBSAMPLERATE_TESTS=OFF",
            "-DBUILD_SHARED_LIBS=OFF",
            f"-DCMAKE_INSTALL_LIBDIR={DEPS_INSTALL_LIB_DIR.name}",
        ],
        "lib_name_win": "samplerate.lib",
        "lib_name_unix_static": "libsamplerate.a",
        "header_dir_name": "", # Results in: build/deps/include/samplerate.h
        "main_header_file_rel_to_header_dir": "samplerate.h",
        "link_name": "samplerate",
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
            subprocess.run(command_parts, cwd=cwd, check=True)
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

        quoted_command_parts = []
        for part in command_parts:
            if ' ' in part and not (part.startswith('"') and part.endswith('"')):
                quoted_command_parts.append(f'"{part}"')
            else:
                quoted_command_parts.append(part)
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

    def get_expected_lib_path(self, config):
        if sys.platform == "win32": return DEPS_INSTALL_LIB_DIR / config["lib_name_win"]
        return DEPS_INSTALL_LIB_DIR / config["lib_name_unix_static"]

    def get_expected_header_path(self, config):
        if config["header_dir_name"]:
            return DEPS_INSTALL_INCLUDE_DIR / config["header_dir_name"] / config["main_header_file_rel_to_header_dir"]
        return DEPS_INSTALL_INCLUDE_DIR / config["main_header_file_rel_to_header_dir"]

    def run(self):
        DEPS_INSTALL_LIB_DIR.mkdir(parents=True, exist_ok=True)
        DEPS_INSTALL_INCLUDE_DIR.mkdir(parents=True, exist_ok=True)
        all_deps_processed_successfully = True

        for name, config in DEPS_CONFIG.items():
            print(f"--- Handling dependency: {name} ---")
            src_dir = config["src_dir"].resolve()
            expected_lib_path = self.get_expected_lib_path(config)
            expected_header_path = self.get_expected_header_path(config)

            if not src_dir.exists():
                print(f"ERROR: Source directory for {name} not found at {src_dir}.", file=sys.stderr)
                print("Ensure git submodules are initialized ('git submodule update --init --recursive').", file=sys.stderr)
                all_deps_processed_successfully = False; continue

            if expected_lib_path.exists() and expected_header_path.exists():
                print(f"{name} artifacts already found ({expected_lib_path}, {expected_header_path}). Skipping build."); continue

            build_successful = False
            try:
                if config["build_system"] == "cmake":
                    cmake_build_dir = src_dir / "build_cmake"
                    if cmake_build_dir.exists(): shutil.rmtree(cmake_build_dir)
                    cmake_build_dir.mkdir(exist_ok=True)

                    configure_cmd_parts = [
                        "cmake", "-S", str(src_dir), "-B", str(cmake_build_dir),
                        f"-DCMAKE_INSTALL_PREFIX={DEPS_INSTALL_DIR}",
                        "-DCMAKE_POLICY_DEFAULT_CMP0077=NEW",
                        "-DCMAKE_CONFIGURATION_TYPES=Release",
                    ]
                    if sys.platform == "win32":
                        configure_cmd_parts.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL")
                    # START MODIFICATION for samplerate PIC on Linux/Unix
                    elif sys.platform != "win32": # For samplerate (and any other CMake dep) on Unix-like
                        configure_cmd_parts.append("-DCMAKE_POSITION_INDEPENDENT_CODE=ON")
                    # END MODIFICATION

                    configure_cmd_parts.extend(config["cmake_args"])

                    print(f"Running CMake configure for {name}...")
                    self._run_subprocess_in_msvc_env(configure_cmd_parts, src_dir, dep_name=f"{name} CMake configure")

                    build_cmd_parts = ["cmake", "--build", str(cmake_build_dir), "--config", "Release", "--target", "install", "-j", str(os.cpu_count() or 1)]
                    print(f"Running CMake build/install for {name}...")
                    self._run_subprocess_in_msvc_env(build_cmd_parts, src_dir, dep_name=f"{name} CMake build")
                    build_successful = True

                elif config["build_system"] == "autotools_or_nmake":
                    if sys.platform == "win32":
                        makefile_rel_path = config.get("nmake_makefile_rel_path_win", "Makefile.MSVC")
                        nmake_target = config.get("nmake_target_win", "")
                        actual_makefile_path = src_dir / makefile_rel_path

                        if not actual_makefile_path.is_file():
                            print(f"ERROR: LAME Makefile '{actual_makefile_path}' not found for {name}.", file=sys.stderr)
                            print(f"Check 'nmake_makefile_rel_path_win' in DEPS_CONFIG or LAME submodule structure at '{src_dir}'.", file=sys.stderr)
                            build_successful = False
                        else:
                            nmake_cmd = ["nmake", "/f", str(actual_makefile_path), nmake_target, "comp=msvc", "asm=no", "MACHINE=", "LN_OPTS=", "LN_DLL="]
                            self._run_subprocess_in_msvc_env(nmake_cmd, src_dir, dep_name=f"{name} nmake")

                            lib_found_and_copied = False
                            for pattern in config["win_lib_search_rel_paths"]:
                                found_libs = list(src_dir.glob(pattern))
                                if found_libs:
                                    found_libs.sort(key=lambda p: (len(str(p)), "x64" not in str(p).lower()))
                                    shutil.copy2(found_libs[0], DEPS_INSTALL_LIB_DIR / config["lib_name_win"])
                                    print(f"Copied {found_libs[0]} to {DEPS_INSTALL_LIB_DIR / config['lib_name_win']}")
                                    lib_found_and_copied = True; break
                            if not lib_found_and_copied:
                                print(f"ERROR: LAME library {config['lib_name_win']} not found in {src_dir} via patterns after nmake.", file=sys.stderr)
                                build_successful = False
                            else: build_successful = True

                            if build_successful:
                                src_hdr_dir = src_dir / config["win_headers_rel_path"]
                                if not src_hdr_dir.is_dir():
                                    print(f"ERROR: LAME source header directory '{src_hdr_dir}' not found for copying.", file=sys.stderr)
                                    print(f"Please ensure 'win_headers_rel_path' in DEPS_CONFIG for LAME is correctly set (e.g., to 'include').")
                                    build_successful = False
                                else:
                                    dest_hdr_dir = DEPS_INSTALL_INCLUDE_DIR / config["header_dir_name"]
                                    if dest_hdr_dir.exists(): shutil.rmtree(dest_hdr_dir)
                                    shutil.copytree(src_hdr_dir, dest_hdr_dir, dirs_exist_ok=True)
                                    print(f"Copied LAME headers from {src_hdr_dir} to {dest_hdr_dir}")

                    else: # LAME autotools build (Linux/macOS)
                        if not (src_dir / "configure").exists() and (src_dir / "autogen.sh").exists():
                            print(f"Running autogen.sh for {name} in {src_dir}...")
                            subprocess.run(["sh", "./autogen.sh"], cwd=src_dir, check=True)

                        configure_cmd = ["./configure", f"--prefix={DEPS_INSTALL_DIR}"] + config["unix_configure_args"]
                        print(f"Running configure for {name}: {' '.join(configure_cmd)}")

                        # START MODIFICATION for LAME PIC on Linux/Unix
                        current_env = os.environ.copy()
                        if sys.platform != "win32": # Add -fPIC for Unix-like systems (LAME)
                            cflags = current_env.get("CFLAGS", "")
                            cxxflags = current_env.get("CXXFLAGS", "")
                            if "-fPIC" not in cflags.split():
                                cflags = (cflags + " -fPIC").strip()
                            if "-fPIC" not in cxxflags.split(): # LAME is C, but good practice
                                cxxflags = (cxxflags + " -fPIC").strip()
                            current_env["CFLAGS"] = cflags
                            current_env["CXXFLAGS"] = cxxflags
                        # END MODIFICATION

                        subprocess.run(configure_cmd, cwd=src_dir, check=True, env=current_env) # Use modified env

                        make_cmd = ["make", "-j", str(os.cpu_count() or 1)]
                        print(f"Running make for {name}: {' '.join(make_cmd)}")
                        subprocess.run(make_cmd, cwd=src_dir, check=True, env=current_env) # Use modified env

                        make_install_cmd = ["make"] + config["unix_make_targets"]
                        print(f"Running make install for {name}: {' '.join(make_install_cmd)}")
                        subprocess.run(make_install_cmd, cwd=src_dir, check=True, env=current_env) # Use modified env
                        build_successful = True

                if build_successful:
                    if not expected_lib_path.exists():
                        print(f"ERROR: Library file {expected_lib_path} for {name} NOT FOUND post-build.", file=sys.stderr); build_successful = False
                    if not expected_header_path.exists():
                        print(f"ERROR: Main header {expected_header_path} for {name} NOT FOUND post-build.", file=sys.stderr); build_successful = False

                if build_successful: print(f"{name} processed and verified successfully.")
                else:
                    print(f"ERROR: Build or verification failed for {name}.", file=sys.stderr)
                    all_deps_processed_successfully = False

            except (subprocess.CalledProcessError, FileNotFoundError, RuntimeError, Exception) as e:
                print(f"CRITICAL ERROR processing dependency {name}: {e}", file=sys.stderr)
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
    "src/audio_engine/rtp_receiver.cpp", "src/audio_engine/raw_scream_receiver.cpp",
    "src/audio_engine/source_input_processor.cpp", "src/audio_engine/sink_audio_mixer.cpp",
    "src/audio_engine/audio_processor.cpp", "src/audio_engine/layout_mixer.cpp",
    "src/audio_engine/biquad/biquad.cpp", "src/configuration/audio_engine_config_applier.cpp",
]
main_extension_include_dirs = [
    str(PROJECT_ROOT / "src/audio_engine"), str(PROJECT_ROOT / "src/configuration"),
]
platform_extra_compile_args = []
platform_extra_link_args = []
main_extension_libraries = [DEPS_CONFIG["lame"]["link_name"], DEPS_CONFIG["samplerate"]["link_name"]]
main_extension_library_dirs = []

if sys.platform == "win32":
    print("Configuring main extension for Windows (MSVC)")
    platform_extra_compile_args.extend([
        "/std:c++17", "/O2", "/W3", "/EHsc",
        "/D_CRT_SECURE_NO_WARNINGS", "/MP",
        "/DLAMELIB_API=", # Define LAMELIB_API to be empty for static linking against LAME
        "/DCDECL="       # Define CDECL to be empty for static linking against LAME
    ])
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
    version="0.2.1", # Incremented version
    author="Cline",
    description="C++ audio engine for ScreamRouter (builds LAME & libsamplerate from submodules, auto MSVC env setup)",
    long_description=(PROJECT_ROOT / "README.md").read_text(encoding="utf-8") if (PROJECT_ROOT / "README.md").exists() else \
                     "Builds LAME & libsamplerate from submodules. Tries to auto-setup MSVC env on Windows.",
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
