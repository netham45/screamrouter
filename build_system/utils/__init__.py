"""
Utility modules for the build system
"""

import sys
import json
import logging
import threading
from datetime import datetime
from pathlib import Path
from typing import Optional, Dict, Any, List

from .object_cache import ObjectCache


class ColoredFormatter(logging.Formatter):
    """Colored log formatter for terminal output"""
    
    COLORS = {
        'DEBUG': '\033[36m',    # Cyan
        'INFO': '\033[32m',     # Green
        'WARNING': '\033[33m',  # Yellow
        'ERROR': '\033[31m',    # Red
        'CRITICAL': '\033[35m', # Magenta
        'SUCCESS': '\033[92m',  # Bright Green
        'RESET': '\033[0m'
    }
    
    def format(self, record):
        if hasattr(record, 'raw') and record.raw:
            return record.getMessage()
        
        # Add color for terminal output
        if sys.stdout.isatty():
            levelname = record.levelname
            if levelname == 'SUCCESS':
                levelname = 'INFO'
            
            color = self.COLORS.get(record.levelname, self.COLORS['RESET'])
            reset = self.COLORS['RESET']
            
            record.levelname = f"{color}{record.levelname}{reset}"
            record.msg = f"{color}{record.msg}{reset}"
        
        return super().format(record)


class Logger:
    """Build system logger"""
    
    SUCCESS = 25  # Between INFO and WARNING
    
    def __init__(self, verbose: bool = False, log_file: Optional[str] = None):
        """
        Initialize logger
        
        Args:
            verbose: Enable verbose output
            log_file: Optional log file path
        """
        self.verbose = verbose
        
        # Add SUCCESS level
        logging.addLevelName(self.SUCCESS, "SUCCESS")
        
        # Create logger
        self.logger = logging.getLogger("build_system")
        self.logger.setLevel(logging.DEBUG if verbose else logging.INFO)
        
        # Remove existing handlers
        self.logger.handlers.clear()
        
        # Console handler
        console_handler = logging.StreamHandler(sys.stdout)
        console_handler.setLevel(logging.DEBUG if verbose else logging.INFO)
        
        # Format
        if verbose:
            fmt = "%(asctime)s [%(levelname)s] %(message)s"
        else:
            fmt = "[%(levelname)s] %(message)s"
        
        console_formatter = ColoredFormatter(fmt, datefmt="%H:%M:%S")
        console_handler.setFormatter(console_formatter)
        self.logger.addHandler(console_handler)
        
        # File handler
        if log_file:
            file_handler = logging.FileHandler(log_file)
            file_handler.setLevel(logging.DEBUG)
            file_formatter = logging.Formatter(
                "%(asctime)s [%(levelname)s] %(message)s",
                datefmt="%Y-%m-%d %H:%M:%S"
            )
            file_handler.setFormatter(file_formatter)
            self.logger.addHandler(file_handler)
    
    def debug(self, msg: str):
        """Log debug message"""
        self.logger.debug(msg)
    
    def info(self, msg: str):
        """Log info message"""
        self.logger.info(msg)
    
    def warning(self, msg: str):
        """Log warning message"""
        self.logger.warning(msg)
    
    def error(self, msg: str):
        """Log error message"""
        self.logger.error(msg)
    
    def critical(self, msg: str):
        """Log critical message"""
        self.logger.critical(msg)
    
    def success(self, msg: str):
        """Log success message"""
        self.logger.log(self.SUCCESS, msg)
    
    def raw(self, msg: str):
        """Log raw message without formatting"""
        record = self.logger.makeRecord(
            self.logger.name, logging.INFO, "", 0, msg, (), None
        )
        record.raw = True
        self.logger.handle(record)


class Cache:
    """Build cache management"""
    
    def __init__(self, cache_dir: Path):
        """
        Initialize cache
        
        Args:
            cache_dir: Directory to store cache files
        """
        self.cache_dir = Path(cache_dir)
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        self.cache_file = self.cache_dir / "build_cache.json"
        self._lock = threading.RLock()
        self.cache_data = self._load_cache()
    
    def _load_cache(self) -> Dict[str, Any]:
        """Load cache from file"""
        if self.cache_file.exists():
            try:
                with open(self.cache_file, 'r') as f:
                    return json.load(f)
            except:
                pass
        return {}
    
    def _save_cache(self):
        """Save cache to file"""
        with self._lock:
            with open(self.cache_file, 'w') as f:
                json.dump(self.cache_data, f, indent=2)
    
    def _get_key(self, name: str, platform: str, arch: str) -> str:
        """Get cache key for dependency"""
        return f"{name}_{platform}_{arch}"
    
    def is_built(self, name: str, platform: str, arch: str) -> bool:
        """
        Check if dependency is built
        
        Args:
            name: Dependency name
            platform: Platform name
            arch: Architecture
            
        Returns:
            True if built and cached
        """
        key = self._get_key(name, platform, arch)
        with self._lock:
            return key in self.cache_data and self.cache_data[key].get("built", False)
    
    def mark_built(self, name: str, platform: str, arch: str):
        """
        Mark dependency as built
        
        Args:
            name: Dependency name
            platform: Platform name
            arch: Architecture
        """
        key = self._get_key(name, platform, arch)
        with self._lock:
            self.cache_data[key] = {
                "built": True,
                "timestamp": datetime.now().isoformat(),
                "platform": platform,
                "arch": arch
            }
            self._save_cache()
    
    def clear_dependency(self, name: str):
        """
        Clear cache for specific dependency
        
        Args:
            name: Dependency name
        """
        with self._lock:
            keys_to_remove = [k for k in self.cache_data.keys() if k.startswith(f"{name}_")]
            for key in keys_to_remove:
                del self.cache_data[key]
            self._save_cache()
    
    def clear(self):
        """Clear all cache"""
        with self._lock:
            self.cache_data = {}
            self._save_cache()
    
    def get_info(self, name: str, platform: str, arch: str) -> Optional[Dict[str, Any]]:
        """
        Get cache info for dependency
        
        Args:
            name: Dependency name
            platform: Platform name
            arch: Architecture
            
        Returns:
            Cache info or None
        """
        key = self._get_key(name, platform, arch)
        with self._lock:
            return self.cache_data.get(key)


class Verifier:
    """Build verification utilities"""
    
    def __init__(self, install_dir: Path, config: Any, platform: str, logger: Logger):
        """
        Initialize verifier
        
        Args:
            install_dir: Installation directory
            config: Configuration loader
            platform: Platform name
            logger: Logger instance
        """
        self.install_dir = Path(install_dir)
        self.config = config
        self.platform = platform
        self.logger = logger
    
    def verify_dependency(self, name: str) -> bool:
        """
        Verify a dependency is properly installed
        
        Args:
            name: Dependency name
            
        Returns:
            True if verification passes
        """
        self.logger.debug(f"Verifying {name}...")
        
        try:
            dep_config = self.config.get_dependency_config(name)
        except ValueError:
            self.logger.error(f"Unknown dependency: {name}")
            return False
        
        outputs = dep_config.get("outputs", {})
        
        # Check libraries
        libs = outputs.get("libraries", {}).get(self.platform, [])
        for lib in libs:
            lib_paths = [
                self.install_dir / "lib" / lib,
                self.install_dir / "lib64" / lib,
            ]
            
            found = False
            for lib_path in lib_paths:
                if lib_path.exists():
                    self.logger.debug(f"  Found library: {lib_path}")
                    found = True
                    break
            
            if not found:
                self.logger.error(f"  Library not found: {lib}")
                return False
        
        # Check headers
        headers = outputs.get("headers", [])
        for header in headers:
            if "*" in header:
                # Glob pattern
                header_files = list(self.install_dir.glob(f"include/{header}"))
                if not header_files:
                    self.logger.error(f"  No headers found matching: {header}")
                    return False
                else:
                    self.logger.debug(f"  Found {len(header_files)} headers matching {header}")
            else:
                header_path = self.install_dir / "include" / header
                if not header_path.exists():
                    self.logger.error(f"  Header not found: {header}")
                    return False
                else:
                    self.logger.debug(f"  Found header: {header_path}")
        
        self.logger.success(f"Verification passed for {name}")
        return True
    
    def verify_all(self) -> bool:
        """
        Verify all dependencies
        
        Returns:
            True if all verifications pass
        """
        deps = self.config.get_dependencies()
        success = True
        
        for dep in deps:
            if not self.verify_dependency(dep):
                success = False
        
        return success
    
    def get_missing_files(self, name: str) -> List[str]:
        """
        Get list of missing files for a dependency
        
        Args:
            name: Dependency name
            
        Returns:
            List of missing file paths
        """
        missing = []
        
        try:
            dep_config = self.config.get_dependency_config(name)
        except ValueError:
            return [f"Unknown dependency: {name}"]
        
        outputs = dep_config.get("outputs", {})
        
        # Check libraries
        libs = outputs.get("libraries", {}).get(self.platform, [])
        for lib in libs:
            lib_paths = [
                self.install_dir / "lib" / lib,
                self.install_dir / "lib64" / lib,
            ]
            
            if not any(p.exists() for p in lib_paths):
                missing.append(f"lib/{lib}")
        
        # Check headers
        headers = outputs.get("headers", [])
        for header in headers:
            if "*" not in header:
                header_path = self.install_dir / "include" / header
                if not header_path.exists():
                    missing.append(f"include/{header}")
        
        return missing


__all__ = ["Logger", "Cache", "Verifier", "ObjectCache"]
