"""
setup script for screamrouter audio engine (cross-platform)
Attempts to download and build LAME on Windows.
libsamplerate on Windows is expected to be pre-installed (e.g., via vcpkg).
pthreads-win32 is NOT included as the project uses std::thread.
"""
import sys
import os
import subprocess
import urllib.request
import tarfile
import zipfile
import shutil
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
DEPS_BUILD_DIR = PROJECT_ROOT / "build" / "deps"
DEPS_SRC_DIR = PROJECT_ROOT / "build" / "temp_deps_src"

# --- Dependency Definitions ---
# (Primarily for Windows auto-build)
DEPS_CONFIG = {
    "lame": {
        "url": "https://sourceforge.net/projects/lame/files/lame/3.100/lame-3.100.tar.gz/download",
        "filename": "lame-3.100.tar.gz",
        "src_subdir": "lame-3.100",
        "build_commands": [ # For Windows MSVC using nmake
            ["nmake", "/f", "Makefile.vc", "dll"], # Adjust if you need static lib
        ],
        "headers_subdir": "include/lame", # Relative to src_subdir after build or in source
        "lib_files": ["libmp3lame/lame.lib"], # Relative to src_subdir, specific to MSVC build
                                             # Path might be ReleaseDLL/x64/libmp3lame.lib or similar
        "output_lib_name": "lame.lib", # What the main extension expects
        "output_header_dir_name": "lame", # Subdirectory in DEPS_BUILD_DIR/include
        "win_only": True,
    },
    # pthreads-win32 removed as project uses std::thread
    # libsamplerate is more complex to build from source on Windows without CMake/autotools chain.
    # Advise manual installation via vcpkg or similar.
}

class BuildExtCommand(_build_ext):
    """Custom build_ext command to handle C++ dependencies."""

    def run(self):
        # Create directories for dependencies
        DEPS_BUILD_DIR.mkdir(parents=True, exist_ok=True)
        (DEPS_BUILD_DIR / "include").mkdir(exist_ok=True)
        (DEPS_BUILD_DIR / "lib").mkdir(exist_ok=True)
        DEPS_SRC_DIR.mkdir(parents=True, exist_ok=True)

        built_something = False

        if sys.platform == "win32":
            print("Attempting to build dependencies for Windows...")
            for name, config in DEPS_CONFIG.items():
                if not config.get("win_only", False):
                    continue

                print(f"--- Handling dependency: {name} ---")
                if "note" in config: # Should not be relevant for LAME
                    print(f"Note for {name}: {config['note']}")

                final_lib_path = DEPS_BUILD_DIR / "lib" / config["output_lib_name"]
                # Crude check if already built
                if final_lib_path.exists():
                    print(f"{name} library already found at {final_lib_path}. Skipping build.")
                    # Ensure this still contributes to adding dirs to extension if it exists
                    built_something = True # Mark as "built" so dirs are added
                    continue

                archive_path = DEPS_SRC_DIR / config["filename"]
                # This is the directory name *inside* the archive, where build commands run.
                extracted_build_root_dir = DEPS_SRC_DIR / config["src_subdir"]
                
                # 1. Download
                if not archive_path.exists():
                    print(f"Downloading {name} from {config['url']}...")
                    try:
                        with urllib.request.urlopen(config["url"]) as response, open(archive_path, 'wb') as out_file:
                            shutil.copyfileobj(response, out_file)
                        print(f"Downloaded {archive_path}")
                    except Exception as e:
                        print(f"Error downloading {name}: {e}", file=sys.stderr)
                        print("Please ensure it's installed manually and headers/libs are findable.")
                        continue
                else:
                    print(f"Archive {archive_path} already exists.")

                # 2. Extract
                # Check if the specific subdirectory for building already exists
                if not extracted_build_root_dir.exists():
                    print(f"Extracting {archive_path}...")
                    try:
                        if archive_path.name.endswith(".tar.gz"):
                            with tarfile.open(archive_path, "r:gz") as tar:
                                tar.extractall(path=DEPS_SRC_DIR)
                        elif archive_path.name.endswith(".zip"):
                            with zipfile.ZipFile(archive_path, "r") as zip_ref:
                                zip_ref.extractall(path=DEPS_SRC_DIR)
                        print(f"Extracted to {DEPS_SRC_DIR}")
                    except Exception as e:
                        print(f"Error extracting {name}: {e}", file=sys.stderr)
                        # Clean up potentially partial extraction of the target build dir
                        if extracted_build_root_dir.exists():
                             shutil.rmtree(extracted_build_root_dir, ignore_errors=True)
                        continue
                else:
                    print(f"Source directory {extracted_build_root_dir} already exists.")

                # 3. Build
                print(f"Building {name} in {extracted_build_root_dir}...")
                build_successful = False
                try:
                    for cmd in config["build_commands"]:
                        print(f"Running command: {' '.join(cmd)}")
                        subprocess.run(cmd, cwd=extracted_build_root_dir, check=True, shell=False)
                    build_successful = True
                    print(f"{name} built successfully.")
                    built_something = True
                except (subprocess.CalledProcessError, FileNotFoundError) as e:
                    print(f"Error building {name}: {e}", file=sys.stderr)
                    print(f"Ensure build tools (like nmake for MSVC) are in PATH and MSVC environment is set up.")
                    print(f"If build fails, please install {name} manually and ensure headers/libs are findable.")
                    continue

                # 4. Copy headers and libs
                if build_successful:
                    try:
                        # Copy library files
                        # The lib_files path is relative to extracted_build_root_dir
                        # LAME's nmake build might place output in subdirectories like ReleaseDLL/x64
                        # The glob needs to search within extracted_build_root_dir
                        lib_copied_successfully = False
                        for lib_file_pattern_in_src_subdir in config["lib_files"]:
                            # Search for the lib file, as its exact path might vary (e.g. ReleaseDLL/x64/lame.lib)
                            # The pattern in config["lib_files"] should be relative to extracted_build_root_dir
                            # Example: "libmp3lame/ReleaseDLL/x64/lame.lib" or "libmp3lame/lame.lib"
                            # For LAME, the Makefile.vc often creates a 'libmp3lame' subdir with the .lib
                            # Let's assume config["lib_files"] is like ["libmp3lame/lame.lib"] and Makefile.vc puts it there
                            # Or it could be more complex like ["ReleaseDLL/x64/libmp3lame.lib"]
                            
                            # Correct globbing within the extracted source directory
                            found_libs = list(extracted_build_root_dir.glob(f"**/{lib_file_pattern_in_src_subdir.split('/')[-1]}"))
                            
                            # A more direct approach if lib_files is specific enough:
                            # src_lib_path = extracted_build_root_dir / lib_file_pattern_in_src_subdir
                            # if src_lib_path.exists():
                            #    found_libs = [src_lib_path]
                            # else:
                            #    found_libs = [] # Fallback to glob if direct path fails

                            if not found_libs:
                                print(f"Warning: Library file pattern '{lib_file_pattern_in_src_subdir}' not found directly or via glob in {extracted_build_root_dir} for {name}", file=sys.stderr)
                                print(f"Please check the LAME Makefile.vc output structure and adjust 'lib_files' in DEPS_CONFIG.", file=sys.stderr)
                                continue
                            
                            src_lib_path = found_libs[0] # Take the first match from glob
                            dest_lib_path = DEPS_BUILD_DIR / "lib" / config["output_lib_name"]
                            shutil.copy2(src_lib_path, dest_lib_path)
                            print(f"Copied {src_lib_path} to {dest_lib_path}")
                            lib_copied_successfully = True
                            break # Copied one, that's enough for this lib name

                        if not lib_copied_successfully:
                             print(f"ERROR: Could not copy library for {name}. Please check build output and DEPS_CONFIG.", file=sys.stderr)
                             built_something = False # Mark as not successfully "built" for our purposes
                             continue


                        # Copy headers
                        # headers_subdir is relative to extracted_build_root_dir
                        header_src_actual_dir = extracted_build_root_dir / config["headers_subdir"]
                        dest_header_dir = DEPS_BUILD_DIR / "include" / config["output_header_dir_name"]
                        
                        if not header_src_actual_dir.exists():
                            print(f"Warning: Header source directory {header_src_actual_dir} not found for {name}.", file=sys.stderr)
                        else:
                            if dest_header_dir.exists():
                                shutil.rmtree(dest_header_dir) # Clean up old headers
                            shutil.copytree(header_src_actual_dir, dest_header_dir, dirs_exist_ok=True)
                            print(f"Copied headers from {header_src_actual_dir} to {dest_header_dir}")

                    except Exception as e:
                        print(f"Error copying files for {name}: {e}", file=sys.stderr)
                        built_something = False # Mark as not successfully "built"
            
            if built_something: # If any dependency was successfully built/found and copied
                 # Add the deps directory to the extension's include and library dirs
                for ext in self.extensions:
                    # Prepend to give priority, or append if that's preferred.
                    # Ensure these are not added multiple times if build_ext runs more than once.
                    if str(DEPS_BUILD_DIR / "include") not in ext.include_dirs:
                        ext.include_dirs.insert(0, str(DEPS_BUILD_DIR / "include"))
                    if str(DEPS_BUILD_DIR / "lib") not in ext.library_dirs:
                        ext.library_dirs.insert(0, str(DEPS_BUILD_DIR / "lib"))
                print(f"Ensured {DEPS_BUILD_DIR / 'include'} is in include_dirs")
                print(f"Ensured {DEPS_BUILD_DIR / 'lib'} is in library_dirs")

        super().run()


# Common source files for the C++ extension
source_files = [
    "src/audio_engine/bindings.cpp",
    "src/audio_engine/audio_manager.cpp",
    "src/audio_engine/rtp_receiver.cpp",
    "src/audio_engine/raw_scream_receiver.cpp",
    "src/audio_engine/source_input_processor.cpp",
    "src/audio_engine/sink_audio_mixer.cpp",
    "src/audio_engine/audio_processor.cpp",
    "src/audio_engine/layout_mixer.cpp",
    "src/audio_engine/biquad/biquad.cpp",
    "src/configuration/audio_engine_config_applier.cpp",
]

# Common include directories (relative to the project root)
common_include_dirs = [
    str(PROJECT_ROOT / "src/audio_engine"),
    str(PROJECT_ROOT / "src/configuration"),
]

# --- Platform-specific configurations ---
platform_extra_compile_args = []
platform_extra_link_args = []
platform_libraries = []
# These will be populated by BuildExtCommand with DEPS_BUILD_DIR if deps are built
platform_library_dirs = []
platform_include_dirs = []

if sys.platform == "win32":
    print("Configuring for Windows (MSVC)")
    platform_extra_compile_args = [
        "/std:c++17", "/O2", "/W3", "/EHsc", "/D_CRT_SECURE_NO_WARNINGS",
    ]
    platform_libraries = [
        "lame",        # Will try to use lame.lib (built or pre-existing from DEPS_BUILD_DIR)
        "samplerate",  # Assumes samplerate.lib is available (e.g., via vcpkg or manual path)
        "ws2_32",      # Windows Sockets API, often needed for networking
    ]
    print("Windows specific: For libsamplerate, please ensure it's installed (e.g., via vcpkg) and its .lib/.h files are discoverable by the linker/compiler.")
    print("If LAME build fails, install it manually and ensure .lib/.h files are discoverable.")
    # Example manual paths for libsamplerate if not found automatically:
    # platform_include_dirs.append("C:/path/to/manual/samplerate/include")
    # platform_library_dirs.append("C:/path/to/manual/samplerate/lib")

elif sys.platform == "darwin":
    print("Configuring for macOS (Clang)")
    platform_extra_compile_args = [
        "-std=c++17", "-O2", "-Wall", "-fPIC",
        "-stdlib=libc++", "-mmacosx-version-min=10.14", # Adjust min macOS version as needed
    ]
    platform_libraries = ["mp3lame", "samplerate"] # No pthread needed here either if std::thread is used
    platform_library_dirs = ["/usr/local/lib", "/opt/homebrew/lib"] # Common Homebrew paths
    platform_include_dirs = ["/usr/local/include", "/opt/homebrew/include"]

else:  # Assuming Linux or other Unix-like (GCC/Clang)
    print(f"Configuring for Linux/Unix-like system ({sys.platform})")
    platform_extra_compile_args = ["-std=c++17", "-O2", "-Wall", "-fPIC"]
    platform_libraries = ["mp3lame", "samplerate"] # No pthread needed
    platform_library_dirs = ["/usr/lib64", "/usr/lib", "/usr/local/lib"]


# Define the C++ extension module
ext_modules = [
    Pybind11Extension(
        "screamrouter_audio_engine",
        sources=sorted([str(PROJECT_ROOT / f) for f in source_files]),
        # common_include_dirs will be supplemented by DEPS_BUILD_DIR/include via BuildExtCommand
        include_dirs=common_include_dirs + platform_include_dirs,
        # common_library_dirs will be supplemented by DEPS_BUILD_DIR/lib via BuildExtCommand
        library_dirs=platform_library_dirs,
        libraries=platform_libraries,
        extra_compile_args=platform_extra_compile_args,
        extra_link_args=platform_extra_link_args,
        language='c++',
        cxx_std=17 # pybind11 uses this to set the C++ standard
    ),
]

# Setup script configuration
setup(
    name="screamrouter_audio_engine",
    version="0.1.3", # Incremented version
    author="Cline",
    description="C++ audio engine extension for ScreamRouter (cross-platform, LAME build for Win)",
    long_description=(PROJECT_ROOT / "README.md").read_text() if (PROJECT_ROOT / "README.md").exists() else \
                     "Provides core audio processing (RTP, mixing, effects) as a C++ extension. "
                     "This version attempts to build LAME on Windows and expects libsamplerate to be pre-installed. Uses std::thread.",
    long_description_content_type="text/markdown",
    ext_modules=ext_modules,
    cmdclass={"build_ext": BuildExtCommand}, # Use our custom build_ext
    zip_safe=False,
    python_requires=">=3.7",
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "Programming Language :: Python :: 3",
        "Programming Language :: C++",
        "Operating System :: Microsoft :: Windows",
        "Operating System :: MacOS :: MacOS X",
        "Operating System :: POSIX :: Linux",
        "Topic :: Multimedia :: Sound/Audio",
    ],
)
