"""Holds custom annotations for fields"""
from typing import Annotated, Literal

from fastapi import Path
from pydantic import AnyUrl, IPvAnyAddress

from screamrouter.constants import constants

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

TimeshiftType = Annotated[
    float,
    Path(
        description="Timeshift type, timeshift backwards in seconds.",
        json_schema_extra={"example": "180.32"},
        le=constants.TIMESHIFT_DURATION,
        ge=0
    )
]
"""Time shift, 0-constants.TIMESHIFT_DURATION seconds"""


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
    Literal["mono", "stereo", "quad", "surround", "3.1", "4.0", "5.1", "5.1(side)", "7.1"],
    Path(
        description="Channel Layout Type",
        json_schema_extra={"example": "stereo"},
    )
]
"""ScreamRouter Channel Layout, one of:
   "mono", "stereo", "quad", "surround", "3.1", "4.0", "5.1", "5.1(side)", "7.1."""

SourceNameType = Annotated[
    str,
    Path(
        description="A Source Name",
        json_schema_extra={"example": "PC"},
    )
]
"""ScreamRouter Source Name"""

SinkNameType = Annotated[
    str,
    Path(
        description="A Sink Name",
        json_schema_extra={"example": "Livingroom Stereo"},
    )
]
"""ScreamRouter Sink Name"""

RouteNameType = Annotated[
    str,
    Path(
        description="A Route Name",
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
