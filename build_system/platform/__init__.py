"""
Platform detection and configuration
"""

import os
import sys
import platform
import subprocess
from pathlib import Path
from typing import Dict, Any, Optional


class PlatformDetector:
    """Detects and provides information about the current platform"""
    
    def detect(self) -> Dict[str, Any]:
        """
        Detect current platform and architecture
        
        Returns:
            Dictionary with platform information
        """
        python_bits = 64 if sys.maxsize > 2**32 else 32
        arch = self._get_architecture()
        
        info = {
            "os": platform.system(),
            "platform": self._get_platform_name(),
            "arch": arch,
            "machine": platform.machine(),
            "python_version": sys.version,
            "python_bits": python_bits
        }
        
        # Log architecture detection for debugging Windows x86 issues
        if info["platform"] == "windows":
            import logging
            logger = logging.getLogger(__name__)
            logger.info(f"Platform Detection: Python {python_bits}-bit, Machine: {platform.machine()}, Detected arch: {arch}")
        
        # Add distribution info for Linux
        if info["platform"] == "linux":
            info["distribution"] = self._detect_linux_distribution()
        
        # Add MSVC info for Windows
        if info["platform"] == "windows":
            info["msvc_available"] = self._check_msvc()
            info["msvc_env"] = self._get_msvc_env()
        
        return info
    
    def _get_platform_name(self) -> str:
        """Get normalized platform name"""
        system = platform.system().lower()
        
        if system == "linux":
            return "linux"
        elif system == "windows":
            return "windows"
        elif system == "darwin":
            return "macos"  # Not supported, but detected
        else:
            return system
    
    def _get_architecture(self) -> str:
        """Get normalized architecture based on Python interpreter"""
        # CRITICAL: Use Python's pointer size as the primary indicator
        # This is essential for cross-compilation scenarios where 32-bit Python
        # runs on 64-bit Windows (platform.machine() returns AMD64 but we need x86)
        
        # Check Python interpreter pointer size first
        python_bits = 64 if sys.maxsize > 2**32 else 32
        
        # For x86/x64, use Python's architecture
        # This is CRITICAL for Windows where 32-bit Python can run on 64-bit OS
        if python_bits == 32:
            # Force x86 architecture when running 32-bit Python
            return "x86"
        elif python_bits == 64:
            # Check if it's ARM64 or x64
            machine = platform.machine().lower()
            if machine in ["aarch64", "arm64"]:
                return "aarch64"
            else:
                return "x64"
        
        # Fallback to machine detection (shouldn't reach here)
        machine = platform.machine().lower()
        if machine in ["aarch64", "arm64"]:
            return "aarch64"
        elif machine in ["i386", "i686", "x86"]:
            return "x86"
        else:
            return "x64"
    
    def _detect_linux_distribution(self) -> str:
        """Detect Linux distribution"""
        # Try to read distribution files
        dist_files = {
            "/etc/debian_version": "debian",
            "/etc/redhat-release": "rhel",
            "/etc/centos-release": "rhel",
            "/etc/fedora-release": "rhel",
            "/etc/rocky-release": "rhel",
            "/etc/almalinux-release": "rhel",
            "/etc/lsb-release": None,  # Could be Ubuntu or others
        }
        
        for file_path, dist_name in dist_files.items():
            if Path(file_path).exists():
                if dist_name:
                    return dist_name
                # Parse lsb-release for Ubuntu
                if file_path == "/etc/lsb-release":
                    try:
                        with open(file_path, 'r') as f:
                            content = f.read()
                            if "Ubuntu" in content:
                                return "debian"
                    except:
                        pass
        
        # Try lsb_release command
        try:
            result = subprocess.run(
                ["lsb_release", "-is"],
                capture_output=True,
                text=True,
                check=True
            )
            distro = result.stdout.strip().lower()
            if distro in ["debian", "ubuntu"]:
                return "debian"
            elif distro in ["redhat", "centos", "fedora", "rocky", "almalinux"]:
                return "rhel"
        except:
            pass
        
        # Try /etc/os-release
        try:
            with open("/etc/os-release", 'r') as f:
                content = f.read().lower()
                if "debian" in content or "ubuntu" in content:
                    return "debian"
                elif any(x in content for x in ["rhel", "redhat", "centos", "fedora", "rocky", "alma"]):
                    return "rhel"
        except:
            pass
        
        return "unknown"
    
    def _check_msvc(self) -> bool:
        """Check if MSVC is available"""
        # Check environment variables
        if os.environ.get("VCINSTALLDIR") or os.environ.get("VCToolsInstallDir"):
            return True
        
        # Check for vswhere
        vswhere_path = Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
        if vswhere_path.exists():
            try:
                result = subprocess.run(
                    [str(vswhere_path), "-latest", "-property", "installationPath"],
                    capture_output=True,
                    text=True,
                    check=True
                )
                return bool(result.stdout.strip())
            except:
                pass
        
        return False
    
    def _get_msvc_env(self) -> Optional[Dict[str, str]]:
        """Get MSVC environment variables if available"""
        if os.environ.get("VCINSTALLDIR"):
            return {
                "VCINSTALLDIR": os.environ.get("VCINSTALLDIR", ""),
                "VCToolsInstallDir": os.environ.get("VCToolsInstallDir", ""),
                "INCLUDE": os.environ.get("INCLUDE", ""),
                "LIB": os.environ.get("LIB", ""),
                "PATH": os.environ.get("PATH", ""),
            }
        return None


class MSVCEnvironment:
    """Manages MSVC environment setup for Windows builds"""
    
    def __init__(self, arch: str = "x64"):
        """
        Initialize MSVC environment manager
        
        Args:
            arch: Target architecture (x64 or x86)
        """
        self.arch = arch
        self.vcvarsall_path = self._find_vcvarsall()
    
    def _find_vcvarsall(self) -> Optional[Path]:
        """Find vcvarsall.bat"""
        # Try vswhere first
        vswhere_path = Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
        if vswhere_path.exists():
            try:
                result = subprocess.run(
                    [str(vswhere_path), "-latest", "-property", "installationPath"],
                    capture_output=True,
                    text=True,
                    check=True
                )
                vs_path = Path(result.stdout.strip())
                
                # Check common locations
                possible_paths = [
                    vs_path / "VC/Auxiliary/Build/vcvarsall.bat",
                    vs_path / "VC/vcvarsall.bat",
                ]
                
                for path in possible_paths:
                    if path.exists():
                        return path
            except:
                pass
        
        # Check common paths
        common_paths = [
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/2019/Enterprise/VC/Auxiliary/Build/vcvarsall.bat",
        ]
        
        for path_str in common_paths:
            path = Path(path_str)
            if path.exists():
                return path
        
        return None
    
    def get_env_command(self, command: str) -> str:
        """
        Get command wrapped with vcvarsall setup
        
        Args:
            command: Command to wrap
            
        Returns:
            Full command with environment setup
        """
        if not self.vcvarsall_path:
            return command
        
        arch_arg = "x64" if self.arch == "x64" else "x86"
        return f'"{self.vcvarsall_path}" {arch_arg} && {command}'
    
    def setup_environment(self) -> Dict[str, str]:
        """
        Get environment variables after vcvarsall setup
        
        Returns:
            Dictionary of environment variables
        """
        if not self.vcvarsall_path:
            return os.environ.copy()
        
        # Run vcvarsall and capture environment
        arch_arg = "x64" if self.arch == "x64" else "x86"
        cmd = f'"{self.vcvarsall_path}" {arch_arg} && set'
        
        try:
            result = subprocess.run(
                cmd,
                shell=True,
                capture_output=True,
                text=True,
                check=True
            )
            
            # Parse environment variables
            env = {}
            for line in result.stdout.split('\n'):
                if '=' in line:
                    key, value = line.split('=', 1)
                    env[key] = value
            
            return env
        except:
            return os.environ.copy()


__all__ = ["PlatformDetector", "MSVCEnvironment"]