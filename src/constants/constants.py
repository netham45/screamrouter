"""Constant variables"""

import os

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
API_PORT: int = int(os.getenv("API_PORT", "443"))
"""This is the port FastAPI runs on"""
API_HOST: str = os.getenv("API_HOST", "0.0.0.0")
"""This is the host FastAPI binds to"""
LOGS_DIR: str = os.getenv("LOGS_DIR", "./logs/")
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
CERTIFICATE: str = os.getenv("CERTIFICATE", "cert/cert.pem")
"""SSL Cert"""
CERTIFICATE_KEY: str = os.getenv("CERTIFICATE_KEY", "cert/privkey.pem")
"""SSL Cert Key"""
TIMESHIFT_DURATION: int = int(os.getenv("TIMESHIFT_DURATION", "300"))
"""Timeshift duration in seconds."""
CONFIGURATION_RELOAD_TIMEOUT: int = int(os.getenv("CONFIGURATION_RELOAD_TIMEOUT", "3"))
"""Configuration reload timeout in seconds."""
CONFIG_PATH = os.path.join(os.getcwd(), os.getenv("CONFIG_PATH", "config.yaml"))
"""Path to the configuration file"""
EQUALIZER_CONFIG_PATH = os.path.join(os.getcwd(),
                                     os.getenv("EQUALIZER_CONFIG_PATH", "equalizers.yaml"))
"""Path to the equalizer configurations file"""

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
