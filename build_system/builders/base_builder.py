"""
Base builder class that all builders inherit from
"""

import os
import subprocess
import shutil
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
        self._setup_environment()
    
    def _setup_environment(self):
        """Setup build environment variables"""
        # Add install directory to paths
        self.env["CMAKE_PREFIX_PATH"] = str(self.install_dir)
        self.env["PKG_CONFIG_PATH"] = f"{self.install_dir}/lib/pkgconfig:{self.install_dir}/lib64/pkgconfig"
        
        if self.platform == "linux":
            # Linux-specific environment
            self.env["CFLAGS"] = self.env.get("CFLAGS", "") + " -fPIC"
            self.env["CXXFLAGS"] = self.env.get("CXXFLAGS", "") + " -fPIC -std=c++17"
            self.env["LDFLAGS"] = f"-L{self.install_dir}/lib -L{self.install_dir}/lib64"
            
        elif self.platform == "windows":
            # Windows-specific environment
            self.env["INCLUDE"] = f"{self.install_dir}\\include;" + self.env.get("INCLUDE", "")
            self.env["LIB"] = f"{self.install_dir}\\lib;" + self.env.get("LIB", "")
    
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
        replacements = {
            "{install_dir}": str(self.install_dir),
            "{source_dir}": str(self.source_dir),
            "{platform}": self.platform,
            "{arch}": self.arch,
            "{cpu_count}": str(os.cpu_count() or 1),
        }
        
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
            text = text.replace(key, value)
        
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