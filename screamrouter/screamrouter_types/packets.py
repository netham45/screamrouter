"""Classes for webstream API"""
from pydantic import BaseModel

from screamrouter.screamrouter_types.annotations import IPAddressType


class WebStreamFrames(BaseModel):
    """Used to pass data from MP3 listening process to Web Stream API"""
    data: bytes
    """MPEG Data"""
    sink_ip: IPAddressType
    """The sink the data is from"""
