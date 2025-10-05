"""
Build orchestrator that manages the build process
"""

import shutil
from pathlib import Path
from typing import Dict, Any, Optional

from .base_builder import BaseBuilder
from .cmake_builder import CMakeBuilder
from .autotools_builder import AutotoolsBuilder
from .openssl_builder import OpenSSLBuilder
from .nmake_builder import NMakeBuilder


class BuildOrchestrator:
    """Orchestrates the build process for all dependencies"""
    
    # Map build systems to builder classes
    BUILDER_MAP = {
        "cmake": CMakeBuilder,
        "autotools": AutotoolsBuilder,
        "configure": OpenSSLBuilder,  # OpenSSL uses special Configure script
        "nmake": NMakeBuilder,
    }
    
    def __init__(self,
                 config: Any,
                 platform: str,
                 arch: str,
                 install_dir: Path,
                 cache: Any,
                 logger: Any,
                 dry_run: bool = False):
        """
        Initialize build orchestrator
        
        Args:
            config: Configuration loader
            platform: Target platform
            arch: Target architecture
            install_dir: Installation directory
            cache: Cache manager
            logger: Logger instance
            dry_run: If True, don't actually build
        """
        self.config = config
        self.platform = platform
        self.arch = arch
        self.install_dir = Path(install_dir)
        self.cache = cache
        self.logger = logger
        self.dry_run = dry_run
        
        # Create install directories
        self.install_dir.mkdir(parents=True, exist_ok=True)
        (self.install_dir / "lib").mkdir(exist_ok=True)
        (self.install_dir / "lib64").mkdir(exist_ok=True)
        (self.install_dir / "include").mkdir(exist_ok=True)
        (self.install_dir / "bin").mkdir(exist_ok=True)
    
    def get_builder(self, name: str) -> BaseBuilder:
        """
        Get appropriate builder for a dependency
        
        Args:
            name: Dependency name
            
        Returns:
            Builder instance
        """
        dep_config = self.config.get_dependency_config(name)
        
        # Get build configuration for platform
        build_configs = dep_config.get("build_configs", {})
        if "all" in build_configs:
            build_config = build_configs["all"]
        elif self.platform in build_configs:
            build_config = build_configs[self.platform]
        else:
            raise ValueError(f"No build configuration for {self.platform} in {name}")
        
        # Determine build system
        build_system = build_config.get("build_system")
        if not build_system:
            raise ValueError(f"No build system specified for {name}")
        
        # Get builder class
        builder_class = self.BUILDER_MAP.get(build_system)
        if not builder_class:
            raise ValueError(f"Unknown build system: {build_system}")
        
        # Create builder instance
        return builder_class(
            name=name,
            config=dep_config,
            platform=self.platform,
            arch=self.arch,
            install_dir=self.install_dir,
            logger=self.logger,
            dry_run=self.dry_run
        )
    
    def build(self, name: str) -> bool:
        """
        Build a dependency
        
        Args:
            name: Dependency name
            
        Returns:
            True if build successful
        """
        try:
            # Check dependencies first
            dep_deps = self.config.get_dependency_dependencies(name)
            for dep_dep in dep_deps:
                if not self.cache.is_built(dep_dep, self.platform, self.arch):
                    self.logger.warning(f"{name} depends on {dep_dep}, which is not built")
                    self.logger.info(f"Building {dep_dep} first...")
                    if not self.build(dep_dep):
                        self.logger.error(f"Failed to build dependency {dep_dep}")
                        return False
            
            # Get builder
            builder = self.get_builder(name)
            
            # Execute build
            return builder.execute()
            
        except Exception as e:
            self.logger.error(f"Failed to build {name}: {e}")
            if self.logger.verbose:
                import traceback
                traceback.print_exc()
            return False
    
    def clean_dependency(self, name: str) -> bool:
        """
        Clean a specific dependency
        
        Args:
            name: Dependency name
            
        Returns:
            True if clean successful
        """
        try:
            builder = self.get_builder(name)
            return builder.clean()
        except Exception as e:
            self.logger.error(f"Failed to clean {name}: {e}")
            return False
    
    def clean_all(self) -> bool:
        """
        Clean all dependencies
        
        Returns:
            True if all cleans successful
        """
        success = True
        for name in self.config.get_dependencies():
            if not self.clean_dependency(name):
                success = False
        return success
    
    def verify_dependency(self, name: str) -> bool:
        """
        Verify a dependency is properly installed
        
        Args:
            name: Dependency name
            
        Returns:
            True if verification passes
        """
        try:
            builder = self.get_builder(name)
            return builder.verify()
        except Exception as e:
            self.logger.error(f"Failed to verify {name}: {e}")
            return False
    
    def get_build_info(self, name: str) -> Dict[str, Any]:
        """
        Get build information for a dependency
        
        Args:
            name: Dependency name
            
        Returns:
            Dictionary with build information
        """
        dep_config = self.config.get_dependency_config(name)
        
        # Get build configuration for platform
        build_configs = dep_config.get("build_configs", {})
        if "all" in build_configs:
            build_config = build_configs["all"]
        elif self.platform in build_configs:
            build_config = build_configs[self.platform]
        else:
            build_config = {}
        
        return {
            "name": name,
            "version": dep_config.get("version", "unknown"),
            "source_dir": dep_config.get("source_dir", ""),
            "build_system": build_config.get("build_system", "unknown"),
            "platform": self.platform,
            "arch": self.arch,
            "install_dir": str(self.install_dir),
            "dependencies": dep_config.get("dependencies", []),
            "outputs": dep_config.get("outputs", {}),
        }