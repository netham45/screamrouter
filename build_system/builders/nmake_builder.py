"""
NMake builder for Windows MSVC projects
"""

import os
import shutil
from pathlib import Path
from .base_builder import BaseBuilder


class NMakeBuilder(BaseBuilder):
    """Builder for NMake-based projects (Windows)"""
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        
        if self.platform != "windows":
            raise ValueError("NMake builder only supports Windows")
    
    def configure(self) -> bool:
        """NMake doesn't need configuration"""
        # NMake projects typically don't have a configure step
        return True
    
    def build(self) -> bool:
        """Build using nmake"""
        # Get makefile path
        makefile_rel = self.build_config.get("makefile", "Makefile.MSVC")
        makefile = self.source_dir / makefile_rel
        
        if not makefile.exists():
            self.logger.error(f"Makefile not found: {makefile}")
            return False
        
        # Build nmake command
        cmd = ["nmake", "/f", str(makefile)]
        
        # Add nmake arguments
        nmake_args = self.build_config.get("nmake_args", [])
        cmd.extend(nmake_args)
        
        # Add target if specified
        target = self.build_config.get("nmake_target")
        if target:
            cmd.append(target)
        
        return self.run_command(cmd).returncode == 0
    
    def install(self) -> bool:
        """Install NMake build outputs"""
        # NMake projects typically don't have install targets
        # We need to manually copy files
        
        # Create lib directory
        lib_dir = self.install_dir / "lib"
        lib_dir.mkdir(parents=True, exist_ok=True)
        
        # Copy libraries
        output_files = self.build_config.get("output_files", [])
        lib_copied = False
        
        for output_pattern in output_files:
            for file in self.source_dir.glob(output_pattern):
                if file.exists():
                    # Determine destination name
                    outputs = self.config.get("outputs", {})
                    win_libs = outputs.get("libraries", {}).get("windows", [])
                    
                    if win_libs:
                        # Use the first library name from outputs
                        dest_name = win_libs[0]
                    else:
                        # Use original name
                        dest_name = file.name
                    
                    dest = lib_dir / dest_name
                    
                    self.logger.debug(f"Copying {file} to {dest}")
                    if not self.dry_run:
                        shutil.copy2(file, dest)
                    lib_copied = True
                    break  # Copy only the first matching file
        
        if not lib_copied:
            self.logger.error(f"No output files found for {self.name}")
            return False
        
        # Copy headers
        headers_dir = self.build_config.get("headers_dir", "include")
        src_headers = self.source_dir / headers_dir
        
        if src_headers.exists():
            # Create include directory with dependency name
            dest_headers = self.install_dir / "include" / self.name
            dest_headers.mkdir(parents=True, exist_ok=True)
            
            self.logger.debug(f"Copying headers from {src_headers} to {dest_headers}")
            if not self.dry_run:
                # Copy all header files
                for header_file in src_headers.glob("*.h"):
                    shutil.copy2(header_file, dest_headers)
                
                # Also copy any subdirectories
                for subdir in src_headers.iterdir():
                    if subdir.is_dir():
                        dest_subdir = dest_headers / subdir.name
                        if dest_subdir.exists():
                            shutil.rmtree(dest_subdir)
                        shutil.copytree(subdir, dest_subdir)
        
        return True
    
    def clean(self) -> bool:
        """Clean NMake build artifacts"""
        super().clean()
        
        # Try nmake clean if available
        makefile_rel = self.build_config.get("makefile", "Makefile.MSVC")
        makefile = self.source_dir / makefile_rel
        
        if makefile.exists():
            self.run_command(["nmake", "/f", str(makefile), "clean"], check=False)
        
        # Remove common Windows build artifacts
        patterns = ["*.obj", "*.lib", "*.dll", "*.exp", "*.pdb", "*.ilk"]
        for pattern in patterns:
            for file in self.source_dir.rglob(pattern):
                self.logger.debug(f"Removing {file}")
                if not self.dry_run:
                    file.unlink(missing_ok=True)
        
        # Remove output directory if it exists
        output_dir = self.source_dir / "output"
        if output_dir.exists():
            self.logger.debug(f"Removing output directory: {output_dir}")
            if not self.dry_run:
                shutil.rmtree(output_dir, ignore_errors=True)
        
        return True