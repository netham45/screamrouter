"""Constant variables"""

import importlib.resources
import os
import sys
from pathlib import Path


def _get_base_paths() -> tuple[str, str, str, int]:
    """Get base paths for config, logs, and certs based on OS and user privileges.
    
    Returns:
        tuple: (config_dir, logs_dir, cert_dir, default_api_port)
    """
    is_windows = sys.platform == 'win32'
    is_root = os.geteuid() == 0 if hasattr(os, 'geteuid') else False
    
    if is_windows:
        # Windows: Use APPDATA
        appdata = os.getenv('APPDATA', os.path.expanduser('~\\AppData\\Roaming'))
        base_dir = os.path.join(appdata, 'screamrouter')
        config_dir = base_dir
        logs_dir = os.path.join(base_dir, 'logs')
        cert_dir = os.path.join(base_dir, 'cert')
        default_api_port = 8443  # Non-privileged port for Windows
    elif is_root:
        # Root user: System-wide paths
        config_dir = '/etc/screamrouter'
        logs_dir = '/var/log/screamrouter/logs'
        cert_dir = '/etc/screamrouter/cert'
        default_api_port = 443  # Privileged port for root
    else:
        # Non-root user: ~/.config/screamrouter
        config_base = os.path.expanduser('~/.config/screamrouter')
        config_dir = config_base
        logs_dir = os.path.join(config_base, 'logs')
        cert_dir = os.path.join(config_base, 'cert')
        default_api_port = 8443  # Non-privileged port for non-root
    
    return config_dir, logs_dir, cert_dir, default_api_port


_CONFIG_DIR, _LOGS_DIR, _CERT_DIR, _DEFAULT_API_PORT = _get_base_paths()

# ##########
# User Configurable Options
# ##########

SCREAM_RECEIVER_PORT: int = int(os.getenv("SCREAM_RECEIVER_PORT", "16401"))
"""This is the port to receive Scream data at"""
SCREAM_PER_PROCESS_RECEIVER_PORT: int = int(os.getenv("SCREAM_PER_PROCESS_RECEIVER_PORT", "16402"))
"""This is the port to receive per-process data at"""
RTP_RECEIVER_PORT: int = int(os.getenv("RTP_RECEIVER_PORT", "40000"))
"""This is the port to receive RTP data at"""
SINK_PORT: int = int(os.getenv("SINK_PORT", "4010"))
"""This is the port for a Scream Sink"""
API_PORT: int = int(os.getenv("API_PORT", str(_DEFAULT_API_PORT)))
"""This is the port FastAPI runs on"""
API_HOST: str = os.getenv("API_HOST", "0.0.0.0")
"""This is the host FastAPI binds to"""
LOGS_DIR: str = os.getenv("LOGS_DIR", _LOGS_DIR)
"""This is the directory logs are stored in"""
CONSOLE_LOG_LEVEL: str = os.getenv("CONSOLE_LOG_LEVEL", "INFO")
"""Log level for stdout
   Valid values are "DEBUG", "INFO", "WARNING", "ERROR"."""
LOG_TO_FILE: bool = os.getenv("LOG_TO_FILE", "True").lower() == "true"
"""Determines rather logs are written to files"""
LOG_ENTRIES_TO_RETAIN: int = int(os.getenv("LOG_ENTRIES_TO_RETAIN", "2"))
"""Number of previous runs to retain logs for"""
SHOW_FFMPEG_OUTPUT: bool = os.getenv("SHOW_FFMPEG_OUTPUT", "False").lower() == "true"
"""Show ffmpeg output"""
NPM_REACT_DEBUG_SITE: bool = os.getenv("NPM_REACT_DEBUG_SITE", "False").lower() == "true"
"""Enable to use a locally running npm dev server for the React site"""
CERTIFICATE: str = os.getenv("CERTIFICATE", os.path.join(_CERT_DIR, "cert.pem"))
"""SSL Cert"""
CERTIFICATE_KEY: str = os.getenv("CERTIFICATE_KEY", os.path.join(_CERT_DIR, "privkey.pem"))
"""SSL Cert Key"""
TIMESHIFT_DURATION: int = int(os.getenv("TIMESHIFT_DURATION", "300"))
"""Timeshift duration in seconds."""
CONFIGURATION_RELOAD_TIMEOUT: int = int(os.getenv("CONFIGURATION_RELOAD_TIMEOUT", "3"))
"""Configuration reload timeout in seconds."""
CONFIG_PATH = os.getenv("CONFIG_PATH", os.path.join(_CONFIG_DIR, "config.yaml"))
"""Path to the configuration file"""
EQUALIZER_CONFIG_PATH = os.getenv("EQUALIZER_CONFIG_PATH", os.path.join(_CONFIG_DIR, "equalizers.yaml"))
"""Path to the equalizer configurations file"""


def get_package_data_dir() -> str:
    """Get the directory containing package data files (site/, images/, etc.)"""
    try:
        # Python 3.9+ way - try to get the screamrouter package location
        if hasattr(importlib.resources, 'files'):
            package_root = importlib.resources.files('screamrouter')
            # Check if site directory exists relative to package
            site_path = os.path.join(str(package_root), 'site')
            if os.path.exists(site_path):
                return str(package_root)
            # If not, go up one level (site is at project root in installed package)
            parent_path = os.path.dirname(str(package_root))
            if os.path.exists(os.path.join(parent_path, 'site')):
                return parent_path
        else:
            # Fallback for older Python
            import pkg_resources
            package_root = pkg_resources.resource_filename('screamrouter', '')
            site_path = os.path.join(package_root, 'site')
            if os.path.exists(site_path):
                return package_root
            parent_path = os.path.dirname(package_root)
            if os.path.exists(os.path.join(parent_path, 'site')):
                return parent_path
    except Exception:
        pass
    
    # Fallback to development mode - check current directory
    if os.path.exists('./site'):
        return os.path.abspath('.')
    
    # Last resort - use package location
    import screamrouter
    return os.path.dirname(os.path.abspath(screamrouter.__file__))


SITE_DIR = os.path.join(get_package_data_dir(), 'site')
"""Directory containing the website files"""

UVICORN_LOG_CONFIG_PATH = os.path.join(get_package_data_dir(), 'uvicorn_log_config.yaml')
"""Path to uvicorn log configuration file"""

# ##########
# Internal Options
# ##########

PACKET_DATA_SIZE: int = 1152
"""This is the packet size minus the header"""
PACKET_DATA_SIZE_INT32: int = int(PACKET_DATA_SIZE / 4)
"""This is the number of int32's in a packet"""
PACKET_HEADER_SIZE: int = 5
"""This is the packet header size"""
TAG_MAX_LENGTH: int = 45
"""Max length for an internal/plugin source tag, set to be long enough for a
   full IPv6 address"""
PACKET_SIZE = PACKET_HEADER_SIZE + PACKET_DATA_SIZE
"""This is the total packet size"""
PER_PROCESS_PACKET_SIZE = PACKET_HEADER_SIZE + TAG_MAX_LENGTH + PACKET_DATA_SIZE
"""This is the total packet size for per-process audio"""
MP3_HEADER_LENGTH: int = 4
"""Length of MP3 header"""
WAIT_FOR_CLOSES: bool = False
"""On configuration reload, wait for existing processes to close
   before starting new processes, mostly for testing.
   Configuration changes reload faster when set to False."""
KILL_AT_CLOSE: bool = True
"""Closes quickly but leaves lingering processes, doesn't reload any faster
   than disabling WAIT_FOR_CLOSES."""


# Print configuration when module loads
print("=" * 80)
print("ScreamRouter Configuration:")
print("=" * 80)
print(f"Config Directory:     {_CONFIG_DIR}")
print(f"Logs Directory:       {LOGS_DIR}")
print(f"Certificate Dir:      {_CERT_DIR}")
print(f"API Port:             {API_PORT}")
print(f"API Host:             {API_HOST}")
print(f"Config Path:          {CONFIG_PATH}")
print(f"Equalizer Config:     {EQUALIZER_CONFIG_PATH}")
print(f"Certificate:          {CERTIFICATE}")
print(f"Certificate Key:      {CERTIFICATE_KEY}")
print(f"Site Directory:       {SITE_DIR}")
print("=" * 80)
