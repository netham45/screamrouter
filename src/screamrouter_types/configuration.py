"""Contains models used by the configuration manager"""

from copy import copy
from typing import List, Literal, Optional, Union

from pydantic import BaseModel

import src.screamrouter_types.annotations as annotations


class Equalizer(BaseModel):
    """Holds data for the equalizer for a sink""" 
    b1: annotations.EqualizerBandType1 = float(1.0)
    """Set 65Hz band gain."""
    b2: annotations.EqualizerBandType2 = float(1.0)
    """Set 92Hz band gain."""
    b3: annotations.EqualizerBandType3 = float(1.0)
    """Set 131Hz band gain."""
    b4: annotations.EqualizerBandType4 = float(1.0)
    """Set 185Hz band gain.`"""
    b5: annotations.EqualizerBandType5 = float(1.0)
    """Set 262Hz band gain."""
    b6: annotations.EqualizerBandType6 = float(1.0)
    """Set 370Hz band gain."""
    b7: annotations.EqualizerBandType7 = float(1.0)
    """Set 523Hz band gain."""
    b8: annotations.EqualizerBandType8 = float(1.0)
    """Set 740Hz band gain."""
    b9: annotations.EqualizerBandType9 = float(1.0)
    """Set 1047Hz band gain."""
    b10: annotations.EqualizerBandType10 = float(1.0)
    """Set 1480Hz band gain."""
    b11: annotations.EqualizerBandType11 = float(1.0)
    """Set 2093Hz band gain."""
    b12: annotations.EqualizerBandType12 = float(1.0)
    """Set 2960Hz band gain."""
    b13: annotations.EqualizerBandType13 = float(1.0)
    """Set 4186Hz band gain."""
    b14: annotations.EqualizerBandType14 = float(1.0)
    """Set 5920Hz band gain."""
    b15: annotations.EqualizerBandType15 = float(1.0)
    """Set 8372Hz band gain."""
    b16: annotations.EqualizerBandType16 = float(1.0)
    """Set 11840Hz band gain."""
    b17: annotations.EqualizerBandType17 = float(1.0)
    """Set 16744Hz band gain."""
    b18: annotations.EqualizerBandType18 = float(1.0)
    """Set 20000Hz band gain."""

    def __eq__(self, other):
        """Compares the name if a string.
           Compares all properties."""
        if isinstance(other, type(self)):
            for field_name in self.model_fields:
                if (not field_name in list(self.model_fields_set) and
                not field_name in list(self.model_fields_set)):
                    continue
                if not getattr(self, field_name) == getattr(other, field_name):
                    return False
            return True
        raise TypeError(f"Can't compare {type(self)} against {type(other)}")

    def __hash__(self):
        """Returns a hash"""
        hashstr: str = ""
        for field_name, field_info in self.model_fields.items():
            try:
                hashstr = hashstr + f"{field_name}:{getattr(self, field_name)}"
            except AttributeError:
                setattr(self, field_name, field_info.default)
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

class SinkDescription(BaseModel):
    """
    Holds either a sink IP and Port or a group of sink names
    """
    name: annotations.SinkNameType = ""
    """Sink Name, Endpoint and Group"""
    ip: Optional[annotations.IPAddressType] = None
    """Sink IP, Endpoint Only"""
    port: Optional[annotations.PortType] = 4010
    """Sink port number, Endpoint Only"""
    is_group: bool = False
    """Sink Is Group"""
    enabled: bool = True
    """Sink is Enabled, Endpoint and Group"""
    group_members: List[annotations.SinkNameType] = []
    """Sink Group Members, Group Only"""
    volume: annotations.VolumeType = 1
    """Holds the volume for the sink (0.0-1.0), Endpoint and Group"""
    bit_depth: annotations.BitDepthType = 32
    """Sink Bit depth, Endpoint Only"""
    sample_rate: annotations.SampleRateType = 48000
    """Sink Sample Rate, Endpoint Only"""
    channels: annotations.ChannelsType = 2
    """Sink Channel Count, Endpoint Only"""
    channel_layout: annotations.ChannelLayoutType = "stereo"
    """Sink Channel Layout, Endpoint Only"""
    delay: annotations.DelayType = 0
    """Delay in ms, Endpoint and Group"""
    equalizer: Equalizer = Equalizer(b1=1, b2=1, b3=1, b4=1, b5=1, b6=1,
                                     b7=1, b8=1, b9=1, b10=1, b11=1, b12=1,
                                     b13=1, b14=1, b15=1, b16=1, b17=1, b18=1)
    """Audio Equalizer"""
    timeshift: annotations.TimeshiftType = 0
    """Timeshift backwards in seconds"""
    time_sync: bool = False
    """Rather the sink is timesynced (Normal Scream receivers are not compatible)"""
    time_sync_delay: int = 0
    """Delay for time sync in ms"""

    def __eq__(self, other):
        """Compares the name if a string.
           Compares all properties."""
        if isinstance(other, str):
            other_route_name: annotations.RouteNameType = other
            return self.name == other_route_name
        if isinstance(other, type(self)):
            for field_name in self.model_fields:
                if (field_name in list(self.model_fields_set) and
                    not field_name in list(other.model_fields_set) or
                    field_name in list(other.model_fields_set) and
                    not field_name in list(self.model_fields_set)):
                    return False
                if (not field_name in list(self.model_fields_set) and
                not field_name in list(self.model_fields_set)):
                    continue
                if not getattr(self, field_name) == getattr(other, field_name):
                    return False
            return True
        raise TypeError(f"Can't compare {type(self)} against {type(other)}")

    def __hash__(self):
        """Returns a hash"""
        hashstr: str = ""
        for field_name, field_info in self.model_fields.items():
            try:
                hashstr = hashstr + f"{field_name}:{getattr(self, field_name)}"
            except AttributeError:
                setattr(self, field_name, field_info.default)
                hashstr = hashstr + f"{field_name}:{getattr(self, field_name)}"
        return hash(hashstr)

class SourceDescription(BaseModel):
    """
    Holds either a source IP or a group of source names
    """
    name: annotations.SourceNameType = ""
    """Source Name, Endpoint and Group"""
    ip: Optional[annotations.IPAddressType] = None
    """Source IP, Endpoint Only"""
    tag: Optional[str] = None
    """Tag if no IP is specified"""
    is_group: bool = False
    """Source Is Group"""
    enabled: bool = True
    """Source Enabled, Endpoint and Group"""
    group_members: List[annotations.SourceNameType] = []
    """"Source Group Members, Group Only"""
    volume: annotations.VolumeType = 1
    """Holds the volume for the source  (0.0-1.0), Endpoint and Group"""
    delay: annotations.DelayType = 0
    """Delay in ms, Endpoint and Group"""
    equalizer: Equalizer = Equalizer(b1=1, b2=1, b3=1, b4=1, b5=1, b6=1,
                                     b7=1, b8=1, b9=1, b10=1, b11=1, b12=1,
                                     b13=1, b14=1, b15=1, b16=1, b17=1, b18=1)
    """Audio Equalizer"""
    timeshift: annotations.TimeshiftType = 0
    """Timeshift backwards in seconds"""
    vnc_ip: Union[annotations.IPAddressType, Literal[""]] = ""
    """IP Address for VNC connections"""
    vnc_port: Union[annotations.PortType, Literal[""]] = ""
    """Port for VNC connections"""

    def __eq__(self, other):
        """Compares the name if a string.
           Compares all properties."""
        if isinstance(other, str):
            other_route_name: annotations.RouteNameType = other
            return self.name == other_route_name
        if isinstance(other, type(self)):
            for field_name in self.model_fields:
                if (field_name in list(self.model_fields_set) and
                    not field_name in list(other.model_fields_set) or
                    field_name in list(other.model_fields_set) and
                    not field_name in list(self.model_fields_set)):
                    return False
                if (not field_name in list(self.model_fields_set) and
                not field_name in list(self.model_fields_set)):
                    continue
                if not getattr(self, field_name) == getattr(other, field_name):
                    return False
            return True
        raise TypeError(f"Can't compare {type(self)} against {type(other)}")

    def __hash__(self):
        """Returns a hash"""
        hashstr: str = ""
        for field_name, field_info in self.model_fields.items():
            try:
                hashstr = hashstr + f"{field_name}:{getattr(self, field_name)}"
            except AttributeError:
                setattr(self, field_name, field_info.default)
                hashstr = hashstr + f"{field_name}:{getattr(self, field_name)}"
        return hash(hashstr)


class RouteDescription(BaseModel):
    """
    Holds a route mapping from source to sink
    """
    name: annotations.RouteNameType = ""
    """Route Name"""
    sink: annotations.SinkNameType = ""
    """Route Sink"""
    source: annotations.SourceNameType = ""
    """Route Source"""
    enabled: bool = True
    """Route Enabled"""
    volume: annotations.VolumeType = 1
    """Route volume (0.0-1.0)"""
    delay: annotations.DelayType = 0
    """Delay in ms"""
    equalizer: Equalizer = Equalizer(b1=1, b2=1, b3=1, b4=1, b5=1, b6=1,
                                     b7=1, b8=1, b9=1, b10=1, b11=1, b12=1,
                                     b13=1, b14=1, b15=1, b16=1, b17=1, b18=1)
    """Audio Equalizer"""
    timeshift: annotations.TimeshiftType = 0
    """Timeshift backwards in seconds"""

    def __eq__(self, other):
        """Compares the name if a string.
           Compares all properties."""
        if isinstance(other, str):
            other_route_name: annotations.RouteNameType = other
            return self.name == other_route_name
        if isinstance(other, type(self)):
            for field_name in self.model_fields:
                if (field_name in list(self.model_fields_set) and
                    not field_name in list(other.model_fields_set) or
                    field_name in list(other.model_fields_set) and
                    not field_name in list(self.model_fields_set)):
                    return False
                if (not field_name in list(self.model_fields_set) and
                not field_name in list(self.model_fields_set)):
                    continue
                if not getattr(self, field_name) == getattr(other, field_name):
                    return False
            return True
        raise TypeError(f"Can't compare {type(self)} against {type(other)}")

    def __hash__(self):
        """Returns a hash"""
        hashstr: str = ""
        for field_name, field_info in self.model_fields.items():
            try:
                hashstr = hashstr + f"{field_name}:{getattr(self, field_name)}"
            except AttributeError:
                setattr(self, field_name, field_info.default)
                hashstr = hashstr + f"{field_name}:{getattr(self, field_name)}"
        return hash(hashstr)
