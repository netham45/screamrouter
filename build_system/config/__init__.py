"""
Configuration management for the build system
"""

import yaml
from pathlib import Path
from typing import Dict, List, Any, Optional


class ConfigLoader:
    """Loads and manages build system configuration"""
    
    def __init__(self, config_dir: Path):
        """
        Initialize configuration loader
        
        Args:
            config_dir: Directory containing configuration files
        """
        self.config_dir = Path(config_dir)
        
        # Load dependencies configuration
        deps_file = self.config_dir / "dependencies.yaml"
        if not deps_file.exists():
            raise FileNotFoundError(f"Dependencies config not found: {deps_file}")
        
        with open(deps_file, 'r') as f:
            self.deps_config = yaml.safe_load(f)
        
        # Load platforms configuration
        platforms_file = self.config_dir / "platforms.yaml"
        if not platforms_file.exists():
            raise FileNotFoundError(f"Platforms config not found: {platforms_file}")
        
        with open(platforms_file, 'r') as f:
            self.platforms_config = yaml.safe_load(f)
    
    def get_dependencies(self) -> List[str]:
        """Get list of all dependencies"""
        return list(self.deps_config.get("dependencies", {}).keys())
    
    def get_dependency_config(self, name: str) -> Dict[str, Any]:
        """
        Get configuration for a specific dependency
        
        Args:
            name: Dependency name
            
        Returns:
            Dependency configuration dictionary
        """
        deps = self.deps_config.get("dependencies", {})
        if name not in deps:
            raise ValueError(f"Unknown dependency: {name}")
        return deps[name]
    
    def has_dependency(self, name: str) -> bool:
        """Check if dependency exists"""
        return name in self.deps_config.get("dependencies", {})
    
    def get_build_order(self) -> List[str]:
        """Get build order for dependencies"""
        return self.deps_config.get("build_order", self.get_dependencies())
    
    def get_platform_config(self, platform: str) -> Dict[str, Any]:
        """
        Get configuration for a specific platform
        
        Args:
            platform: Platform name (linux, windows)
            
        Returns:
            Platform configuration dictionary
        """
        platforms = self.platforms_config.get("platforms", {})
        if platform not in platforms:
            raise ValueError(f"Unknown platform: {platform}")
        return platforms[platform]
    
    def get_architecture_config(self, arch: str) -> Dict[str, Any]:
        """
        Get configuration for a specific architecture
        
        Args:
            arch: Architecture name
            
        Returns:
            Architecture configuration dictionary
        """
        architectures = self.platforms_config.get("architectures", {})
        
        # Check direct match
        if arch in architectures:
            return architectures[arch]
        
        # Check aliases
        for arch_name, arch_config in architectures.items():
            if arch in arch_config.get("aliases", []):
                return arch_config
        
        # Default
        return {"bits": 64 if "64" in arch else 32}
    
    def get_option(self, key: str, default: Any = None) -> Any:
        """
        Get a build option
        
        Args:
            key: Option key
            default: Default value if not found
            
        Returns:
            Option value
        """
        options = self.deps_config.get("build_options", {})
        return options.get(key, default)
    
    def get_dependency_dependencies(self, name: str) -> List[str]:
        """
        Get dependencies of a dependency
        
        Args:
            name: Dependency name
            
        Returns:
            List of dependency names
        """
        config = self.get_dependency_config(name)
        return config.get("dependencies", [])
    
    def get_all_configs(self) -> Dict[str, Any]:
        """Get all configuration data"""
        return {
            "dependencies": self.deps_config,
            "platforms": self.platforms_config
        }


__all__ = ["ConfigLoader"]