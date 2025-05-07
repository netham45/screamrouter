import sys
import pybind11 # Import pybind11
from setuptools import setup

try:
    from pybind11.setup_helpers import Pybind11Extension, build_ext
except ImportError:
    print("Error: pybind11 not found. Please install it using 'pip install pybind11>=2.6'", file=sys.stderr)
    sys.exit(1)

# Define the C++ extension module
ext_modules = [
    Pybind11Extension(
        "screamrouter_audio_engine", # Module name as imported in Python
        [
            # List of source files relative to setup.py
            "src/audio_engine/bindings.cpp",
            "src/audio_engine/audio_manager.cpp",
            "src/audio_engine/rtp_receiver.cpp",
            "src/audio_engine/source_input_processor.cpp",
            "src/audio_engine/sink_audio_mixer.cpp",
            "c_utils/audio_processor.cpp",
            "c_utils/layout_mixer.cpp", # Added missing source
            "c_utils/speaker_mix.cpp",  # Added missing source
            "c_utils/biquad/biquad.cpp",
            "src/configuration/audio_engine_config_applier.cpp", # Added Config Applier source
            # Note: libsamplerate sources are NOT included here.
            # We rely on linking against a pre-built library (-lsamplerate).
        ],
        include_dirs=[
            # Directories containing header files needed by the sources
            "src/audio_engine",
            "src/configuration", # Added Config Applier include dir
            "c_utils",
            "c_utils/biquad",
            "c_utils/libsamplerate/include"
        ],
        library_dirs=[
            # Add paths to libraries if they are not in standard locations
            "/usr/lib64", # Explicitly add common 64-bit library path for EL systems
            # e.g., "/usr/local/lib", "/path/to/libsamplerate/libs"
        ],
        libraries=[
            # Libraries to link against
            "mp3lame",     # Correct library name for LAME MP3 encoder
            "samplerate",  # For resampling in AudioProcessor
            "pthread",     # For std::thread used in components
        ],
        extra_compile_args=[
            # Compiler flags
            "-std=c++17",  # Use C++17 standard
            "-O2",         # Optimization level
            "-Wall",       # Enable common warnings
            "-Wextra",     # Enable extra warnings
            "-fPIC",       # Position Independent Code (often default but good to be explicit)
            # Add defines if necessary, e.g. '-DSOME_MACRO'
            # Enable SIMD flags if desired and target architecture supports them:
            # "-msse2",
            # "-mavx2",
        ],
        extra_link_args=[
            # Linker flags (if any)
        ],
        language='c++', # Specify language
    ),
]

# Use setuptools to build the extension
setup(
    name="screamrouter_audio_engine",
    version="0.1.0", # Initial version
    author="Cline", # Or your name/team
    description="C++ audio engine extension for ScreamRouter",
    long_description="Provides core audio processing (RTP, mixing, effects) as a C++ extension.",
    ext_modules=ext_modules,
    # Use the build_ext command provided by pybind11
    cmdclass={"build_ext": build_ext},
    # Prevent installation as a zip archive (needed for C extensions)
    zip_safe=False,
    # Specify minimum Python version compatibility
    python_requires=">=3.7",
    # Add other package info as needed (e.g., packages, install_requires for Python deps)
    # install_requires=[ 'pybind11>=2.6' ], # Not strictly needed here if installed via requirements.txt
)
