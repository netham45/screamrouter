#!/usr/bin/env python3
"""
Main entry point for the ScreamRouter build system
Supports Linux (Debian/RHEL) and Windows
"""

import argparse
import sys
import os
import shutil
import subprocess
import logging
from pathlib import Path
from typing import Optional, List, Dict, Any

from .builders import BuildOrchestrator
from .platform import PlatformDetector
from .utils import Logger, Cache, Verifier, ObjectCache
from .config import ConfigLoader


class BuildSystem:
    """Main build system class"""
    
    SUPPORTED_PLATFORMS = ["linux", "windows"]
    
    def __init__(self, 
                 root_dir: Optional[Path] = None,
                 install_dir: Optional[Path] = None,
                 platform: str = "auto",
                 arch: str = "auto",
                 verbose: bool = False,
                 dry_run: bool = False):
        """
        Initialize the build system
        
        Args:
            root_dir: Project root directory
            install_dir: Installation directory for dependencies
            platform: Target platform (auto, linux, windows)
            arch: Target architecture (auto, x64, x86)
            verbose: Enable verbose output
            dry_run: Perform dry run without actual building
        """
        self.root_dir = root_dir or Path.cwd()
        self.install_dir = install_dir or self.root_dir / "build" / "deps"
        self.verbose = verbose
        self.dry_run = dry_run
        
        # Setup logging
        self.logger = Logger(verbose=verbose)
        
        # Detect platform
        if platform == "auto":
            self.platform_info = PlatformDetector().detect()
            self.platform = self.platform_info["platform"]
        else:
            if platform not in self.SUPPORTED_PLATFORMS:
                raise ValueError(f"Unsupported platform: {platform}. "
                               f"Supported: {', '.join(self.SUPPORTED_PLATFORMS)}")
            self.platform = platform
            self.platform_info = {"platform": platform}
        
        # Detect architecture
        if arch == "auto":
            self.arch = self.platform_info.get("arch", "x64")
        else:
            self.arch = arch
            
        # Enhanced logging for architecture detection
        self.logger.info(f"Platform: {self.platform} ({self.arch})")
        if self.verbose or self.platform == "windows":
            # Always log detailed info on Windows to help diagnose x86/x64 issues
            self.logger.info(f"Platform info: {self.platform_info}")
            # Use top-level sys import (avoid shadowing local variable)
            python_bits = 64 if sys.maxsize > 2**32 else 32
            self.logger.info(f"Python bits: {python_bits}")
            import platform as plat
            self.logger.info(f"Platform machine: {plat.machine()}")
            self.logger.info(f"Detected architecture: {self.arch} (will be used for all dependency builds)")
        
        # Check for macOS and provide helpful message
        if sys.platform == "darwin":
            self.logger.error("macOS is not supported. Please use Docker instead.")
            self.logger.info("See: https://hub.docker.com/r/netham45/screamrouter")
            sys.exit(1)
        
        # Load configuration
        config_dir = self.root_dir / "build_system" / "config"
        if not config_dir.exists():
            # Fallback to package directory
            import build_system
            config_dir = Path(build_system.__file__).parent / "config"
            
        self.config = ConfigLoader(config_dir)
        
        # Initialize cache
        cache_dir = self.root_dir / "build" / ".cache"
        cache_dir.mkdir(parents=True, exist_ok=True)
        self.cache = Cache(cache_dir)
        self.object_cache = ObjectCache(cache_dir)
        
        # Initialize orchestrator
        self.orchestrator = BuildOrchestrator(
            config=self.config,
            platform=self.platform,
            arch=self.arch,
            install_dir=self.install_dir,
            cache=self.cache,
            logger=self.logger,
            dry_run=dry_run
        )
        
        # Initialize verifier
        self.verifier = Verifier(
            install_dir=self.install_dir,
            config=self.config,
            platform=self.platform,
            logger=self.logger
        )
    
    def check_prerequisites(self) -> bool:
        """
        Check if all required tools are installed
        
        Returns:
            True if all prerequisites are met
        """
        self.logger.info("Checking prerequisites...")
        
        platform_config = self.config.get_platform_config(self.platform)
        
        if self.platform == "linux":
            # Check for required packages
            distro = self.platform_info.get("distribution", "unknown")
            if distro in platform_config.get("distributions", {}):
                distro_config = platform_config["distributions"][distro]
                packages = distro_config.get("required_packages", [])
                
                missing = []
                # Check for essential build tools instead of package names
                essential_tools = {
                    "gcc": "build-essential",
                    "make": "build-essential",
                    "cmake": "cmake",
                    "autoconf": "autoconf",
                    "automake": "automake",
                    "libtool": "libtool-bin",
                    "pkg-config": "pkg-config"
                }
                
                for tool, package in essential_tools.items():
                    if not shutil.which(tool):
                        if package not in missing:
                            missing.append(package)
                
                if missing:
                    self.logger.warning(f"Missing packages: {', '.join(missing)}")
                    install_cmd = distro_config.get("package_install_cmd", "")
                    if install_cmd:
                        self.logger.info(f"Install with: sudo {install_cmd} {' '.join(missing)}")
                    return False
                    
        elif self.platform == "windows":
            # Check for required tools
            tools = platform_config.get("required_tools", [])
            missing = []
            
            for tool in tools:
                check_cmd = tool.get("check_command", [])
                if check_cmd:
                    try:
                        subprocess.run(check_cmd, capture_output=True, check=True)
                    except (subprocess.CalledProcessError, FileNotFoundError):
                        missing.append(tool["name"])
                        self.logger.warning(f"Missing tool: {tool['name']}")
                        if "install_hint" in tool:
                            self.logger.info(f"  {tool['install_hint']}")
            
            if missing:
                return False
                
            # Check for MSVC
            if not self._check_msvc():
                self.logger.error("Visual Studio C++ tools not found")
                self.logger.info("Please install Visual Studio with C++ development tools")
                return False
        
        return True
    
    def _check_msvc(self) -> bool:
        """Check if MSVC is available"""
        # Check environment variables
        if os.environ.get("VCINSTALLDIR"):
            return True
            
        # Check vswhere
        vswhere_path = Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
        if vswhere_path.exists():
            try:
                result = subprocess.run(
                    [str(vswhere_path), "-latest", "-property", "installationPath"],
                    capture_output=True, text=True, check=True
                )
                return bool(result.stdout.strip())
            except subprocess.CalledProcessError:
                pass
                
        return False
    
    def build_all(self, force: bool = False, parallel: bool = False) -> bool:
        """
        Build all dependencies
        
        Args:
            force: Force rebuild even if cached
            parallel: Build independent dependencies in parallel
            
        Returns:
            True if all builds successful
        """
        self.logger.info("Building all dependencies...")
        
        # Check prerequisites
        if not self.check_prerequisites():
            self.logger.error("Prerequisites check failed")
            return False
        
        # Get build order from config
        build_order = self.config.get_build_order()
        
        self.logger.info(f"Build order: {' -> '.join(build_order)}")
        
        success = True
        for dep_name in build_order:
            self.logger.info(f"[{build_order.index(dep_name)+1}/{len(build_order)}] "
                           f"Building {dep_name}...")
            
            if not self.build_dependency(dep_name, force=force):
                success = False
                if not self.config.get_option("continue_on_error", False):
                    self.logger.error(f"Build failed for {dep_name}, stopping")
                    break
                else:
                    self.logger.warning(f"Build failed for {dep_name}, continuing...")
        
        if success:
            self.logger.success("All dependencies built successfully!")
        else:
            self.logger.error("Some dependencies failed to build")
            
        return success
    
    def build_dependency(self, name: str, force: bool = False) -> bool:
        """
        Build a specific dependency
        
        Args:
            name: Dependency name
            force: Force rebuild even if cached
            
        Returns:
            True if build successful
        """
        self.logger.info(f"Building dependency: {name}")
        
        # Check if dependency exists
        if not self.config.has_dependency(name):
            self.logger.error(f"Unknown dependency: {name}")
            self.logger.info(f"Available: {', '.join(self.config.get_dependencies())}")
            return False
        
        # Check cache
        if not force and self.cache.is_built(name, self.platform, self.arch):
            self.logger.info(f"Dependency {name} already built (cached)")
            
            # Verify it's still valid
            if self.verifier.verify_dependency(name):
                return True
            else:
                self.logger.warning(f"Cached build invalid, rebuilding {name}")
        
        # Clean if requested
        if self.config.get_option("clean_before_build", True):
            self.logger.debug(f"Cleaning {name} build artifacts...")
            self.orchestrator.clean_dependency(name)
        
        # Build
        success = self.orchestrator.build(name)
        
        if success:
            # Update cache
            self.cache.mark_built(name, self.platform, self.arch)
            
            # Verify installation
            if self.config.get_option("verify_after_build", True):
                if not self.verifier.verify_dependency(name):
                    self.logger.warning(f"Verification failed for {name}")
                    success = False
                else:
                    self.logger.success(f"Successfully built and verified {name}")
        else:
            self.logger.error(f"Build failed for {name}")
        
        return success
    
    def clean(self, deps: Optional[List[str]] = None, full: bool = False) -> None:
        """
        Clean build artifacts
        
        Args:
            deps: Specific dependencies to clean (None for all)
            full: Full clean including install directory
        """
        self.logger.info("Cleaning build artifacts...")
        
        if deps is None:
            # Clean all
            self.orchestrator.clean_all()
            self.cache.clear()

            if full:
                self.logger.debug("Clearing object cache...")
                self.object_cache.clear()
                # Remove install directory
                if self.install_dir.exists():
                    self.logger.info(f"Removing install directory: {self.install_dir}")
                    shutil.rmtree(self.install_dir, ignore_errors=True)
        else:
            # Clean specific
            for dep in deps:
                self.orchestrator.clean_dependency(dep)
                self.cache.clear_dependency(dep)
    
    def verify(self, deps: Optional[List[str]] = None) -> bool:
        """
        Verify installation
        
        Args:
            deps: Specific dependencies to verify (None for all)
            
        Returns:
            True if all verifications pass
        """
        self.logger.info("Verifying installation...")
        
        if deps is None:
            return self.verifier.verify_all()
        else:
            success = True
            for dep in deps:
                if not self.verifier.verify_dependency(dep):
                    success = False
            return success
    
    def show_info(self) -> None:
        """Show build system information"""
        from . import __version__
        
        print(f"\nScreamRouter Build System v{__version__}")
        print(f"{'='*50}")
        print(f"Platform: {self.platform} ({self.arch})")
        print(f"Root Directory: {self.root_dir}")
        print(f"Install Directory: {self.install_dir}")
        print(f"\nDependencies ({len(self.config.get_dependencies())}):")
        
        for dep in self.config.get_dependencies():
            # Use ASCII-safe characters for Windows compatibility
            if self.cache.is_built(dep, self.platform, self.arch):
                status = "[OK] Built"
            else:
                status = "[X] Not built"
            print(f"  - {dep:20} {status}")
        
        print(f"\nSupported platforms: {', '.join(self.SUPPORTED_PLATFORMS)}")
        print(f"Note: macOS users should use Docker")


def main():
    """Command-line interface"""
    parser = argparse.ArgumentParser(
        description="ScreamRouter Build System - Clean, modular dependency builder",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s build --all              # Build all dependencies
  %(prog)s build --dep opus         # Build specific dependency
  %(prog)s clean --all              # Clean all build artifacts
  %(prog)s verify --all             # Verify installation
  %(prog)s info                     # Show system information
        """
    )
    
    parser.add_argument(
        "command",
        choices=["build", "clean", "verify", "info"],
        help="Command to execute"
    )
    
    parser.add_argument(
        "--dep",
        action="append",
        help="Specific dependency to process (can be used multiple times)"
    )
    
    parser.add_argument(
        "--all",
        action="store_true",
        help="Process all dependencies"
    )
    
    parser.add_argument(
        "--force",
        action="store_true",
        help="Force rebuild even if cached"
    )
    
    parser.add_argument(
        "--platform",
        choices=["auto", "linux", "windows"],
        default="auto",
        help="Target platform (default: auto-detect)"
    )
    
    parser.add_argument(
        "--arch",
        choices=["auto", "x64", "x86"],
        default="auto",
        help="Target architecture (default: auto-detect)"
    )
    
    parser.add_argument(
        "--install-dir",
        type=Path,
        help="Installation directory for dependencies"
    )
    
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Enable verbose output"
    )
    
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Perform dry run without actual building"
    )
    
    parser.add_argument(
        "--full-clean",
        action="store_true",
        help="Full clean including install directory (use with clean command)"
    )
    
    args = parser.parse_args()
    
    # Validate arguments
    if args.command in ["build", "clean", "verify"]:
        if not args.all and not args.dep:
            parser.error(f"Either --all or --dep must be specified for {args.command}")
    
    # Initialize build system
    try:
        bs = BuildSystem(
            platform=args.platform,
            arch=args.arch,
            install_dir=args.install_dir,
            verbose=args.verbose,
            dry_run=args.dry_run
        )
    except Exception as e:
        print(f"Error initializing build system: {e}", file=sys.stderr)
        sys.exit(1)
    
    # Execute command
    try:
        if args.command == "build":
            if args.all:
                success = bs.build_all(force=args.force)
            else:
                success = all(bs.build_dependency(d, force=args.force) for d in args.dep)
                
            sys.exit(0 if success else 1)
            
        elif args.command == "clean":
            bs.clean(
                deps=args.dep if not args.all else None,
                full=args.full_clean
            )
            
        elif args.command == "verify":
            success = bs.verify(deps=args.dep if not args.all else None)
            sys.exit(0 if success else 1)
            
        elif args.command == "info":
            bs.show_info()
            
    except KeyboardInterrupt:
        print("\nBuild interrupted by user", file=sys.stderr)
        sys.exit(130)
    except Exception as e:
        logging.error(f"Build system error: {e}")
        if args.verbose:
            import traceback
            traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
