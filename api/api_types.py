"""Holds post types to be passed from FastAPI"""
from typing import List
from pydantic import BaseModel
from logger import get_logger

logger = get_logger(__name__)

class Equalizer(BaseModel):
    """Holds data for the equalizer for a sink""" 
    b1: float
    """Set 65Hz band gain."""
    b2: float
    """Set 92Hz band gain."""
    b3: float
    """Set 131Hz band gain."""
    b4: float
    """Set 185Hz band gain.`"""
    b5: float
    """Set 262Hz band gain."""
    b6: float
    """Set 370Hz band gain."""
    b7: float
    """Set 523Hz band gain."""
    b8: float
    """Set 740Hz band gain."""
    b9: float
    """Set 1047Hz band gain."""
    b10: float
    """Set 1480Hz band gain."""
    b11: float
    """Set 2093Hz band gain."""
    b12: float
    """Set 2960Hz band gain."""
    b13: float
    """Set 4186Hz band gain."""
    b14: float
    """Set 5920Hz band gain."""
    b15: float
    """Set 8372Hz band gain."""
    b16: float
    """Set 11840Hz band gain."""
    b17: float
    """Set 16744Hz band gain."""
    b18: float
    """Set 20000Hz band gain."""

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
    equalizer: Equalizer
    """Equalizer"""


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
