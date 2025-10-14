"""
Base builder class that all builders inherit from
"""

import os
import subprocess
import shutil
import struct
import re
import platform
from abc import ABC, abstractmethod
from pathlib import Path
from typing import Dict, List, Optional, Any


class BaseBuilder(ABC):
    """Abstract base class for all builders"""
    
    def __init__(self,
                 name: str,
                 config: Dict[str, Any],
                 platform: str,
                 arch: str,
                 install_dir: Path,
                 logger: Any,
                 dry_run: bool = False):
        """
        Initialize base builder
        
        Args:
            name: Dependency name
            config: Dependency configuration
            platform: Target platform (linux, windows)
            arch: Target architecture (x64, x86)
            install_dir: Installation directory
            logger: Logger instance
            dry_run: If True, don't actually run commands
        """
        self.name = name
        self.config = config
        self.platform = platform
        self.arch = arch
        self.install_dir = install_dir
        self.logger = logger
        self.dry_run = dry_run
        
        # Get source directory
        self.source_dir = Path(config["source_dir"]).resolve()
        if not self.source_dir.exists():
            raise FileNotFoundError(f"Source directory not found: {self.source_dir}")
        
        # Get build configuration for platform
        build_configs = config.get("build_configs", {})
        if "all" in build_configs:
            self.build_config = build_configs["all"]
        elif platform in build_configs:
            self.build_config = build_configs[platform]
        else:
            raise ValueError(f"No build configuration for {platform} in {name}")
        
        # Setup environment
        self.env = os.environ.copy()
        self._vcvarsall_path = None
        self._vcvars_arch = None
        self._setup_environment()
    
    def _find_vcvarsall(self):
        """Find vcvarsall.bat for MSVC environment setup on Windows"""
        if self.platform != "windows":
            return None
            
        if self._vcvarsall_path is not None:
            return self._vcvarsall_path
            
        # Try vswhere first
        vswhere_path = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
        
        if vswhere_path.exists():
            try:
                cmd = [
                    str(vswhere_path), "-latest", "-prerelease", "-products", "*",
                    "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                    "-property", "installationPath"
                ]
                result = subprocess.run(cmd, capture_output=True, text=True, check=True)
                vs_install_path = Path(result.stdout.strip())
                vcvarsall = vs_install_path / "VC/Auxiliary/Build/vcvarsall.bat"
                
                if vcvarsall.exists():
                    self._vcvarsall_path = str(vcvarsall)
                    # Use target architecture instead of Python interpreter architecture
                    self._vcvars_arch = "x64" if self.arch == "x64" else "x86"
                    return self._vcvarsall_path
            except Exception as e:
                self.logger.debug(f"vswhere failed: {e}")
        
        self.logger.debug("Could not find vcvarsall.bat. Commands will run without MSVC environment setup.")
        return None
    
    def _setup_environment(self):
        """Setup build environment variables"""
        # Preserve critical cross-compilation environment variables
        cross_vars = ['CC', 'CXX', 'AR', 'AS', 'LD', 'RANLIB', 'STRIP',
                      'CROSS_COMPILE', 'HOST', 'TARGET', 'CONFIG_SITE',
                      'CFLAGS', 'CXXFLAGS', 'LDFLAGS', 'CPPFLAGS']
        
        for var in cross_vars:
            if var in os.environ:
                self.env[var] = os.environ[var]
        
        # Add install directory to paths
        self.env["CMAKE_PREFIX_PATH"] = str(self.install_dir)
        self.env["PKG_CONFIG_PATH"] = f"{self.install_dir}/lib/pkgconfig:{self.install_dir}/lib64/pkgconfig"
        
        if self.platform == "linux":
            # Linux-specific environment
            # Append to existing CFLAGS/CXXFLAGS to preserve cross-compilation flags
            existing_cflags = self.env.get("CFLAGS", "")
            if "-fPIC" not in existing_cflags:
                self.env["CFLAGS"] = existing_cflags + " -fPIC"
            
            existing_cxxflags = self.env.get("CXXFLAGS", "")
            if "-fPIC" not in existing_cxxflags:
                self.env["CXXFLAGS"] = existing_cxxflags + " -fPIC"
            if "-std=c++17" not in existing_cxxflags:
                self.env["CXXFLAGS"] = self.env.get("CXXFLAGS", "") + " -std=c++17"
            
            existing_ldflags = self.env.get("LDFLAGS", "")
            install_ldflags = f"-L{self.install_dir}/lib -L{self.install_dir}/lib64"
            if install_ldflags not in existing_ldflags:
                self.env["LDFLAGS"] = existing_ldflags + " " + install_ldflags if existing_ldflags else install_ldflags
            
        elif self.platform == "windows":
            # Windows-specific environment
            # On Windows, use only 'lib' directory for both x86 and x64
            self.env["INCLUDE"] = f"{self.install_dir}\\include;" + self.env.get("INCLUDE", "")
            self.env["LIB"] = f"{self.install_dir}\\lib;" + self.env.get("LIB", "")
    
    def _detect_cross_compilation(self) -> Optional[str]:
        """
        Detect cross-compilation and return target triplet.
        
        Checks environment variables to determine if we're cross-compiling.
        Returns the target triplet (e.g., 'aarch64-linux-musl') if detected.
        
        Returns:
            Target triplet string or None if not cross-compiling
        """
        # Check if already detected
        if hasattr(self, '_cross_target'):
            return self._cross_target
        
        self._cross_target = None
        
        # Method 1: Parse CC environment variable
        cc = os.environ.get('CC', '')
        if cc:
            # Extract triplet from compiler name
            # Patterns: <triplet>-gcc, <triplet>-clang, ccache <triplet>-gcc
            cc_base = cc.split()[-1]  # Handle 'ccache aarch64-...-gcc'
            
            # Match patterns like: aarch64-openwrt-linux-musl-gcc
            patterns = [
                r'([a-z0-9_]+-[a-z0-9_]+-[a-z0-9_]+-[a-z0-9_]+)-(?:gcc|g\+\+|clang)',  # quad
                r'([a-z0-9_]+-[a-z0-9_]+-[a-z0-9_]+)-(?:gcc|g\+\+|clang)',  # triple
                r'([a-z0-9_]+-[a-z0-9_]+)-(?:gcc|g\+\+|clang)',  # double
            ]
            
            for pattern in patterns:
                match = re.match(pattern, cc_base, re.IGNORECASE)
                if match:
                    self._cross_target = match.group(1)
                    self.logger.debug(f"Detected cross-target from CC: {self._cross_target}")
                    return self._cross_target
        
        # Method 2: Check CROSS_COMPILE
        cross_compile = os.environ.get('CROSS_COMPILE', '')
        if cross_compile:
            self._cross_target = cross_compile.rstrip('-')
            self.logger.debug(f"Detected cross-target from CROSS_COMPILE: {self._cross_target}")
            return self._cross_target
        
        # Method 3: Check HOST variable (OpenWrt, Buildroot)
        host = os.environ.get('HOST', '')
        if host and host != platform.machine():
            self._cross_target = host
            self.logger.debug(f"Detected cross-target from HOST: {self._cross_target}")
            return self._cross_target
        
        # Not cross-compiling
        self.logger.debug("No cross-compilation detected")
        return None
    
    def _detect_build_triplet(self) -> Optional[str]:
        """
        Detect the build system triplet.
        
        Returns:
            Build triplet string or None
        """
        # Use config.guess if available
        config_guess = self.source_dir / "config.guess"
        if config_guess.exists():
            try:
                result = subprocess.run(
                    [str(config_guess)],
                    capture_output=True,
                    text=True,
                    timeout=5,
                    check=False
                )
                if result.returncode == 0:
                    triplet = result.stdout.strip()
                    self.logger.debug(f"Build triplet from config.guess: {triplet}")
                    return triplet
            except Exception as e:
                self.logger.debug(f"Failed to run config.guess: {e}")
        
        # Fallback: construct from platform info
        machine = platform.machine().lower()
        system = platform.system().lower()
        
        if system == 'linux':
            if machine in ['x86_64', 'amd64']:
                return 'x86_64-linux-gnu'
            elif machine in ['aarch64', 'arm64']:
                return 'aarch64-linux-gnu'
            elif machine in ['i386', 'i686']:
                return 'i686-linux-gnu'
            elif machine.startswith('arm'):
                return 'arm-linux-gnueabihf'
        
        return None
    
    def run_command(self,
                   cmd: List[str],
                   cwd: Optional[Path] = None,
                   env: Optional[Dict] = None,
                   check: bool = True,
                   capture_output: bool = False,
                   shell: bool = False) -> subprocess.CompletedProcess:
        """
        Run a command with logging
        
        Args:
            cmd: Command and arguments
            cwd: Working directory
            env: Environment variables
            check: Raise exception on non-zero exit
            capture_output: Capture stdout/stderr
            shell: Run through shell
            
        Returns:
            CompletedProcess instance
        """
        if cwd is None:
            cwd = self.source_dir
        if env is None:
            env = self.env
            
        cmd_str = " ".join(str(c) for c in cmd) if isinstance(cmd, list) else str(cmd)
        self.logger.debug(f"Running: {cmd_str}")
        self.logger.debug(f"  in: {cwd}")
        
        if self.dry_run:
            self.logger.info(f"[DRY RUN] Would run: {cmd_str}")
            return subprocess.CompletedProcess(cmd, 0, "", "")
        
        # On Windows, check if we need MSVC environment for certain commands
        if self.platform == "windows" and not shell:
            cmd_name = cmd[0] if isinstance(cmd, list) else cmd.split()[0]
            needs_msvc = cmd_name.lower() in ["nmake", "cl", "link", "lib"]
            
            # Check if MSVC tools are available
            if needs_msvc and not shutil.which(cmd_name) and "VCINSTALLDIR" not in os.environ:
                # Need to set up MSVC environment
                vcvarsall = self._find_vcvarsall()
                if vcvarsall:
                    # Wrap command with vcvarsall
                    cmd_str = " ".join(f'"{c}"' if " " in str(c) else str(c) for c in cmd)
                    full_cmd = f'"{vcvarsall}" {self._vcvars_arch} && {cmd_str}'
                    
                    self.logger.debug(f"Running with MSVC environment: {full_cmd}")
                    
                    # Run with shell=True for vcvarsall
                    try:
                        return subprocess.run(
                            full_cmd,
                            cwd=cwd,
                            env=env,
                            check=check,
                            capture_output=capture_output,
                            text=True,
                            shell=True
                        )
                    except subprocess.CalledProcessError as e:
                        self.logger.error(f"Command failed: {full_cmd}")
                        if e.stdout:
                            self.logger.error(f"stdout: {e.stdout}")
                        if e.stderr:
                            self.logger.error(f"stderr: {e.stderr}")
                        raise
        
        try:
            result = subprocess.run(
                cmd,
                cwd=cwd,
                env=env,
                check=check,
                capture_output=capture_output,
                text=True,
                shell=shell
            )
            
            if capture_output and result.stdout:
                self.logger.debug(f"Output: {result.stdout}")
            
            return result
            
        except subprocess.CalledProcessError as e:
            self.logger.error(f"Command failed: {cmd_str}")
            if e.stdout:
                self.logger.error(f"stdout: {e.stdout}")
            if e.stderr:
                self.logger.error(f"stderr: {e.stderr}")
            raise
    
    def replace_variables(self, text: str) -> str:
        """Replace variables in configuration strings"""
        # Map architecture to machine type for linker
        machine_arch = "I386" if self.arch == "x86" else "X64"
        
        self.logger.debug(f"replace_variables: self.arch={self.arch}, machine_arch={machine_arch}")
        
        replacements = {
            "{install_dir}": str(self.install_dir),
            "{source_dir}": str(self.source_dir),
            "{platform}": self.platform,
            "{arch}": machine_arch,
            "{cpu_count}": str(os.cpu_count() or 1),
        }
        
        self.logger.debug(f"replace_variables: input='{text}'")
        
        # Add platform-specific targets
        if "platform_target" in self.build_config:
            import platform
            machine = platform.machine().lower()
            targets = self.build_config["platform_target"]
            
            if machine in targets:
                replacements["{platform_target}"] = targets[machine]
            elif "x86_64" in machine or "amd64" in machine:
                replacements["{platform_target}"] = targets.get("x86_64", "")
            elif "aarch64" in machine or "arm64" in machine:
                replacements["{platform_target}"] = targets.get("aarch64", "")
            else:
                replacements["{platform_target}"] = targets.get("i686", "")
        
        for key, value in replacements.items():
            if key in text:
                self.logger.debug(f"replace_variables: replacing {key} with {value}")
            text = text.replace(key, value)
        
        self.logger.debug(f"replace_variables: output='{text}'")
        return text
    
    @abstractmethod
    def configure(self) -> bool:
        """Configure the build"""
        pass
    
    @abstractmethod
    def build(self) -> bool:
        """Build the dependency"""
        pass
    
    @abstractmethod
    def install(self) -> bool:
        """Install the dependency"""
        pass
    
    def clean(self) -> bool:
        """Clean build artifacts (preserves source distribution files)"""
        self.logger.info(f"Cleaning {self.name}...")
        
        # Only clean dedicated build directories
        # Do NOT clean source directory artifacts - they'll be overwritten anyway
        for dir_name in ["build", "build_cmake", "build_msvc", "_build"]:
            build_dir = self.source_dir / dir_name
            if build_dir.exists():
                self.logger.debug(f"Removing {build_dir}")
                if not self.dry_run:
                    shutil.rmtree(build_dir, ignore_errors=True)
        
        # Clean CMake cache files (these can cause issues)
        cmake_cache = self.source_dir / "CMakeCache.txt"
        if cmake_cache.exists():
            self.logger.debug(f"Removing {cmake_cache}")
            if not self.dry_run:
                cmake_cache.unlink(missing_ok=True)
        
        cmake_files = self.source_dir / "CMakeFiles"
        if cmake_files.exists():
            self.logger.debug(f"Removing {cmake_files}")
            if not self.dry_run:
                shutil.rmtree(cmake_files, ignore_errors=True)
        
        return True
    
    def verify(self) -> bool:
        """Verify the installation"""
        self.logger.debug(f"Verifying {self.name} installation...")
        
        outputs = self.config.get("outputs", {})
        
        # Check libraries
        libs = outputs.get("libraries", {}).get(self.platform, [])
        for lib in libs:
            lib_path = self.install_dir / "lib" / lib
            if not lib_path.exists():
                # Check lib64
                lib_path = self.install_dir / "lib64" / lib
            
            if not lib_path.exists():
                self.logger.error(f"Library not found: {lib}")
                return False
            else:
                self.logger.debug(f"Found library: {lib_path}")
        
        # Check headers
        headers = outputs.get("headers", [])
        for header in headers:
            if "*" in header:
                # Glob pattern
                header_files = list(self.install_dir.glob(f"include/{header}"))
                if not header_files:
                    self.logger.error(f"No headers found matching: {header}")
                    return False
            else:
                header_path = self.install_dir / "include" / header
                if not header_path.exists():
                    self.logger.error(f"Header not found: {header}")
                    return False
                else:
                    self.logger.debug(f"Found header: {header_path}")
        
        return True
    
    def handle_special_requirements(self) -> bool:
        """Handle special requirements like downloading models"""
        # Handle opus model download
        if self.config.get("requires_model") and self.name == "opus":
            return self._download_opus_model()
        return True
    
    def _download_opus_model(self) -> bool:
        """Download and extract opus model"""
        self.logger.info("Handling Opus model...")
        
        model_config = self.config.get("model_config", {})
        # Use absolute path from project root
        project_root = self.source_dir.parent.parent.parent  # src/audio_engine/deps/opus -> project root
        cache_dir = project_root / model_config.get("cache_dir", "build_cache")
        cache_dir.mkdir(parents=True, exist_ok=True)
        
        checksum = model_config["checksum"]
        model_filename = f"opus_data-{checksum}.tar.gz"
        model_url = model_config["url"].replace("{checksum}", checksum)
        cached_model_path = cache_dir / model_filename
        
        if not cached_model_path.exists():
            self.logger.info(f"Downloading Opus model from {model_url}")
            
            # Try wget first
            try:
                self.run_command(
                    ["wget", "-O", str(cached_model_path), model_url],
                    cwd=cache_dir
                )
            except (subprocess.CalledProcessError, FileNotFoundError):
                # Try curl
                try:
                    self.run_command(
                        ["curl", "-L", "-o", str(cached_model_path), model_url],
                        cwd=cache_dir
                    )
                except (subprocess.CalledProcessError, FileNotFoundError) as e:
                    self.logger.error(f"Failed to download Opus model: {e}")
                    return False
        
        # Extract to opus source directory
        target_dir = self.source_dir / model_config.get("target_dir", "dnn")
        target_dir.mkdir(exist_ok=True)
        
        model_dest = target_dir / model_filename
        if not model_dest.exists() or model_dest.stat().st_size != cached_model_path.stat().st_size:
            self.logger.info(f"Copying Opus model to {model_dest}")
            if not self.dry_run:
                shutil.copy2(cached_model_path, model_dest)
        
        return True
    
    def execute(self) -> bool:
        """Execute the full build process"""
        try:
            self.logger.info(f"Building {self.name} for {self.platform} ({self.arch})...")
            
            # Handle special requirements
            if not self.handle_special_requirements():
                return False
            
            # Clean if needed
            if self.build_config.get("clean_before_build", False):
                if not self.clean():
                    return False
            
            # Configure
            self.logger.info(f"Configuring {self.name}...")
            if not self.configure():
                self.logger.error(f"Configuration failed for {self.name}")
                return False
            
            # Build
            self.logger.info(f"Building {self.name}...")
            if not self.build():
                self.logger.error(f"Build failed for {self.name}")
                return False
            
            # Install
            self.logger.info(f"Installing {self.name}...")
            if not self.install():
                self.logger.error(f"Installation failed for {self.name}")
                return False
            
            # Verify
            if not self.verify():
                self.logger.warning(f"Verification failed for {self.name}")
                return False
            
            self.logger.success(f"Successfully built {self.name}")
            return True
            
        except Exception as e:
            self.logger.error(f"Error building {self.name}: {e}")
            return False