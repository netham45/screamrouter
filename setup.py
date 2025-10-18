"""
Minimal setup.py using the new modular build system

Build Requirements:
- C++ compiler (gcc/clang on Linux, MSVC on Windows)
- CMake >= 3.14
- Node.js and npm (for building React frontend)
- Python development headers
- OpenSSL development libraries

Parallel Build Support:
- Automatically uses all CPU cores for compilation (configurable)
- Override with: pip install -e . --config-settings="--build-option=--parallel=N"
- Or set environment: export MAX_JOBS=N before building
- Linux: Install ccache for faster incremental rebuilds

Cross-Compilation Support:
- Set CC and CXX to your cross-compiler
- Set SCREAMROUTER_PLAT_NAME to override the wheel platform tag
- Example: CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ SCREAMROUTER_PLAT_NAME=linux_aarch64 python3 setup.py bdist_wheel
"""

import os
import sys
import shutil
import subprocess
import re
from pathlib import Path
from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext
from setuptools.command.build_py import build_py
from distutils.command.build import build as _build

# Conditionally import bdist_wheel (not always installed)
try:
    from setuptools.command.bdist_wheel import bdist_wheel
    HAVE_WHEEL = True
except ImportError:
    try:
        from wheel.bdist_wheel import bdist_wheel
        HAVE_WHEEL = True
    except ImportError:
        HAVE_WHEEL = False
        bdist_wheel = None

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


class BuildReactCommand(_build):
    """Custom command to build React frontend"""
    
    def run(self):
        react_dir = Path("screamrouter-react")
        site_dir = Path("site")
        
        if not react_dir.exists():
            print(f"WARNING: React directory {react_dir} not found, skipping React build", file=sys.stderr)
            return
        
        # Determine npm command based on platform
        # On Windows, npm is typically npm.cmd
        npm_cmd = 'npm.cmd' if sys.platform == 'win32' else 'npm'
        
        # Check if npm exists
        if not shutil.which(npm_cmd):
            raise RuntimeError(
                f"{npm_cmd} not found. Please install Node.js and npm to build the React frontend.\n"
                "Visit https://nodejs.org/ for installation instructions."
            )
        
        print("Building React frontend...")
        
        # Run npm install
        print("Running npm install...")
        try:
            subprocess.run(
                [npm_cmd, "install"],
                cwd=str(react_dir),
                check=True,
                capture_output=False
            )
        except subprocess.CalledProcessError as e:
            raise RuntimeError(f"npm install failed: {e}")
        except FileNotFoundError:
            raise RuntimeError(
                f"{npm_cmd} not found. Please install Node.js and npm to build the React frontend.\n"
                "Visit https://nodejs.org/ for installation instructions."
            )
        
        # Run npm run build
        print("Running npm run build...")
        try:
            subprocess.run(
                [npm_cmd, "run", "build"],
                cwd=str(react_dir),
                check=True,
                capture_output=False
            )
        except subprocess.CalledProcessError as e:
            raise RuntimeError(f"npm run build failed: {e}")
        
        # Webpack is configured to output directly to ../site directory
        # Copy site directory into screamrouter package for proper packaging
        package_site_dir = Path("screamrouter/site")
        
        if not site_dir.exists():
            raise RuntimeError(f"React build output not found at {site_dir}")
        
        # Copy site directory to screamrouter package directory
        if package_site_dir.exists():
            shutil.rmtree(package_site_dir)
        shutil.copytree(site_dir, package_site_dir)
        
        print(f"React build completed successfully. Files copied to {package_site_dir}")


def detect_cross_compile_platform():
    """
    Detect cross-compilation target from CC environment variable.
    Returns platform tag like 'linux_aarch64' or None if native build.
    """
    cc = os.environ.get('CC', '')
    
    # Check if SCREAMROUTER_PLAT_NAME is explicitly set
    explicit_plat = os.environ.get('SCREAMROUTER_PLAT_NAME')
    if explicit_plat:
        print(f"Using explicit platform tag from SCREAMROUTER_PLAT_NAME: {explicit_plat}")
        return explicit_plat
    
    # Try to detect from CC
    if not cc:
        return None
    
    # Extract target triplet from compiler name
    # Examples: aarch64-linux-gnu-gcc, arm-linux-gnueabihf-gcc, x86_64-w64-mingw32-gcc
    match = re.match(r'^(?:ccache\s+)?([^-\s]+)-([^-]+)-([^-]+)(?:-([^-]+))?-(?:gcc|g\+\+|clang)', cc)
    if not match:
        return None
    
    arch = match.group(1)
    os_name = match.group(3)
    
    # Map architecture names to Python wheel tags
    arch_map = {
        'aarch64': 'aarch64',
        'arm': 'armv7l',
        'armv7': 'armv7l',
        'x86_64': 'x86_64',
        'i686': 'i686',
        'i386': 'i686',
        'mips': 'mips',
        'mipsel': 'mipsel',
        'powerpc': 'ppc',
        'powerpc64': 'ppc64',
        'powerpc64le': 'ppc64le',
        's390x': 's390x',
    }
    
    # Map OS names
    os_map = {
        'linux': 'linux',
        'darwin': 'darwin',
        'mingw32': 'win',
    }
    
    wheel_arch = arch_map.get(arch, arch)
    wheel_os = os_map.get(os_name, os_name)
    
    platform_tag = f"{wheel_os}_{wheel_arch}"
    print(f"Detected cross-compilation target from CC={cc}: {platform_tag}")
    return platform_tag


class BuildPyCommand(build_py):
    """Custom build_py that builds React frontend before copying Python files"""
    
    def run(self):
        # Build React frontend FIRST, before copying Python files
        self.run_command('build_react')
        # Now run the normal build_py
        super().run()


if HAVE_WHEEL:
    class BdistWheelCommand(bdist_wheel):
        """Custom bdist_wheel that supports cross-compilation platform tags"""
        
        def finalize_options(self):
            super().finalize_options()
            
            # Override platform name if cross-compiling
            cross_plat = detect_cross_compile_platform()
            if cross_plat:
                self.plat_name = cross_plat
                self.plat_name_supplied = True
                print(f"Building wheel for platform: {cross_plat}")
else:
    BdistWheelCommand = None


class BuildExtCommand(build_ext):
    """
    Custom build_ext that uses our modular build system.

    Features:
    - Builds C++ dependencies (OpenSSL, Opus, libdatachannel, etc.)
    - Compiles screamrouter C++ extension in parallel (uses all CPU cores)
    - Supports ccache for faster incremental builds on Linux
    - Generates pybind11 type stubs automatically
    """

    def run(self):
        # Enable parallel compilation for faster builds
        # Use all available CPU cores by default
        if self.parallel is None:
            self.parallel = os.cpu_count() or 1

        print(f"Building with {self.parallel} parallel jobs...")

        # Enable ccache for faster incremental builds on Linux
        # Preserve existing CC/CXX for cross-compilation
        if sys.platform != "win32" and shutil.which("ccache"):
            current_cc = os.environ.get("CC", "gcc")
            current_cxx = os.environ.get("CXX", "g++")
            # Only prepend ccache if not already there
            if not current_cc.startswith("ccache"):
                os.environ["CC"] = "ccache " + current_cc
            if not current_cxx.startswith("ccache"):
                os.environ["CXX"] = "ccache " + current_cxx
            print("Using ccache for faster incremental builds")

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
        
        # Detect cross-compilation
        is_cross_compiling = bool(detect_cross_compile_platform())
        
        # Update extension paths
        for ext in self.extensions:
            # Add include directories
            ext.include_dirs.insert(0, str(install_dir / "include"))
            
            # When cross-compiling, ONLY use our built libraries
            # Clear any system library paths that distutils may have added
            if is_cross_compiling:
                ext.library_dirs.clear()
                ext.runtime_library_dirs = []
            
            # Add library directories
            # On Windows, use only 'lib' directory (32-bit and 64-bit both use lib)
            # On Linux, check both lib and lib64
            if sys.platform == "win32":
                ext.library_dirs.insert(0, str(install_dir / "lib"))
            else:
                ext.library_dirs.insert(0, str(install_dir / "lib"))
                lib64_path = install_dir / "lib64"
                if lib64_path.exists():
                    ext.library_dirs.insert(0, str(lib64_path))
            
            # Platform-specific adjustments
            if sys.platform == "win32":
                # Windows-specific flags
                ext.extra_compile_args.extend([
                    "/std:c++17", "/O2", "/W3", "/EHsc",
                    "/D_CRT_SECURE_NO_WARNINGS", "/MP",
                    # Define static linking for libdatachannel
                    "/DRTC_STATIC"
                ])
                # Add Windows system libraries required by OpenSSL and libjuice
                ext.libraries.extend([
                    "advapi32",  # Event logging, registry, crypto
                    "crypt32",   # Certificate store
                    "user32",    # MessageBox, window station
                    "bcrypt",    # BCryptGenRandom
                    "ws2_32",    # Winsock (network)
                    "iphlpapi",  # IP Helper (network)
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
                env = os.environ.copy()
                env["PYTHONPATH"] = self.build_lib + os.pathsep + env.get("PYTHONPATH", "")
                
                subprocess.run([
                    sys.executable, "-m", "pybind11_stubgen",
                    "screamrouter_audio_engine",
                    "--output-dir", "."
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
            # OpenSSL (libssl/libcrypto on Windows, ssl/crypto on Linux)
            "libssl" if sys.platform == "win32" else "ssl",
            "libcrypto" if sys.platform == "win32" else "crypto",
            # libdatachannel dependencies
            "juice",
            "usrsctp",
            "srtp2",
        ] + (["asound"] if sys.platform.startswith("linux") else []),
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
    name="screamrouter",
    version="0.3.0",
    author="Netham45",
    description="ScreamRouter audio routing system with web interface and C++ audio engine",
    long_description=long_description,
    long_description_content_type="text/markdown",
    ext_modules=ext_modules,
    cmdclass={
        "build_react": BuildReactCommand,
        "build_py": BuildPyCommand,
        "build_ext": BuildExtCommand,
        **({} if not HAVE_WHEEL else {"bdist_wheel": BdistWheelCommand}),
    },
    packages=find_packages(include=["screamrouter", "screamrouter.*"]),
    package_data={
        "screamrouter": [
            "site/**/*",
            "uvicorn_log_config.yaml",
        ]
    },
    include_package_data=True,
    entry_points={
        "console_scripts": [
            "screamrouter=screamrouter.__main__:main",
        ],
    },
    zip_safe=False,
    python_requires=">=3.10",
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
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "Programming Language :: Python :: 3.14",
        "Programming Language :: C++",
        "Operating System :: Microsoft :: Windows",
        "Operating System :: POSIX :: Linux",
        "Topic :: Multimedia :: Sound/Audio",
    ],
)
