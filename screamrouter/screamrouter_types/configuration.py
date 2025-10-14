"""Contains models used by the configuration manager"""

import uuid
from copy import copy, deepcopy
from typing import Dict, List, Literal, Optional, Union

from pydantic import BaseModel, ConfigDict, Field, model_validator

import screamrouter.screamrouter_types.annotations as annotations


class RtpReceiverMapping(BaseModel):
    """Holds mapping configuration for RTP receivers in multi-device mode"""
    model_config = ConfigDict(from_attributes=True,
                              arbitrary_types_allowed=True,
                              json_schema_serialization_defaults_required=True)
    
    receiver_sink_name: annotations.SinkNameType
    """Name of the receiver sink to map to"""
    left_channel: int = Field(default=0, ge=0, le=7)
    """Left channel mapping (0-7)"""
    right_channel: int = Field(default=1, ge=0, le=7)
    """Right channel mapping (0-7)"""

    def __hash__(self):
        """Returns a hash of the mapping."""
        return hash((self.receiver_sink_name, self.left_channel, self.right_channel))


class Equalizer(BaseModel):
    """Holds data for an equalizer"""
    model_config = ConfigDict(from_attributes=True,
                              arbitrary_types_allowed=True,
                              json_schema_serialization_defaults_required=True)

    name: Optional[str] = None
    """Custom name for the equalizer configuration."""
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
    normalization_enabled: bool = True
    """Enable or disable equalizer normalization."""

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
        if not isinstance(other, Equalizer):
            return NotImplemented
        
        new_eq = Equalizer()
        new_eq.b1 = self.b1 * other.b1
        new_eq.b2 = self.b2 * other.b2
        new_eq.b3 = self.b3 * other.b3
        new_eq.b4 = self.b4 * other.b4
        new_eq.b5 = self.b5 * other.b5
        new_eq.b6 = self.b6 * other.b6
        new_eq.b7 = self.b7 * other.b7
        new_eq.b8 = self.b8 * other.b8
        new_eq.b9 = self.b9 * other.b9
        new_eq.b10 = self.b10 * other.b10
        new_eq.b11 = self.b11 * other.b11
        new_eq.b12 = self.b12 * other.b12
        new_eq.b13 = self.b13 * other.b13
        new_eq.b14 = self.b14 * other.b14
        new_eq.b15 = self.b15 * other.b15
        new_eq.b16 = self.b16 * other.b16
        new_eq.b17 = self.b17 * other.b17
        new_eq.b18 = self.b18 * other.b18
        return new_eq

    __rmul__ = __mul__


class SpeakerLayout(BaseModel):
    """Holds data for a speaker layout configuration."""
    model_config = ConfigDict(from_attributes=True,
                              arbitrary_types_allowed=True,
                              json_schema_serialization_defaults_required=True)

    auto_mode: bool = True
    """If true, the speaker mix is automatically determined based on input/output channels.
       If false, the custom matrix is used."""

    matrix: List[List[float]] = [[(1.0 if i == j else 0.0) for j in range(8)] for i in range(8)]
    """An 8x8 matrix defining gain from input channel (row) to output channel (column).
       Values typically range from 0.0 to 1.0."""

    def __eq__(self, other):
        if isinstance(other, type(self)):
            return self.auto_mode == other.auto_mode and self.matrix == other.matrix
        return False

    def __hash__(self):
        # Convert matrix to a tuple of tuples to make it hashable
        matrix_tuple = tuple(tuple(row) for row in self.matrix)
        return hash((self.auto_mode, matrix_tuple))

    def __mul__(self, other):
        """
        Multiplies two SpeakerLayout objects.
        - If both are manual: matrices are multiplied element-wise. Result is manual.
        - If one is manual and one is auto: the manual layout wins. Result is manual.
        - If both are auto: the result is auto (with a default identity matrix).
        """
        if not isinstance(other, SpeakerLayout):
            return NotImplemented

        new_layout = SpeakerLayout()

        if not self.auto_mode and not other.auto_mode:
            # Both are manual: element-wise matrix multiplication
            # Both are manual: Perform standard matrix multiplication C = self.matrix * other.matrix
            new_layout.auto_mode = False
            # Initialize new_matrix with zeros
            new_matrix = [[0.0 for _ in range(8)] for _ in range(8)]
            
            # Assuming self.matrix and other.matrix are 8x8
            # If not, this needs more robust handling or validation upstream
            # Pydantic default ensures they are 8x8 if not provided, or they come from config
            
            # Standard matrix multiplication: C[i][j] = sum(A[i][k] * B[k][j])
            # Here, A is self.matrix, B is other.matrix
            for i in range(8): # Row of the result matrix C (and self.matrix A)
                for j in range(8): # Column of the result matrix C (and other.matrix B)
                    current_sum = 0.0
                    for k in range(8): # Inner dimension for dot product
                        val_self = self.matrix[i][k] if i < len(self.matrix) and k < len(self.matrix[i]) else 0.0
                        val_other = other.matrix[k][j] if k < len(other.matrix) and j < len(other.matrix[k]) else 0.0
                        # If a matrix is smaller than 8x8, this defaults to 0.0 for out-of-bounds,
                        # which is generally okay for matrix multiplication if padding with zeros is intended.
                        # However, Pydantic default should ensure 8x8.
                        current_sum += val_self * val_other
                    new_matrix[i][j] = current_sum
            new_layout.matrix = new_matrix
        elif not self.auto_mode and other.auto_mode:
            # Self is manual, other is auto (effectively identity): self (manual) wins
            new_layout.auto_mode = False
            new_layout.matrix = deepcopy(self.matrix)
        elif self.auto_mode and not other.auto_mode:
            # Self is auto, other is manual: other (manual) wins
            new_layout.auto_mode = False
            new_layout.matrix = deepcopy(other.matrix)
        else:  # Both are auto_mode = True
            new_layout.auto_mode = True
            # new_layout.matrix remains default identity from SpeakerLayout() constructor
        
        return new_layout

    __rmul__ = __mul__


class AudioManagerConfig(BaseModel):
    """
    Configuration for the AudioManager.
    """
    model_config = ConfigDict(from_attributes=True,
                              arbitrary_types_allowed=True,
                              json_schema_serialization_defaults_required=True)

    global_timeshift_buffer_duration_sec: int = 300
    """Global timeshift buffer duration in seconds for the TimeshiftManager."""
    # Add other AudioManager specific configurations here if needed in the future.


class SinkDescription(BaseModel):
    """
    Holds either a sink IP and Port or a group of sink names
    """
    model_config = ConfigDict(from_attributes=True,
                              arbitrary_types_allowed=True,
                              json_schema_serialization_defaults_required=True)

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
    equalizer: Equalizer = Field(default_factory=Equalizer)
    """Audio Equalizer"""
    timeshift: annotations.TimeshiftType = 0
    """Timeshift backwards in seconds"""
    speaker_layouts: Dict[int, SpeakerLayout] = Field(default_factory=dict)
    """Speaker Layouts keyed by input channel count"""
    volume_normalization: bool = False
    """Enable or disable volume normalization."""
    time_sync: bool = False
    """Rather the sink is timesynced (Normal Scream receivers are not compatible)"""
    time_sync_delay: int = 0
    """Delay for time sync in ms"""
    config_id: Optional[str] = None
    """Unique GUID for this sink, auto-generated on creation if not provided"""
    use_tcp: bool = False
    enable_mp3: bool = True
    protocol: str = "scream"
    """The network protocol to use for the sink. Can be 'scream', 'rtp', or 'web_receiver'."""
    multi_device_mode: bool = False
    """Enable multi-device RTP output mode"""
    rtp_receiver_mappings: List[RtpReceiverMapping] = Field(default_factory=list)
    """RTP receiver mappings for multi-device mode"""
    is_temporary: bool = Field(default=False, exclude=True)
    """Indicates if this sink is temporary and should not be persisted"""

    @model_validator(mode='after')
    def ensure_config_id(self):
        """Auto-generate config_id if not provided"""
        if self.config_id is None:
            self.config_id = str(uuid.uuid4())
        return self

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
        return False

    def __hash__(self):
        """Returns a hash"""
        values_to_hash = []
        for field_name in sorted(self.model_fields.keys()): # Sort keys for consistent hash
            if field_name == "speaker_layouts":
                # Convert dict to a hashable representation (tuple of sorted items)
                # SpeakerLayout itself must be hashable
                layouts_hashable_items = []
                # Ensure speaker_layouts is initialized (Pydantic default_factory should handle this)
                layouts_dict = getattr(self, field_name, {}) 
                for k, v in sorted(layouts_dict.items()):
                    layouts_hashable_items.append((k, v))
                values_to_hash.append(tuple(layouts_hashable_items))
            elif field_name == "group_members":
                # Convert list to a sorted tuple to make it hashable
                members_list = getattr(self, field_name, [])
                values_to_hash.append(tuple(sorted(members_list)))
            elif field_name == "rtp_receiver_mappings":
                # Convert list of mappings to a tuple to make it hashable
                mappings_list = getattr(self, field_name, [])
                # Since RtpReceiverMapping is a Pydantic model, it should be hashable
                values_to_hash.append(tuple(sorted(mappings_list, key=lambda m: m.receiver_sink_name)))
            else:
                try:
                    values_to_hash.append(getattr(self, field_name))
                except AttributeError:
                    # Handle cases where a field might not be set, using its default
                    values_to_hash.append(self.model_fields[field_name].default)
        return hash(tuple(values_to_hash))

class SourceDescription(BaseModel):
    """
    Holds either a source IP or a group of source names
    """
    model_config = ConfigDict(from_attributes=True,
                              arbitrary_types_allowed=True,
                              json_schema_serialization_defaults_required=True)

    name: annotations.SourceNameType = ""
    """Source Name, Endpoint and Group"""
    ip: Optional[annotations.IPAddressType] = None
    """Source IP, Endpoint Only"""
    tag: Optional[str] = None
    """Tag if no IP is specified"""
    channels: Optional[int] = None # Added for Task 13
    """Source Channel Count, Endpoint Only. If None, typically assumed to be 2."""
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
    equalizer: Equalizer = Field(default_factory=Equalizer)
    """Audio Equalizer"""
    timeshift: annotations.TimeshiftType = 0
    """Timeshift backwards in seconds"""
    speaker_layouts: Dict[int, SpeakerLayout] = Field(default_factory=dict)
    """Speaker Layouts keyed by input channel count"""
    vnc_ip: Union[annotations.IPAddressType, Literal[""]] = ""
    """IP Address for VNC connections"""
    vnc_port: Union[annotations.PortType, Literal[""]] = ""
    """Port for VNC connections"""
    is_process: bool = False
    """Is a process and should be grouped with a source by IP"""
    config_id: Optional[str] = None
    """Unique GUID for this source, auto-generated on creation if not provided"""

    @model_validator(mode='after')
    def ensure_config_id(self):
        """Auto-generate config_id if not provided"""
        if self.config_id is None:
            self.config_id = str(uuid.uuid4())
        return self

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
        return False

    def __hash__(self):
        """Returns a hash"""
        values_to_hash = []
        for field_name in sorted(self.model_fields.keys()): # Sort keys for consistent hash
            if field_name == "speaker_layouts":
                # Convert dict to a hashable representation (tuple of sorted items)
                layouts_hashable_items = []
                layouts_dict = getattr(self, field_name, {})
                for k, v in sorted(layouts_dict.items()):
                    layouts_hashable_items.append((k, v))
                values_to_hash.append(tuple(layouts_hashable_items))
            elif field_name == "group_members":
                # Convert list to a sorted tuple to make it hashable
                members_list = getattr(self, field_name, [])
                values_to_hash.append(tuple(sorted(members_list)))
            else:
                try:
                    values_to_hash.append(getattr(self, field_name))
                except AttributeError:
                    values_to_hash.append(self.model_fields[field_name].default)
        return hash(tuple(values_to_hash))


class RouteDescription(BaseModel):
    """
    Holds a route mapping from source to sink
    """
    model_config = ConfigDict(from_attributes=True,
                              arbitrary_types_allowed=True,
                              json_schema_serialization_defaults_required=True)

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
    equalizer: Equalizer = Field(default_factory=Equalizer)
    """Audio Equalizer"""
    timeshift: annotations.TimeshiftType = 0
    """Timeshift backwards in seconds"""
    speaker_layouts: Dict[int, SpeakerLayout] = Field(default_factory=dict)
    """Speaker Layouts keyed by input channel count"""
    config_id: Optional[str] = None
    """Unique GUID for this route, auto-generated on creation if not provided"""
    is_temporary: bool = Field(default=False, exclude=True)
    """Indicates if this route is temporary and should not be persisted"""

    @model_validator(mode='after')
    def ensure_config_id(self):
        """Auto-generate config_id if not provided"""
        if self.config_id is None:
            self.config_id = str(uuid.uuid4())
        return self

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
        return False

    def __hash__(self):
        """Returns a hash"""
        values_to_hash = []
        for field_name in sorted(self.model_fields.keys()): # Sort keys for consistent hash
            if field_name == "speaker_layouts":
                # Convert dict to a hashable representation (tuple of sorted items)
                layouts_hashable_items = []
                layouts_dict = getattr(self, field_name, {})
                for k, v in sorted(layouts_dict.items()):
                    layouts_hashable_items.append((k, v))
                values_to_hash.append(tuple(layouts_hashable_items))
            else:
                try:
                    values_to_hash.append(getattr(self, field_name))
                except AttributeError:
                    values_to_hash.append(self.model_fields[field_name].default)
        return hash(tuple(values_to_hash))
