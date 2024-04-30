"""Types used by the configuration controller"""
from typing import List
from pydantic import BaseModel
from configuration.type_verification import verify_volume, verify_bit_depth, verify_channel_layout
from configuration.type_verification import verify_channels, verify_ip, verify_name
from configuration.type_verification import verify_port, verify_sample_rate

from logger import get_logger

logger = get_logger(__name__)

class InUseError(Exception):
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
    def __init__(self, name: str, ip: str,
                 port: int, is_group: bool,
                 enabled: bool, group_members: List[str],
                 volume: float, bit_depth: int = 32,
                 sample_rate: int = 48000, channels: int = 2,
                 channel_layout: str = "stereo"):
        if not isinstance(channel_layout, str):
            channel_layout = str(channel_layout)
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
    def __init__(self, name: str, ip: str,
                 is_group: bool, enabled: bool,
                 group_members: List[str],
                 volume: float):
        if not is_group:
            verify_ip(ip)
        verify_name(name)
        verify_volume(volume)
        super().__init__(name = name, ip = ip,
                         is_group = is_group, enabled = enabled,
                         group_members = group_members, volume = volume)

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
    def __init__(self, name: str, sink: str,
                 source: str, enabled: bool,
                 volume: float):
        verify_name(name)
        verify_volume(volume)
        super().__init__(name = name, sink = sink,
                         source = source, enabled = enabled,
                         volume = volume)

    def set_volume(self, volume: float):
        """Verifies volume then sets i"""
        verify_volume(volume)
        self.volume = volume
