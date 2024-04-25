import ipaddress
from typing import List
from pydantic import BaseModel

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
    volume: float = 1
    """Holds the volume for the sink"""
    def __init__(self, name: str, ip: str, port: int, is_group: bool, enabled: bool, group_members: List[str],  volume: float):
        if not is_group:
            verify_ip(ip)
            verify_port(port)
        verify_name(name)
        verify_volume(volume)
        super().__init__(name = name, ip = ip, port = port, is_group = is_group, enabled = enabled, group_members = group_members, volume = volume)

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
    volume: float = 1
    """Holds the volume for the source"""
    def __init__(self, name: str, ip: str, is_group: bool, enabled: bool, group_members: List[str], volume: float):
        print(f"Adding source: {name} {ip} {is_group} {enabled} {group_members} {volume}")
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
    volume: float = 1
    """Route volume"""
    def __init__(self, name: str, sink: str, source: str, enabled: bool, volume: float):
        verify_name(name)
        verify_volume(volume)
        super().__init__(name = name, sink = sink, source = source, enabled = enabled, volume = volume)

    def set_volume(self, volume: float):
        """Verifies volume then sets i"""
        verify_volume(volume)
        self.volume = volume
