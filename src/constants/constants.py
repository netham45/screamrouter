"""Constant variables"""

# ##########
# User Configurable Options
# ##########

SCREAM_RECEIVER_PORT: int = 16401
"""This is the port to receive Scream data at"""
RTP_RECEIVER_PORT: int = 40000
"""This is the port to receive RTP data at"""
SINK_PORT: int = 4010
"""This is the port for a Scream Sink"""
API_PORT: int = 443
"""This is the port FastAPI runs on"""
API_HOST: str = "0.0.0.0"
"""This is the host FastAPI binds to"""
MP3_STREAM_BITRATE: str = "320k"
"""MP3 stream bitrate for the web API"""
MP3_STREAM_SAMPLERATE: int = 48000
"""MP3 stream sample for the web API"""
LOGS_DIR: str = "./logs/"
"""This is the directory logs are stored in"""
CONSOLE_LOG_LEVEL: str = "DEBUG"
"""Log level for stdout. Log level for stdout
   Valid values are "DEBUG", "INFO", "WARNING", "ERROR"."""
LOG_TO_FILE: bool = True
"""Determines rather logs are written to files"""
LOG_ENTRIES_TO_RETAIN: int = 2
"""Number of previous runs to retain logs for"""
SHOW_FFMPEG_OUTPUT: bool = False
"""Show ffmpeg output"""
DEBUG_MULTIPROCESSING: bool = False
"""Debugs Multiprocessing to stdout."""
SOURCE_INACTIVE_TIME_MS: int = 350
"""Inactive time for a source before it's closed. 
   Some plugins may need this raised.
   If this is too long there will be gaps when a source stops sending."""
CERTIFICATE: str = "/root/screamrouter/cert/cert.pem"
"""SSL Cert"""
CERTIFICATE_KEY: str = "/root/screamrouter/cert/privkey.pem"
"""SSL Cert Key"""
SYNCED_TIME_BUFFER: int = 5
"""Number of ms that synced players play out in the future
   negative delays can be up to this amount"""

# ##########
# Internal Options
# ##########

PACKET_DATA_SIZE: int = 1152
"""This is the packet size minus the header"""
PACKET_DATA_SIZE_INT32: int = int(PACKET_DATA_SIZE / 4)
"""This is the number of int32's in a packet"""
PACKET_HEADER_SIZE: int = 5
"""This is the packet header size"""
PACKET_SIZE = PACKET_HEADER_SIZE + PACKET_DATA_SIZE
"""This is the total packet size"""
MP3_HEADER_LENGTH: int = 4
"""Length of MP3 header"""
WAIT_FOR_CLOSES: bool = False
"""On configuration reload, wait for existing processes to close
   before starting new processes, mostly for testing.
   Configuration changes reload faster when set to False."""
KILL_AT_CLOSE: bool = False
"""Closes quickly but leaves lingering processes, doesn't reload any faster
   than disabling WAIT_FOR_CLOSES."""
TAG_MAX_LENGTH: int = 45
"""Max length for an internal/plugin source tag, set to be long enough for a
   full IPv6 address"""
