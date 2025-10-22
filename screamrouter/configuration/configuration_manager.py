"""This manages the target state of sinks, sources, and routes
   then runs audio controllers for each source"""
import asyncio
import concurrent.futures
import os
import socket
import sys
import threading
import traceback
import uuid
from datetime import datetime, timezone

import screamrouter.screamrouter_logger.screamrouter_logger as screamrouter_logger

# --- C++ Engine Import ---
try:
    import screamrouter_audio_engine
except ImportError as e:
    # Log the error but allow ScreamRouter to potentially run without the C++ engine
    # (though functionality will be severely limited or broken)
    screamrouter_logger.get_logger(__name__).critical(
        "Failed to import C++ audio engine module (screamrouter_audio_engine). "
        "Ensure it's compiled correctly. Error: %s", e)
    screamrouter_audio_engine = None
# --- End C++ Engine Import ---

"""This manages the target state of sinks, sources, and routes
   then runs audio controllers for each source"""
from copy import copy, deepcopy
import ipaddress
from ipaddress import IPv4Address
from typing import Any, Dict, List, Optional, Set, Tuple
from subprocess import TimeoutExpired

_CAPTURE_ALLOWED_SAMPLE_RATES = {44100, 48000, 96000}
_CAPTURE_DEFAULT_SAMPLE_RATE = 48000

import dns.nameserver
import dns.rdtypes
import dns.rdtypes.ANY
import dns.rdtypes.ANY.PTR
import dns.resolver
import dns.rrset
import fastapi
import yaml
from fastapi import Request
from zeroconf import ServiceInfo, Zeroconf

import screamrouter.constants.constants as constants
import screamrouter.screamrouter_logger.screamrouter_logger as screamrouter_logger
from screamrouter.api.api_websocket_config import APIWebsocketConfig
from screamrouter.api.api_webstream import APIWebStream
from screamrouter.configuration.configuration_solver import ConfigurationSolver
from screamrouter.configuration.temporary_entity_manager import TemporaryEntityManager
from screamrouter.plugin_manager.plugin_manager import PluginManager
from screamrouter.screamrouter_types.annotations import (DelayType, IPAddressType,
                                                RouteNameType, SinkNameType,
                                                SourceNameType, TimeshiftType,
                                                VolumeType)
from screamrouter.screamrouter_types.configuration import (Equalizer, RouteDescription,
                                                  SinkDescription,
                                                  SourceDescription,
                                                  SpeakerLayout,
                                                  SystemAudioDeviceInfo)
from screamrouter.screamrouter_types.mdns import (DiscoveredDevice,
                                          MdnsSnapshot,
                                          UnifiedDiscoverySnapshot)
from screamrouter.screamrouter_types.exceptions import InUseError
from screamrouter.utils.mdns_pinger import MDNSPinger
from screamrouter.utils.mdns_responder import MDNSResponder
from screamrouter.utils.mdns_settings_pinger import MDNSSettingsPinger
from screamrouter.utils.mdns_scream_advertiser import ScreamAdvertiser

# Import and initialize logger *before* the try block that might use it
_logger = screamrouter_logger.get_logger(__name__)

from screamrouter.utils.simple_vnc_client import SimpleVNCClient

# --- C++ Engine Import ---
try:
    import screamrouter_audio_engine
except ImportError as e:
    # Log the error but allow ScreamRouter to potentially run without the C++ engine
    # (though functionality will be severely limited or broken)
    _logger.critical(
        "Failed to import C++ audio engine module (screamrouter_audio_engine). "
        "Ensure it's compiled correctly. Error: %s", e)
    screamrouter_audio_engine = None
# --- End C++ Engine Import ---

class ConfigurationManager(threading.Thread):
    """Tracks configuration and loading the main receiver/sinks based off of it"""
    @staticmethod
    def _sanitize_screamrouter_label(label: str) -> str:
        return ''.join(c.lower() if c.isalnum() or c in ('-', '_') else '_' for c in label)

    @staticmethod
    def _normalize_process_tag(tag: Any) -> str:
        """Coerce discovery tags to clean strings without embedded NULs."""
        try:
            value = str(tag)
        except Exception:  # pylint: disable=broad-except
            return ""
        return value.replace("\x00", "")

    def __init__(self, websocket: APIWebStream,
                 plugin_manager: PluginManager,
                 websocket_config: APIWebsocketConfig,
                 audio_manager: screamrouter_audio_engine.AudioManager): # Added audio_manager parameter
        """Initialize the controller"""
        super().__init__(name="Configuration Manager")

        # --- C++ Engine Members ---
        # Use the AudioManager instance passed from the main script
        self.cpp_audio_manager: Optional[screamrouter_audio_engine.AudioManager] = audio_manager
        self.cpp_config_applier: Optional[screamrouter_audio_engine.AudioEngineConfigApplier] = None
        # --- End C++ Engine Members ---

        self.mdns_responder: MDNSResponder = MDNSResponder()
        """MDNS Responder, handles returning responses over MDNS for receivers/senders to configure to"""
        self.mdns_responder.start()
        self.mdns_pinger: MDNSPinger = MDNSPinger()
        """MDNS Responder, handles querying for receivers and senders to add entries for"""
        self.mdns_pinger.start()
        self.mdns_settings_pinger: MDNSSettingsPinger = MDNSSettingsPinger(self)
        """MDNS Settings Pinger, handles querying for settings to sync with sources"""
        self.mdns_settings_pinger.start()
        self.mdns_scream_advertiser: ScreamAdvertiser = ScreamAdvertiser(port=constants.RTP_RECEIVER_PORT)
        """MDNS Scream Advertiser, advertises the RTP listener as a _scream._udp service"""
        self.mdns_scream_advertiser.start()
        self.sink_descriptions: List[SinkDescription] = []
        """List of Sinks the controller knows of"""
        self.source_descriptions:  List[SourceDescription] = []
        """List of Sources the controller knows of"""
        self.route_descriptions: List[RouteDescription] = []
        """List of Routes the controller knows of"""
        
        # Temporary entity management
        self.temp_entity_manager: TemporaryEntityManager = TemporaryEntityManager()
        """Manages temporary sinks and routes for WebRTC listeners"""
        self.active_temporary_sinks: List[SinkDescription] = []
        """List of active temporary sinks"""
        self.active_temporary_routes: List[RouteDescription] = []
        """List of active temporary routes"""
        self.__api_webstream: APIWebStream = websocket
        """Holds the WebStream API for streaming MP3s to browsers"""
        self.active_configuration: ConfigurationSolver
        """Holds the solved configuration"""
        self.old_configuration_solver: ConfigurationSolver = ConfigurationSolver([], [], [])
        """Holds the previously solved configuration to compare against for changes"""
        self.configuration_semaphore: threading.Semaphore = threading.Semaphore(1)
        """Semaphore so only one thread can reload the config or save to YAML at a time"""
        self.reload_condition: threading.Condition = threading.Condition()
        """Condition to indicate the Configuration Manager needs to reload"""
        self.volume_eq_reload_condition: threading.Condition = threading.Condition()
        """Condition to indicate the Configuration Manager needs to apply volume and eq"""
        self.running: bool = True
        """Rather the thread is running or not"""
        self.reload_config: bool = False
        """Set to true to reload the config. Used so config
           changes during another config reload still trigger
           a reload"""
        self.websocket_config = websocket_config
        """Websocket Config Update Notifier"""
        self.plugin_manager: PluginManager = plugin_manager

        self.discovered_devices: Dict[str, DiscoveredDevice] = {}
        """Stores the latest set of discovered devices keyed by discovery method"""

        self._hostname_cache: Dict[str, str] = {}
        """Caches hostname lookups keyed by IP string"""
        self._hostname_cache_lock: threading.Lock = threading.Lock()
        """Synchronizes access to the hostname cache"""

        self.system_capture_devices: List[SystemAudioDeviceInfo] = []
        """Cached metadata for system capture endpoints."""
        self.system_playback_devices: List[SystemAudioDeviceInfo] = []
        """Cached metadata for system playback endpoints."""
        self._system_device_lock: threading.Lock = threading.Lock()
        """Protects system device metadata during updates."""
        self._system_device_poll_interval: float = 3.0
        """Polling interval (seconds) for system device notifications."""
        self._device_notification_task: Optional[asyncio.Task] = None
        """Asyncio task handle for system device watcher."""

        # --- C++ Engine Initialization (using the passed AudioManager) ---
        if self.cpp_audio_manager and screamrouter_audio_engine: # Check if AudioManager was provided and import succeeded
            # The AudioManager instance is already initialized by the main script.
            # We just need to create the ConfigApplier if the AudioManager is valid.
            try:
                _logger.info("[Configuration Manager] Using provided C++ AudioManager instance.")
                # Ensure the provided AudioManager is not None (already checked by `if self.cpp_audio_manager`)
                # and that it's likely initialized (though we can't directly check its initialized state from here easily
                # without adding a specific method to AudioManager). We assume it is.
                
                self.cpp_config_applier = screamrouter_audio_engine.AudioEngineConfigApplier(self.cpp_audio_manager)
                _logger.info("[Configuration Manager] C++ AudioEngineConfigApplier created using provided AudioManager.")

            except Exception as e: # pylint: disable=broad-except
                _logger.exception("[Configuration Manager] Exception during C++ engine setup with provided AudioManager: %s", e)
                # self.cpp_audio_manager remains as passed, but applier might be None
                self.cpp_config_applier = None
        elif not self.cpp_audio_manager:
            _logger.warning("[Configuration Manager] No C++ AudioManager instance provided. C++ engine features will be disabled.")
        # --- End C++ Engine Initialization ---

        self.__load_config() # Load YAML config
        self._initialize_system_device_registry()

        _logger.info("------------------------------------------------------------------------")
        _logger.info("  ScreamRouter")
        _logger.info("     Console Log level: %s", constants.CONSOLE_LOG_LEVEL)
        _logger.info("     Log Dir: %s", os.path.realpath(constants.LOGS_DIR))
        _logger.info("------------------------------------------------------------------------")

        self.start()

    def _safe_async_run(self, coro):
        """Safely run an async coroutine, handling both sync and async contexts.
        
        This method checks if we're already in an async event loop and uses
        the appropriate method to run the coroutine.
        
        Args:
            coro: The coroutine to run
        """
        try:
            # Check if we're already in an async context
            loop = asyncio.get_running_loop()
            # We're in an async context, create a task
            task = asyncio.create_task(coro)
            return task
        except RuntimeError:
            # We're not in an async context, use asyncio.run()
            return asyncio.run(coro)

    def _broadcast_full_configuration(self):
        """Return coroutine to broadcast current configuration and device metadata."""
        return self.websocket_config.broadcast_config_update(
            self.source_descriptions,
            self.sink_descriptions,
            self.route_descriptions,
            self.system_capture_devices,
            self.system_playback_devices,
        )

    # Public functions

    def get_sinks(self) -> List[SinkDescription]:
        """Returns a list of all sinks (including temporary ones)"""
        _sinks: List[SinkDescription] = []
        for sink in self.sink_descriptions:
            sink_copy = copy(sink)
            _sinks.append(self._apply_hostname_to_entity(sink_copy))
        # Include temporary sinks
        for temp_sink in self.active_temporary_sinks:
            temp_sink_copy = copy(temp_sink)
            _sinks.append(self._apply_hostname_to_entity(temp_sink_copy))
        return _sinks

    def get_permanent_sinks(self) -> List[SinkDescription]:
        """Returns a list of all sinks (excluding temporary ones)"""
        _sinks: List[SinkDescription] = []
        for sink in self.sink_descriptions:
            if sink.is_temporary:
                continue
            sink_copy = copy(sink)
            _sinks.append(self._apply_hostname_to_entity(sink_copy))
        return _sinks

    def add_sink(self, sink: SinkDescription) -> bool:
        """Adds a sink or sink group"""
        self.__verify_new_sink(sink)
        # Ensure config_id is set
        if not sink.config_id:
            sink.config_id = str(uuid.uuid4())
            _logger.info(f"Generated config_id {sink.config_id} for new sink '{sink.name}'")
        self.sink_descriptions.append(sink)
        self.__reload_configuration()
        return True

    def update_sink(self, new_sink: SinkDescription, old_sink_name: SinkNameType) -> bool:
        """Updates fields on the sink indicated by old_sink_name to what is specified in new_sink
           Undefined fields are ignored"""
        print(f"Updating {old_sink_name}")
        is_eq_only: bool = True
        is_eq_found: bool = False
        changed_sink: SinkDescription = self.get_sink_by_name(old_sink_name)
        if new_sink.name != old_sink_name:
            for sink in self.sink_descriptions:
                if sink.name == new_sink.name:
                    raise ValueError(f"Name {new_sink.name} already used")
        for field in new_sink.model_fields_set:
            if field == "config_id":
                # config_id should never be modified after creation
                _logger.warning(f"Attempt to modify config_id for sink {old_sink_name} - ignoring this change")
                continue
            if field == "equalizer":
                is_eq_found = True
            elif field != "name":
                is_eq_only = False
            setattr(changed_sink, field, getattr(new_sink, field))

        for sink in self.sink_descriptions:
            for index, group_member in enumerate(sink.group_members):
                if group_member == old_sink_name:
                    sink.group_members[index] = changed_sink.name
        for route in self.route_descriptions:
            if route.sink == old_sink_name:
                route.sink = changed_sink.name

        # Always trigger a full reload for simplicity and robustness with C++ engine
        self.__reload_configuration()
        return True

    def delete_sink(self, sink_name: SinkNameType) -> bool:
        """Deletes a sink by name"""
        _logger.debug(f"Deleting {sink_name}")
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        removed_groups = self.__remove_sink_from_groups(sink.name)
        if removed_groups:
            _logger.debug("Removed sink '%s' from groups %s", sink.name, list(removed_groups.keys()))
        try:
            self.__verify_sink_unused(sink)
        except InUseError as exc:
            if removed_groups:
                self.__restore_sink_memberships(removed_groups)
            raise exc
        self.sink_descriptions.remove(sink)
        self.delete_route_by_sink(sink)
        self.__reload_configuration()
        return True

    def enable_sink(self, sink_name: SinkNameType) -> bool:
        """Enables a sink by name"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        sink.enabled = True
        self.__reload_configuration()
        return True

    def disable_sink(self, sink_name: SinkNameType) -> bool:
        """Disables a sink by name"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        sink.enabled = False
        self.__reload_configuration()
        return True

    def get_sources(self) -> List[SourceDescription]:
        """Get a list of all sources"""
        sources: List[SourceDescription] = []
        for source in self.source_descriptions:
            source_copy = copy(source)
            sources.append(self._apply_hostname_to_entity(source_copy))
        return sources

    def get_mdns_snapshot(self) -> Dict[str, Any]:
        """Aggregate discovery information from the mDNS pingers."""
        devices = self.mdns_pinger.serialize_devices()
        sink_settings = self.mdns_settings_pinger.serialize_sink_settings()
        source_settings = self.mdns_settings_pinger.serialize_source_settings()

        snapshot = MdnsSnapshot(
            devices=devices,
            sink_settings=sink_settings,
            source_settings=source_settings,
        )

        return snapshot.model_dump(mode="json")

    def _update_discovered_device_last_seen(self, method: str, identifier: str, **kwargs) -> bool:
        """Refresh last_seen and optional metadata for an existing discovered device."""
        key = f"{method}:{identifier}"
        existing_device: Optional[DiscoveredDevice] = self.discovered_devices.get(key)
        if not existing_device:
            return False

        updates: Dict[str, Any] = {"last_seen": datetime.now(timezone.utc).isoformat()}

        if "ip" in kwargs and kwargs["ip"]:
            updates["ip"] = str(kwargs["ip"])
        if "port" in kwargs and kwargs["port"] is not None:
            updates["port"] = kwargs["port"]
        if "name" in kwargs and kwargs["name"]:
            updates["name"] = kwargs["name"]
        if "tag" in kwargs and kwargs["tag"]:
            updates["tag"] = kwargs["tag"]
        if "device_type" in kwargs and kwargs["device_type"]:
            updates["device_type"] = kwargs["device_type"]

        incoming_properties = kwargs.get("properties")
        if isinstance(incoming_properties, dict) and incoming_properties:
            merged_properties = dict(existing_device.properties)
            merged_properties.update(incoming_properties)
            updates["properties"] = merged_properties

        self.discovered_devices[key] = existing_device.model_copy(update=updates)
        return True

    def _store_discovered_device(self, method: str, role: str, identifier: str, **kwargs) -> None:
        """Persist discovery metadata for later presentation and manual addition."""
        key = f"{method}:{identifier}"
        existing_device: Optional[DiscoveredDevice] = self.discovered_devices.get(key)
        properties: Dict[str, Any] = {}
        if existing_device:
            properties.update(existing_device.properties)
        incoming_properties = kwargs.get("properties") or {}
        if isinstance(incoming_properties, dict):
            properties.update(incoming_properties)
        tag = kwargs.get("tag") or (existing_device.tag if existing_device else None)
        if tag:
            properties.setdefault("tag", tag)
        properties.setdefault("identifier", identifier)

        ip = kwargs.get("ip") or (existing_device.ip if existing_device else None)
        if not ip:
            _logger.debug("[Configuration Manager] Skipping discovered device %s - no IP available", key)
            return

        ip_str = str(ip)

        port = kwargs.get("port") if "port" in kwargs else (existing_device.port if existing_device else None)
        name = kwargs.get("name") if "name" in kwargs else (existing_device.name if existing_device else None)
        device_type = kwargs.get("device_type") if "device_type" in kwargs else (existing_device.device_type if existing_device else None)

        if not name or name == ip_str:
            resolved_name = self.get_hostname_by_ip(ip_str)
            if resolved_name:
                name = resolved_name

        last_seen = datetime.now(timezone.utc).isoformat()

        device_data = {
            "discovery_method": method,
            "role": role,
            "ip": ip_str,
            "port": port,
            "name": name,
            "tag": tag,
            "properties": properties,
            "last_seen": last_seen,
            "device_type": device_type,
        }

        if existing_device:
            device = existing_device.model_copy(update=device_data)
        else:
            device = DiscoveredDevice(**device_data)
            _logger.info("[Configuration Manager] Discovered %s via %s: %s", role, method, device.ip)

        self.discovered_devices[key] = device

    def get_discovery_snapshot(self) -> Dict[str, Any]:
        """Return the consolidated discovery state for all mechanisms."""
        sink_settings = self.mdns_settings_pinger.serialize_sink_settings()
        source_settings = self.mdns_settings_pinger.serialize_source_settings()

        discovered_devices = sorted(
            self.discovered_devices.values(),
            key=lambda device: device.last_seen,
            reverse=True,
        )

        snapshot = UnifiedDiscoverySnapshot(
            discovered_devices=discovered_devices,
            sink_settings=sink_settings,
            source_settings=source_settings,
        )

        return snapshot.model_dump(mode="json")
    
    def get_processes_by_ip(self, ip: IPAddressType) -> List[SourceDescription]:
        """Get a list of all processes for a specific IP"""
        return [source for source in self.source_descriptions 
            if source.is_process and source.tag and source.tag.startswith(str(ip))]

    def add_source(self, source: SourceDescription) -> bool:
        """Add a source or source group"""
        self.__verify_new_source(source)
        # Ensure config_id is set
        if not source.config_id:
            source.config_id = str(uuid.uuid4())
            _logger.info(f"Generated config_id {source.config_id} for new source '{source.name}'")
        self.source_descriptions.append(source)
        self.__reload_configuration()
        return True

    def update_source(self, new_source: SourceDescription, old_source_name: SourceNameType) -> bool:
        """Updates fields on source 'old_source_name' to what's specified in new_source
           Undefined fields are not changed"""
        is_eq_only: bool = True
        is_eq_found: bool = False
        changed_source: SourceDescription = self.get_source_by_name(old_source_name)
        if new_source.name != old_source_name:
            for source in self.source_descriptions:
                if source.name == new_source.name:
                    raise ValueError(f"Name {new_source.name} already used")
        for field in new_source.model_fields_set:
            if field == "config_id":
                # config_id should never be modified after creation
                _logger.warning(f"Attempt to modify config_id for source {old_source_name} - ignoring this change")
                continue
            if field == "equalizer":
                is_eq_found = True
            elif field != "name":
                is_eq_only = False
        
            setattr(changed_source, field, getattr(new_source, field))
        for source in self.source_descriptions:
            for index, group_member in enumerate(source.group_members):
                if group_member == old_source_name:
                    source.group_members[index] = changed_source.name
        for route in self.route_descriptions:
            if route.source == old_source_name:
                route.source = changed_source.name

        # Always trigger a full reload
        self.__reload_configuration()
        return True

    def delete_source(self, source_name: SourceNameType) -> bool:
        """Deletes a source by name"""
        source: SourceDescription = self.get_source_by_name(source_name)
        removed_groups = self.__remove_source_from_groups(source.name)
        if removed_groups:
            _logger.debug("Removed source '%s' from groups %s", source.name, list(removed_groups.keys()))
        try:
            self.__verify_source_unused(source)
        except InUseError as exc:
            if removed_groups:
                self.__restore_source_memberships(removed_groups)
            raise exc
        self.source_descriptions.remove(source)
        self.delete_route_by_source(source)
        self.__reload_configuration()
        return True

    def enable_source(self, source_name: SourceNameType) -> bool:
        """Enables a source by index"""
        source: SourceDescription = self.get_source_by_name(source_name)
        source.enabled = True
        self.__reload_configuration()
        return True

    def disable_source(self, source_name: SourceNameType) -> bool:
        """Disables a source by index"""
        source: SourceDescription = self.get_source_by_name(source_name)
        source.enabled = False
        self.__reload_configuration()
        return True

    def get_routes(self) -> List[RouteDescription]:
        """Returns a list of all routes (including temporary ones)"""
        all_routes = copy(self.route_descriptions)
        # Include temporary routes
        all_routes.extend(copy(self.active_temporary_routes))
        return all_routes

    def get_permanent_routes(self) -> List[RouteDescription]:
        """Returns a list of all routes (including temporary ones)"""
        _routes: List[RoutekDescription] = []
        for route in self.route_descriptions:
            if route.is_temporary:
                continue
            _routes.append(copy(route))
        return _routes

    def add_route(self, route: RouteDescription) -> bool:
        """Adds a route"""
        self.__verify_new_route(route)
        # Ensure config_id is set
        if not route.config_id:
            route.config_id = str(uuid.uuid4())
            _logger.info(f"Generated config_id {route.config_id} for new route '{route.name}'")
        self.route_descriptions.append(route)
        self.__reload_configuration()
        return True

    def update_route(self, new_route: RouteDescription, old_route_name: RouteNameType) -> bool:
        """Updates fields on the route indicated by old_route_name to what is specified in new_route
           Undefined fields are ignored"""
        is_eq_only: bool = True
        is_eq_found: bool = False
        changed_route: RouteDescription = self.get_route_by_name(old_route_name)
        if new_route.name != old_route_name:
            for route in self.route_descriptions:
                if route.name == new_route.name:
                    raise ValueError(f"Name {new_route.name} already used")
        for field in new_route.model_fields_set:
            if field == "config_id":
                # config_id should never be modified after creation
                _logger.warning(f"Attempt to modify config_id for route {old_route_name} - ignoring this change")
                continue
            if field == "equalizer":
                is_eq_found = True
            elif field != "name":
                is_eq_only = False
            setattr(changed_route, field, getattr(new_route, field))
        # Always trigger a full reload
        self.__reload_configuration()
        return True

    def delete_route(self, route_name: RouteNameType) -> bool:
        """Deletes a route by name"""
        route: RouteDescription = self.get_route_by_name(route_name)
        self.route_descriptions.remove(route)
        self.__reload_configuration()
        return True

    def enable_route(self, route_name: RouteNameType) -> bool:
        """Enables a route by name"""
        route: RouteDescription = self.get_route_by_name(route_name)
        route.enabled = True
        self.__reload_configuration()
        return True

    def disable_route(self, route_name: RouteNameType) -> bool:
        """Disables a route by name"""
        route: RouteDescription = self.get_route_by_name(route_name)
        route.enabled = False
        self.__reload_configuration()
        return True
    
    def add_temporary_sink(self, sink: SinkDescription) -> bool:
        """Adds a temporary sink that won't be persisted to disk"""
        self.__verify_new_sink(sink)
        # Mark as temporary
        sink.is_temporary = True
        # Ensure config_id is set
        if not sink.config_id:
            sink.config_id = str(uuid.uuid4())
            _logger.info(f"Generated config_id {sink.config_id} for temporary sink '{sink.name}'")
        
        # Add to active temporary sinks list
        self.active_temporary_sinks.append(sink)
        
        # Apply configuration synchronously for temporary entities
        # This ensures the C++ engine knows about the sink before WebRTC tries to use it
        _logger.info(f"Applying temporary sink '{sink.name}' configuration synchronously")
        self.__apply_temporary_configuration_sync()
        return True
    
    def add_temporary_route(self, route: RouteDescription) -> bool:
        """Adds a temporary route that won't be persisted to disk"""
        self.__verify_new_route(route)
        # Mark as temporary
        route.is_temporary = True
        # Ensure config_id is set
        if not route.config_id:
            route.config_id = str(uuid.uuid4())
            _logger.info(f"Generated config_id {route.config_id} for temporary route '{route.name}'")
        
        # Add to active temporary routes list
        self.active_temporary_routes.append(route)
        
        # Apply configuration synchronously for temporary entities
        # This ensures the C++ engine knows about the route before it's used
        _logger.info(f"Applying temporary route '{route.name}' configuration synchronously")
        self.__apply_temporary_configuration_sync()
        return True
    
    def remove_temporary_sink(self, sink_name: SinkNameType) -> bool:
        """Removes a temporary sink"""
        # Find and remove from active temporary sinks
        for sink in self.active_temporary_sinks[:]:
            if sink.name == sink_name:
                self.active_temporary_sinks.remove(sink)
                _logger.info(f"Removed temporary sink '{sink_name}'")
                # Also remove any associated temporary routes
                for route in self.active_temporary_routes[:]:
                    if route.sink == sink_name:
                        self.active_temporary_routes.remove(route)
                        _logger.info(f"Removed associated temporary route '{route.name}'")
                # Apply configuration synchronously for temporary entity removal
                self.__apply_temporary_configuration_sync()
                return True
        
        _logger.warning(f"Temporary sink '{sink_name}' not found")
        return False
    
    def remove_temporary_route(self, route_name: RouteNameType) -> bool:
        """Removes a temporary route"""
        # Find and remove from active temporary routes
        for route in self.active_temporary_routes[:]:
            if route.name == route_name:
                self.active_temporary_routes.remove(route)
                _logger.info(f"Removed temporary route '{route_name}'")
                # Apply configuration synchronously for temporary entity removal
                self.__apply_temporary_configuration_sync()
                return True
        
        _logger.warning(f"Temporary route '{route_name}' not found")
        return False

    def update_source_equalizer(self, source_name: SourceNameType, equalizer: Equalizer) -> bool:
        """Set the equalizer for a source or source group"""
        source: SourceDescription = self.get_source_by_name(source_name)
        source.equalizer = equalizer
        self.__reload_configuration() # Trigger full reload
        return True

    def update_source_position(self, source_name: SourceNameType, new_index: int):
        """Set the position of a source in the list of sources"""
        source = self.get_source_by_name(source_name)
        self.source_descriptions.remove(source)
        self.source_descriptions.insert(new_index, source)

    def update_source_volume(self, source_name: SourceNameType, volume: VolumeType) -> bool:
        """Set the volume for a source or source group"""
        source: SourceDescription = self.get_source_by_name(source_name)
        source.volume = volume
        self.__reload_configuration() # Trigger full reload
        return True

    def update_source_timeshift(self, source_name: SourceNameType,
                                timeshift: TimeshiftType) -> bool:
        """Set the timeshift for a source or source group"""
        source: SourceDescription = self.get_source_by_name(source_name)
        source.timeshift = timeshift
        self.__reload_configuration() # Trigger full reload
        return True

    def update_source_delay(self, source_name: SourceNameType, delay: DelayType) -> bool:
        """Set the delay for a source or source group"""
        source: SourceDescription = self.get_source_by_name(source_name)
        source.delay = delay
        self.__reload_configuration() # Trigger full reload
        return True

    _VNC_MEDIA_KEYCODES: Dict[str, int] = {
        "play_pause": 0x1008FF14,
        "next_track": 0x1008FF17,
        "previous_track": 0x1008FF16,
    }

    def _send_vnc_key_for_source(self, source: SourceDescription, key: str) -> bool:
        """Send a single key press to the configured VNC endpoint."""

        if not source.vnc_ip:
            raise RuntimeError(
                f"Source '{source.name}' does not have a VNC IP configured"
            )

        keycode = self._VNC_MEDIA_KEYCODES.get(key)
        if keycode is None:
            raise RuntimeError(f"Unsupported VNC media key '{key}'")

        # Default to the standard VNC port when none is supplied.
        port = 5900
        if source.vnc_port:
            try:
                port = int(source.vnc_port)
            except (TypeError, ValueError):
                _logger.warning(
                    "[Controller Manager] Invalid VNC port '%s' for source %s, using %s",
                    source.vnc_port,
                    source.name,
                    port,
                )

        target_host = str(source.vnc_ip)
        address = f"{target_host}::{port}"

        _logger.info(
            "[Controller Manager] Sending VNC key '%s' (keycode=0x%X) to %s (%s)",
            key,
            keycode,
            source.name,
            address,
        )

        client = SimpleVNCClient(target_host, port=port, timeout=5.0)

        try:
            client.connect()
            client.send_key(keycode, pressed=True)
            client.send_key(keycode, pressed=False)
        except Exception as exc:  # pylint: disable=broad-except
            _logger.error(
                "[Controller Manager] Failed to send VNC key '%s' to %s (%s): %s",
                key,
                source.name,
                address,
                exc,
            )
            raise RuntimeError(
                f"Failed to send VNC key '{key}' to source '{source.name}'"
            ) from exc
        finally:
            client.close()

        return True

    def source_next_track(self, source_name: SourceNameType) -> bool:
        """Send a Next Track command to the source"""
        source: SourceDescription = self.get_source_by_name(source_name)
        return self._send_vnc_key_for_source(source, "next_track")

    async def source_self_previous_track(self, request: Request) -> bool:
        """Send a Previous Track command to the source that made the request"""
        client_ip = await self.__get_client_ip(request)
        source = self.__get_source_by_ip(client_ip)
        return self.source_previous_track(source.name)

    async def update_self_source_volume(self, request: fastapi.Request, volume: VolumeType) -> bool:
        """Set the volume for the source that made the request"""
        client_ip = await self.__get_client_ip(request)
        source = self.__get_source_by_ip(client_ip)
        return self.update_source_volume(source.name, volume)

    async def source_self_play(self, request: fastapi.Request) -> bool:
        """Send a Play/Pause command to the source that made the request"""
        client_ip = await self.__get_client_ip(request)
        source = self.__get_source_by_ip(client_ip)
        return self.source_play(source.name)

    async def source_self_next_track(self, request: fastapi.Request) -> bool:
        """Send a Next Track command to the source that made the request"""
        client_ip = await self.__get_client_ip(request)
        source = self.__get_source_by_ip(client_ip)
        return self.source_next_track(source.name)

    async def __get_client_ip(self, request: fastapi.Request) -> IPv4Address:
        """Get the IP address of the client making the request"""
        if not request.client:
            raise Exception("Web request for client IP with no client object set")
        client_host = request.client.host
        return IPv4Address(client_host)

    def __get_source_by_ip(self, ip: IPv4Address) -> SourceDescription:
        """Get the source description by IP address"""
        for source in self.source_descriptions:
            if str(source.ip) == str(ip):
                return source
        raise ValueError(f"No source found with IP {ip}")

    def __get_sink_by_ip(self, ip: IPv4Address) -> SinkDescription:
        """Get the sink description by IP address"""
        for sink in self.sink_descriptions:
            if str(sink.ip) == str(ip):
                return sink
        raise ValueError(f"No sink found with IP {ip}")

    def __get_sink_by_config_id(self, config_id: str) -> SinkDescription:
        """Get the sink description by config ID"""
        for sink in self.sink_descriptions:
            if str(sink.config_id) == config_id:
                return sink
        raise ValueError(f"No sink found with ID {config_id}")

    def source_previous_track(self, source_name: SourceNameType) -> bool:
        """Send a Previous Track command to the source"""
        source: SourceDescription = self.get_source_by_name(source_name)
        return self._send_vnc_key_for_source(source, "previous_track")

    def source_play(self, source_name: SourceNameType) -> bool:
        """Send a Play/Pause media command to the source"""
        source: SourceDescription = self.get_source_by_name(source_name)
        _logger.info(
            "[Controller Manager] Sending Play/Pause via VNC to %s", source.vnc_ip
        )
        return self._send_vnc_key_for_source(source, "play_pause")

    def update_sink_equalizer(self, sink_name: SinkNameType, equalizer: Equalizer) -> bool:
        """Set the equalizer for a sink or sink group"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        sink.equalizer = equalizer
        _logger.debug("Updating EQ for %s", sink_name)
        self.__reload_configuration() # Trigger full reload
        return True

    def update_sink_position(self, sink_name: SinkNameType, new_index: int):
        """Set the position of a sink in the list of sinks"""
        sink = self.get_sink_by_name(sink_name)
        self.sink_descriptions.remove(sink)
        self.sink_descriptions.insert(new_index, sink)

    def update_sink_volume(self, sink_name: SinkNameType, volume: VolumeType) -> bool:
        """Set the volume for a sink or sink group"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        sink.volume = volume
        self.__reload_configuration() # Trigger full reload
        return True

    def update_sink_timeshift(self, sink_name: SinkNameType,
                                timeshift: TimeshiftType) -> bool:
        """Set the timeshift for a sink or sink group"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        sink.timeshift = timeshift
        self.__reload_configuration() # Trigger full reload
        return True

    def update_sink_delay(self, sink_name: SinkNameType, delay: DelayType) -> bool:
        """Set the delay for a sink or sink group"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        sink.delay = delay
        self.__reload_configuration() # Trigger full reload
        return True

    def update_sink_volume_normalization(self, sink_name: SinkNameType, volume_normalization: bool) -> bool:
        """Set the volume normalization for a sink or sink group"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        sink.volume_normalization = volume_normalization
        self.__reload_configuration() # Trigger full reload
        return True

    def update_route_equalizer(self, route_name: RouteNameType, equalizer: Equalizer) -> bool:
        """Set the equalizer for a route"""
        route: RouteDescription = self.get_route_by_name(route_name)
        route.equalizer = equalizer
        self.__reload_configuration() # Trigger full reload
        return True

    def update_route_position(self, route_name: RouteNameType, new_index: int):
        """Set the position of a route in the list of routes"""
        route = self.get_route_by_name(route_name)
        self.route_descriptions.remove(route)
        self.route_descriptions.insert(new_index, route)

    def update_route_volume(self, route_name: RouteNameType, volume: VolumeType) -> bool:
        """Set the volume for a route"""
        route: RouteDescription = self.get_route_by_name(route_name)
        route.volume = volume
        self.__reload_configuration() # Trigger full reload
        return True

    def update_route_timeshift(self, route_name: RouteNameType,
                                timeshift: TimeshiftType) -> bool:
        """Set the timeshift for a route"""
        route: RouteDescription = self.get_route_by_name(route_name)
        route.timeshift = timeshift
        self.__reload_configuration() # Trigger full reload
        return True

    def update_route_delay(self, route_name: RouteNameType, delay: DelayType) -> bool:
        """Set the delay for a route"""
        route: RouteDescription = self.get_route_by_name(route_name)
        route.delay = delay
        self.__reload_configuration() # Trigger full reload
        return True

    def stop(self) -> bool:
        """Stop all threads/processes"""
        _logger.debug("[Configuration Manager] Stopping webstream")
        self.__api_webstream.stop()
        _logger.debug("[Configuration Manager] Webstream stopped")
        # Stop plugins first to prevent further writes into the engine during shutdown
        _logger.debug("[Configuration Manager] Stopping Plugin Manager")
        self.plugin_manager.stop_registered_plugins()
        _logger.debug("[Configuration Manager] Plugin Manager Stopped")

        # --- C++ Engine Shutdown ---
        if self.cpp_audio_manager:
            try:
                _logger.info("[Configuration Manager] Shutting down C++ AudioManager...")
                self.cpp_audio_manager.shutdown()
                _logger.info("[Configuration Manager] C++ AudioManager shutdown complete.")
            except Exception as e:
                _logger.exception("[Configuration Manager] Exception during C++ AudioManager shutdown: %s", e)
        # --- End C++ Engine Shutdown ---

        _logger.debug("[Configuration Manager] Stopping Python receivers")
        _logger.debug("[Configuration Manager] Receiver stopped")
        _logger.debug("[Configuration Manager] Stopping mDNS")
        self.mdns_responder.stop()
        self.mdns_pinger.stop()
        self.mdns_settings_pinger.stop()
        self.mdns_scream_advertiser.stop()
        _logger.debug("[Configuration Manager] mDNS stopped")
        
        # Clean up temporary entities
        self.temp_entity_manager.cleanup_all_entities()
        self.active_temporary_sinks.clear()
        self.active_temporary_routes.clear()
        _logger.debug("[Configuration Manager] Temporary entities cleaned up")
        
        self.running = False

        if constants.WAIT_FOR_CLOSES:
            try:
                self.join(5)
            except TimeoutExpired:
                _logger.warning("[Configuration Manager] Configuration Manager failed to close")
        _logger.debug("[Configuration Manager] Done, returning")
        return True

    def set_webstream(self, webstream: APIWebStream) -> None:
        """Sets the webstream"""
        self.__api_webstream = webstream
        self.__reload_configuration()

    def get_source_by_name(self, source_name: SourceNameType) -> SourceDescription:
        """Returns a SourceDescription by name"""
        print(self.source_descriptions)
        source_locator: List[SourceDescription] = [source for source in self.source_descriptions
                                                    if source.name == source_name]
        if len(source_locator) == 0:
            raise NameError(f"source {source_name} not found")
        return source_locator[0]

    def get_sink_by_name(self, sink_name: SinkNameType) -> SinkDescription:
        """Returns a SinkDescription by name (checks both permanent and temporary sinks)"""
        # Check permanent sinks first
        sink_locator: List[SinkDescription] = [sink for sink in self.sink_descriptions
                                                    if sink.name == sink_name]
        if len(sink_locator) > 0:
            return sink_locator[0]
        
        # Check temporary sinks
        temp_sink_locator: List[SinkDescription] = [sink for sink in self.active_temporary_sinks
                                                         if sink.name == sink_name]
        if len(temp_sink_locator) > 0:
            return temp_sink_locator[0]
        
        raise NameError(f"sink {sink_name} not found")

    def get_route_by_name(self, route_name: RouteNameType) -> RouteDescription:
        """Returns a RouteDescription by name (checks both permanent and temporary routes)"""
        # Check permanent routes first
        route_locator: List[RouteDescription] = [route for route in self.route_descriptions
                                                    if route.name == route_name]
        if len(route_locator) > 0:
            return route_locator[0]
        
        # Check temporary routes
        temp_route_locator: List[RouteDescription] = [route for route in self.active_temporary_routes
                                                           if route.name == route_name]
        if len(temp_route_locator) > 0:
            return temp_route_locator[0]
        
        raise NameError(f"Route {route_name} not found")

    def get_website_context(self):
        """Returns a tuple containing (List[SourceDescription],
                                       List[SinkDescription],
                                       List[RouteDescription])
           with all of the known Sources, Sinks, and Routes (including temporary ones)
        """
        # Combine permanent and temporary entities
        all_sinks = self.sink_descriptions + self.active_temporary_sinks
        all_routes = self.route_descriptions + self.active_temporary_routes
        
        data: Tuple[List[SourceDescription], List[SinkDescription], List[RouteDescription]]
        data = (self.source_descriptions, all_sinks, all_routes)
        return data

    # Private Functions

    # Verification functions

    def __verify_new_sink(self, sink: SinkDescription) -> None:
        """Verifies all sink group members exist and the sink doesn't
           Throws exception on failure"""
        # Check both permanent and temporary sinks for name conflicts
        for _sink in self.sink_descriptions + self.active_temporary_sinks:
            if sink.name == _sink.name:
                raise ValueError(f"Sink name '{sink.name}' already in use")
        for member in sink.group_members:
            self.get_sink_by_name(member)
        if sink.is_group:
            for member in sink.group_members:
                self.get_sink_by_name(member)

    def __verify_new_source(self, source: SourceDescription) -> None:
        """Verifies all source group members exist and the sink doesn't
           Throws exception on failure"""
        for _source in self.source_descriptions:
            if source.name == _source.name:
                raise ValueError(f"Source name '{source.name}' already in use")
        if source.is_group:
            for member in source.group_members:
                self.get_source_by_name(member)

    def __verify_new_route(self, route: RouteDescription) -> None:
        """Verifies route sink and source exist, throws exception if not"""
        self.get_sink_by_name(route.sink)
        self.get_source_by_name(route.source)

    def __remove_sink_from_groups(self, sink_name: SinkNameType) -> Dict[SinkNameType, List[SinkNameType]]:
        """Remove the sink from every group that references it."""
        groups_updated: Dict[SinkNameType, List[SinkNameType]] = {}
        for group in self.sink_descriptions + self.active_temporary_sinks:
            if not group.is_group or group.name == sink_name:
                continue
            if sink_name in group.group_members:
                groups_updated[group.name] = list(group.group_members)
                group.group_members = [member for member in group.group_members if member != sink_name]
        return groups_updated

    def __remove_source_from_groups(self, source_name: SourceNameType) -> Dict[SourceNameType, List[SourceNameType]]:
        """Remove the source from every group that references it."""
        groups_updated: Dict[SourceNameType, List[SourceNameType]] = {}
        for group in self.source_descriptions:
            if not group.is_group or group.name == source_name:
                continue
            if source_name in group.group_members:
                groups_updated[group.name] = list(group.group_members)
                group.group_members = [member for member in group.group_members if member != source_name]
        return groups_updated

    def __restore_sink_memberships(self, original_memberships: Dict[SinkNameType, List[SinkNameType]]) -> None:
        """Restore group membership state when deletion cannot proceed."""
        for group in self.sink_descriptions + self.active_temporary_sinks:
            if group.name in original_memberships:
                group.group_members = original_memberships[group.name]

    def __restore_source_memberships(self, original_memberships: Dict[SourceNameType, List[SourceNameType]]) -> None:
        """Restore group membership state when deletion cannot proceed."""
        for group in self.source_descriptions:
            if group.name in original_memberships:
                group.group_members = original_memberships[group.name]

    def __collect_sink_groups_for_member(self, member_name: SinkNameType, visited: Optional[Set[str]] = None) -> List[SinkDescription]:
        """Return all groups that include the member (directly or indirectly)."""
        if visited is None:
            visited = set()
        sink_groups: List[SinkDescription] = []
        for group in self.sink_descriptions + self.active_temporary_sinks:
            if not group.is_group or group.name in visited:
                continue
            if member_name in group.group_members:
                visited.add(group.name)
                sink_groups.append(group)
                sink_groups.extend(self.__collect_sink_groups_for_member(group.name, visited))
        return sink_groups

    def __collect_source_groups_for_member(self, member_name: SourceNameType, visited: Optional[Set[str]] = None) -> List[SourceDescription]:
        """Return all groups that include the member (directly or indirectly)."""
        if visited is None:
            visited = set()
        source_groups: List[SourceDescription] = []
        for group in self.source_descriptions:
            if not group.is_group or group.name in visited:
                continue
            if member_name in group.group_members:
                visited.add(group.name)
                source_groups.append(group)
                source_groups.extend(self.__collect_source_groups_for_member(group.name, visited))
        return source_groups

    def __verify_source_unused(self, source: SourceDescription) -> None:
        """Verifies a source is unused by any routes, throws exception if not"""
        groups: List[SourceDescription] = self.__collect_source_groups_for_member(source.name)
        if groups:
            group_names = [group.name for group in groups]
            raise InUseError(f"Source: {source.name} is in use by Groups {group_names}")
        enabled_routes = [route.name for route in self.route_descriptions if route.enabled and route.source == source.name]
        if enabled_routes:
            routes_str = ", ".join(enabled_routes)
            raise InUseError(f"Source: {source.name} is in use by Route(s) {routes_str}")

    def __verify_sink_unused(self, sink: SinkDescription) -> None:
        """Verifies a sink is unused by any routes, throws exception if not"""
        groups: List[SinkDescription] = self.__collect_sink_groups_for_member(sink.name)
        if groups:
            group_names = [group.name for group in groups]
            raise InUseError(f"Sink: {sink.name} is in use by Groups {group_names}")
        enabled_routes = [route.name for route in self.route_descriptions if route.enabled and route.sink == sink.name]
        if enabled_routes:
            routes_str = ", ".join(enabled_routes)
            raise InUseError(f"Sink: {sink.name} is in use by Route(s) {routes_str}")
            
    def delete_route_by_source(self, source: SourceDescription) -> None:
        """Deletes all routes associated with a given Source"""
        self.__verify_source_unused(source)
        routes_to_remove: List[RouteDescription] = []
        for route in self.route_descriptions:
            if route.source == source.name:
                routes_to_remove.append(route)

        for route in routes_to_remove:
            self.route_descriptions.remove(route)

    def delete_route_by_sink(self, sink: SinkDescription) -> None:
        """Deletes all routes associated with a given Sink"""
        self.__verify_sink_unused(sink)
        routes_to_remove: List[RouteDescription] = []
        for route in self.route_descriptions:
            if route.sink == sink.name:
                routes_to_remove.append(route)

        for route in routes_to_remove:
            self.route_descriptions.remove(route)

# Configuration load/save functions

    def __load_config(self) -> None:
        """Loads the config"""
        self.sink_descriptions = []
        self.source_descriptions = []
        self.route_descriptions = []
        self.system_capture_devices = []
        self.system_playback_devices = []
        config_needs_save = False  # Track if we need to save due to missing config_ids
        
        try:
            with open(constants.CONFIG_PATH, "r", encoding="UTF-8") as f:
                savedata: dict = yaml.unsafe_load(f)

                raw_sinks = savedata.get("sinks", [])
                for item_data in raw_sinks:
                    try:
                        if isinstance(item_data, dict):
                            # Migrate old speaker_layout to speaker_layouts
                            if "speaker_layout" in item_data and "speaker_layouts" not in item_data:
                                old_layout_data = item_data.pop("speaker_layout")
                                default_input_key = 2
                                migrated_layout = SpeakerLayout(**old_layout_data) if isinstance(old_layout_data, dict) else \
                                                  (old_layout_data if isinstance(old_layout_data, SpeakerLayout) else SpeakerLayout())
                                item_data["speaker_layouts"] = {default_input_key: migrated_layout}
                                _logger.info(f"Migrated old 'speaker_layout' to 'speaker_layouts' for sink '{item_data.get('name', 'Unknown')}' using default key {default_input_key}.")

                            # Correct string keys from YAML to int keys for speaker_layouts
                            if "speaker_layouts" in item_data and isinstance(item_data["speaker_layouts"], dict):
                                corrected_layouts = {}
                                for key_str, layout_data_dict in item_data["speaker_layouts"].items():
                                    try:
                                        key_int = int(key_str)
                                        parsed_layout = SpeakerLayout(**layout_data_dict) if isinstance(layout_data_dict, dict) else \
                                                        (layout_data_dict if isinstance(layout_data_dict, SpeakerLayout) else SpeakerLayout())
                                        corrected_layouts[key_int] = parsed_layout
                                    except ValueError:
                                        _logger.warning(f"Could not convert speaker_layouts key '{key_str}' to int for sink '{item_data.get('name', 'Unknown')}'. Skipping.")
                                    except Exception as e_sl_parse:
                                        _logger.error(f"Failed to parse SpeakerLayout data for key '{key_str}' in sink '{item_data.get('name', 'Unknown')}': {e_sl_parse}. Skipping.")
                                item_data["speaker_layouts"] = corrected_layouts
                            
                            # Create the SinkDescription instance
                            sink_instance = SinkDescription(**item_data)
                            
                            # ALWAYS generate config_id if missing or null
                            if not sink_instance.config_id or sink_instance.config_id == 'null' or sink_instance.config_id == 'None':
                                sink_instance.config_id = str(uuid.uuid4())
                                _logger.info(f"Auto-generated config_id {sink_instance.config_id} for sink '{sink_instance.name}'")
                                config_needs_save = True  # Mark that we need to save
                            
                            self.sink_descriptions.append(sink_instance)
                        elif isinstance(item_data, SinkDescription):
                            self.sink_descriptions.append(SinkDescription(**item_data.model_dump()))
                        else:
                            _logger.warning(f"Skipping unexpected sink data type: {type(item_data)} for data: {item_data}")
                    except Exception as e:
                        _logger.error(f"Failed to parse sink data: {item_data}. Error: {e}")
                
                raw_sources = savedata.get("sources", [])
                for item_data in raw_sources:
                    try:
                        if isinstance(item_data, dict):
                            # Migrate old speaker_layout to speaker_layouts
                            if "speaker_layout" in item_data and "speaker_layouts" not in item_data:
                                old_layout_data = item_data.pop("speaker_layout")
                                default_input_key = 2
                                migrated_layout = SpeakerLayout(**old_layout_data) if isinstance(old_layout_data, dict) else \
                                                  (old_layout_data if isinstance(old_layout_data, SpeakerLayout) else SpeakerLayout())
                                item_data["speaker_layouts"] = {default_input_key: migrated_layout}
                                _logger.info(f"Migrated old 'speaker_layout' to 'speaker_layouts' for source '{item_data.get('name', 'Unknown')}' using default key {default_input_key}.")

                            # Correct string keys from YAML to int keys for speaker_layouts
                            if "speaker_layouts" in item_data and isinstance(item_data["speaker_layouts"], dict):
                                corrected_layouts = {}
                                for key_str, layout_data_dict in item_data["speaker_layouts"].items():
                                    try:
                                        key_int = int(key_str)
                                        parsed_layout = SpeakerLayout(**layout_data_dict) if isinstance(layout_data_dict, dict) else \
                                                        (layout_data_dict if isinstance(layout_data_dict, SpeakerLayout) else SpeakerLayout())
                                        corrected_layouts[key_int] = parsed_layout
                                    except ValueError:
                                        _logger.warning(f"Could not convert speaker_layouts key '{key_str}' to int for source '{item_data.get('name', 'Unknown')}'. Skipping.")
                                    except Exception as e_sl_parse:
                                        _logger.error(f"Failed to parse SpeakerLayout data for key '{key_str}' in source '{item_data.get('name', 'Unknown')}': {e_sl_parse}. Skipping.")
                                item_data["speaker_layouts"] = corrected_layouts
                            
                            # Create the SourceDescription instance
                            source_instance = SourceDescription(**item_data)
                            
                            # ALWAYS generate config_id if missing or null
                            if not source_instance.config_id or source_instance.config_id == 'null' or source_instance.config_id == 'None':
                                source_instance.config_id = str(uuid.uuid4())
                                _logger.info(f"Auto-generated config_id {source_instance.config_id} for source '{source_instance.name}'")
                                config_needs_save = True  # Mark that we need to save
                            
                            self.source_descriptions.append(source_instance)
                        elif isinstance(item_data, SourceDescription):
                            self.source_descriptions.append(SourceDescription(**item_data.model_dump()))
                        else:
                            _logger.warning(f"Skipping unexpected source data type: {type(item_data)} for data: {item_data}")
                    except Exception as e:
                        _logger.error(f"Failed to parse source data: {item_data}. Error: {e}")

                raw_routes = savedata.get("routes", [])
                for item_data in raw_routes:
                    try:
                        if isinstance(item_data, dict):
                            # Migrate old speaker_layout to speaker_layouts
                            if "speaker_layout" in item_data and "speaker_layouts" not in item_data:
                                old_layout_data = item_data.pop("speaker_layout")
                                default_input_key = 2
                                migrated_layout = SpeakerLayout(**old_layout_data) if isinstance(old_layout_data, dict) else \
                                                  (old_layout_data if isinstance(old_layout_data, SpeakerLayout) else SpeakerLayout())
                                item_data["speaker_layouts"] = {default_input_key: migrated_layout}
                                _logger.info(f"Migrated old 'speaker_layout' to 'speaker_layouts' for route '{item_data.get('name', 'Unknown')}' using default key {default_input_key}.")

                            # Correct string keys from YAML to int keys for speaker_layouts
                            if "speaker_layouts" in item_data and isinstance(item_data["speaker_layouts"], dict):
                                corrected_layouts = {}
                                for key_str, layout_data_dict in item_data["speaker_layouts"].items():
                                    try:
                                        key_int = int(key_str)
                                        parsed_layout = SpeakerLayout(**layout_data_dict) if isinstance(layout_data_dict, dict) else \
                                                        (layout_data_dict if isinstance(layout_data_dict, SpeakerLayout) else SpeakerLayout())
                                        corrected_layouts[key_int] = parsed_layout
                                    except ValueError:
                                        _logger.warning(f"Could not convert speaker_layouts key '{key_str}' to int for route '{item_data.get('name', 'Unknown')}'. Skipping.")
                                    except Exception as e_sl_parse:
                                        _logger.error(f"Failed to parse SpeakerLayout data for key '{key_str}' in route '{item_data.get('name', 'Unknown')}': {e_sl_parse}. Skipping.")
                                item_data["speaker_layouts"] = corrected_layouts
                            
                            # Create the RouteDescription instance
                            route_instance = RouteDescription(**item_data)
                            
                            # ALWAYS generate config_id if missing or null
                            if not route_instance.config_id or route_instance.config_id == 'null' or route_instance.config_id == 'None':
                                route_instance.config_id = str(uuid.uuid4())
                                _logger.info(f"Auto-generated config_id {route_instance.config_id} for route '{route_instance.name}'")
                                config_needs_save = True  # Mark that we need to save
                            
                            self.route_descriptions.append(route_instance)
                        elif isinstance(item_data, RouteDescription):
                            self.route_descriptions.append(RouteDescription(**item_data.model_dump()))
                        else:
                            _logger.warning(f"Skipping unexpected route data type: {type(item_data)} for data: {item_data}")
                    except Exception as e:
                        _logger.error(f"Failed to parse route data: {item_data}. Error: {e}")
                raw_capture_devices = savedata.get("system_capture_devices")
                raw_playback_devices = savedata.get("system_playback_devices")

                if raw_capture_devices is None:
                    raw_capture_devices = []
                    config_needs_save = True
                if raw_playback_devices is None:
                    raw_playback_devices = []
                    config_needs_save = True

                self.system_capture_devices = self._parse_system_device_list(raw_capture_devices, "capture")
                self.system_playback_devices = self._parse_system_device_list(raw_playback_devices, "playback")

                # The Pydantic models now have default_factory=dict for speaker_layouts,
                # so the old check loop for None speaker_layout is no longer needed.

        except FileNotFoundError:
            _logger.warning("[Configuration Manager] Configuration not found., making new config. Initializing with empty lists.")
            self.sink_descriptions = []
            self.source_descriptions = []
            self.route_descriptions = []
        except KeyError as exc:
            _logger.error("[Configuration Manager] Configuration key %s missing, exiting.", exc)
            # Initialize with empty lists to prevent further errors down the line if a key is missing
            self.sink_descriptions = []
            self.source_descriptions = []
            self.route_descriptions = []
            raise exc # Re-raise after logging and setting defaults
        except Exception as e: # Catch any other unexpected errors during loading/parsing
            _logger.exception("[Configuration Manager] An unexpected error occurred during config loading: %s. Initializing with empty lists.", e)
            self.sink_descriptions = []
            self.source_descriptions = []
            self.route_descriptions = []

        # Save configuration if we generated any new config_ids
        if config_needs_save:
            _logger.info("[Configuration Manager] Saving configuration with newly generated config_ids")
            self.__save_config()

        self._safe_async_run(self._broadcast_full_configuration())

    def _infer_card_device_from_tag(self, tag: str, hw_id: Optional[str] = None) -> Tuple[int, int]:
        """Extract card and device indexes from a system device tag or hw_id."""
        try:
            if ":" in tag:
                _, body = tag.split(":", 1)
            else:
                body = tag
            if "." in body:
                card_str, device_str = body.split(".", 1)
                return int(card_str), int(device_str)
        except (ValueError, TypeError):
            pass

        if hw_id:
            try:
                if hw_id.startswith("hw:"):
                    hw_body = hw_id[3:]
                else:
                    hw_body = hw_id
                if "," in hw_body:
                    card_str, device_str = hw_body.split(",", 1)
                    return int(card_str), int(device_str)
            except (ValueError, TypeError):
                pass
        return -1, -1

    def _parse_system_device_list(self, raw_devices: Optional[List[Any]], direction_hint: Optional[str] = None) -> List[SystemAudioDeviceInfo]:
        """Coerce persisted device metadata into SystemAudioDeviceInfo models."""
        devices: List[SystemAudioDeviceInfo] = []
        if not raw_devices:
            return devices

        for item in raw_devices:
            model_data: Dict[str, Any]
            if isinstance(item, SystemAudioDeviceInfo):
                devices.append(item)
                continue

            if hasattr(item, "model_dump"):
                try:
                    model_data = item.model_dump()
                except Exception:  # pylint: disable=broad-except
                    model_data = dict(item)
            elif isinstance(item, dict):
                model_data = dict(item)
            else:
                continue

            tag = str(model_data.get("tag", "")).strip()
            direction = str(model_data.get("direction", direction_hint or "capture")).lower()
            if direction not in ("capture", "playback"):
                direction = direction_hint or "capture"

            channels_supported_raw = model_data.get("channels_supported", [])
            if isinstance(channels_supported_raw, list):
                channels_supported = [int(c) for c in channels_supported_raw if isinstance(c, (int, float))]
            else:
                channels_supported = []

            sample_rates_raw = model_data.get("sample_rates", [])
            if isinstance(sample_rates_raw, list):
                sample_rates = [int(r) for r in sample_rates_raw if isinstance(r, (int, float))]
            else:
                sample_rates = []

            card_index = int(model_data.get("card_index", -1))
            device_index = int(model_data.get("device_index", -1))
            if card_index < 0 or device_index < 0:
                inferred_card, inferred_device = self._infer_card_device_from_tag(tag, model_data.get("hw_id"))
                if card_index < 0:
                    card_index = inferred_card
                if device_index < 0:
                    device_index = inferred_device

            friendly_name = str(model_data.get("friendly_name", tag or ""))
            hw_id = model_data.get("hw_id")
            endpoint_id = model_data.get("endpoint_id")
            present = bool(model_data.get("present", False))

            devices.append(SystemAudioDeviceInfo(
                tag=tag,
                direction=direction,
                friendly_name=friendly_name,
                hw_id=hw_id if hw_id else None,
                endpoint_id=endpoint_id if endpoint_id else None,
                card_index=card_index,
                device_index=device_index,
                channels_supported=channels_supported,
                sample_rates=sample_rates,
                present=present,
            ))

        return devices

    def _normalize_device_direction(self, direction_obj: Any, default: str = "capture") -> str:
        """Normalize C++ direction enums/values to capture/playback strings."""
        if hasattr(direction_obj, "name"):
            value = str(direction_obj.name).lower()
        elif isinstance(direction_obj, str):
            value = direction_obj.lower()
        else:
            value = str(direction_obj).lower()

        if "playback" in value:
            return "playback"
        if "capture" in value:
            return "capture"
        try:
            numeric = int(direction_obj)
            if numeric == 1:
                return "playback"
            if numeric == 0:
                return "capture"
        except (TypeError, ValueError):
            pass
        return default

    def _build_device_from_snapshot(self, tag: str, info: Any) -> SystemAudioDeviceInfo:
        """Convert C++ SystemDeviceInfo snapshot object into Pydantic model."""
        friendly_name = str(getattr(info, "friendly_name", tag) or tag)
        hw_id = str(getattr(info, "hw_id", ""))
        endpoint_id = str(getattr(info, "endpoint_id", ""))
        direction = self._normalize_device_direction(getattr(info, "direction", None))

        channels_supported: List[int] = []
        channel_range = getattr(info, "channels", None)
        if channel_range is not None:
            for attr in ("min", "max"):
                value = getattr(channel_range, attr, 0)
                if isinstance(value, (int, float)) and value > 0:
                    channels_supported.append(int(value))
        channels_supported = sorted(set(channels_supported))

        sample_rates: List[int] = []
        rate_range = getattr(info, "sample_rates", None)
        if rate_range is not None:
            for attr in ("min", "max"):
                value = getattr(rate_range, attr, 0)
                if isinstance(value, (int, float)) and value > 0:
                    sample_rates.append(int(value))
        sample_rates = sorted(set(sample_rates))

        card_index, device_index = self._infer_card_device_from_tag(tag, hw_id)
        present = bool(getattr(info, "present", True))
        bit_depth_value = getattr(info, "bit_depth", None)
        try:
            bit_depth_value = int(bit_depth_value) if bit_depth_value not in (None, "") else None
        except Exception:  # pylint: disable=broad-except
            bit_depth_value = None

        return SystemAudioDeviceInfo(
            tag=tag,
            direction=direction,
            friendly_name=friendly_name,
            hw_id=hw_id or None,
            endpoint_id=endpoint_id or None,
            card_index=card_index,
            device_index=device_index,
            channels_supported=channels_supported,
            sample_rates=sample_rates,
            bit_depth=bit_depth_value,
            present=present,
        )

    def _merge_system_device_snapshot(self, snapshot: Any) -> bool:
        """Merge engine snapshot into cached device lists."""
        if not snapshot:
            _logger.debug("[Configuration Manager] Received empty system device snapshot.")
            return False

        try:
            items = list(snapshot.items())
        except AttributeError:
            try:
                items = [(key, snapshot[key]) for key in snapshot]
            except Exception:  # pylint: disable=broad-except
                _logger.exception("[Configuration Manager] Failed to iterate system device snapshot")
                return False

        _logger.debug("[Configuration Manager] Merging system device snapshot with %d entries", len(items))

        changed = False
        with self._system_device_lock:
            capture_map = {device.tag: device for device in self.system_capture_devices}
            playback_map = {device.tag: device for device in self.system_playback_devices}
            seen_capture: set[str] = set()
            seen_playback: set[str] = set()

            for tag_obj, info in items:
                try:
                    tag = str(tag_obj)
                except Exception:  # pylint: disable=broad-except
                    continue

                device_model = self._build_device_from_snapshot(tag, info)
                if device_model.direction == "capture":
                    existing = capture_map.get(tag)
                    if not existing or existing != device_model:
                        _logger.debug("[Configuration Manager] Capture device set: %s -> %s", tag, device_model.friendly_name)
                        capture_map[tag] = device_model
                        changed = True
                    seen_capture.add(tag)
                else:
                    existing = playback_map.get(tag)
                    if not existing or existing != device_model:
                        _logger.debug("[Configuration Manager] Playback device set: %s -> %s", tag, device_model.friendly_name)
                        playback_map[tag] = device_model
                        changed = True
                    seen_playback.add(tag)

            for tag, device in list(capture_map.items()):
                if tag not in seen_capture and device.present:
                    capture_map[tag] = device.model_copy(update={"present": False})
                    changed = True

            for tag, device in list(playback_map.items()):
                if tag not in seen_playback and device.present:
                    playback_map[tag] = device.model_copy(update={"present": False})
                    changed = True

            new_capture = sorted(capture_map.values(), key=lambda d: (d.card_index, d.device_index, d.tag))
            new_playback = sorted(playback_map.values(), key=lambda d: (d.card_index, d.device_index, d.tag))

            if new_capture != self.system_capture_devices:
                self.system_capture_devices = new_capture
                _logger.info("[Configuration Manager] Updated system capture device cache (%d devices)", len(new_capture))
                changed = True
            if new_playback != self.system_playback_devices:
                self.system_playback_devices = new_playback
                _logger.info("[Configuration Manager] Updated system playback device cache (%d devices)", len(new_playback))
                changed = True

        return changed

    def _apply_device_presence_notifications(self, notifications: List[Any]) -> bool:
        """Update cached presence flags based on notification list."""
        if not notifications:
            return False

        changed = False
        with self._system_device_lock:
            for note in notifications:
                try:
                    tag = str(getattr(note, "tag", ""))
                except Exception:  # pylint: disable=broad-except
                    continue
                if not tag:
                    continue

                direction = self._normalize_device_direction(getattr(note, "direction", None))
                present = bool(getattr(note, "present", False))
                target_list = self.system_capture_devices if direction == "capture" else self.system_playback_devices

                updated = False
                for idx, device in enumerate(target_list):
                    if device.tag == tag:
                        if device.present != present:
                            _logger.debug("[Configuration Manager] Updating presence for %s (direction=%s) -> %s", tag, direction, present)
                            target_list[idx] = device.model_copy(update={"present": present})
                            changed = True
                        updated = True
                        break

                if not updated and present:
                    inferred_card, inferred_device = self._infer_card_device_from_tag(tag)
                    _logger.debug("[Configuration Manager] Creating placeholder device for notification: %s", tag)
                    new_device = SystemAudioDeviceInfo(
                        tag=tag,
                        direction=direction,
                        friendly_name=tag,
                        card_index=inferred_card,
                        device_index=inferred_device,
                        channels_supported=[],
                        sample_rates=[],
                        present=True,
                    )
                    target_list.append(new_device)
                    changed = True

            if changed:
                _logger.debug("[Configuration Manager] Presence notifications triggered resort of device caches")
                self.system_capture_devices = sorted(self.system_capture_devices, key=lambda d: (d.card_index, d.device_index, d.tag))
                self.system_playback_devices = sorted(self.system_playback_devices, key=lambda d: (d.card_index, d.device_index, d.tag))

        return changed

    def _find_system_device_by_tag(self, tag: str) -> Optional[SystemAudioDeviceInfo]:
        """Lookup cached device information by tag."""
        with self._system_device_lock:
            for device in self.system_capture_devices:
                if device.tag == tag:
                    return device
            for device in self.system_playback_devices:
                if device.tag == tag:
                    return device
        return None

    async def _system_device_monitor_loop(self):
        """Periodic task to process system device notifications."""
        _logger.info("[Configuration Manager] System device monitor started")
        try:
            while self.running:
                try:
                    changed = await self._poll_system_device_notifications()
                except asyncio.CancelledError:
                    raise
                except Exception as exc:  # pylint: disable=broad-except
                    _logger.exception("[Configuration Manager] System device poller error: %s", exc)
                    changed = False

                if changed:
                    await self._broadcast_full_configuration()

                await asyncio.sleep(self._system_device_poll_interval)
        except asyncio.CancelledError:
            _logger.info("[Configuration Manager] System device monitor cancelled")
            raise
        finally:
            _logger.info("[Configuration Manager] System device monitor stopped")

    async def _poll_system_device_notifications(self) -> bool:
        """Drain notifications from C++ audio manager and update caches."""
        if not self.cpp_audio_manager or not screamrouter_audio_engine:
            return False

        notifications: List[Any]
        try:
            notifications = self.cpp_audio_manager.drain_device_notifications()
        except Exception as exc:  # pylint: disable=broad-except
            _logger.exception("[Configuration Manager] Failed to drain device notifications: %s", exc)
            return False

        if not notifications:
            return False

        changed = False
        if any(bool(getattr(note, "present", False)) for note in notifications):
            try:
                snapshot = self.cpp_audio_manager.list_system_devices()
            except Exception as exc:  # pylint: disable=broad-except
                _logger.exception("[Configuration Manager] Failed to list system devices: %s", exc)
                snapshot = None
            if snapshot is not None:
                changed |= self._merge_system_device_snapshot(snapshot)

        changed |= self._apply_device_presence_notifications(notifications)
        return changed

    def start_background_tasks(self) -> None:
        """Start asyncio background tasks that depend on a running event loop."""
        if not self.cpp_audio_manager or not screamrouter_audio_engine:
            _logger.debug("[Configuration Manager] Skipping system device monitor startup; audio manager unavailable")
            return
        if self._device_notification_task and not self._device_notification_task.done():
            return
        self._device_notification_task = asyncio.create_task(self._system_device_monitor_loop())

    def get_system_audio_device_snapshot(self) -> dict[str, list[SystemAudioDeviceInfo]]:
        """Thread-safe deep copy of cached system capture/playback devices for API responses."""
        with self._system_device_lock:
            capture = [device.model_copy(deep=True) for device in self.system_capture_devices]
            playback = [device.model_copy(deep=True) for device in self.system_playback_devices]
        return {
            "system_capture_devices": capture,
            "system_playback_devices": playback,
        }

    def _initialize_system_device_registry(self) -> None:
        """Populate system device caches from the audio manager on startup."""
        if not self.cpp_audio_manager or not screamrouter_audio_engine:
            return
        try:
            snapshot = self.cpp_audio_manager.list_system_devices()
        except Exception as exc:  # pylint: disable=broad-except
            _logger.warning("[Configuration Manager] Failed to fetch system device snapshot: %s", exc)
            return
        if snapshot:
            changed = self._merge_system_device_snapshot(snapshot)
            if changed:
                self._safe_async_run(self._broadcast_full_configuration())

    def __multiprocess_save(self):
        """Saves the config to config.yaml (excludes temporary entities)"""
        # Filter out temporary entities before saving
        permanent_sinks = [s for s in self.sink_descriptions if not getattr(s, 'is_temporary', False)]
        permanent_sources = [s for s in self.source_descriptions if not getattr(s, 'is_temporary', False)]
        permanent_routes = [r for r in self.route_descriptions if not getattr(r, 'is_temporary', False)]

        with self._system_device_lock:
            capture_devices = [device.model_copy(deep=True) for device in self.system_capture_devices]
            playback_devices = [device.model_copy(deep=True) for device in self.system_playback_devices]

        save_data: dict = {
            "sinks": permanent_sinks,
            "sources": permanent_sources,
            "routes": permanent_routes,
            "system_capture_devices": capture_devices,
            "system_playback_devices": playback_devices,
        }
        with open(constants.CONFIG_PATH, 'w', encoding="UTF-8") as yaml_file:
            yaml.dump(save_data, yaml_file)

    def __save_config(self) -> None:
        """Saves the config"""

        _logger.info("[Configuration Manager] Saving config")
        if not self.configuration_semaphore.acquire(timeout=1):
            raise TimeoutError("Failed to get configuration semaphore")
        proc = threading.Thread(target=self.__multiprocess_save)
        proc.start()
        proc.join()
        self._safe_async_run(self._broadcast_full_configuration())
        self.configuration_semaphore.release()

    # --- C++ State Translation (Task 04_02) ---

    def _translate_config_to_cpp_desired_state(self) -> Optional[screamrouter_audio_engine.DesiredEngineState]:
        """Translates the current active Python configuration into the C++ DesiredEngineState struct."""
        if not self.cpp_audio_manager or not self.cpp_config_applier or not screamrouter_audio_engine:
            _logger.warning("[Config Translator] C++ engine components not available. Skipping translation.")
            return None
        
        if not hasattr(self, 'active_configuration') or not self.active_configuration:
             _logger.warning("[Config Translator] No active configuration solved yet. Skipping translation.")
             return None

        _logger.info("[Config Translator] Starting translation to C++ DesiredEngineState...")
        cpp_desired_state = screamrouter_audio_engine.DesiredEngineState()
        processed_source_paths: dict[str, screamrouter_audio_engine.AppliedSourcePathParams] = {}
        processed_sinks_list: list[screamrouter_audio_engine.AppliedSinkParams] = [] # Temporary list for sinks

        # Ensure we have the necessary C++ types available
        try:
            CppSinkConfig = screamrouter_audio_engine.SinkConfig
            CppAppliedSinkParams = screamrouter_audio_engine.AppliedSinkParams
            CppAppliedSourcePathParams = screamrouter_audio_engine.AppliedSourcePathParams
            EQ_BANDS = screamrouter_audio_engine.EQ_BANDS # Get constant from C++ module
        except AttributeError as e:
            _logger.error("[Config Translator] Failed to access required C++ types/constants from module: %s", e)
            return None

        solved_config: dict[SinkDescription, List[SourceDescription]] = self.active_configuration.real_sinks_to_real_sources
        _logger.debug("[Config Translator] Solved config keys (Sinks): %s", list(solved_config.keys())) # Log the sink keys

        for py_sink_desc, py_source_desc_list in solved_config.items():
            is_temporary = getattr(py_sink_desc, 'is_temporary', False)
            _logger.debug("[Config Translator] Processing Sink: %s (temporary=%s, protocol=%s)",
                         py_sink_desc.name, is_temporary, py_sink_desc.protocol)
            
            # A. Create C++ AppliedSinkParams
            cpp_applied_sink = CppAppliedSinkParams()
            cpp_applied_sink.sink_id = py_sink_desc.config_id or py_sink_desc.name # Prefer config_id if available
            _logger.debug("[Config Translator]   Using sink_id: %s", cpp_applied_sink.sink_id)

            # B. Create and populate the nested C++ SinkConfig
            cpp_sink_engine_config = CppSinkConfig()
            cpp_sink_engine_config.id = cpp_applied_sink.sink_id # Use the same ID
            sink_ip_value = py_sink_desc.ip
            if isinstance(sink_ip_value, str):
                sink_address = sink_ip_value
            elif sink_ip_value is not None:
                sink_address = str(sink_ip_value)
            else:
                sink_address = ""

            if isinstance(sink_address, str) and sink_address.startswith("sr_in:"):
                label = sink_address[len("sr_in:"):]
                sanitized_label = self._sanitize_screamrouter_label(label)
                if sanitized_label and sanitized_label != label:
                    sink_address = f"sr_in:{sanitized_label}"
                    try:
                        py_sink_desc.ip = sink_address
                    except Exception:  # pylint: disable=broad-except
                        pass

            cpp_sink_engine_config.output_port = py_sink_desc.port if py_sink_desc.port else 0

            protocol_lower = (py_sink_desc.protocol or "").lower()
            protocol_is_system = protocol_lower in {"system_audio", "alsa", "wasapi"}
            if not protocol_is_system and isinstance(sink_address, str):
                if sink_address.startswith(("ap:", "hw:", "wp:", "ws:", "sr_in:")):
                    protocol_is_system = True
                    protocol_lower = "system_audio"
                    try:
                        py_sink_desc.protocol = "system_audio"
                    except Exception:  # pylint: disable=broad-except
                        pass

            if protocol_is_system:
                if protocol_lower != "system_audio":
                    try:
                        py_sink_desc.protocol = "system_audio"
                        protocol_lower = "system_audio"
                    except Exception:  # pylint: disable=broad-except
                        pass
                cpp_sink_engine_config.output_ip = sink_address
                lookup_tag = sink_address
                is_windows = sys.platform.startswith("win")

                if not is_windows and lookup_tag.startswith("hw:"):
                    hw_body = lookup_tag[3:]
                    if "," in hw_body:
                        card_str, device_str = hw_body.split(",", 1)
                        lookup_tag = f"ap:{card_str}.{device_str}"

                if lookup_tag:
                    device_info = self._find_system_device_by_tag(lookup_tag)
                    backend_label = "WASAPI" if (protocol_lower == "wasapi" or is_windows) else "ALSA"
                    if device_info:
                        if not device_info.present:
                            _logger.warning(
                                "[Config Translator] System audio sink '%s' references %s device %s which is currently offline.",
                                py_sink_desc.name,
                                backend_label,
                                lookup_tag,
                            )
                    else:
                        _logger.warning(
                            "[Config Translator] System audio sink '%s' references unknown %s device %s.",
                            py_sink_desc.name,
                            backend_label,
                            lookup_tag,
                        )
                else:
                    _logger.warning(
                        "[Config Translator] System audio sink '%s' missing device tag/address; playback may fail.",
                        py_sink_desc.name,
                    )
            else:
                cpp_sink_engine_config.output_ip = sink_address
            cpp_sink_engine_config.bitdepth = py_sink_desc.bit_depth
            cpp_sink_engine_config.samplerate = py_sink_desc.sample_rate
            cpp_sink_engine_config.channels = py_sink_desc.channels

            if protocol_is_system and isinstance(sink_address, str) and sink_address.startswith("sr_in:"):
                device_info = self._find_system_device_by_tag(sink_address)
                if device_info:
                    if device_info.hw_id:
                        cpp_sink_engine_config.output_ip = device_info.hw_id
                    if device_info.sample_rates:
                        cpp_sink_engine_config.samplerate = int(device_info.sample_rates[0])
                    if device_info.channels_supported:
                        cpp_sink_engine_config.channels = int(device_info.channels_supported[0])
                    if device_info.bit_depth:
                        cpp_sink_engine_config.bitdepth = int(device_info.bit_depth)
                        try:
                            py_sink_desc.bit_depth = int(device_info.bit_depth)
                        except Exception:  # pylint: disable=broad-except
                            pass

            # Define a mapping for channel layout strings to byte values
            channel_layout_map = {
                "mono": (0x04, 0x00),  # FC
                "stereo": (0x03, 0x00),  # FL, FR
                "2.1": (0x0B, 0x00),  # FL, FR, LFE
                "3.0": (0x07, 0x00),  # FL, FR, FC
                "3.1": (0x0F, 0x00),  # FL, FR, FC, LFE
                "quad": (0x33, 0x00), # FL, FR, BL, BR
                "surround": (0x07, 0x01), # FL, FR, FC, BC (common for older surround)
                "4.0": (0x07, 0x01), # FL, FR, FC, BC (Dolby Pro Logic)
                "4.1": (0x0F, 0x01), # FL, FR, FC, LFE, BC
                "5.0": (0x37, 0x00), # FL, FR, FC, BL, BR
                "5.1": (0x3F, 0x00),  # FL, FR, FC, LFE, BL, BR (standard 5.1 with back surrounds)
                "5.1(side)": (0x0F, 0x06), # FL, FR, FC, LFE, SL, SR (5.1 with side surrounds)
                "6.0": (0x37, 0x01), # FL, FR, FC, BL, BR, BC
                "6.1": (0x3F, 0x01), # FL, FR, FC, LFE, BL, BR, BC
                "7.0": (0x37, 0x06), # FL, FR, FC, BL, BR, SL, SR
                "7.1": (0x3F, 0x06)   # FL, FR, FC, LFE, BL, BR, SL, SR
                # Add other layouts as needed
            }

            # Get the layout string from the Python SinkDescription
            py_channel_layout_str = py_sink_desc.channel_layout.lower() if py_sink_desc.channel_layout else "stereo"

            # Look up the byte values, defaulting to stereo if not found
            chlayout1, chlayout2 = channel_layout_map.get(py_channel_layout_str, (0x03, 0x00))
            
            _logger.debug("[Config Translator]   Sink %s: Python layout '%s' -> C++ chlayout1=0x%02X, chlayout2=0x%02X",
                          py_sink_desc.name, py_channel_layout_str, chlayout1, chlayout2)

            cpp_sink_engine_config.chlayout1 = chlayout1
            cpp_sink_engine_config.chlayout2 = chlayout2
            if protocol_is_system:
                cpp_sink_engine_config.protocol = "system_audio"
            else:
                cpp_sink_engine_config.protocol = py_sink_desc.protocol
            
            # Log protocol being set for debugging WebRTC sinks
            if py_sink_desc.protocol == "webrtc":
                _logger.info("[Config Translator]   Setting protocol='webrtc' for sink '%s' (config_id=%s)",
                            py_sink_desc.name, cpp_sink_engine_config.id)
            
            # Add time sync configuration
            cpp_sink_engine_config.time_sync_enabled = py_sink_desc.time_sync
            cpp_sink_engine_config.time_sync_delay_ms = py_sink_desc.time_sync_delay

            # Handle multi-device RTP mode
            cpp_sink_engine_config.multi_device_mode = py_sink_desc.multi_device_mode
            if py_sink_desc.protocol == "rtp" and py_sink_desc.multi_device_mode:
                _logger.info("[Config Translator] Processing multi-device RTP mode for sink %s", py_sink_desc.name)
                
                # Process RTP receiver mappings
                resolved_receivers = []
                for mapping in py_sink_desc.rtp_receiver_mappings:
                    # Look up the receiver sink by name
                    receiver_sink = self.active_configuration.get_sink_from_name(mapping.receiver_sink_name)
                    
                    if receiver_sink and receiver_sink.name != "Not Found":
                        # Create a proper RtpReceiverConfig object
                        try:
                            cpp_receiver = screamrouter_audio_engine.RtpReceiverConfig()
                            cpp_receiver.receiver_id = receiver_sink.config_id or receiver_sink.name
                            cpp_receiver.ip_address = str(receiver_sink.ip) if receiver_sink.ip else ""
                            cpp_receiver.port = receiver_sink.port if receiver_sink.port else 0
                            # Set channel_map as a tuple (left, right)
                            cpp_receiver.channel_map = (mapping.left_channel, mapping.right_channel)
                            cpp_receiver.enabled = True
                            
                            resolved_receivers.append(cpp_receiver)
                            _logger.debug("[Config Translator]   Added receiver: %s:%d (L:%d, R:%d)",
                                        cpp_receiver.ip_address, cpp_receiver.port,
                                        mapping.left_channel, mapping.right_channel)
                        except Exception as e:
                            _logger.error("[Config Translator]   Failed to create RtpReceiverConfig: %s", e)
                    else:
                        _logger.warning("[Config Translator]   Receiver sink '%s' not found", mapping.receiver_sink_name)
                
                # Store the resolved receivers in a format the C++ engine can use
                # This assumes the C++ engine has been updated to handle multi-device RTP
                if hasattr(cpp_sink_engine_config, 'rtp_receivers'):
                    cpp_sink_engine_config.rtp_receivers = resolved_receivers
                    _logger.info("[Config Translator]   Configured %d RTP receivers for multi-device mode", len(resolved_receivers))
                else:
                    _logger.warning("[Config Translator]   C++ engine does not support rtp_receivers field yet")

            cpp_applied_sink.sink_engine_config = cpp_sink_engine_config

            # C. Process each source connected to this sink
            connected_path_ids_for_this_sink = []
            for py_source_desc in py_source_desc_list:
                _logger.debug("[Config Translator]   Processing Source Path: %s -> %s", 
                              py_source_desc.tag or py_source_desc.name, py_sink_desc.name)
                
                # Use source config_id if available, otherwise tag, otherwise name
                source_identifier = py_source_desc.config_id or py_source_desc.tag or py_source_desc.name
                # Use sink config_id if available, otherwise name
                sink_identifier = py_sink_desc.config_id or py_sink_desc.name
                
                # Create a unique path ID
                path_id = f"{source_identifier}_to_{sink_identifier}" 
                _logger.debug("[Config Translator]     Generated Path ID: %s", path_id)

                connected_path_ids_for_this_sink.append(path_id)

                # D. Create or retrieve C++ AppliedSourcePathParams
                if path_id not in processed_source_paths:
                    cpp_source_path = CppAppliedSourcePathParams()
                    cpp_source_path.path_id = path_id
                    
                    # Prioritize IP for source_tag, fallback to tag, then empty string.
                    # This tag is crucial for RtpReceiver packet routing.
                    source_tag_for_cpp = str(py_source_desc.ip) if py_source_desc.ip else \
                                         (py_source_desc.tag if py_source_desc.tag is not None else "")
                    cpp_source_path.source_tag = source_tag_for_cpp
                    
                    cpp_source_path.target_sink_id = cpp_applied_sink.sink_id # Link to the sink
                    
                    cpp_source_path.volume = py_source_desc.volume

                    # --- Combine Equalizers ---
                    # The final EQ for a path is a combination of the source's and sink's EQs.
                    # We assume the Equalizer type has a __mul__ or __imul__ method for combination.
                    # This mirrors the logic used for plugin sources.
                    combined_equalizer = deepcopy(py_source_desc.equalizer)
                    if py_sink_desc.equalizer:
                        # This assumes the Equalizer class overloads the multiplication operator
                        # to combine the bands and normalization settings.
                        combined_equalizer *= py_sink_desc.equalizer
                    
                    # Ensure EQ bands list is the correct size, using the combined equalizer
                    eq_bands = [combined_equalizer.b1,
                                combined_equalizer.b2,
                                combined_equalizer.b3,
                                combined_equalizer.b4,
                                combined_equalizer.b5,
                                combined_equalizer.b6,
                                combined_equalizer.b7,
                                combined_equalizer.b8,
                                combined_equalizer.b9,
                                combined_equalizer.b10,
                                combined_equalizer.b11,
                                combined_equalizer.b12,
                                combined_equalizer.b13,
                                combined_equalizer.b14,
                                combined_equalizer.b15,
                                combined_equalizer.b16,
                                combined_equalizer.b17,
                                combined_equalizer.b18]
                    if len(eq_bands) != EQ_BANDS:
                         _logger.warning("[Config Translator]     EQ band count mismatch for source %s (%d vs %d). Using default flat EQ.",
                                         py_source_desc.name, len(eq_bands), EQ_BANDS)
                         cpp_source_path.eq_values = [1.0] * EQ_BANDS # Default flat EQ (1.0)
                    else:
                        cpp_source_path.eq_values = eq_bands
                    
                    cpp_source_path.eq_normalization = combined_equalizer.normalization_enabled
                    cpp_source_path.volume_normalization = py_sink_desc.volume_normalization
                        
                    cpp_source_path.delay_ms = py_source_desc.delay
                    cpp_source_path.timeshift_sec = py_source_desc.timeshift
                    
                    # --- Populate Speaker Layouts for C++ ---
                    # py_source_desc now carries the speaker_layouts dictionary.
                    # This dictionary needs to be passed to C++.
                    # We assume cpp_source_path will have a member like 'speaker_layouts_map'
                    # that can accept a Dict[int, Dict[str, Union[bool, List[List[float]]]]]
                    # which Pybind11 can convert to std::map<int, CppSpeakerLayoutStruct>.
                    
                    # Ensure py_source_desc has speaker_layouts (it should due to Pydantic defaults)
                    if hasattr(py_source_desc, 'speaker_layouts') and isinstance(py_source_desc.speaker_layouts, dict):
                        cpp_layouts_map = {}
                        for key, layout_obj in py_source_desc.speaker_layouts.items():
                            if isinstance(layout_obj, SpeakerLayout): # Ensure it's the Python Pydantic model
                                try:
                                    # Create an instance of the C++ CppSpeakerLayout type
                                    cpp_speaker_layout_instance = screamrouter_audio_engine.CppSpeakerLayout()
                                    cpp_speaker_layout_instance.auto_mode = layout_obj.auto_mode
                                    # Ensure matrix is correctly formatted if necessary, though direct assignment
                                    # of List[List[float]] should work if bindings are set up for it.
                                    cpp_speaker_layout_instance.matrix = layout_obj.matrix 
                                    cpp_layouts_map[key] = cpp_speaker_layout_instance
                                except Exception as e_cpp_create:
                                    _logger.error(f"[Config Translator]     Path {path_id}: Failed to create CppSpeakerLayout for key {key}. Error: {e_cpp_create}. Skipping.")
                                    continue # Skip this layout if creation fails
                            else:
                                _logger.warning(f"[Config Translator]     Path {path_id}: Invalid SpeakerLayout object type for key {key} in speaker_layouts (type: {type(layout_obj)}). Skipping.")
                        
                        cpp_source_path.speaker_layouts_map = cpp_layouts_map 
                        _logger.debug(f"[Config Translator]     Path {path_id}: Populated speaker_layouts_map with {len(cpp_layouts_map)} entries.")
                        
                        # Remove old singular layout attributes if they exist on cpp_source_path,
                        # or ensure they are not used if C++ side is updated.
                        # For now, we assume the C++ side will prefer speaker_layouts_map.
                        # If cpp_source_path still has use_auto_speaker_mix and speaker_mix_matrix,
                        # they might need to be handled or cleared if no longer primary.
                        # Example: del cpp_source_path.use_auto_speaker_mix (if possible and safe)
                        # For now, we just set the new map.
                    else:
                        _logger.warning(f"[Config Translator]     Path {path_id}: py_source_desc missing or has invalid speaker_layouts. Sending empty map.")
                        cpp_source_path.speaker_layouts_map = {} # Send empty map
                    # --- End Populate Speaker Layouts for C++ ---

                    # Get target format from the *sink* description
                    cpp_source_path.target_output_channels = py_sink_desc.channels
                    cpp_source_path.target_output_samplerate = py_sink_desc.sample_rate

                    preferred_input_channels = py_source_desc.channels
                    if preferred_input_channels is None:
                        preferred_input_channels = py_sink_desc.channels
                    cpp_source_path.source_input_channels = int(preferred_input_channels or 0)

                    preferred_input_samplerate = None
                    if py_source_desc.sample_rate in _CAPTURE_ALLOWED_SAMPLE_RATES:
                        preferred_input_samplerate = int(py_source_desc.sample_rate)

                    fallback_capture_rate = None
                    if py_sink_desc.sample_rate in _CAPTURE_ALLOWED_SAMPLE_RATES:
                        fallback_capture_rate = int(py_sink_desc.sample_rate)

                    effective_capture_rate = (
                        preferred_input_samplerate
                        if preferred_input_samplerate is not None
                        else fallback_capture_rate
                    )
                    if effective_capture_rate is None:
                        effective_capture_rate = _CAPTURE_DEFAULT_SAMPLE_RATE

                    cpp_source_path.source_input_samplerate = int(effective_capture_rate)

                    preferred_input_bitdepth = py_source_desc.bit_depth
                    if preferred_input_bitdepth is None:
                        preferred_input_bitdepth = py_sink_desc.bit_depth
                    cpp_source_path.source_input_bitdepth = int(preferred_input_bitdepth or 0)
                    
                    # generated_instance_id remains empty - C++ will fill it
                    
                    processed_source_paths[path_id] = cpp_source_path
                    _logger.debug("[Config Translator]     Created new AppliedSourcePathParams for path %s", path_id)
                # else: Path already processed (e.g., multiple routes to same source/sink pair)

            # E. Finalize AppliedSinkParams
            cpp_applied_sink.connected_source_path_ids = connected_path_ids_for_this_sink
            processed_sinks_list.append(cpp_applied_sink) # Add to temporary list
            _logger.debug("[Config Translator]   Finished processing sink %s (protocol=%s). Connected paths: %s",
                          cpp_applied_sink.sink_id, py_sink_desc.protocol, cpp_applied_sink.connected_source_path_ids)

        # F. Populate the sinks and source_paths lists in DesiredEngineState AFTER the loop
        cpp_desired_state.sinks = processed_sinks_list
        cpp_desired_state.source_paths = list(processed_source_paths.values())
        _logger.info("[Config Translator] Translation complete. %d sinks, %d source paths.", 
                     len(cpp_desired_state.sinks), len(cpp_desired_state.source_paths)) # Log final counts

        return cpp_desired_state

    # --- End C++ State Translation ---


# Configuration processing functions

    def __find_added_removed_changed_sinks(self) -> Tuple[List[SinkDescription],
                                            List[SinkDescription],
                                            List[SinkDescription]]:
        """Finds changed sinks
        Returns a tuple
        ( added_sinks: List[SinkDescription],
          removed_sinks: List[SinkDescription],
          changed_sinks: List[SinkDescription] )
        Must be ran by __process_configuration while it still has the old and new states separated
        When sinks, routes, or sources are compared it is
        True if all of their properties are the same, or
        False if any of they differ.
        """
        added_sinks: List[SinkDescription] = []
        changed_sinks: List[SinkDescription] = []
        removed_sinks: List[SinkDescription] = []

        new_map: dict[SinkDescription,List[SourceDescription]]
        new_map = self.active_configuration.real_sinks_to_real_sources

        old_map: dict[SinkDescription,List[SourceDescription]]
        old_map = self.old_configuration_solver.real_sinks_to_real_sources

        # Check entries in the new map against the old map to see if they're new or changed
        for sink, sources in new_map.items():
            if not sink in old_map.keys():  # If the sink is not in the old map
                # Search for the sink in old_map by name to make sure it hasn't just changed
                if sink.name in [old_sink.name for old_sink in old_map.keys()]:
                    # If it was found under the name but didn't equal it's changed
                    changed_sinks.append(sink)
                else:
                    added_sinks.append(sink)  # It wasn't found by name, it's new
                continue
            old_sources: List[SourceDescription] = old_map[sink]
            # Check each one of the new sources for the sink to
            # verify it was in and the same in the old sources
            for old_source in sources:
                if old_source not in old_sources:
                    if sink not in changed_sinks:
                        changed_sinks.append(sink)

        # Check entries in the old map against the new map to see if they're removed
        for old_sink, old_sources in old_map.items():
            if not old_sink in new_map:  # If the sink was in the old map but not the new
                # If no sink is found in new_map with the same name it's been removed
                if not old_sink.name in [sink.name for sink in new_map.keys()]:
                    removed_sinks.append(old_sink)
                continue
            # Check for removed sources on a sink
            sources: List[SourceDescription] = new_map[old_sink]
            for old_source in old_sources:
                # If a source is in old_sources but not in sources for the new config
                if old_source not in sources:
                    # Get the new sink by name from the new config so it can be appended
                    sink: SinkDescription = [sink for sink in new_map.keys()
                                             if sink.name == old_sink.name][0]
                    if sink not in changed_sinks:
                        changed_sinks.append(sink)


        return added_sinks, removed_sinks, changed_sinks

    def __process_configuration(self) -> Tuple[List[SinkDescription],
                                               List[SinkDescription],
                                               List[SinkDescription]]:
        """Rebuilds the configuration solver and returns what sinks changed
           Returns a tuple
        ( added_sinks: List[SinkDescription],
          removed_sinks: List[SinkDescription],
          changed_sinks: List[SinkDescription] )
        """

        _logger.debug("[Configuration Manager] Processing new configuration")
        
        # Merge temporary entities with regular ones for solving
        combined_sinks = self.sink_descriptions + self.active_temporary_sinks
        combined_routes = self.route_descriptions + self.active_temporary_routes
        
        # Add plugin sources, don't overwrite existing unless they were from the plugin
        plugin_permanent_sources: List[SourceDescription]
        plugin_permanent_sources = self.plugin_manager.get_permanent_sources()
        for plugin_source in plugin_permanent_sources:
            found: bool = False
            for index, source in enumerate(self.source_descriptions):
                if source.name == plugin_source.name:
                    found = True
                    if source.tag == plugin_source.tag:
                        self.source_descriptions[index] = plugin_source
            if not found:
                self.source_descriptions.append(plugin_source)

        # Get a new resolved configuration from the current state (including temporary entities)
        _logger.debug("[Configuration Manager] Solving Configuration (including %d temporary sinks and %d temporary routes)",
                     len(self.active_temporary_sinks), len(self.active_temporary_routes))
        self.active_configuration = ConfigurationSolver(self.source_descriptions,
                                                        combined_sinks,
                                                        combined_routes)

        _logger.debug("[Configuration Manager] Configuration solved, adding temporary sources")
        # Add temporary plugin sources
        temporary_sources: dict[SinkNameType, List[SourceDescription]]
        temporary_sources = self.plugin_manager.get_temporary_sources()
        for plugin_sink_name, plugin_sources in temporary_sources.items():
            found: bool = False
            for sink in self.active_configuration.real_sinks_to_real_sources:
                if plugin_sink_name == sink.name:
                    _logger.info("[Configuration Manager] Adding temp sources to existing sink %s",
                                 sink.name)
                    found = True
                    for plugin_source in plugin_sources:
                        source_copy: SourceDescription = deepcopy(plugin_source)
                        source_copy.volume *= sink.volume
                        source_copy.equalizer *= sink.equalizer
                        source_copy.delay += sink.delay
                        _logger.info("Adding Plugin Source %s to %s", source_copy.tag, sink.name)
                        self.active_configuration.real_sinks_to_real_sources[sink].append(
                            source_copy)
            if not found:
                sink: SinkDescription = self.get_sink_by_name(plugin_sink_name)
                if sink.enabled:
                    new_plugins: List[SourceDescription] = []
                    for plugin_source in plugin_sources:
                        source_copy: SourceDescription = deepcopy(plugin_source)
                        source_copy.volume *= sink.volume
                        source_copy.equalizer *= sink.equalizer
                        source_copy.delay += sink.delay
                        new_plugins.append(source_copy)
                        _logger.info("Adding Plugin Source %s to %s", source_copy.tag, sink.name)
                    self.active_configuration.real_sinks_to_real_sources[sink] = new_plugins
                    _logger.info("[Configuration Manager] Adding temp sources to new sink %s",
                                 sink.name)

        added_sinks: List[SinkDescription]
        removed_sinks: List[SinkDescription]
        changed_sinks: List[SinkDescription]
        # While the old state is still set figure out the differences
        added_sinks, removed_sinks, changed_sinks = self.__find_added_removed_changed_sinks()
        # Set the old state to the new state
        self.old_configuration_solver = deepcopy(self.active_configuration)
        # Return what's changed
        return added_sinks, removed_sinks, changed_sinks

    def __apply_cpp_engine_state(self) -> bool:
        """Translates the active config and applies it to the C++ engine."""
        if not self.cpp_config_applier:
            _logger.warning("[Configuration Manager] C++ Config Applier not available. Skipping C++ engine configuration.")
            return False # Indicate that C++ state was not applied

        self.__process_configuration() # This updates self.active_configuration

        _logger.info("[Configuration Manager] Translating configuration for C++ engine...")
        cpp_desired_state = self._translate_config_to_cpp_desired_state()

        if cpp_desired_state:
            try:
                _logger.info("[Configuration Manager] Applying translated state to C++ engine...")
                success = self.cpp_config_applier.apply_state(cpp_desired_state)
                if success:
                    _logger.info("[Configuration Manager] C++ engine state applied successfully.")
                    return True
                else:
                    _logger.error("[Configuration Manager] C++ AudioEngineConfigApplier reported failure during apply_state.")
                    return False
            except Exception as e:
                _logger.exception("[Configuration Manager] Exception calling C++ apply_state: %s", e)
                return False
        else:
            _logger.error("[Configuration Manager] Failed to translate configuration to C++ DesiredEngineState.")
            return False
        return False # Should not be reached, but default to false

    def __reload_configuration(self) -> None:
        """Notifies the configuration manager to reload the configuration"""
        self._safe_async_run(self._broadcast_full_configuration())
        _logger.debug("[Configuration Manager] Requesting config reload")
        if not self.reload_condition.acquire(timeout=10):
            raise TimeoutError("Failed to get configuration reload condition")
        try:
            _logger.debug("[Configuration Manager] Requesting Reload - Got lock")
            self.reload_condition.notify()
        except RuntimeError:
            pass
        _logger.debug("[Configuration Manager] Requesting Reload - Released lock")
        self.reload_condition.release()
        self.reload_config = True
        _logger.debug("[Configuration Manager] Marking config for reload")
    
    def __reload_configuration_without_save(self) -> None:
        """Notifies the configuration manager to reload the configuration without saving to disk"""
        # Log temporary entities being processed
        _logger.info("[Configuration Manager] Reloading configuration without save - Temporary sinks: %d, Temporary routes: %d",
                     len(self.active_temporary_sinks), len(self.active_temporary_routes))
        for sink in self.active_temporary_sinks:
            _logger.debug("[Configuration Manager] Temporary sink: name='%s', config_id='%s', protocol='%s', enabled=%s",
                         sink.name, sink.config_id, sink.protocol, sink.enabled)
        
        self._safe_async_run(self.websocket_config.broadcast_config_update(
            self.source_descriptions,
            self.sink_descriptions + self.active_temporary_sinks,
            self.route_descriptions + self.active_temporary_routes,
            self.system_capture_devices,
            self.system_playback_devices,
        ))
        _logger.debug("[Configuration Manager] Requesting config reload (without save)")
        if not self.reload_condition.acquire(timeout=10):
            raise TimeoutError("Failed to get configuration reload condition")
        try:
            _logger.debug("[Configuration Manager] Requesting Reload (without save) - Got lock")
            self.reload_condition.notify()
        except RuntimeError:
            pass
        _logger.debug("[Configuration Manager] Requesting Reload (without save) - Released lock")
        self.reload_condition.release()
        self.reload_config = True
        _logger.debug("[Configuration Manager] Marking config for reload (without save)")

    # Removed __reload_volume_eq_timeshift_delay_configuration as it now just calls __reload_configuration

    # Removed __process_and_apply_volume_eq_delay_timeshift as it's handled by full reload
    
    def __apply_temporary_configuration_sync(self) -> None:
        """
        Synchronously applies temporary configuration changes to the C++ engine.
        This is used for temporary entities that need immediate availability (e.g., WebRTC sinks).
        """
        _logger.info("[Configuration Manager] Applying temporary configuration synchronously")
        
        # Process the configuration to update the active_configuration
        self.__process_configuration()
        
        # Apply to C++ engine immediately
        if self.cpp_config_applier:
            _logger.info("[Configuration Manager] Applying temporary configuration to C++ engine")
            cpp_desired_state = self._translate_config_to_cpp_desired_state()
            if cpp_desired_state:
                try:
                    success = self.cpp_config_applier.apply_state(cpp_desired_state)
                    if success:
                        _logger.info("[Configuration Manager] Temporary configuration applied successfully to C++ engine")
                    else:
                        _logger.error("[Configuration Manager] Failed to apply temporary configuration to C++ engine")
                except Exception as e:
                    _logger.exception("[Configuration Manager] Exception applying temporary configuration: %s", e)
            else:
                _logger.error("[Configuration Manager] Failed to translate temporary configuration for C++ engine")
        else:
            _logger.warning("[Configuration Manager] C++ Config Applier not available for temporary configuration")
        
        # Update websocket clients
        self._safe_async_run(self.websocket_config.broadcast_config_update(
            self.source_descriptions,
            self.sink_descriptions + self.active_temporary_sinks,
            self.route_descriptions + self.active_temporary_routes,
            self.system_capture_devices,
            self.system_playback_devices,
        ))

    def __process_and_apply_configuration(self) -> None:
        """Process the configuration, get which sinks have changed and need reloaded,
           then reload them."""
        _logger.debug("[Configuration Manager] Reloading configuration")
        
        self.__apply_cpp_engine_state()
        
        # Only save if there are no temporary entities being processed
        # or if this is not a temporary-only reload
        should_save = True
        # Check if this reload was triggered by temporary entity changes
        # We can determine this by checking if the configuration contains temporary entities
        for sink in self.active_configuration.real_sinks_to_real_sources.keys():
            if getattr(sink, 'is_temporary', False):
                should_save = False
                break
        
        if should_save:
            self.__save_config()
        else:
            _logger.debug("[Configuration Manager] Skipping save due to temporary entities")

        try:
            self.reload_condition.release()
            _logger.debug("Process - Releasedconfiguration reload condition")
        except:
            pass

    def __process_and_apply_configuration_with_timeout(self):
        """Apply the configuration with a timeout for logging and ensuring it doesn't hang"""
        with concurrent.futures.ThreadPoolExecutor() as executor:
            _logger.debug("Running with timeout")
            future = executor.submit(self.__process_and_apply_configuration)
            try:
                future.result(timeout=constants.CONFIGURATION_RELOAD_TIMEOUT)
            except concurrent.futures.TimeoutError:
                _logger.error(f"Configuration reload timed out after {constants.CONFIGURATION_RELOAD_TIMEOUT} seconds. Aborting reload.")
                _logger.error("Stack trace:\n%s", ''.join(traceback.format_stack()))
                return False
            except Exception as e:
                _logger.error("Error during configuration reload: %s", str(e))
                _logger.error("Stack trace:\n%s", traceback.format_exc())
                return False
        return True

    def get_hostname_by_ip(self, ip: IPAddressType) -> str:
        """Gets a hostname by IP"""
        ip_str = str(ip)
        with self._hostname_cache_lock:
            cached = self._hostname_cache.get(ip_str)
        if cached:
            return cached

        hostname: str = ip_str
        mdns_name = self._lookup_mdns_name(ip_str)
        if mdns_name:
            hostname = mdns_name
        else:
            try:
                hostname = socket.gethostbyaddr(ip_str)[0].split(".")[0]
                _logger.debug("[Configuration Manager] Adding source %s got hostname %s via DNS",
                              ip, hostname)
            except socket.herror:
                try:
                    _logger.debug(
                        "[Configuration Manager] Adding source %s couldn't get DNS, trying mDNS",
                        ip)
                    resolver = dns.resolver.Resolver()
                    resolver.nameservers = [ip_str]
                    resolver.nameserver_ports = {ip_str: 5353}
                    answer = resolver.resolve_address(ip_str)
                    rrset: dns.rrset.RRset = answer.response.answer[0]
                    if isinstance(rrset[0], dns.rdtypes.ANY.PTR.PTR):
                        ptr: dns.rdtypes.ANY.PTR.PTR = rrset[0]  # type: ignore
                        hostname = str(ptr.target).split(".", maxsplit=1)[0]
                        _logger.debug(
                            "[Configuration Manager] Adding source %s got hostname %s via mDNS",
                            ip, hostname)
                except dns.resolver.LifetimeTimeout:
                    _logger.debug(
                        "[Configuration Manager] Adding source %s couldn't get hostname, using IP",
                        ip)

        with self._hostname_cache_lock:
            self._hostname_cache[ip_str] = hostname
        return hostname

    def _lookup_mdns_name(self, ip_str: str) -> Optional[str]:
        """Attempt to derive a friendly hostname from cached mDNS data."""
        if not ip_str:
            return None

        listener = getattr(self.mdns_pinger, "listener", None)
        if not listener:
            return None

        try:
            service_info = listener.get_service_info(ip_str)
        except Exception:  # pylint: disable=broad-except
            return None

        if not service_info:
            return None

        raw_name = service_info.get('name')
        if not raw_name:
            return None

        name_str = str(raw_name)
        if not name_str:
            return None

        # Strip trailing dot(s) and service suffixes
        if name_str.endswith('.local.'):
            name_str = name_str[:-7]
        elif name_str.endswith('.local'):
            name_str = name_str[:-6]

        if '._' in name_str:
            name_str = name_str.split('._', 1)[0]
        elif '.' in name_str:
            name_str = name_str.split('.', 1)[0]

        return name_str or None

    def _apply_hostname_to_entity(self, entity: Any) -> Any:
        """Ensure entity has a hostname-based name when only an IP was provided."""
        if not hasattr(entity, "ip") or not hasattr(entity, "name"):
            return entity

        ip_value = getattr(entity, "ip", None)
        current_name = getattr(entity, "name", None)
        if not ip_value:
            return entity

        ip_str = str(ip_value)
        if not current_name or current_name == ip_str:
            resolved_name = self.get_hostname_by_ip(ip_value)
            if resolved_name:
                entity.name = resolved_name
        return entity

    def auto_add_source(self, ip: IPAddressType, service_info: dict = None):
        """Checks if VNC is available and adds a source by IP with the correct options

        Args:
            ip: IP address of the source
            service_info: Optional mDNS service info containing properties like channels, samplerates, etc.
        """
        # Use hostname from service_info if available, otherwise resolve it
        if service_info and 'name' in service_info:
            # Extract hostname from service name (e.g., "Device-Name._scream._udp.local" -> "Device-Name")
            hostname = service_info['name'].split('.')[0] if '.' in service_info['name'] else service_info['name']
        else:
            hostname = self.get_hostname_by_ip(ip)

        try:
            original_hostname: str = hostname
            counter: int = 1
            while self.get_source_by_name(hostname):
                hostname = f"{original_hostname} ({counter})"
                counter += 1
        except NameError:
            pass

        # Extract settings from service_info if available
        config_id = None
        channels = None
        samplerate = None

        if service_info and 'properties' in service_info:
            props = service_info['properties']
            # Extract config_id if present
            config_id = props.get('id') or props.get('config_id')
            # Extract audio properties
            channels = props.get('channels')
            samplerates = props.get('samplerates', '').split(',')
            if samplerates and samplerates[0]:
                samplerate = int(samplerates[0])  # Use first available sample rate

            _logger.info("[Configuration Manager] Using mDNS service info for source %s: channels=%s, samplerate=%s, config_id=%s",
                        ip, channels, samplerate, config_id)

        # If no service_info or missing config_id, try to query settings
        if not config_id:
            try:
                # Create a UDP socket for a direct query
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sock.settimeout(1.0)  # 1 second timeout
                
                try:
                    # Send a query to get settings including config_id
                    query_msg = "query_audio_settings".encode('ascii')
                    sock.sendto(query_msg, (str(ip), 5353))
                    
                    # Try to receive a response
                    response, _ = sock.recvfrom(1500)
                    response_str = response.decode('utf-8', errors='ignore')
                    
                    _logger.debug("[Configuration Manager] Received settings response from source: %s", response_str)
                    
                    # Parse response - expected format is key=value pairs separated by semicolons
                    settings = {}
                    for pair in response_str.split(';'):
                        if '=' in pair:
                            key, value = pair.split('=', 1)
                            settings[key.strip()] = value.strip()
                    
                    # Extract config_id if available
                    if 'id' in settings:
                        config_id = settings['id']
                        _logger.info("[Configuration Manager] Found config_id %s for source %s", 
                                    config_id, ip)
                except socket.timeout:
                    _logger.debug("[Configuration Manager] No settings response from %s", ip)
                except Exception as e:
                    _logger.debug("[Configuration Manager] Error in settings query: %s", str(e))
                finally:
                    sock.close()
            except Exception as e:
                _logger.warning("[Configuration Manager] Error querying source settings: %s", str(e))
            
        # Check if VNC is available
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(1)
        try:
            sock.connect((str(ip), 5900))
            _logger.debug("[Configuration Manager] Adding source %s, VNC available", ip)
            
            source_desc = SourceDescription(name=hostname, ip=ip, vnc_ip=ip, vnc_port=5900)
            # Set config_id if it was found in the settings
            if config_id:
                source_desc.config_id = config_id
                
            self.add_source(source_desc)
            sock.close()
        except OSError:
            _logger.debug("[Configuration Manager] Adding source %s, VNC not available", ip)
            
            source_desc = SourceDescription(name=hostname, ip=ip)
            # Set config_id if it was found in the settings
            if config_id:
                source_desc.config_id = config_id
                
            self.add_source(source_desc)
            
        self.__process_and_apply_configuration_with_timeout()

    def auto_add_process_source(self, tag: str):
        """Auto Add Process per Source"""
        if not tag:
            _logger.debug("[Configuration Manager] Skipping auto add for empty process tag")
            return

        raw_tag = self._normalize_process_tag(tag)
        normalized_tag = raw_tag.strip()
        if not normalized_tag:
            _logger.debug("[Configuration Manager] Skipping auto add for whitespace-only process tag")
            return

        device_lookup_keys = [f"per_process:{raw_tag}"]
        if raw_tag != normalized_tag:
            device_lookup_keys.append(f"per_process:{normalized_tag}")

        discovered_device = None
        for key in device_lookup_keys:
            discovered_device = self.discovered_devices.get(key)
            if discovered_device:
                break

        def _valid_ip(candidate: Optional[str]) -> Optional[str]:
            if candidate is None:
                return None
            candidate_str = str(candidate).strip()
            if not candidate_str:
                return None
            try:
                ipaddress.ip_address(candidate_str)
            except ValueError:
                return None
            return candidate_str

        def _split_tag_components(tag_str: str) -> Tuple[str, str]:
            if not tag_str:
                return "", ""

            for separator in ("::", " - ", "\t", "  "):
                if separator in tag_str:
                    prefix, suffix = tag_str.split(separator, 1)
                    return prefix.strip(), suffix.strip()

            parts = tag_str.split(None, 1)
            if len(parts) == 2:
                return parts[0].strip(), parts[1].strip()
            if parts:
                return "", parts[0].strip()
            return "", ""

        ip_candidate: Optional[str] = _valid_ip(raw_tag[:15].strip())
        if not ip_candidate and discovered_device and discovered_device.ip:
            ip_candidate = _valid_ip(discovered_device.ip)
        if not ip_candidate:
            for token in normalized_tag.replace("::", " ").replace("|", " ").split():
                ip_candidate = _valid_ip(token)
                if ip_candidate:
                    break

        host_component, process_component = _split_tag_components(normalized_tag)

        hostname = host_component
        if ip_candidate:
            try:
                hostname = self.get_hostname_by_ip(ip_candidate) or ip_candidate
            except Exception:  # pylint: disable=broad-except
                hostname = ip_candidate
        elif discovered_device and discovered_device.ip:
            hostname = hostname or str(discovered_device.ip).strip()

        if not hostname:
            hostname = "Process Source"

        process = process_component
        if not process:
            if ip_candidate and normalized_tag:
                remainder = normalized_tag
                if normalized_tag.startswith(ip_candidate):
                    remainder = normalized_tag[len(ip_candidate):].strip()
                else:
                    remainder = remainder.replace(ip_candidate, "", 1).strip()
                process = remainder
            if not process:
                process = normalized_tag if hostname != normalized_tag else "Process"

        hostname = hostname.strip()
        process = process.strip()

        if hostname and process and hostname == process:
            process = "Process"

        name_parts = [part for part in (hostname, process) if part]
        sourcename = " - ".join(name_parts) if name_parts else normalized_tag

        try:
            original_sourcename: str = sourcename
            counter: int = 1
            while self.get_source_by_name(sourcename):
                sourcename = f"{original_sourcename} ({counter})"
                counter += 1
        except NameError:
            pass

        _logger.info("[Configuration Manager] Adding Source Process %s - %s", hostname, process)

        source = SourceDescription(
            name=sourcename,
            tag=raw_tag,
            is_process=True,
        )

        source_group_search: List[SourceDescription]
        group_tag = f"{hostname} All Processes" if hostname else "Process Sources"
        source_group_search = [desc for desc in self.source_descriptions if desc.tag == group_tag]
        if not source_group_search:
            source_group = SourceDescription(
                name=group_tag,
                tag=group_tag,
                is_group=True,
            )
            self.add_source(source_group)
        else:
            source_group = source_group_search[0]

        if source.name not in source_group.group_members:
            source_group.group_members.append(source.name)

        self.add_source(source)
        self.__reload_configuration()

    def auto_add_sink(self, ip: IPAddressType, service_info: dict = None):
        """Adds a sink with settings queried from the device or defaults if query fails

        Args:
            ip: IP address of the sink
            service_info: Optional mDNS service info containing properties like channels, samplerates, etc.
        """
        # Use hostname from service_info if available, otherwise resolve it
        if service_info and 'name' in service_info:
            # Extract hostname from service name (e.g., "Screamrouter-549BFD._scream._udp.local" -> "Screamrouter-549BFD")
            hostname = service_info['name'].split('.')[0] if '.' in service_info['name'] else service_info['name']
        else:
            hostname = str(ip)
            try:
                hostname = socket.gethostbyaddr(str(ip))[0].split(".")[0]
                _logger.debug("[Configuration Manager] Adding sink %s got hostname %s via DNS",
                              ip, hostname)
            except socket.herror:
                try:
                    _logger.debug(
                        "[Configuration Manager] Adding sink %s couldn't get DNS, trying mDNS",
                        ip)
                    resolver: dns.resolver.Resolver = dns.resolver.Resolver()
                    resolver.nameservers = [str(ip)]
                    resolver.nameserver_ports = {str(ip): 5353}
                    answer = resolver.resolve_address(str(ip))
                    rrset: dns.rrset.RRset = answer.response.answer[0]
                    if isinstance(rrset[0], dns.rdtypes.ANY.PTR.PTR):
                        ptr: dns.rdtypes.ANY.PTR.PTR = rrset[0] # type: ignore
                        hostname = str(ptr.target).split(".", maxsplit=1)[0]
                        _logger.debug(
                            "[Configuration Manager] Adding sink %s got hostname %s via mDNS",
                            ip, hostname)
                except dns.resolver.LifetimeTimeout:
                    _logger.debug(
                        "[Configuration Manager] Adding sink %s couldn't get hostname, using IP",
                        ip)
        try:
            original_hostname: str = hostname
            counter: int = 1
            while self.get_source_by_name(hostname):
                hostname = f"{original_hostname} ({counter})"
                counter += 1
        except NameError:
            pass
       
        # Extract settings from service_info if available
        config_id = None
        bit_depth: int = 16  # Default
        sample_rate: int = 48000  # Default
        channels: int = 2  # Default
        channel_layout: str = "stereo"  # Default
        port: int = 40000  # Default Scream port

        if service_info:
            # Use port from service info if available
            port = service_info.get('port', 40000)

            if 'properties' in service_info:
                props = service_info['properties']
                # Extract config_id if present
                config_id = props.get('id') or props.get('config_id')

                # Extract audio properties
                if 'channels' in props:
                    try:
                        channels = int(props['channels'])
                        # Set channel layout based on channel count
                        if channels == 1:
                            channel_layout = "mono"
                        elif channels == 2:
                            channel_layout = "stereo"
                        else:
                            channel_layout = f"{channels}ch"
                    except ValueError:
                        pass

                if 'samplerates' in props:
                    samplerates = props['samplerates'].split(',')
                    if samplerates and samplerates[0]:
                        try:
                            sample_rate = int(samplerates[0])  # Use first available sample rate
                        except ValueError:
                            pass

                _logger.info("[Configuration Manager] Using mDNS service info for sink %s: port=%s, channels=%s, samplerate=%s, config_id=%s",
                            ip, port, channels, sample_rate, config_id)
        
        # Try to query audio settings using a simple UDP request
        try:
            _logger.debug("[Configuration Manager] Querying audio settings from %s", ip)
            
            # Create a UDP socket for a direct query
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.settimeout(1.0)  # 1 second timeout
            
            try:
                # Send a simple query string that the C# code might recognize
                query_msg = "query_audio_settings".encode('ascii')
                sock.sendto(query_msg, (str(ip), 5353))
                
                # Try to receive a response
                response, _ = sock.recvfrom(1500)
                response_str = response.decode('utf-8', errors='ignore')
                
                _logger.debug("[Configuration Manager] Received response: %s", response_str)
                
                # Parse response - expected format is key=value pairs separated by semicolons
                settings = {}
                for pair in response_str.split(';'):
                    if '=' in pair:
                        key, value = pair.split('=', 1)
                        settings[key.strip()] = value.strip()
                
                # Extract settings if available
                config_id = None
                if settings:
                    if 'bit_depth' in settings:
                        bit_depth = int(settings['bit_depth'])
                    if 'sample_rate' in settings:
                        sample_rate = int(settings['sample_rate'])
                    if 'channels' in settings:
                        channels = int(settings['channels'])
                    if 'channel_layout' in settings:
                        channel_layout = settings['channel_layout']
                    if 'id' in settings:
                        config_id = settings['id']
                        _logger.info("[Configuration Manager] Found config_id %s for sink %s", 
                                    config_id, ip)
                    
                    _logger.info("[Configuration Manager] Using queried settings for sink %s: "
                                "bit_depth=%d, sample_rate=%d, channels=%d, channel_layout=%s",
                                ip, bit_depth, sample_rate, channels, channel_layout)
            
            except socket.timeout:
                _logger.debug("[Configuration Manager] No response received from %s, using default settings", ip)
            except Exception as e:
                _logger.debug("[Configuration Manager] Error in UDP query: %s, using default settings", str(e))
            finally:
                sock.close()
                
        except Exception as e:
            _logger.warning("[Configuration Manager] Error querying settings: %s, using default settings", str(e))
        except Exception as e:
            _logger.warning("[Configuration Manager] Error querying sink settings: %s", str(e))
            _logger.debug("[Configuration Manager] Using default settings for sink %s", ip)
        
        _logger.debug("[Configuration Manager] Adding sink %s with settings: "
                     "port=%d, bit_depth=%d, sample_rate=%d, channels=%d, channel_layout=%s",
                     ip, port, bit_depth, sample_rate, channels, channel_layout)

        sink_desc = SinkDescription(name=hostname,
                                   ip=ip,
                                   port=port,
                                   bit_depth=bit_depth,
                                   sample_rate=sample_rate,
                                   channels=channels,
                                   channel_layout=channel_layout)
        
        # Set config_id if it was found in the settings
        if config_id:
            sink_desc.config_id = config_id
            
        self.add_sink(sink_desc)
        self.__process_and_apply_configuration_with_timeout()

    def check_autodetected_sinks_sources(self):
        """This checks the IPs receivers have seen and adds any as sources if they don't exist"""
        #self.scream_recevier.check_known_ips()
        #self.scream_per_process_recevier.check_known_sources()
        #self.multicast_scream_recevier.check_known_ips()
        #self.rtp_receiver.check_known_ips()
        known_source_tags: List[str] = [str(desc.tag) for desc in self.source_descriptions if desc.tag is not None]
        known_source_ips: List[str] = [str(desc.ip) for desc in self.source_descriptions if desc.ip is not None]
        known_sink_ips: List[str] = [str(desc.ip) for desc in self.sink_descriptions if desc.ip is not None]
        known_sink_config_ids: List[str] = [str(desc.config_id) for desc in self.sink_descriptions if desc.config_id is not None]
        
        # --- C++ Engine Based Auto Source Detection ---
        if self.cpp_audio_manager and screamrouter_audio_engine:
            # RTP Receiver (IP-based sources)
            try:
                rtp_seen_tags = self.cpp_audio_manager.get_rtp_receiver_seen_tags()
                for ip_str in rtp_seen_tags:
                    ip_value = str(ip_str)
                    if not ip_value:
                        continue

                    rtp_properties = {"source": "cpp_engine"}
                    if self._update_discovered_device_last_seen(
                        method="rtp",
                        identifier=ip_value,
                        ip=ip_value,
                        device_type="rtp_stream",
                        properties=rtp_properties,
                    ):
                        continue

                    is_new_source = ip_value not in known_source_ips
                    if is_new_source:
                        _logger.info(
                            "[Configuration Manager] Discovered new source from C++ RTP Receiver: %s",
                            ip_value,
                        )
                        known_source_ips.append(ip_value)

                    self._store_discovered_device(
                        method="rtp",
                        role="source",
                        identifier=ip_value,
                        ip=ip_value,
                        device_type="rtp_stream",
                        properties=rtp_properties,
                    )
            except Exception as e:
                _logger.error("[Configuration Manager] Error getting seen tags from C++ RTP Receiver: %s", e)

            # Raw Scream Receivers (IP-based sources)
            # Using ports configured in __init__
            raw_receiver_ports = [4010, 16401] 
            for port in raw_receiver_ports:
                try:
                    raw_seen_tags = self.cpp_audio_manager.get_raw_scream_receiver_seen_tags(port)
                    for ip_str in raw_seen_tags:
                        ip_value = str(ip_str)
                        if not ip_value:
                            continue

                        raw_properties = {"receiver_port": port, "source": "cpp_engine"}
                        if self._update_discovered_device_last_seen(
                            method="raw_scream",
                            identifier=ip_value,
                            ip=ip_value,
                            port=port,
                            device_type="raw_scream_receiver",
                            properties=raw_properties,
                        ):
                            continue

                        is_new_source = ip_value not in known_source_ips
                        if is_new_source:
                            _logger.info(
                                "[Configuration Manager] Discovered new source from C++ Raw Scream Receiver (port %d): %s",
                                port,
                                ip_value,
                            )
                            known_source_ips.append(ip_value)

                        self._store_discovered_device(
                            method="raw_scream",
                            role="source",
                            identifier=ip_value,
                            ip=ip_value,
                            port=port,
                            device_type="raw_scream_receiver",
                            properties=raw_properties,
                        )
                except Exception as e:
                    _logger.error("[Configuration Manager] Error getting seen tags from C++ Raw Scream Receiver (port %d): %s", port, e)

            # Per-Process Scream Receiver (Tag-based sources)
            per_process_receiver_port = 16402 # Port configured in __init__
            try:
                per_process_seen_tags = self.cpp_audio_manager.get_per_process_scream_receiver_seen_tags(per_process_receiver_port)
                for tag_str in per_process_seen_tags:
                    identifier = self._normalize_process_tag(tag_str)
                    if not identifier:
                        continue

                    ip_segment = identifier[:15].strip()
                    try:
                        parsed_ip = str(IPAddressType(ip_segment))
                    except Exception:  # pylint: disable=broad-except
                        parsed_ip = ip_segment

                    per_process_properties = {
                        "receiver_port": per_process_receiver_port,
                        "source": "cpp_engine",
                    }
                    if self._update_discovered_device_last_seen(
                        method="per_process",
                        identifier=identifier,
                        ip=parsed_ip,
                        tag=identifier,
                        device_type="per_process",
                        properties=per_process_properties,
                    ):
                        continue

                    is_new_source = identifier not in known_source_tags
                    if is_new_source:
                        _logger.info(
                            "[Configuration Manager] Discovered new per-process source from C++ Receiver (port %d): %s",
                            per_process_receiver_port,
                            identifier,
                        )
                        known_source_tags.append(identifier)

                        self._store_discovered_device(
                            method="per_process",
                            role="process",
                            identifier=identifier,
                            ip=parsed_ip,
                            tag=identifier,
                            device_type="per_process",
                            properties=per_process_properties,
                        )

                        try:
                            self.auto_add_process_source(identifier)
                        except Exception as auto_exc:  # pylint: disable=broad-except
                            _logger.error(
                                "[Configuration Manager] Failed to auto add process source for tag %s: %s",
                                identifier,
                                auto_exc,
                            )
            except Exception as e:
                _logger.error("[Configuration Manager] Error getting seen tags from C++ Per-Process Receiver (port %d): %s", per_process_receiver_port, e)

            # PulseAudio Receiver (process-style sources)
            if hasattr(self.cpp_audio_manager, "get_pulse_receiver_seen_tags"):
                try:
                    pulse_seen_tags = self.cpp_audio_manager.get_pulse_receiver_seen_tags()
                    for tag_str in pulse_seen_tags:
                        identifier = self._normalize_process_tag(tag_str)
                        if not identifier:
                            continue

                        ip_candidate: Optional[str] = None
                        ip_segment = identifier[:15].strip()
                        try:
                            ip_candidate = str(IPAddressType(ip_segment))
                        except Exception:  # pylint: disable=broad-except
                            ip_candidate = None

                        pulse_properties = {"source": "pulse"}
                        store_ip = ip_candidate
                        if not store_ip:
                            parts = identifier.strip().split(None, 1)
                            if parts:
                                store_ip = parts[0]
                        if not store_ip:
                            store_ip = identifier

                        if self._update_discovered_device_last_seen(
                            method="pulse",
                            identifier=identifier,
                            ip=store_ip,
                            tag=identifier,
                            device_type="pulse_process",
                            properties=pulse_properties,
                        ):
                            continue

                        is_new_pulse_source = identifier not in known_source_tags
                        if is_new_pulse_source:
                            _logger.info(
                                "[Configuration Manager] Discovered new PulseAudio source: %s",
                                identifier,
                            )
                            known_source_tags.append(identifier)

                            self._store_discovered_device(
                                method="pulse",
                                role="process",
                                identifier=identifier,
                                ip=store_ip,
                                tag=identifier,
                                device_type="pulse_process",
                                properties=pulse_properties,
                            )

                            try:
                                self.auto_add_process_source(identifier)
                            except Exception as auto_exc:  # pylint: disable=broad-except
                                _logger.error(
                                    "[Configuration Manager] Failed to auto add PulseAudio process source for tag %s: %s",
                                    identifier,
                                    auto_exc,
                                )
                        else:
                            self._store_discovered_device(
                                method="pulse",
                                role="process",
                                identifier=identifier,
                                ip=store_ip,
                                tag=identifier,
                                device_type="pulse_process",
                                properties=pulse_properties,
                            )
                except Exception as e:  # pylint: disable=broad-except
                    _logger.error("[Configuration Manager] Error getting seen tags from PulseAudio Receiver: %s", e)

            # SAP Announcements (metadata-driven sources)
            try:
                if hasattr(self.cpp_audio_manager, "get_rtp_sap_announcements"):
                    sap_announcements = self.cpp_audio_manager.get_rtp_sap_announcements() or []
                    for announcement in sap_announcements:
                        if not isinstance(announcement, dict):
                            continue

                        ip_value = str(announcement.get("ip") or announcement.get("stream_ip") or "").strip()
                        if not ip_value:
                            continue

                        port_value = announcement.get("port")
                        identifier = f"{ip_value}:{port_value}" if port_value else ip_value

                        properties = {
                            "source": "cpp_engine",
                            "discovery": "sap",
                        }
                        for key in ("sample_rate", "channels", "bit_depth", "endianness", "announcer_ip"):
                            value = announcement.get(key)
                            if value is not None:
                                properties[key] = value

                        if ip_value not in known_source_ips:
                            _logger.info(
                                "[Configuration Manager] Discovered SAP-announced source from C++ RTP Receiver: %s:%s",
                                ip_value,
                                port_value if port_value else "unknown",
                            )
                            known_source_ips.append(ip_value)

                        self._store_discovered_device(
                            method="sap",
                            role="source",
                            identifier=identifier,
                            ip=ip_value,
                            port=port_value,
                            device_type="rtp_stream",
                            properties=properties,
                        )
            except Exception as e:
                _logger.error("[Configuration Manager] Error processing SAP announcements from C++ RTP Receiver: %s", e)
        # --- End C++ Engine Based Auto Source Detection ---

        # mDNS pinger for sources (senders)
        for ip in self.mdns_pinger.get_source_ips():
            ip_value = str(ip)
            service_info = self.mdns_pinger.get_service_info(ip)
            properties = service_info.get('properties', {}) if isinstance(service_info, dict) else {}

            self._store_discovered_device(
                method="mdns",
                role="source",
                identifier=ip_value,
                ip=ip_value,
                name=service_info.get('name') if isinstance(service_info, dict) else None,
                port=service_info.get('port') if isinstance(service_info, dict) else None,
                tag=properties.get('tag') if isinstance(properties, dict) else None,
                properties=properties if isinstance(properties, dict) else {},
                device_type=properties.get('type') if isinstance(properties, dict) else None,
            )

            if ip_value not in known_source_ips:
                _logger.info("[Configuration Manager] Discovered new source (sender) from mDNS %s with info: %s", ip, service_info)
                known_source_ips.append(ip_value)
        # mDNS pinger for sinks (receivers)
        for ip in self.mdns_pinger.get_sink_ips():
            ip_value = str(ip)
            service_info = self.mdns_pinger.get_service_info(ip)
            properties = service_info.get('properties', {}) if isinstance(service_info, dict) else {}

            self._store_discovered_device(
                method="mdns",
                role="sink",
                identifier=ip_value,
                ip=ip_value,
                name=service_info.get('name') if isinstance(service_info, dict) else None,
                port=service_info.get('port') if isinstance(service_info, dict) else None,
                tag=properties.get('tag') if isinstance(properties, dict) else None,
                properties=properties if isinstance(properties, dict) else {},
                device_type=properties.get('type') if isinstance(properties, dict) else None,
            )

            if ip_value not in known_sink_ips:
                _logger.info("[Configuration Manager] Discovered new sink (receiver) from mDNS %s with info: %s", ip, service_info)
                known_sink_ips.append(ip_value)
        # Process sink settings
        known_source_config_ids: List[str] = [str(desc.config_id) for desc in self.source_descriptions if desc.config_id]
        
        for entry in self.mdns_settings_pinger.get_all_sink_settings():
            if entry.ip in known_sink_ips:
                sink: SinkDescription = self.__get_sink_by_ip(entry.ip)
                if not sink.config_id:
                    sink.config_id = entry.receiver_id
                    _logger.info("[Configuration Manager] Tagging sink at IP %s with ID %s",
                                 entry.ip, entry.receiver_id)
            if entry.receiver_id in known_sink_config_ids:
                sink: SinkDescription = self.__get_sink_by_config_id(entry.receiver_id)
                changed: bool = False
                if (sink.bit_depth != entry.bit_depth or
                    sink.channel_layout != entry.channel_layout or
                    sink.channels != entry.channels or
                    sink.sample_rate != entry.sample_rate):
                    changed = True
                    if entry.bit_depth:
                        sink.bit_depth = entry.bit_depth
                    if entry.channel_layout:
                        sink.channel_layout = entry.channel_layout
                    if entry.channels:
                        sink.channels = entry.channels
                    if entry.sample_rate:
                        sink.sample_rate = entry.sample_rate
                    _logger.info("[Configuration Manager] Sink %s (%s) reports settings change",
                                 sink.name, entry.receiver_id)
                if changed:
                    self.__reload_configuration()
                    
        # Process source settings
        for entry in self.mdns_settings_pinger.get_all_source_settings():
            source_changed = False

            # If not found by config_id, check by IP
            if entry.ip in known_source_ips:
                source = self.__get_source_by_ip(entry.ip)
                
                # If source doesn't have a config_id, assign it
                if not source.config_id:
                    source.config_id = entry.source_id
                    #source_changed = True
                    _logger.info("[Configuration Manager] Tagging source at IP %s with ID %s",
                                entry.ip, entry.source_id)
                    
            for source in [source for source in self.source_descriptions if source.is_process and
                           source.tag[:15].strip() == str(entry.ip)]:
                if source.config_id == entry.source_id or not source.config_id:
                    source.config_id = entry.source_id
                    if entry.tag and entry.tag != source.tag:
                        source_changed = True
                        source.tag = entry.tag
                    if entry.vnc_ip and entry.vnc_ip != source.vnc_ip:
                        source_changed = True
                        source.vnc_ip = entry.vnc_ip
                    if entry.vnc_port and entry.vnc_port != source.vnc_port:
                        source_changed = True
                        source.vnc_port = entry.vnc_port

            # First check if we have a source with this config_id
            if entry.source_id in known_source_config_ids:
                for source in self.source_descriptions:
                    if source.config_id == entry.source_id:
                        if entry.ip and entry.ip != source.ip:
                            source_changed = True
                            source.ip = entry.ip
                        if entry.tag and entry.tag != source.tag:
                            source_changed = True
                            source.tag = entry.tag
                        if entry.vnc_ip and entry.vnc_ip != source.vnc_ip:
                            source_changed = True
                            source.vnc_ip = entry.vnc_ip
                        if entry.vnc_port and entry.vnc_port != source.vnc_port:
                            source_changed = True
                            source.vnc_port = entry.vnc_port
                        if source_changed:
                            _logger.info("[Configuration Manager] Source %s (%s) updated from settings",
                                        source.name, entry.source_id)
            
            # Reload configuration if any source was changed
            if source_changed:
                self.__reload_configuration()
        #for tag in self.scream_per_process_recevier.known_sources:
        #    if not str(tag) in known_source_tags:
        #        _logger.info("[Configuration Manager] Adding new per-process source %s", tag)
        #        self.auto_add_process_source(tag)
        
    def run(self):
        """Monitors for the reload condition to be set and reloads the config when it is set"""
        self.__process_and_apply_configuration()
        while self.running:
            try:
                if not self.reload_condition.acquire(timeout=1):
                    raise TimeoutError("Failed to get configuration reload condition")

                try:
                    notified = self.reload_condition.wait(timeout=.3)
                    plugin_requests_reload = self.plugin_manager.wants_reload(False)

                    if plugin_requests_reload and self.plugin_manager.wants_reload():
                        _logger.info("[Configuration Manager] Plugin Manager requests reload.")
                        self.reload_config = True

                    if notified or plugin_requests_reload or self.reload_config:
                        self.reload_config = True if self.reload_config or plugin_requests_reload else False
                        if self.reload_config:
                            self.reload_config = False
                            _logger.info("[Configuration Manager] Reloading the configuration")
                            if not self.__process_and_apply_configuration_with_timeout():
                                _logger.error("Configuration reload aborted due to errors or timeout.")
                                continue  # Skip the rest of the loop iteration
                finally:
                    try:
                        self.reload_condition.release()
                    except RuntimeError:
                        pass

                # Removed check/call for __process_and_apply_volume_eq_delay_timeshift
                self.check_autodetected_sinks_sources()
            except Exception:  # pylint: disable=broad-except
                pass

    def update_source_speaker_layout(self, source_name: SourceNameType, input_channel_key: int, speaker_layout_for_key: SpeakerLayout) -> bool:
        """Set the speaker layout for a specific input channel key on a source or source group"""
        source: SourceDescription = self.get_source_by_name(source_name)
        # Ensure speaker_layouts dict exists (Pydantic default_factory should handle this)
        if source.speaker_layouts is None: # Should ideally not happen
            source.speaker_layouts = {}
        source.speaker_layouts[input_channel_key] = speaker_layout_for_key
        _logger.info(f"Updating SpeakerLayout for source {source_name}, input key {input_channel_key}. Auto mode: {speaker_layout_for_key.auto_mode}")
        self.__reload_configuration() # Trigger full reload
        return True

    def update_sink_speaker_layout(self, sink_name: SinkNameType, input_channel_key: int, speaker_layout_for_key: SpeakerLayout) -> bool:
        """Set the speaker layout for a specific input channel key on a sink or sink group"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        if sink.speaker_layouts is None: # Should ideally not happen
            sink.speaker_layouts = {}
        sink.speaker_layouts[input_channel_key] = speaker_layout_for_key
        _logger.info(f"Updating SpeakerLayout for sink {sink_name}, input key {input_channel_key}. Auto mode: {speaker_layout_for_key.auto_mode}")
        self.__reload_configuration() # Trigger full reload
        return True

    def update_route_speaker_layout(self, route_name: RouteNameType, input_channel_key: int, speaker_layout_for_key: SpeakerLayout) -> bool:
        """Set the speaker layout for a specific input channel key on a route"""
        route: RouteDescription = self.get_route_by_name(route_name)
        if route.speaker_layouts is None: # Should ideally not happen
            route.speaker_layouts = {}
        route.speaker_layouts[input_channel_key] = speaker_layout_for_key
        _logger.info(f"Updating SpeakerLayout for route {route_name}, input key {input_channel_key}. Auto mode: {speaker_layout_for_key.auto_mode}")
        self.__reload_configuration() # Trigger full reload
        return True
