import ipaddress
from typing import List
from pydantic import BaseModel
from mixer.stream_info import StreamInfo

def verify_ip(ip: str) -> None:
    """Verifies an ip address can be parsed correctly"""
    try:
        ipaddress.ip_address(ip)  # Verify IP address is formatted right
    except ValueError:
        raise ValueError(f"Invalid IP address {ip}")
    
def verify_port(port: int) -> None:
    """Verifies a port is between 1 and 65535"""
    if port < 1 or port > 65535:
        raise ValueError(f"Invalid port {port}")

def verify_name(name: str) -> None:
    """Verifies a name is non-blank"""
    if len(name) == 0:
        raise ValueError(f"Invalid name (Blank)")

def verify_volume(volume: float) -> None:
    """Verifies a volume is between 0 and 1"""
    if volume < 0 or volume > 1:
        raise ValueError(f"Invalid Volume {volume} is not between 0 and 1")
    
def verify_sample_rate(sample_rate: int) -> None:
    """Verifies a sample rate has a base of 44.1 or 48 kHz"""
    if sample_rate % 44100 != 0 and sample_rate % 48000 != 0:
        raise ValueError("Unknown sample rate base")
    
def verify_bit_depth(bit_depth: int) -> None:
    """Verifies bit depth is 16, 24, or 32"""
    if bit_depth == 24:
        print("WARNING: Using 24-bit depth is not recommended.")
    if bit_depth != 16 and bit_depth != 24 and bit_depth != 32:
        raise ValueError("Invalid Bit Depth")
    
def verify_channels(channels: int) -> None:
    """Verifies the channel count is between 1 and 8"""
    if channels < 1 or channels > 8:
        raise ValueError("Invalid Channel Count")
    
def verify_channel_layout(channel_layout: str) -> None:
    """Verifies the channel layout string is known"""
    for layout in StreamInfo.CHANNEL_LAYOUT_TABLE.values():
        if layout == channel_layout:
            return
    raise ValueError("Invalid Channel Layout")
    
class InUseException(Exception):
    """Called when removal is attempted of something that is in use"""
    def __init__(self, message: str):
        super().__init__(self, message)

class SinkDescription(BaseModel): 
    """
    Holds either a sink IP and Port or a group of sink names
    """
    name: str
    """Sink Name"""
    ip: str
    """Sink IP"""
    port: int
    """Sink port number"""
    is_group: bool
    """Sink Is Group"""
    enabled: bool
    """Sink is Enabled"""
    group_members: List[str]
    """Sink Group Members"""
    volume: float
    """Holds the volume for the sink (0.0-1.0)"""
    bit_depth: int
    """Sink Bit depth"""
    sample_rate: int
    """Sink Sample Rate"""
    channels: int
    """Sink Channels Rate"""
    channel_layout: str
    """Sink Channel Layout"""
    def __init__(self, name: str, ip: str, port: int, is_group: bool, enabled: bool, group_members: List[str], volume: float, bit_depth: int = 32, sample_rate: int = 48000, channels: int = 2, channel_layout: str = "stereo"):
        if not is_group:
            verify_ip(ip)
            verify_port(port)
        verify_name(name)
        verify_volume(volume)
        verify_sample_rate(sample_rate)
        verify_bit_depth(bit_depth)
        verify_channels(channels)
        verify_channel_layout(channel_layout)
        super().__init__(name = name,
                         ip = ip,
                         port = port,
                         is_group = is_group,
                         enabled = enabled,
                         group_members = group_members,
                         volume = volume,
                         bit_depth = bit_depth,
                         sample_rate = sample_rate,
                         channels = channels,
                         channel_layout = channel_layout)

    def set_volume(self, volume: float):
        """Verifies volume then sets i"""
        verify_volume(volume)
        self.volume = volume

class SourceDescription(BaseModel):
    """
    Holds either a source IP or a group of source names
    """
    name: str
    """Source Name"""
    ip: str
    """Source IP"""
    is_group: bool
    """Source Is Group"""
    enabled: bool
    """Source Enabled"""
    group_members: List[str]
    """"Source Group Members"""
    volume: float
    """Holds the volume for the source  (0.0-1.0)"""
    def __init__(self, name: str, ip: str, is_group: bool, enabled: bool, group_members: List[str], volume: float):
        if not is_group:
            verify_ip(ip)
        verify_name(name)
        verify_volume(volume)
        super().__init__(name = name, ip = ip, is_group = is_group, enabled = enabled, group_members = group_members, volume = volume)

    def set_volume(self, volume: float):
        """Verifies volume then sets i"""
        verify_volume(volume)
        self.volume = volume

class RouteDescription(BaseModel):
    """
    Holds a route mapping from source to sink
    """
    name: str
    """Route Name"""
    sink: str
    """Route Sink"""
    source: str
    """Route Source"""
    enabled: bool
    """Route Enabled"""
    volume: float
    """Route volume (0.0-1.0)"""
    def __init__(self, name: str, sink: str, source: str, enabled: bool, volume: float):
        verify_name(name)
        verify_volume(volume)
        super().__init__(name = name, sink = sink, source = source, enabled = enabled, volume = volume)

    def set_volume(self, volume: float):
        """Verifies volume then sets i"""
        verify_volume(volume)
        self.volume = volume
