"""
OpenSSL-specific builder implementation
"""

import os
import platform
import shutil
from pathlib import Path
from .base_builder import BaseBuilder


class OpenSSLBuilder(BaseBuilder):
    """Special builder for OpenSSL"""
    
    def configure(self) -> bool:
        """Configure OpenSSL build"""
        
        # WORKAROUND: OpenSSL 3.6.0-dev has an empty apps/ directory
        # which causes Configure to fail looking for apps/build.info
        # Create a dummy build.info file to work around this
        apps_dir = self.source_dir / "apps"
        if apps_dir.exists() and not (apps_dir / "build.info").exists():
            self.logger.debug("Creating dummy apps/build.info for OpenSSL 3.6.0-dev")
            (apps_dir / "build.info").touch()
        
        # Determine configure script
        if self.platform == "windows":
            # Need Perl for Windows
            if not shutil.which("perl"):
                self.logger.error("Perl is required to build OpenSSL on Windows")
                self.logger.info("Install Strawberry Perl from https://strawberryperl.com/")
                return False
            
            configure_cmd = ["perl", "Configure"]
        else:
            # Linux - check for cross-compilation first
            cross_target = self._detect_cross_compilation()
            
            if cross_target:
                # When cross-compiling, MUST use Configure with explicit platform target
                # because 'config' auto-detects the build host, not the target
                configure_script = self.source_dir / "Configure"
                self.logger.info(f"Cross-compilation detected, using Configure script")
            else:
                # Native build - prefer 'config' over 'Configure' for auto-detection
                configure_script = self.source_dir / "config"
                if not configure_script.exists():
                    configure_script = self.source_dir / "Configure"
            
            if not configure_script.exists():
                self.logger.error("OpenSSL configure script not found")
                return False
            
            configure_cmd = [str(configure_script)]
        
        # Only add platform target if using Configure (not config)
        # The 'config' script auto-detects the platform
        if self.platform == "linux" and configure_script.name == "Configure":
            platform_targets = self.build_config.get("platform_target", {})
            
            # Check for cross-compilation first
            cross_target = self._detect_cross_compilation()
            if cross_target:
                # Extract architecture from cross-compilation target
                # e.g., aarch64-linux-gnu -> aarch64
                arch = cross_target.split('-')[0].lower()
                self.logger.info(f"OpenSSL: Cross-compiling for {arch}")
                
                # Map to OpenSSL platform target
                if arch in platform_targets:
                    target = platform_targets[arch]
                elif "aarch64" in arch or "arm64" in arch:
                    target = platform_targets.get("aarch64", "linux-aarch64")
                elif "arm" in arch:
                    target = "linux-armv4"  # Generic ARM target
                elif "x86_64" in arch or "amd64" in arch:
                    target = platform_targets.get("x86_64", "linux-x86_64")
                else:
                    target = platform_targets.get("i686", "linux-generic32")
            else:
                # Native build - use build host's architecture
                machine = platform.machine().lower()
                if machine in platform_targets:
                    target = platform_targets[machine]
                elif "x86_64" in machine or "amd64" in machine:
                    target = platform_targets.get("x86_64", "linux-x86_64")
                elif "aarch64" in machine or "arm64" in machine:
                    target = platform_targets.get("aarch64", "linux-aarch64")
                else:
                    target = platform_targets.get("i686", "linux-generic32")
            
            configure_cmd.append(target)
        elif self.platform == "windows":
            platform_targets = self.build_config.get("platform_target", {})
            if self.arch == "x64":
                target = platform_targets.get("x64", "VC-WIN64A")
                self.logger.info(f"OpenSSL: Building for x64 with target {target}")
            else:
                target = platform_targets.get("x86", "VC-WIN32")
                self.logger.info(f"OpenSSL: Building for x86 with target {target}")
            configure_cmd.append(target)
        
        # Add prefix and openssldir
        configure_cmd.extend([
            f"--prefix={self.install_dir}",
            f"--openssldir={self.install_dir}/ssl"
        ])
        
        # On Windows, explicitly set libdir to 'lib' to avoid lib64
        if self.platform == "windows":
            configure_cmd.append("--libdir=lib")
        
        # Add configure arguments
        configure_args = self.build_config.get("configure_args", [])
        for arg in configure_args:
            if "{platform_target}" not in arg:  # Skip platform target placeholder
                configure_cmd.append(self.replace_variables(arg))
        
        # Set environment for Linux
        env = self.env.copy()
        if self.platform == "linux":
            # CRITICAL: OpenSSL's build system spawns subprocesses that ignore
            # the platform target. We MUST explicitly set CC/CXX if cross-compiling.
            cross_target = self._detect_cross_compilation()
            if cross_target and "CC" in os.environ:
                # Force OpenSSL to use our cross-compiler
                env["CC"] = os.environ["CC"]
                if "CXX" in os.environ:
                    env["CXX"] = os.environ["CXX"]
                self.logger.info(f"OpenSSL: Forcing CC={env['CC']}, CXX={env.get('CXX', 'N/A')}")
            
            # Add environment variables from config
            env_vars = self.build_config.get("env_vars", {})
            for key, value in env_vars.items():
                env[key] = self.replace_variables(value)
            
            # Ensure -fPIC is set
            if "-fPIC" not in env.get("CFLAGS", ""):
                env["CFLAGS"] = env.get("CFLAGS", "") + " -fPIC"
            if "-fPIC" not in env.get("CXXFLAGS", ""):
                env["CXXFLAGS"] = env.get("CXXFLAGS", "") + " -fPIC"
        
        # Run configuration
        result = self.run_command(configure_cmd, env=env)
        if result.returncode != 0:
            return False
        
        # WORKAROUND: OpenSSL 3.6.0-dev doesn't generate opensslconf.h
        # Create it manually from configuration.h (which is generated)
        opensslconf_h = self.source_dir / "include/openssl/opensslconf.h"
        configuration_h = self.source_dir / "include/openssl/configuration.h"
        
        if not opensslconf_h.exists() and configuration_h.exists():
            self.logger.debug("Creating opensslconf.h from configuration.h for OpenSSL 3.6.0-dev")
            # opensslconf.h should just include configuration.h
            opensslconf_content = """/* Automatically generated opensslconf.h wrapper */
#ifndef OPENSSL_OPENSSLCONF_H
# define OPENSSL_OPENSSLCONF_H
# pragma once

# include <openssl/configuration.h>

#endif
"""
            if not self.dry_run:
                opensslconf_h.write_text(opensslconf_content)
                self.logger.info(f"Created {opensslconf_h}")
        
        return True
    
    def build(self) -> bool:
        """Build OpenSSL"""
        if self.platform == "windows":
            # Use nmake on Windows
            make_cmd = self.build_config.get("make_command", "nmake")
            cmd = [make_cmd]
        else:
            # Use make on Linux
            make_cmd = self.build_config.get("make_command", "make")
            cmd = [make_cmd, f"-j{os.cpu_count() or 1}"]
            
            # Run depend first for OpenSSL (if specified in make_targets)
            make_targets = self.build_config.get("make_targets", [])
            if "depend" in make_targets:
                self.logger.info("Running make depend...")
                self.run_command([make_cmd, "depend"], check=False)
        
        # Build
        return self.run_command(cmd).returncode == 0
    
    def install(self) -> bool:
        """Install OpenSSL"""
        if self.platform == "windows":
            make_cmd = self.build_config.get("make_command", "nmake")
            install_targets = self.build_config.get("install_targets_win", ["install_sw"])
        else:
            make_cmd = self.build_config.get("make_command", "make")
            install_targets = self.build_config.get("install_targets_unix", ["install_sw"])
        
        # Install each target
        for target in install_targets:
            cmd = [make_cmd, target]
            if self.run_command(cmd).returncode != 0:
                # Some targets might be optional
                if target in ["install_ssldirs"]:
                    self.logger.warning(f"Optional install target '{target}' failed, continuing...")
                else:
                    return False
        
        return True
    
    def clean(self) -> bool:
        """Clean OpenSSL build artifacts"""
        super().clean()
        
        # OpenSSL-specific clean - use distclean to remove ALL cached config
        if (self.source_dir / "Makefile").exists():
            if self.platform == "windows":
                make_cmd = self.build_config.get("make_command", "nmake")
            else:
                make_cmd = self.build_config.get("make_command", "make")
            
            # Use distclean instead of clean to remove configdata.pm and all cached state
            self.run_command([make_cmd, "distclean"], check=False)
        
        # Remove OpenSSL-specific files
        openssl_files = [
            "Makefile",
            "Makefile.bak",
            "configdata.pm",
            "libcrypto.a",
            "libssl.a",
            "libcrypto.so*",
            "libssl.so*",
            "libcrypto.lib",
            "libssl.lib",
            "libcrypto.pc",
            "libssl.pc",
            "openssl.pc"
        ]
        
        for pattern in openssl_files:
            for file in self.source_dir.glob(pattern):
                self.logger.debug(f"Removing {file}")
                if not self.dry_run:
                    file.unlink(missing_ok=True)
        
        return True