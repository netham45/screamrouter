"""Holds all the custom types ScreamRouter uses with Pydantic"""
from copy import copy
from typing import Annotated, List, Literal, Optional
from fastapi import Path
from pydantic import AnyUrl, BaseModel, IPvAnyAddress

PACKET_SIZE: int = 1152

VolumeType = Annotated[
    float,
    Path(
        description="Volume. Float, must be between 0 and 1. 1 is no change, 0 is silent.",
        json_schema_extra={"example": ".65"},
        le=1,
        ge=0
    )
]
"""Volume, 0.0-1.0"""

DelayType = Annotated[
    int,
    Path(
        description="Delay in ms. Int, must be between 0 and 5000.",
        json_schema_extra={"example": "180"},
        le=5000,
        ge=0
    )
]
"""Delay, 0-5000ms"""

PlaybackURLType = Annotated[
    AnyUrl,
    Path(
        description="URL for playback.",
        json_schema_extra={"example": "http://www.example.com/cowbell.wav"},
    )
]
"""Playback URL Type"""

SampleRateType = Annotated[
    Literal[44100, 48000, 88200, 96000, 192000],
    Path(
        description="Sample Rate, must be a valid choice.",
        json_schema_extra={"example": "48000"},
    )
]
"""Sample Rate, 44100, 48000, 88200, 96000, or 192000"""

BitDepthType = Annotated[
    Literal[16, 24, 32],
    Path(
        description="Bit Depth, must be a valid choice",
        json_schema_extra={"example": "32"},
    )
]
"""Bit Depth, 16, 24, or 32"""

EqualizerBandType1 = Annotated[
    float,
    Path(
        description="65Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 1, 65Hz"""

EqualizerBandType2 = Annotated[
    float,
    Path(
        description="92Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 2, 92Hz"""

EqualizerBandType3 = Annotated[
    float,
    Path(
        description="131Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 3, 131Hz"""

EqualizerBandType4 = Annotated[
    float,
    Path(
        description="185Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 4, 185Hz"""

EqualizerBandType5 = Annotated[
    float,
    Path(
        description="262Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 5, 262Hz"""

EqualizerBandType6 = Annotated[
    float,
    Path(
        description="370Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 6, 370Hz"""

EqualizerBandType7 = Annotated[
    float,
    Path(
        description="523Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 7, 523Hz"""

EqualizerBandType8 = Annotated[
    float,
    Path(
        description="740Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 8, 740Hz"""

EqualizerBandType9 = Annotated[
    float,
    Path(
        description="1047Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 9, 1047Hz"""

EqualizerBandType10 = Annotated[
    float,
    Path(
        description="1480Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 10, 1480Hz"""

EqualizerBandType11 = Annotated[
    float,
    Path(
        description="2093Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 11, 2093Hz"""

EqualizerBandType12 = Annotated[
    float,
    Path(
        description="2960Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 12, 2960Hz"""

EqualizerBandType13 = Annotated[
    float,
    Path(
        description="4186Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 13, 4186Hz"""

EqualizerBandType14 = Annotated[
    float,
    Path(
        description="5920Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 14, 5920Hz"""

EqualizerBandType15 = Annotated[
    float,
    Path(
        description="8372Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 15, 8372Hz"""

EqualizerBandType16 = Annotated[
    float,
    Path(
        description="11840Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 16, 11840Hz"""

EqualizerBandType17 = Annotated[
    float,
    Path(
        description="16744Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 17, 16744Hz"""

EqualizerBandType18 = Annotated[
    float,
    Path(
        description="20000Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]
"""Equalizer Band 18, 20000Hz"""

PortType = Annotated[
    int,
    Path(
        description="Network Port",
        json_schema_extra={"example": "4010"},
        le=65535,
        ge=1
    )
]
"""Port, 1-65535"""

ChannelsType = Annotated[
    int,
    Path(
        description="Number of speaker channels",
        json_schema_extra={"example": "2"},
        le=8,
        ge=1
    )
]
"""Channel Count, 1-8"""

ChannelLayoutType = Annotated[
    Literal["mono", "stereo", "quad", "surround", "5.1", "7.1"],
    Path(
        description="Channel Layout Type",
        json_schema_extra={"example": "stereo"},
    )
]
"""ScreamRouter Channel Layout, one of "mono", "stereo", "quad", "surround", "5.1", "7.1."""

class _SourceNameType(str):
    def __eq__(self, other):
        if not isinstance(other, _SourceNameType):
            return False
        return super().__eq__(other)

SourceNameType = Annotated[
    str,
    Path(
        description="A Source Name",
        json_schema_extra={"example": "PC"},
    )
]
"""ScreamRouter Source Name"""

class _SinkNameType(str):
    def __eq__(self, other):
        if not isinstance(other, _SinkNameType):
            return False
        return super().__eq__(other)

SinkNameType = Annotated[
    str,
    Path(
        description="A Sink Name",
        json_schema_extra={"example": "Livingroom Stereo"},
    )
]
"""ScreamRouter Sink Name"""

class _RouteNameType(str):
    def __eq__(self, other):
        if not isinstance(other, _RouteNameType):
            return False
        return super().__eq__(other)

RouteNameType = Annotated[
    str,
    Path(
        description="A Sink Name",
        json_schema_extra={"example": "Music"},
    )
]
"""ScreamRouter Route Name"""

IPAddressType = Annotated[
    IPvAnyAddress,
    Path(
        description="An IP address",
        json_schema_extra={"example": "192.168.3.114"},
    )
]
"""ScreamRouter IP Address Type"""

class Equalizer(BaseModel):
    """Holds data for the equalizer for a sink""" 
    b1: EqualizerBandType1 = 1
    """Set 65Hz band gain."""
    b2: EqualizerBandType2 = 1
    """Set 92Hz band gain."""
    b3: EqualizerBandType3 = 1
    """Set 131Hz band gain."""
    b4: EqualizerBandType4 = 1
    """Set 185Hz band gain.`"""
    b5: EqualizerBandType5 = 1
    """Set 262Hz band gain."""
    b6: EqualizerBandType6 = 1
    """Set 370Hz band gain."""
    b7: EqualizerBandType7 = 1
    """Set 523Hz band gain."""
    b8: EqualizerBandType8 = 1
    """Set 740Hz band gain."""
    b9: EqualizerBandType9 = 1
    """Set 1047Hz band gain."""
    b10: EqualizerBandType10 = 1
    """Set 1480Hz band gain."""
    b11: EqualizerBandType11 = 1
    """Set 2093Hz band gain."""
    b12: EqualizerBandType12 = 1
    """Set 2960Hz band gain."""
    b13: EqualizerBandType13 = 1
    """Set 4186Hz band gain."""
    b14: EqualizerBandType14 = 1
    """Set 5920Hz band gain."""
    b15: EqualizerBandType15 = 1
    """Set 8372Hz band gain."""
    b16: EqualizerBandType16 = 1
    """Set 11840Hz band gain."""
    b17: EqualizerBandType17 = 1
    """Set 16744Hz band gain."""
    b18: EqualizerBandType18 = 1
    """Set 20000Hz band gain."""

    def __eq__(self, other):
        """Compares all properties if an Equalizer."""
        if isinstance(other, Equalizer):
            other_equalizer: Equalizer = other
            for field_name in self.model_fields:
                if not getattr(self, field_name) == getattr(other_equalizer, field_name):
                    return False
            return True
        raise TypeError(f"Can't compare {type(self)} against {type(other)}")

    def __hash__(self):
        """Returns a hash"""
        hashstr: str = ""
        for field_name in self.model_fields:
            hashstr = hashstr + f"{field_name}:{getattr(self, field_name)}"
        return hash(hashstr)

    def __mul__(self, other):
        other_eq: Equalizer = copy(other)
        other_eq.b1 = other_eq.b1 * self.b1
        other_eq.b2 = other_eq.b2 * self.b2
        other_eq.b3 = other_eq.b3 * self.b3
        other_eq.b4 = other_eq.b4 * self.b4
        other_eq.b5 = other_eq.b5 * self.b5
        other_eq.b6 = other_eq.b6 * self.b6
        other_eq.b7 = other_eq.b7 * self.b7
        other_eq.b8 = other_eq.b8 * self.b8
        other_eq.b9 = other_eq.b9 * self.b9
        other_eq.b10 = other_eq.b10 * self.b10
        other_eq.b11 = other_eq.b11 * self.b11
        other_eq.b12 = other_eq.b12 * self.b12
        other_eq.b13 = other_eq.b13 * self.b13
        other_eq.b14 = other_eq.b14 * self.b14
        other_eq.b15 = other_eq.b15 * self.b15
        other_eq.b16 = other_eq.b16 * self.b16
        other_eq.b17 = other_eq.b17 * self.b17
        other_eq.b18 = other_eq.b18 * self.b18
        return other_eq

    __rmul__ = __mul__


class URL(BaseModel):
    """Post data containing a URL"""
    url: PlaybackURLType
    """URL to play back"""

class InUseError(Exception):
    """Called when removal is attempted of something that is in use"""
    def __init__(self, message: str):
        super().__init__(self, message)

class SinkDescription(BaseModel):
    """
    Holds either a sink IP and Port or a group of sink names
    """
    name: SinkNameType
    """Sink Name, Endpoint and Group"""
    ip: Optional[IPAddressType] = None
    """Sink IP, Endpoint Only"""
    port: Optional[PortType] = 4010
    """Sink port number, Endpoint Only"""
    is_group: bool = False
    """Sink Is Group"""
    enabled: bool = True
    """Sink is Enabled, Endpoint and Group"""
    group_members: List[SinkNameType] = []
    """Sink Group Members, Group Only"""
    volume: VolumeType = 1
    """Holds the volume for the sink (0.0-1.0), Endpoint and Group"""
    bit_depth: BitDepthType = 32
    """Sink Bit depth, Endpoint Only"""
    sample_rate: SampleRateType = 48000
    """Sink Sample Rate, Endpoint Only"""
    channels: ChannelsType = 2
    """Sink Channel Count, Endpoint Only"""
    channel_layout: ChannelLayoutType = "stereo"
    """Sink Channel Layout, Endpoint Only"""
    delay: DelayType = 0
    """Delay in ms, Endpoint and Group"""
    equalizer: Equalizer = Equalizer(b1=1, b2=1, b3=1, b4=1, b5=1, b6=1,
                                     b7=1, b8=1, b9=1, b10=1, b11=1, b12=1,
                                     b13=1, b14=1, b15=1, b16=1, b17=1, b18=1)
    """Audio Equalizer"""

    def __eq__(self, other):
        """Compares the name if a string.
           Compares all properties if a SinkDescription."""
        if isinstance(other, str):
            other_sink_name: SinkNameType = other
            return self.name == other_sink_name
        if isinstance(other, SinkDescription):
            other_sink: SinkDescription = other
            for field_name in self.model_fields:
                if not getattr(self, field_name) == getattr(other_sink, field_name):
                    return False
            return True
        raise TypeError(f"Can't compare {type(self)} against {type(other)}")

    def __hash__(self):
        """Returns a hash"""
        hashstr: str = ""
        for field_name in self.model_fields:
            hashstr = hashstr + f"{field_name}:{getattr(self, field_name)}"
        return hash(hashstr)

class SourceDescription(BaseModel):
    """
    Holds either a source IP or a group of source names
    """
    name: SourceNameType
    """Source Name, Endpoint and Group"""
    ip: Optional[IPAddressType] = None
    """Source IP, Endpoint Only"""
    is_group: bool = False
    """Source Is Group"""
    enabled: bool = True
    """Source Enabled, Endpoint and Group"""
    group_members: List[SourceNameType] = []
    """"Source Group Members, Group Only"""
    volume: VolumeType = 1
    """Holds the volume for the source  (0.0-1.0), Endpoint and Group"""
    delay: DelayType = 0
    """Delay in ms, Endpoint and Group"""
    equalizer: Equalizer = Equalizer(b1=1, b2=1, b3=1, b4=1, b5=1, b6=1,
                                     b7=1, b8=1, b9=1, b10=1, b11=1, b12=1,
                                     b13=1, b14=1, b15=1, b16=1, b17=1, b18=1)
    """Audio Equalizer"""

    def __eq__(self, other):
        """Compares the name if a string.
           Compares all properties if a SourceDescription."""
        if isinstance(other, str):
            other_source_name: SourceNameType = other
            return self.name == other_source_name
        if isinstance(other, SourceDescription):
            other_source: SourceDescription = other
            for field_name in self.model_fields:
                if not getattr(self, field_name) == getattr(other_source, field_name):
                    return False
            return True
        raise TypeError(f"Can't compare {type(self)} against {type(other)}")

    def __hash__(self):
        """Returns a hash"""
        hashstr: str = ""
        for field_name in self.model_fields:
            hashstr = hashstr + f"{field_name}:{getattr(self, field_name)}"
        return hash(hashstr)


class RouteDescription(BaseModel):
    """
    Holds a route mapping from source to sink
    """
    name: RouteNameType
    """Route Name"""
    sink: SinkNameType
    """Route Sink"""
    source: SourceNameType
    """Route Source"""
    enabled: bool = True
    """Route Enabled"""
    volume: VolumeType = 1
    """Route volume (0.0-1.0)"""
    delay: DelayType = 0
    """Delay in ms"""
    equalizer: Equalizer = Equalizer(b1=1, b2=1, b3=1, b4=1, b5=1, b6=1,
                                     b7=1, b8=1, b9=1, b10=1, b11=1, b12=1,
                                     b13=1, b14=1, b15=1, b16=1, b17=1, b18=1)
    """Audio Equalizer"""

    def __eq__(self, other):
        """Compares the name if a string.
           Compares all properties if a RouteDescription."""
        if isinstance(other, str):
            other_route_name: RouteNameType = other
            return self.name == other_route_name
        if isinstance(other, RouteDescription):
            other_route: RouteDescription = other
            for field_name in self.model_fields:
                if not getattr(self, field_name) == getattr(other_route, field_name):
                    return False
            return True
        raise TypeError(f"Can't compare {type(self)} against {type(other)}")

    def __hash__(self):
        """Returns a hash"""
        hashstr: str = ""
        for field_name in self.model_fields:
            hashstr = hashstr + f"{field_name}:{getattr(self, field_name)}"
        return hash(hashstr)
