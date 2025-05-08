"""setup script for screamrouter audio engine"""
import sys
from setuptools import setup

try:
    from pybind11.setup_helpers import Pybind11Extension, build_ext
except ImportError:
    print("Error: pybind11 not found. Please install it using 'pip install pybind11>=2.6'",
          file=sys.stderr)
    sys.exit(1)

ext_modules = [
    Pybind11Extension(
        "screamrouter_audio_engine",
        [
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
        ],
        include_dirs=[
            "src/audio_engine",
            "src/configuration",
        ],
        library_dirs=[
            "/usr/lib64",
        ],
        libraries=[
            "mp3lame",
            "samplerate",
            "pthread",
        ],
        extra_compile_args=[
            "-std=c++17",
            "-O2",
            "-Wall",
            "-fPIC",
        ],
        extra_link_args=[],
        language='c++',
    ),
]

setup(
    name="screamrouter_audio_engine",
    version="0.1.0",
    author="Cline",
    description="C++ audio engine extension for ScreamRouter",
    long_description="Provides core audio processing (RTP, mixing, effects) as a C++ extension.",
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    zip_safe=False,
    python_requires=">=3.7",
)
