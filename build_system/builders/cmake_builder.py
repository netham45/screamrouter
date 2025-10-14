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
        
        # Remove entire build directory if it exists to avoid stale cache
        if self.build_dir.exists():
            self.logger.debug(f"Removing stale build directory: {self.build_dir}")
            if not self.dry_run:
                shutil.rmtree(self.build_dir, ignore_errors=True)
        
        # Create fresh build directory
        self.build_dir.mkdir(parents=True, exist_ok=True)
        
        # Build CMake command
        cmd = [self.cmake, "-S", str(self.source_dir), "-B", str(self.build_dir)]
        
        # Add install prefix
        cmd.append(f"-DCMAKE_INSTALL_PREFIX={self.install_dir}")
        
        # Add cross-compilation support
        host_triplet = self._detect_cross_compilation()
        if host_triplet:
            self.logger.info(f"Cross-compilation detected: target={host_triplet}")
            
            # CMake uses CMAKE_SYSTEM_NAME and CMAKE_SYSTEM_PROCESSOR
            # Parse triplet: <arch>-<vendor>-<os>-<abi>
            parts = host_triplet.split('-')
            if len(parts) >= 2:
                arch = parts[0]
                # Find the OS part (usually 'linux', 'darwin', etc.)
                os_name = None
                for part in parts:
                    if 'linux' in part.lower():
                        os_name = 'linux'
                        break
                    elif 'darwin' in part.lower():
                        os_name = 'darwin'
                        break
                
                if os_name:
                    # Map to CMake system names
                    cmake_system = 'Linux' if os_name == 'linux' else os_name.capitalize()
                    
                    cmd.extend([
                        f"-DCMAKE_SYSTEM_NAME={cmake_system}",
                        f"-DCMAKE_SYSTEM_PROCESSOR={arch}"
                    ])
                    
                    # Set compilers from environment (need full paths for cross-compilation)
                    if os.environ.get('CC'):
                        cc = os.environ.get('CC')
                        # Remove ccache prefix if present
                        if cc.startswith('ccache '):
                            cc = cc[7:].strip()
                        # Get full path to compiler
                        cc_path = shutil.which(cc.split()[0])
                        if cc_path:
                            cmd.append(f"-DCMAKE_C_COMPILER={cc_path}")
                        else:
                            cmd.append(f"-DCMAKE_C_COMPILER={cc}")
                    
                    if os.environ.get('CXX'):
                        cxx = os.environ.get('CXX')
                        # Remove ccache prefix if present
                        if cxx.startswith('ccache '):
                            cxx = cxx[7:].strip()
                        # Get full path to compiler
                        cxx_path = shutil.which(cxx.split()[0])
                        if cxx_path:
                            cmd.append(f"-DCMAKE_CXX_COMPILER={cxx_path}")
                        else:
                            cmd.append(f"-DCMAKE_CXX_COMPILER={cxx}")
                    
                    self.logger.debug(f"CMake cross-compilation: system={cmake_system}, processor={arch}")
        
        # Add platform-specific options
        if self.platform == "windows":
            # On Windows, always use 'lib' directory to avoid 32-bit/64-bit confusion
            cmd.append("-DCMAKE_INSTALL_LIBDIR=lib")
            
            # Generator
            generator = self.build_config.get("generator")
            if generator:
                cmd.extend(["-G", generator])
            
            # Architecture (only for Visual Studio generators)
            # Ninja and other generators don't support -A flag
            if generator and "Visual Studio" in generator:
                if self.arch == "x64":
                    self.logger.info(f"Setting CMake architecture to x64 for {self.name}")
                    cmd.extend(["-A", "x64"])
                elif self.arch == "x86":
                    self.logger.info(f"Setting CMake architecture to Win32 (x86) for {self.name}")
                    cmd.extend(["-A", "Win32"])
                else:
                    self.logger.warning(f"Unknown architecture '{self.arch}' for {self.name}, CMake may use default")
            elif generator:
                # For non-Visual Studio generators, log architecture detection
                self.logger.info(f"Using generator '{generator}' with auto-detected architecture {self.arch}")
            
            # Runtime library - use MultiThreadedDLL for Release builds
            cmd.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL")
            
            # Explicitly set the architecture for CMake to avoid any auto-detection issues
            if self.arch == "x86":
                # Force 32-bit build flags
                cmd.append("-DCMAKE_C_FLAGS=/arch:IA32")
                cmd.append("-DCMAKE_CXX_FLAGS=/arch:IA32")
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