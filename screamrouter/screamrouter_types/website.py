"""Contains types for interacting with site Jinja templates"""
from typing import Literal, Union

from pydantic import BaseModel

from screamrouter.screamrouter_types.annotations import (RouteNameType, SinkNameType,
                                                SourceNameType)
from screamrouter.screamrouter_types.configuration import (Equalizer, RouteDescription,
                                                  SinkDescription,
                                                  SourceDescription)


class AddEditRouteInfo(BaseModel):
    """Contains information for starting the add_edit_route dialog"""
    add_new: bool = True
    """Set true if we're adding a new entry"""
    data: RouteDescription
    """Set to the existing entry if we're editing one"""

class AddEditSourceInfo(BaseModel):
    """Contains information for starting the add_edit_source dialog"""
    add_new: bool = True
    """Set true if we're adding a new entry"""
    data: SourceDescription
    """Set to the existing entry if we're editing one"""

class AddEditSinkInfo(BaseModel):
    """Contains information for starting the add_edit_sink dialog"""
    add_new: bool = True
    """Set true if we're adding a new entry"""
    data: SinkDescription
    """Set to the existing entry if we're editing one"""

class EditEqualizerInfo(BaseModel):
    """Contains information for starting the equalizer dialog"""
    add_new: bool = True
    """Set true if we're adding a new entry"""
    data: Equalizer
    """Set to the existing entry if we're editing one"""
    equalizer_holder_name: Union[SourceNameType, SinkNameType, RouteNameType]
    """Name of the object holding the equalizer"""
    equalizer_holder_type: Union[Literal["source"], Literal["sink"], Literal["route"]]
