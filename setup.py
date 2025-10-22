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
    import pybind11
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
    - Delegates the screamrouter C++ extension build to CMake/Ninja for
      true multi-core compilation
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
        self.build_system = BuildSystem(
            root_dir=Path.cwd(),
            verbose=self.verbose > 0
        )

        # Show build info
        if self.verbose:
            self.build_system.show_info()

        # Check prerequisites
        print("Checking build prerequisites...")
        if not self.build_system.check_prerequisites():
            raise RuntimeError(
                "Missing build prerequisites. Please install required packages.\n"
                "See the error messages above for details."
            )

        # Build all dependencies
        print("Building C++ dependencies...")
        if not self.build_system.build_all():
            raise RuntimeError(
                "Failed to build one or more dependencies.\n"
                "See the error messages above for details."
            )

        # Capture paths used during CMake builds
        self.install_dir = self.build_system.install_dir
        self.cross_target = detect_cross_compile_platform()
        self.is_cross_compiling = bool(self.cross_target)

        # Prepare base environment for CMake invocations (ensure MSVC vars on Windows)
        self._env_base = os.environ.copy()
        if sys.platform == "win32":
            from build_system.platform import MSVCEnvironment

            detected_arch = getattr(self.build_system, "arch", "x64")
            normalized_arch = str(detected_arch).lower()
            msvc_arch = "x86" if normalized_arch in {"x86", "win32", "i386", "i686"} else "x64"

            msvc_env = MSVCEnvironment(arch=msvc_arch)
            if not msvc_env.vcvarsall_path:
                raise RuntimeError(
                    "Unable to locate vcvarsall.bat. Install the MSVC Build Tools "
                    "or run the build from a Visual Studio Developer Command Prompt."
                )

            activated_env = msvc_env.setup_environment()
            # Preserve any custom environment variables that vcvarsall doesn't set
            for key, value in os.environ.items():
                if key not in activated_env:
                    activated_env[key] = value

            print(f"Using MSVC environment from {msvc_env.vcvarsall_path}")
            self._env_base = activated_env

        self.cmake_executable = shutil.which("cmake")
        if not self.cmake_executable:
            raise RuntimeError("cmake not found in PATH. Please install CMake >= 3.14")

        self.pybind11_cmake_dir = Path(pybind11.get_cmake_dir())
        self._built_extensions = []

        # Run the standard build_ext flow which now delegates to CMake
        super().run()

        if not self.dry_run:
            self._generate_stubs()

    # --- Helpers -------------------------------------------------

    def build_extension(self, ext):
        """Build the pybind11 module via CMake instead of distutils."""
        if ext.name != "screamrouter_audio_engine":
            raise RuntimeError(f"Unsupported extension '{ext.name}'")

        cmake_source_dir = project_root / "src" / "audio_engine"
        build_dir = Path(self.build_temp) / ext.name
        build_dir.mkdir(parents=True, exist_ok=True)

        ext_fullpath = Path(self.get_ext_fullpath(ext.name)).resolve()
        output_dir = ext_fullpath.parent
        output_dir.mkdir(parents=True, exist_ok=True)

        build_type = "Debug" if self.debug else "Release"

        cmake_args = [
            self.cmake_executable,
            "-S", str(cmake_source_dir),
            "-B", str(build_dir),
            f"-DSCREAMROUTER_DEPS_PREFIX={self.install_dir}",
            f"-DSCREAMROUTER_MODULE_OUTPUT_DIR={output_dir}",
            f"-Dpybind11_DIR={self.pybind11_cmake_dir}",
        ]

        # Set build type for single-config generators (ignored by multi-config)
        cmake_args.append(f"-DCMAKE_BUILD_TYPE={build_type}")

        if self.is_cross_compiling and self.cross_target:
            system_part, _, arch_part = self.cross_target.partition("_")
            system_map = {
                "linux": "Linux",
                "darwin": "Darwin",
                "win": "Windows",
                "windows": "Windows",
            }
            if system_part:
                cmake_args.append(f"-DCMAKE_SYSTEM_NAME={system_map.get(system_part, system_part)}")
            if arch_part:
                cmake_args.append(f"-DCMAKE_SYSTEM_PROCESSOR={arch_part}")

        env = self._create_cmake_env()
        # Ensure function tracing can be toggled via env during pip builds
        if env.get("SCREAMROUTER_FNTRACE"):
            cmake_args.append("-DSR_ENABLE_FNTRACE=ON")
        # Help CMake locate dependencies and pybind11

        print("Configuring CMake project...")
        subprocess.run(cmake_args, cwd=str(project_root), check=True, env=env)

        build_cmd = [
            self.cmake_executable,
            "--build", str(build_dir),
        ]

        if sys.platform == "win32":
            build_cmd.extend(["--config", build_type])

        if self.parallel:
            build_cmd.extend(["--parallel", str(self.parallel)])

        print("Building screamrouter_audio_engine via CMake...")
        subprocess.run(build_cmd, cwd=str(project_root), check=True, env=env)

        expected_name = Path(self.get_ext_filename(ext.name)).name
        built_path = output_dir / expected_name

        if not built_path.exists():
            candidates = list(output_dir.glob(f"{ext.name}*"))
            if not candidates:
                raise RuntimeError(
                    f"Failed to locate built module for {ext.name} in {output_dir}"
                )
            shutil.copy2(candidates[0], built_path)

        if built_path != ext_fullpath:
            shutil.copy2(built_path, ext_fullpath)

        self._built_extensions.append(ext.name)

    def _generate_stubs(self) -> None:
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

    def _create_cmake_env(self):
        """Create an environment dict for running CMake builds."""
        base_env = getattr(self, "_env_base", None)
        if base_env is None:
            base_env = os.environ.copy()
        else:
            base_env = base_env.copy()

        prefix_components = [str(self.install_dir), str(self.pybind11_cmake_dir)]
        existing_prefix = base_env.get("CMAKE_PREFIX_PATH")
        if existing_prefix:
            prefix_components.append(existing_prefix)
        # Preserve order while removing duplicates / empty entries
        seen = set()
        merged_prefix = []
        for entry in prefix_components:
            if not entry:
                continue
            # Split existing prefix entries on os.pathsep to deduplicate accurately
            parts = entry.split(os.pathsep)
            for part in parts:
                normalized = part.strip()
                if not normalized or normalized in seen:
                    continue
                seen.add(normalized)
                merged_prefix.append(normalized)
        if merged_prefix:
            base_env["CMAKE_PREFIX_PATH"] = os.pathsep.join(merged_prefix)

        base_env.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", str(self.parallel))
        return base_env


# Create extension metadata (actual build handled by CMake)
ext_modules = [
    Extension("screamrouter_audio_engine", sources=[])
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
