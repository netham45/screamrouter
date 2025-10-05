"""
Minimal setup.py using the new modular build system
"""

import os
import sys
from pathlib import Path
from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext

# Ensure we can import from the project directory
project_root = Path(__file__).parent.resolve()
if str(project_root) not in sys.path:
    sys.path.insert(0, str(project_root))

# Import pybind11 first (it's installed via pip)
try:
    from pybind11.setup_helpers import Pybind11Extension
except ImportError:
    print("Error: pybind11 not found. Please install it using 'pip install pybind11>=2.6'", file=sys.stderr)
    sys.exit(1)


class BuildExtCommand(build_ext):
    """Custom build_ext that uses our modular build system"""
    
    def run(self):
        # Import BuildSystem here to avoid issues with pip's isolated build environment
        try:
            from build_system import BuildSystem
        except ImportError as e:
            print(f"Error importing build_system: {e}", file=sys.stderr)
            print("Make sure the build_system directory exists in the project root.", file=sys.stderr)
            raise
        
        # Initialize build system
        bs = BuildSystem(
            root_dir=Path.cwd(),
            verbose=self.verbose > 0
        )
        
        # Show build info
        if self.verbose:
            bs.show_info()
        
        # Check prerequisites
        print("Checking build prerequisites...")
        if not bs.check_prerequisites():
            raise RuntimeError(
                "Missing build prerequisites. Please install required packages.\n"
                "See the error messages above for details."
            )
        
        # Build all dependencies
        print("Building C++ dependencies...")
        if not bs.build_all():
            raise RuntimeError(
                "Failed to build one or more dependencies.\n"
                "See the error messages above for details."
            )
        
        # Get install directory
        install_dir = bs.install_dir
        
        # Update extension paths
        for ext in self.extensions:
            # Add include directories
            ext.include_dirs.insert(0, str(install_dir / "include"))
            
            # Add library directories
            ext.library_dirs.insert(0, str(install_dir / "lib"))
            ext.library_dirs.insert(0, str(install_dir / "lib64"))
            
            # Platform-specific adjustments
            if sys.platform == "win32":
                # Windows-specific flags
                ext.extra_compile_args.extend([
                    "/std:c++17", "/O2", "/W3", "/EHsc",
                    "/D_CRT_SECURE_NO_WARNINGS", "/MP"
                ])
            else:
                # Linux-specific flags
                ext.extra_compile_args.extend([
                    "-std=c++17", "-O2", "-Wall", "-fPIC"
                ])
                ext.extra_link_args.extend([
                    "-lpthread", "-ldl"
                ])
        
        # Run normal build
        super().run()
        
        # Generate pybind11 stubs
        if not self.dry_run:
            print("Generating pybind11 stubs...")
            try:
                import subprocess
                import os
                
                env = os.environ.copy()
                env["PYTHONPATH"] = self.build_lib + os.pathsep + env.get("PYTHONPATH", "")
                
                subprocess.run([
                    sys.executable, "-m", "pybind11_stubgen",
                    "screamrouter_audio_engine",
                    "--output-dir", ".",
                    "--no-setup-py-cmd"
                ], check=True, env=env)
                
                print("Successfully generated stubs for screamrouter_audio_engine.")
            except (subprocess.CalledProcessError, FileNotFoundError) as e:
                print(f"WARNING: Failed to generate pybind11 stubs: {e}", file=sys.stderr)


# Find source files
src_root = Path("src")
source_files = []
for root, dirs, files in os.walk(str(src_root)):
    # Exclude deps directories
    if 'deps' in dirs:
        dirs.remove('deps')
    for file in files:
        if file.endswith(".cpp"):
            source_files.append(str(Path(root) / file))

# Sort for consistent ordering
source_files.sort()

# Create extension
ext_modules = [
    Pybind11Extension(
        "screamrouter_audio_engine",
        sources=source_files,
        include_dirs=[
            "src/audio_engine",
            "src/audio_engine/json/include",  # If json headers are needed
        ],
        libraries=[
            # Core dependencies
            "mp3lame",
            "opus", 
            "samplerate",
            "datachannel",
            # OpenSSL
            "ssl",
            "crypto",
            # libdatachannel dependencies
            "juice",
            "usrsctp",
            "srtp2",
        ],
        language="c++",
        cxx_std=17
    )
]

# Read README for long description
readme_path = Path("README.md")
long_description = ""
if readme_path.exists():
    with open(readme_path, "r", encoding="utf-8") as f:
        long_description = f.read()

setup(
    name="screamrouter_audio_engine",
    version="0.3.0",
    author="Netham45",
    description="C++ audio engine for ScreamRouter with modular build system",
    long_description=long_description,
    long_description_content_type="text/markdown",
    ext_modules=ext_modules,
    cmdclass={"build_ext": BuildExtCommand},
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    zip_safe=False,
    python_requires=">=3.7",
    install_requires=[
        "pybind11>=2.6",
        "pybind11-stubgen",
    ],
    extras_require={
        "dev": [
            "pytest",
            "pytest-cov",
            "black",
            "flake8",
            "mypy",
        ]
    },
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: C++",
        "Operating System :: Microsoft :: Windows",
        "Operating System :: POSIX :: Linux",
        "Topic :: Multimedia :: Sound/Audio",
    ],
)