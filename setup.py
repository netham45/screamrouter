"""Setup script for cpython for src.audio module"""
from setuptools import setup
from Cython.Build import cythonize
from distutils.extension import Extension

packages = [Extension("src.audio.audio_controller",["src/audio/audio_controller.py"]),
            Extension("src.audio.mp3_header_parser",["src/audio/mp3_header_parser.py"]),
            Extension("src.audio.rtp_recevier",["src/audio/rtp_recevier.py"]),
            Extension("src.audio.scream_header_parser",["src/audio/scream_header_parser.py"]),
            Extension("src.audio.scream_receiver",["src/audio/scream_receiver.py"]),
            Extension("src.audio.sink_mp3_processor",["src/audio/sink_mp3_processor.py"]),
            Extension("src.audio.sink_output_mixer",["src/audio/sink_output_mixer.py"]),
            Extension("src.audio.source_input_processor",["src/audio/source_input_processor.py"]),
            Extension("src.audio.tcp_manager",["src/audio/tcp_manager.py"])]
print(packages)

setup(
    name='ScreamRouter',
    ext_modules=cythonize(packages),
    packages=['src.audio']
)
