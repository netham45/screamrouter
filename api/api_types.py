"""Holds post types to be passed from FastAPI"""
from typing import Annotated, List, Literal
from fastapi import Path
from pydantic import AnyUrl, BaseModel, IPvAnyAddress
from logger import get_logger

logger = get_logger(__name__)

VolumeType = Annotated[
    float,
    Path(
        description="Volume. Float, must be between 0 and 1. 1 is no change, 0 is silent.",
        json_schema_extra={"example": ".65"},
        le=1,
        ge=0
    )
]

DelayType = Annotated[
    int,
    Path(
        description="Delay in ms. Int, must be between 0 and 5000.",
        json_schema_extra={"example": "180"},
        le=1,
        ge=0
    )
]

PlaybackURLType = Annotated[
    AnyUrl,
    Path(
        description="URL for playback.",
        json_schema_extra={"example": "http://www.example.com/cowbell.wav"},
    )
]

SampleRateType = Annotated[
    Literal[44100, 48000, 88200, 96000, 192000],
    Path(
        description="Sample Rate, must be a valid choice.",
        json_schema_extra={"example": "48000"},
    )
]

BitDepthType = Annotated[
    Literal[16, 24, 32],
    Path(
        description="Bit Depth, must be a valid choice",
        json_schema_extra={"example": "32"},
    )
]

EqualizerBandType1 = Annotated[
    float,
    Path(
        description="65Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType2 = Annotated[
    float,
    Path(
        description="92Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType3 = Annotated[
    float,
    Path(
        description="131Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType4 = Annotated[
    float,
    Path(
        description="185Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType5 = Annotated[
    float,
    Path(
        description="262Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType6 = Annotated[
    float,
    Path(
        description="370Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType7 = Annotated[
    float,
    Path(
        description="523Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType8 = Annotated[
    float,
    Path(
        description="740Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType9 = Annotated[
    float,
    Path(
        description="1047Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType10 = Annotated[
    float,
    Path(
        description="1480Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType11 = Annotated[
    float,
    Path(
        description="2093Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType12 = Annotated[
    float,
    Path(
        description="2960Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType13 = Annotated[
    float,
    Path(
        description="4186Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType14 = Annotated[
    float,
    Path(
        description="5920Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType15 = Annotated[
    float,
    Path(
        description="8372Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType16 = Annotated[
    float,
    Path(
        description="11840Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType17 = Annotated[
    float,
    Path(
        description="16744Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

EqualizerBandType18 = Annotated[
    float,
    Path(
        description="20000Hz Band Gain, 0 is full attenuation, 2 is 200% volume",
        json_schema_extra={"example": "1.3"},
        le=2,
        ge=0
    )
]

PortType = Annotated[
    int,
    Path(
        description="Network Port",
        json_schema_extra={"example": "4010"},
        le=65535,
        ge=1
    )
]

ChannelsType = Annotated[
    int,
    Path(
        description="Number of speaker channels",
        json_schema_extra={"example": "2"},
        le=8,
        ge=1
    )
]

ChannelLayoutType = Annotated[
    Literal["mono", "stereo", "quad", "surround", "5.1", "7.1"],
    Path(
        description="Channel Layout Type",
        json_schema_extra={"example": "stereo"},
    )
]

SourceNameType = Annotated[
    str,
    Path(
        description="A Source Name",
        json_schema_extra={"example": "PC"},
    )
]

SinkNameType = Annotated[
    str,
    Path(
        description="A Sink Name",
        json_schema_extra={"example": "Livingroom Stereo"},
    )
]

RouteNameType = Annotated[
    str,
    Path(
        description="A Sink Name",
        json_schema_extra={"example": "Music"},
    )
]

IPAddressType = Annotated[
    IPvAnyAddress,
    Path(
        description="An IP address",
        json_schema_extra={"example": "192.168.3.114"},
    )
]

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

class PostSink(BaseModel):
    """Post data to configure or add a Sink"""
    ip: IPAddressType
    """Sink IP"""
    port: PortType = 4010
    """Sink Port"""
    bit_depth: BitDepthType = 32
    """Bit Depth"""
    sample_rate: SampleRateType = 48000
    """Sample Rate"""
    channels:ChannelsType = 2
    """Channel Count"""
    channel_layout: ChannelLayoutType = "stereo"
    """Channel Layout"""
    delay: DelayType = 0
    """Delay in ms"""
    equalizer: Equalizer
    """Equalizer"""

class PostSinkGroup(BaseModel):
    """Post data to configure or add a Sink Group"""
    sinks: List[SinkNameType]
    """List of names of grouped sinks"""

class PostSource(BaseModel):
    """Post data to configure or add a Source"""
    ip: IPAddressType
    """Source IP"""

class PostSourceGroup(BaseModel):
    """Post data to configure or add a Source Group"""
    sources: List[SourceNameType]
    """List of names of grouped Sources"""

class PostRoute(BaseModel):
    """Post data to configure or add a Route"""
    source: SourceNameType
    """Route Source"""
    sink: SinkNameType
    """Route Sink"""

class PostURL(BaseModel):
    """Post data containing a URL"""
    url: PlaybackURLType
    """URL"""
