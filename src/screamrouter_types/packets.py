"""Classes for webstream API"""
from pydantic import BaseModel

from src.screamrouter_types.annotations import IPAddressType


class WebStreamFrames(BaseModel):
    """Used to pass data from MP3 listening process to Web Stream API"""
    data: bytes
    """MPEG Data"""
    sink_ip: IPAddressType
    """The sink the data is from"""


class InputQueueEntry():
    """A data entry in the ffmpeg input queue, holds the data and the source IP address."""
    tag: str
    """Source IP address for data sent to an input queue"""
    data: bytes
    """Data sent to an input queue"""
    def __init__(self, tag: str, data: bytes):
        self.tag = tag
        self.data = data
