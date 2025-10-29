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
- Or set environment: export SCREAMROUTER_MAX_JOBS=N (falls back to MAX_JOBS or CPU count)
- Linux: Install ccache for faster incremental rebuilds

Extension Compilation Cache:
- Incremental C++ builds reuse unchanged object files
- Disable with SCREAMROUTER_DISABLE_OBJECT_CACHE=1
- Cache stored under build/.cache/objects

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
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext
from setuptools.command.build_py import build_py
from distutils.command.build import build as _build
from distutils import log
from distutils.dep_util import newer_group
from distutils.errors import DistutilsSetupError

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

    def initialize_options(self):
        super().initialize_options()
        self.object_cache = None
        self._cache_stats = {"reused": 0, "compiled": 0}

    def run(self):
        env_jobs = None
        if self.parallel is None:
            env_value = os.environ.get("SCREAMROUTER_MAX_JOBS") or os.environ.get("MAX_JOBS")
            if env_value:
                try:
                    env_jobs_int = int(env_value)
                    if env_jobs_int > 0:
                        env_jobs = env_jobs_int
                except ValueError:
                    print(f"WARNING: Ignoring invalid parallel job count '{env_value}'", file=sys.stderr)
            if env_jobs:
                self.parallel = env_jobs
            else:
                self.parallel = os.cpu_count() or 1
        else:
            env_value = os.environ.get("SCREAMROUTER_MAX_JOBS") or os.environ.get("MAX_JOBS")
            if env_value:
                try:
                    env_jobs_int = int(env_value)
                    if env_jobs_int > 0 and env_jobs_int != self.parallel:
                        print(
                            f"Ignoring SCREAMROUTER_MAX_JOBS={env_jobs_int} because --parallel={self.parallel} is set."
                        )
                except ValueError:
                    pass

        if self.parallel < 1:
            self.parallel = 1

        self._cache_stats = {"reused": 0, "compiled": 0}
        print(f"Building with {self.parallel} parallel job{'s' if self.parallel != 1 else ''}...")

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

        self.object_cache = getattr(bs, "object_cache", None)
        if self.object_cache and self.object_cache.enabled:
            print(f"Object cache directory: {self.object_cache.cache_dir}")
        elif self.object_cache:
            print("Object cache disabled via SCREAMROUTER_DISABLE_OBJECT_CACHE")

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
                "Failed to build one or more dependencies. Please inspect the log output above."
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

        if self.object_cache and self.object_cache.enabled:
            reused = self._cache_stats.get("reused", 0)
            compiled = self._cache_stats.get("compiled", 0)
            if reused:
                print(f"Object cache reused {reused} object file{'s' if reused != 1 else ''}")
            if compiled:
                print(f"Compiled {compiled} object file{'s' if compiled != 1 else ''}")

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

    def build_extension(self, ext):
        sources = ext.sources
        if sources is None or not isinstance(sources, (list, tuple)):
            raise DistutilsSetupError(
                "in 'ext_modules' option (extension '%s'), "
                "'sources' must be present and must be "
                "a list of source filenames" % ext.name
            )
        sources = sorted(sources)

        ext_path = self.get_ext_fullpath(ext.name)
        depends = sources + list(ext.depends)
        if not (self.force or newer_group(depends, ext_path, 'newer')):
            log.debug("skipping '%s' extension (up-to-date)", ext.name)
            return
        else:
            log.info("building '%s' extension", ext.name)

        sources = self.swig_sources(sources, ext)

        macros = ext.define_macros[:]
        for undef in ext.undef_macros:
            macros.append((undef,))

        extra_args = ext.extra_compile_args or []

        objects = self._compile_with_cache(ext, sources, macros, extra_args)
        self._built_objects = objects[:]

        if ext.extra_objects:
            objects.extend(ext.extra_objects)
        extra_link_args = ext.extra_link_args or []

        language = ext.language or self.compiler.detect_language(sources)

        self.compiler.link_shared_object(
            objects,
            ext_path,
            libraries=self.get_libraries(ext),
            library_dirs=ext.library_dirs,
            runtime_library_dirs=ext.runtime_library_dirs,
            extra_postargs=extra_link_args,
            export_symbols=self.get_export_symbols(ext),
            debug=self.debug,
            build_temp=self.build_temp,
            target_lang=language,
        )

    def _compile_with_cache(self, ext, sources, macros, extra_postargs):
        compiler = self.compiler
        include_dirs = ext.include_dirs
        depends = ext.depends
        output_dir = self.build_temp

        macros_for_setup = list(macros or [])
        macros_for_compile = list(macros or [])
        extra_postargs_input = list(extra_postargs or [])

        macros_cfg, objects, extra_postargs_final, pp_opts, build_map = compiler._setup_compile(
            output_dir, macros_for_setup, include_dirs, sources, depends, extra_postargs_input
        )

        object_cache = self.object_cache if getattr(self.object_cache, "enabled", False) else None
        reused_count = 0
        compiled_count = 0

        compiler_descriptor = [
            getattr(compiler, "compiler_type", "unknown"),
            compiler.__class__.__name__,
        ]
        extra_key = [
            repr(getattr(compiler, "executables", {})),
            os.environ.get("CFLAGS", ""),
            os.environ.get("CXXFLAGS", ""),
            os.environ.get("LDFLAGS", ""),
        ]
        macros_key = self._serialise_macros(macros_cfg)
        include_key = sorted(include_dirs or [])
        postargs_key = list(extra_postargs_final or [])
        pp_opts_key = list(pp_opts)
        debug_flag = "debug=1" if self.debug else "debug=0"

        compile_jobs = []
        for obj_path in objects:
            src, src_ext = build_map[obj_path]
            obj_file = Path(obj_path)
            obj_file.parent.mkdir(parents=True, exist_ok=True)

            fingerprint = None
            if object_cache:
                compile_args = (
                    pp_opts_key
                    + postargs_key
                    + macros_key
                    + include_key
                    + [debug_flag, src_ext]
                )
                fingerprint = object_cache.fingerprint(
                    Path(src), compiler_descriptor, compile_args, extra_key=extra_key
                )
                cached_obj = object_cache.get_cached_object(fingerprint)
                if cached_obj:
                    shutil.copy2(cached_obj, obj_file)
                    reused_count += 1
                    continue

            compile_jobs.append((obj_path, src, src_ext, fingerprint))

        if compile_jobs:
            extra_postargs_list = list(extra_postargs_final or [])
            if compiler.compiler_type == "msvc":
                compiled_paths = compiler.compile(
                    [src for _, src, _, _ in compile_jobs],
                    output_dir=output_dir,
                    macros=macros_for_compile,
                    include_dirs=include_dirs,
                    debug=self.debug,
                    extra_postargs=extra_postargs_list,
                    depends=depends,
                )
                for (dest, _, _, fingerprint), produced in zip(compile_jobs, compiled_paths):
                    produced_path = Path(produced)
                    dest_path = Path(dest)
                    if produced_path.resolve() != dest_path.resolve():
                        shutil.copy2(produced_path, dest_path)
                    if object_cache and fingerprint:
                        object_cache.store_object(fingerprint, dest_path)
                    compiled_count += 1
            else:
                cc_args = compiler._get_cc_args(pp_opts, self.debug, None)
                max_workers = max(1, min(self.parallel or 1, len(compile_jobs)))
                if max_workers > 1:
                    with ThreadPoolExecutor(max_workers=max_workers) as executor:
                        futures = {
                            executor.submit(
                                self._compile_unix_job,
                                compiler,
                                job,
                                cc_args,
                                extra_postargs_list,
                                pp_opts,
                                object_cache,
                            ): job for job in compile_jobs
                        }
                        for future in as_completed(futures):
                            future.result()
                            compiled_count += 1
                else:
                    for job in compile_jobs:
                        self._compile_unix_job(
                            compiler,
                            job,
                            cc_args,
                            extra_postargs_list,
                            pp_opts,
                            object_cache,
                        )
                        compiled_count += 1

        if object_cache:
            log.info(
                "Object cache for %s: reused %d, compiled %d",
                ext.name,
                reused_count,
                compiled_count,
            )

        self._cache_stats["reused"] += reused_count
        self._cache_stats["compiled"] += compiled_count

        return objects

    @staticmethod
    def _serialise_macros(macros):
        serialised = []
        for macro in macros or []:
            if len(macro) == 1:
                serialised.append(f"U:{macro[0]}")
            else:
                name, value = macro
                serialised.append(f"D:{name}={value}")
        serialised.sort()
        return serialised

    def _compile_unix_job(self, compiler, job, cc_args, extra_postargs, pp_opts, object_cache):
        obj_path, src, src_ext, fingerprint = job
        postargs = list(extra_postargs) if extra_postargs else []
        compiler._compile(obj_path, src, src_ext, cc_args, postargs, pp_opts)
        if object_cache and fingerprint:
            object_cache.store_object(fingerprint, Path(obj_path))
        return obj_path


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
        ]
        + (["asound"] if sys.platform.startswith("linux") else [])
        + (["ole32", "oleaut32", "avrt", "mmdevapi", "uuid", "propsys"] if sys.platform == "win32" else []),
        extra_link_args=(
            ["ole32.lib", "oleaut32.lib", "avrt.lib", "mmdevapi.lib", "uuid.lib", "propsys.lib"]
            if sys.platform == "win32"
            else []
        ),
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
