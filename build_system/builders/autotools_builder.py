"""
Autotools builder implementation
"""

import os
import shutil
from pathlib import Path
from .base_builder import BaseBuilder


class AutotoolsBuilder(BaseBuilder):
    """Builder for autotools-based projects"""
    
    def configure(self) -> bool:
        """Configure using autotools"""
        # Check if we need to run autogen.sh or similar
        configure_script = self.source_dir / "configure"
        makefile_in = self.source_dir / "Makefile.in"
        
        # Run pre-configure scripts if specified
        pre_configure = self.build_config.get("pre_configure", [])
        for script in pre_configure:
            if (self.source_dir / script.lstrip("./")):
                self.logger.info(f"Running {script} for {self.name}...")
                if self.run_command(["sh", script], cwd=self.source_dir).returncode != 0:
                    return False
        
        # Only run autoreconf/autogen if configure doesn't exist
        # Many projects ship with pre-generated configure scripts
        if not configure_script.exists():
            self.logger.info(f"configure script not found, attempting to generate it...")
            
            # Try autogen.sh first (preferred for many projects)
            autogen = self.source_dir / "autogen.sh"
            if autogen.exists():
                self.logger.info(f"Running autogen.sh for {self.name}...")
                if self.run_command(["sh", "./autogen.sh"], cwd=self.source_dir).returncode != 0:
                    self.logger.warning("autogen.sh failed")
                    return False
            elif shutil.which("autoreconf"):
                # Fall back to autoreconf
                self.logger.info(f"Running autoreconf for {self.name}...")
                if self.run_command(["autoreconf", "-fiv"], cwd=self.source_dir).returncode != 0:
                    self.logger.error("autoreconf failed")
                    return False
            else:
                self.logger.error("No way to generate configure script (no autogen.sh or autoreconf)")
                return False
        
        # Check configure script exists now
        if not configure_script.exists():
            self.logger.error(f"Configure script not found for {self.name}")
            return False
        
        # Build configure command
        configure_script_name = self.build_config.get("configure_script", "./configure")
        cmd = [configure_script_name]
        
        # Add configure arguments (check if prefix is already included)
        configure_args = self.build_config.get("configure_args", [])
        has_prefix = any("--prefix" in arg for arg in configure_args)
        
        if not has_prefix:
            cmd.append(f"--prefix={self.install_dir}")
        
        for arg in configure_args:
            cmd.append(self.replace_variables(arg))
        
        # Add environment variables if specified
        env = self.env.copy()
        env_vars = self.build_config.get("env_vars", {})
        for key, value in env_vars.items():
            env[key] = self.replace_variables(value)
        
        # Run configuration
        return self.run_command(cmd, env=env).returncode == 0
    
    def build(self) -> bool:
        """Build using make"""
        # Determine make command
        make_cmd = "make"
        if self.platform == "windows":
            # Shouldn't happen for autotools, but just in case
            make_cmd = "mingw32-make" if shutil.which("mingw32-make") else "make"
        
        cmd = [make_cmd, f"-j{os.cpu_count() or 1}"]
        
        # Add make targets
        make_targets = self.build_config.get("make_targets", [])
        if make_targets and "all" not in make_targets and "install" not in make_targets:
            # If specific targets are given (not including install), use them
            for target in make_targets:
                if target != "install":  # Install is handled separately
                    result = self.run_command([make_cmd, target])
                    if result.returncode != 0:
                        return False
            return True
        
        return self.run_command(cmd).returncode == 0
    
    def install(self) -> bool:
        """Install using make install"""
        make_cmd = "make"
        if self.platform == "windows":
            make_cmd = "mingw32-make" if shutil.which("mingw32-make") else "make"
        
        # Check if install is in make_targets
        make_targets = self.build_config.get("make_targets", [])
        if "install" in make_targets:
            cmd = [make_cmd, "install"]
        else:
            # Default to make install
            cmd = [make_cmd, "install"]
        
        return self.run_command(cmd).returncode == 0
    
    def clean(self) -> bool:
        """Clean autotools build artifacts"""
        super().clean()
        
        # Try make clean first
        if (self.source_dir / "Makefile").exists():
            make_cmd = "make"
            if self.platform == "windows":
                make_cmd = "mingw32-make" if shutil.which("mingw32-make") else "make"
            
            self.run_command([make_cmd, "clean"], check=False)
        
        # Remove autotools-specific files
        autotools_files = [
            "Makefile",
            "Makefile.in",
            "config.status",
            "config.log",
            "config.h",
            "stamp-h1",
            "libtool",
            "*.la",
            "*.pc"
        ]
        
        for pattern in autotools_files:
            for file in self.source_dir.glob(pattern):
                self.logger.debug(f"Removing {file}")
                if not self.dry_run:
                    file.unlink(missing_ok=True)
        
        # Remove .deps and .libs directories
        for dir_name in [".deps", ".libs"]:
            for dir_path in self.source_dir.rglob(dir_name):
                if dir_path.is_dir():
                    self.logger.debug(f"Removing {dir_path}")
                    if not self.dry_run:
                        shutil.rmtree(dir_path, ignore_errors=True)
        
        return True