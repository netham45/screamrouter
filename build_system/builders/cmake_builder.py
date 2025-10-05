"""
CMake builder implementation
"""

import os
import shutil
from pathlib import Path
from typing import Optional, List
from .base_builder import BaseBuilder


class CMakeBuilder(BaseBuilder):
    """Builder for CMake-based projects"""
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        
        # Get build directory
        build_dir_name = self.build_config.get("build_dir", "build")
        self.build_dir = self.source_dir / build_dir_name
        
        # Get CMake executable
        self.cmake = shutil.which("cmake")
        if not self.cmake:
            raise FileNotFoundError("cmake not found in PATH")
    
    def configure(self) -> bool:
        """Configure using CMake"""
        # Create build directory
        self.build_dir.mkdir(parents=True, exist_ok=True)
        
        # Build CMake command
        cmd = [self.cmake, "-S", str(self.source_dir), "-B", str(self.build_dir)]
        
        # Add install prefix
        cmd.append(f"-DCMAKE_INSTALL_PREFIX={self.install_dir}")
        
        # Add platform-specific options
        if self.platform == "windows":
            # Generator
            generator = self.build_config.get("generator")
            if generator:
                cmd.extend(["-G", generator])
            
            # Architecture
            if self.arch == "x64":
                cmd.extend(["-A", "x64"])
            elif self.arch == "x86":
                cmd.extend(["-A", "Win32"])
            
            # Runtime library
            cmd.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL")
        else:
            # Position independent code for Linux
            cmd.append("-DCMAKE_POSITION_INDEPENDENT_CODE=ON")
            
            # Build type
            cmd.append("-DCMAKE_BUILD_TYPE=Release")
        
        # Add custom CMake arguments
        cmake_args = self.build_config.get("cmake_args", [])
        for arg in cmake_args:
            cmd.append(self.replace_variables(arg))
        
        # Run configuration
        return self.run_command(cmd, cwd=self.build_dir).returncode == 0
    
    def build(self) -> bool:
        """Build using CMake"""
        cmd = [
            self.cmake,
            "--build", str(self.build_dir),
            "--config", "Release",
            "--parallel", str(os.cpu_count() or 1)
        ]
        
        return self.run_command(cmd).returncode == 0
    
    def install(self) -> bool:
        """Install using CMake"""
        cmd = [
            self.cmake,
            "--install", str(self.build_dir),
            "--config", "Release"
        ]
        
        return self.run_command(cmd).returncode == 0
    
    def clean(self) -> bool:
        """Clean CMake build artifacts"""
        super().clean()
        
        # Remove CMake-specific files
        cmake_files = [
            "CMakeCache.txt",
            "cmake_install.cmake",
            "CTestTestfile.cmake"
        ]
        
        for file in cmake_files:
            file_path = self.source_dir / file
            if file_path.exists():
                self.logger.debug(f"Removing {file_path}")
                if not self.dry_run:
                    file_path.unlink()
        
        # Remove CMakeFiles directory
        cmake_files_dir = self.source_dir / "CMakeFiles"
        if cmake_files_dir.exists():
            self.logger.debug(f"Removing {cmake_files_dir}")
            if not self.dry_run:
                shutil.rmtree(cmake_files_dir, ignore_errors=True)
        
        return True