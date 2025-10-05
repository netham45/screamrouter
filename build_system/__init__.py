"""
ScreamRouter Build System
A modular, maintainable build system for C++ dependencies
Supports Linux (Debian/RHEL) and Windows
"""

__version__ = "1.0.0"
__supported_platforms__ = ["linux", "windows"]

from .main import BuildSystem

__all__ = ["BuildSystem", "__version__", "__supported_platforms__"]