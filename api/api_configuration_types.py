from typing import List
from pydantic import BaseModel
class PostSink(BaseModel):
    name: str
    """Sink Name"""
    ip: str
    """Sink IP"""
    port: int
    """Sink Port"""

class PostSinkGroup(BaseModel):
    name: str
    """Sink Group Name"""
    sinks: List[str]
    """List of names of grouped sinks"""

class PostSource(BaseModel):
    name: str
    """Source Name"""
    ip: str
    """Source IP"""

class PostSourceGroup(BaseModel):
    name: str
    """Source Group Name"""
    sources: List[str]
    """List of names of grouped Sources"""

class PostRoute(BaseModel):
    name: str
    """Route Name"""
    source: str
    """Route Source"""
    sink: str
    """Route Sink"""