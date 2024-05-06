"""Constant variables"""

# ##########
# User Configurable Options
# ##########

RECEIVER_PORT: int = 16401
"""This is the port for the receiver"""
SINK_PORT: int = 4010
"""This is the port for a Scream Sink"""
API_PORT: int = 8080
"""This is the port FastAPI runs on"""
API_HOST: str = "0.0.0.0"
"""This is the host FastAPI binds to"""
MP3_STREAM_BITRATE: str = "320k"
"""MP3 stream bitrate for the web API"""
MP3_STREAM_SAMPLERATE: int = 48000
"""MP3 stream sample for the web API"""
LOGS_DIR: str = "./logs/"
"""This is the directory logs are stored in"""
CONSOLE_LOG_LEVEL: str = "INFO"
"""Log level for stdout"""
LOG_TO_FILE: bool = True
"""Determines rather logs are written to files"""
CLEAR_LOGS_ON_RESTART: bool = True
"""Determines rather logs are cleared on restart"""
DEBUG_MULTIPROCESSING: bool = False
"""Debugs Multiprocessing to stdout."""
SHOW_FFMPEG_OUTPUT: bool = False
"""Show ffmpeg output to stdout."""

# ##########
# Internal Options
# ##########

PACKET_DATA_SIZE: int = 1152
"""This is the packet size minus the header"""
PACKET_HEADER_SIZE: int = 5
"""This is the packet header size"""
PACKET_SIZE = PACKET_HEADER_SIZE + PACKET_DATA_SIZE
"""This is the total packet size"""
INPUT_BUFFER_SIZE: int = PACKET_DATA_SIZE * 64
"""-bufsize used for ffmpeg input"""
MP3_HEADER_LENGTH: int = 4
"""Length of MP3 header"""
SOURCE_INACTIVE_TIME_MS: int = 350
"""Inactive time for a source before it's closed"""
WAIT_FOR_CLOSES: bool = False
"""On configuration reload, wait for existing processes to close
   before starting new processes, mostly for testing.
   Configuration changes reload faster when set to False."""
KILL_AT_CLOSE: bool = False
"""Closes quickly but leaves lingering processes, doesn't reload any faster
   than disabling WAIT_FOR_CLOSES."""
