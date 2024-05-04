"""Constant variables"""

# ##########
# User Configurable Options
# ##########

RECEIVER_PORT: int = 16401
"""This is the default port for the receiver"""
SINK_PORT: int = 4010
"""This is the default port for a Scream Sink"""
API_PORT: int = 8080
"""This is the port FastAPI runs on"""
API_HOST: str = "0.0.0.0"
"""This is the host FastAPI binds to"""
LOGS_DIR: str = "./logs/"
"""This is the directory logs are stored in"""
CONSOLE_LOG_LEVEL: str = "DEBUG"
"""Log level for stdout"""
LOG_TO_FILE: bool = True
"""Determines rather logs are written to files"""
CLEAR_LOGS_ON_RESTART: bool = True
"""Determines rather logs are cleared on restart"""
MP3_STREAM_BITRATE: str = "320k"
"""MP3 stream bitrate for the web API"""

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
SOURCE_INACTIVE_TIME_MS: int = 35
"""Inactive time for a source before it's closed"""
