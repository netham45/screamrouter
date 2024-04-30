"""Holds post types to be passed from FastAPI"""
from typing import List
from pydantic import BaseModel
from logger import get_logger

logger = get_logger(__name__)

class PostSink(BaseModel):
    """Post data to configure or add a Sink"""
    name: str
    """Sink Name"""
    ip: str
    """Sink IP"""
    port: int
    """Sink Port"""
    bit_depth: int
    """Bit Depth"""
    sample_rate: int
    """Sample Rate"""
    channels: int
    """Channel Count"""
    channel_layout: str
    """Channel Layout"""
    delay: int
    """Delay in ms"""

class PostSinkGroup(BaseModel):
    """Post data to configure or add a Sink Group"""
    name: str
    """Sink Group Name"""
    sinks: List[str]
    """List of names of grouped sinks"""

class PostSource(BaseModel):
    """Post data to configure or add a Source"""
    name: str
    """Source Name"""
    ip: str
    """Source IP"""

class PostSourceGroup(BaseModel):
    """Post data to configure or add a Source Group"""
    name: str
    """Source Group Name"""
    sources: List[str]
    """List of names of grouped Sources"""

class PostRoute(BaseModel):
    """Post data to configure or add a Route"""
    name: str
    """Route Name"""
    source: str
    """Route Source"""
    sink: str
    """Route Sink"""

class PostURL(BaseModel):
    """Post data containing a URL"""
    url: str
    """URL"""
