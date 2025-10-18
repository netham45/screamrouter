"""Pydantic models for mDNS discovery data."""
from __future__ import annotations

from typing import Any, Dict, List, Optional, TypeAlias

from pydantic import BaseModel, ConfigDict, Field


SinkSettings: TypeAlias = Dict[str, Any]
SourceSettings: TypeAlias = Dict[str, Any]


class DiscoveredDevice(BaseModel):
    """Represents a device discovered via any discovery mechanism."""

    model_config = ConfigDict(
        from_attributes=True,
        arbitrary_types_allowed=True,
        json_schema_serialization_defaults_required=True,
    )

    discovery_method: str = Field(description="Mechanism that located the device (mdns, cpp_rtp, etc.)")
    role: str = Field(description="Intended routing role for the device (source/sink)")
    ip: str = Field(description="IPv4/IPv6 address of the device")
    port: Optional[int] = Field(default=None, description="Advertised service port, when known")
    name: Optional[str] = Field(default=None, description="Friendly name provided by the discovery method")
    tag: Optional[str] = Field(default=None, description="Discovery specific tag or identifier")
    properties: Dict[str, Any] = Field(default_factory=dict, description="Discovery metadata and TXT properties")
    last_seen: str = Field(default="", description="ISO timestamp representing the most recent observation")
    device_type: Optional[str] = Field(default=None, description="Detailed device type classification, if available")


class UnifiedDiscoverySnapshot(BaseModel):
    """Unified discovery data for all discovery mechanisms."""

    model_config = ConfigDict(
        from_attributes=True,
        arbitrary_types_allowed=True,
        json_schema_serialization_defaults_required=True,
    )

    discovered_devices: List[DiscoveredDevice] = Field(default_factory=list, description="Discovered device entries")
    sink_settings: List[SinkSettings] = Field(default_factory=list, description="Discovered sink settings")
    source_settings: List[SourceSettings] = Field(default_factory=list, description="Discovered source settings")


class MdnsSnapshot(BaseModel):
    """Aggregated view of legacy mDNS discovery data."""

    model_config = ConfigDict(
        from_attributes=True,
        arbitrary_types_allowed=True,
        json_schema_serialization_defaults_required=True,
    )

    devices: List[DiscoveredDevice] = Field(default_factory=list, description="Discovered devices")
    sink_settings: List[SinkSettings] = Field(default_factory=list, description="Discovered sink settings")
    source_settings: List[SourceSettings] = Field(default_factory=list, description="Discovered source settings")
