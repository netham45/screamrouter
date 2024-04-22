from typing import List
from pydantic import BaseModel

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
        super().__init__(name = name, ip = ip, port = port, is_group = is_group, enabled = enabled, group_members = group_members, volume = volume)

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
        super().__init__(name = name, ip = ip, is_group = is_group, enabled = enabled, group_members = group_members, volume = volume)

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
        super().__init__(name = name, sink = sink, source = source, enabled = enabled, volume = volume)
