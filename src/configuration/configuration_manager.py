"""This manages the target state of sinks, sources, and routes
   then runs audio controllers for each source"""
import os
import socket
import sys
import asyncio
import traceback
import concurrent.futures
import threading
import uuid
"""This manages the target state of sinks, sources, and routes
   then runs audio controllers for each source"""
from copy import copy, deepcopy
from ipaddress import IPv4Address
from multiprocessing import Process
from subprocess import TimeoutExpired
from typing import List, Optional, Tuple

import dns.nameserver
import dns.rdtypes
import dns.rdtypes.ANY
import dns.rdtypes.ANY.PTR
import dns.resolver
import dns.rrset
import fastapi
import yaml
from fastapi import Request
from zeroconf import Zeroconf, ServiceInfo

import src.constants.constants as constants
import src.screamrouter_logger.screamrouter_logger as screamrouter_logger
from src.api.api_webstream import APIWebStream
from src.api.api_websocket_config import APIWebsocketConfig
from src.audio.audio_controller import AudioController
from src.audio.multicast_scream_receiver import MulticastScreamReceiver
from src.audio.rtp_recevier import RTPReceiver
from src.audio.scream_receiver import ScreamReceiver
from src.audio.scream_per_process_receiver import ScreamPerProcessReceiver
from src.audio.tcp_manager import TCPManager
from src.configuration.configuration_solver import ConfigurationSolver
from src.plugin_manager.plugin_manager import PluginManager
from src.screamrouter_types.annotations import (DelayType, IPAddressType,
                                                RouteNameType, SinkNameType,
                                                SourceNameType, TimeshiftType,
                                                VolumeType)
from src.screamrouter_types.configuration import (Equalizer, RouteDescription,
                                                  SinkDescription,
                                                  SourceDescription)
                                                  
from src.screamrouter_types.exceptions import InUseError
from src.utils.mdns_pinger import MDNSPinger
from src.utils.mdns_responder import MDNSResponder
from src.utils.mdns_settings_pinger import MDNSSettingsPinger

_logger = screamrouter_logger.get_logger(__name__)

class ConfigurationManager(threading.Thread):
    """Tracks configuration and loading the main receiver/sinks based off of it"""
    def __init__(self, websocket: APIWebStream,
                 plugin_manager: PluginManager,
                 websocket_config: APIWebsocketConfig):
        """Initialize the controller"""
        super().__init__(name="Configuration Manager")
        self.mdns_responder: MDNSResponder = MDNSResponder()
        """MDNS Responder, handles returning responses over MDNS for receivers/senders to configure to"""
        self.mdns_responder.start()
        self.mdns_pinger: MDNSPinger = MDNSPinger()
        """MDNS Responder, handles querying for receivers and senders to add entries for"""
        self.mdns_pinger.start()
        self.mdns_settings_pinger: MDNSSettingsPinger = MDNSSettingsPinger(self)
        """MDNS Settings Pinger, handles querying for settings to sync with sources"""
        self.mdns_settings_pinger.start()
        self.sink_descriptions: List[SinkDescription] = []
        """List of Sinks the controller knows of"""
        self.source_descriptions:  List[SourceDescription] = []
        """List of Sources the controller knows of"""
        self.route_descriptions: List[RouteDescription] = []
        """List of Routes the controller knows of"""
        self.audio_controllers: List[AudioController] = []
        """Holds a list of active Audio Controllers"""
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
        self.scream_recevier: ScreamReceiver = ScreamReceiver([])
        """Holds the thread that receives UDP packets from Scream"""
        self.scream_per_process_recevier: ScreamPerProcessReceiver = ScreamPerProcessReceiver([])
        """Holds the thread that receives per-process UDP packets from Scream"""
        self.multicast_scream_recevier: MulticastScreamReceiver = MulticastScreamReceiver([])
        """Holds the thread that receives UDP packets from Multicast Scream Streams"""
        self.rtp_receiver: RTPReceiver = RTPReceiver([])
        """Holds the thread that receives UDP packets from an RTP source"""
        self.tcp_manager: TCPManager = TCPManager([])
        self.reload_config: bool = False
        """Set to true to reload the config. Used so config
           changes during another config reload still trigger
           a reload"""
        self.websocket_config = websocket_config
        """Websocket Config Update Notifier"""
        self.plugin_manager: PluginManager = plugin_manager
        self.__load_config()

        _logger.info("------------------------------------------------------------------------")
        _logger.info("  ScreamRouter")
        _logger.info("     Console Log level: %s", constants.CONSOLE_LOG_LEVEL)
        _logger.info("     Log Dir: %s", os.path.realpath(constants.LOGS_DIR))
        _logger.info("------------------------------------------------------------------------")

        self.start()


    # Public functions

    def get_sinks(self) -> List[SinkDescription]:
        """Returns a list of all sinks"""
        _sinks: List[SinkDescription] = []
        for sink in self.sink_descriptions:
            _sinks.append(copy(sink))
        return _sinks

    def add_sink(self, sink: SinkDescription) -> bool:
        """Adds a sink or sink group"""
        self.__verify_new_sink(sink)
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

        if is_eq_found and is_eq_only:
            self.__reload_volume_eq_timeshift_delay_configuration()
        else:
            self.__reload_configuration()
        return True

    def delete_sink(self, sink_name: SinkNameType) -> bool:
        """Deletes a sink by name"""
        _logger.debug(f"Deleting {sink_name}")
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        self.__verify_sink_unused(sink)
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
        return copy(self.source_descriptions)
    
    def get_processes_by_ip(self, ip: IPAddressType) -> List[SourceDescription]:
        """Get a list of all processes for a specific IP"""
        return [source for source in self.source_descriptions 
            if source.is_process and source.tag and source.tag.startswith(str(ip))]

    def add_source(self, source: SourceDescription) -> bool:
        """Add a source or source group"""
        self.__verify_new_source(source)
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

        if is_eq_found and is_eq_only:
            self.__reload_volume_eq_timeshift_delay_configuration()
        else:
            self.__reload_configuration()
        return True

    def delete_source(self, source_name: SourceNameType) -> bool:
        """Deletes a source by name"""
        source: SourceDescription = self.get_source_by_name(source_name)
        self.__verify_source_unused(source)
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
        """Returns a list of all routes"""
        return copy(self.route_descriptions)

    def add_route(self, route: RouteDescription) -> bool:
        """Adds a route"""
        self.__verify_new_route(route)
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
            if field == "equalizer":
                is_eq_found = True
            elif field != "name":
                is_eq_only = False
            setattr(changed_route, field, getattr(new_route, field))
        if is_eq_found and is_eq_only:
            self.__reload_volume_eq_timeshift_delay_configuration()
        else:
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

    def update_source_equalizer(self, source_name: SourceNameType, equalizer: Equalizer) -> bool:
        """Set the equalizer for a source or source group"""
        source: SourceDescription = self.get_source_by_name(source_name)
        source.equalizer = equalizer
        self.__reload_volume_eq_timeshift_delay_configuration()
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
        self.__reload_volume_eq_timeshift_delay_configuration()
        return True

    def update_source_timeshift(self, source_name: SourceNameType,
                                timeshift: TimeshiftType) -> bool:
        """Set the timeshift for a source or source group"""
        source: SourceDescription = self.get_source_by_name(source_name)
        source.timeshift = timeshift
        self.__reload_volume_eq_timeshift_delay_configuration()
        return True

    def update_source_delay(self, source_name: SourceNameType, delay: DelayType) -> bool:
        """Set the delay for a source or source group"""
        source: SourceDescription = self.get_source_by_name(source_name)
        source.delay = delay
        self.__reload_volume_eq_timeshift_delay_configuration()
        return True

    def source_next_track(self, source_name: SourceNameType) -> bool:
        """Send a Next Track command to the source"""
        source: SourceDescription = self.get_source_by_name(source_name)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.sendto("n".encode("ascii"), (str(source.vnc_ip), 9999))
        return True

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
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.sendto("p".encode("ascii"), (str(source.vnc_ip), 9999))
        return True

    def source_play(self, source_name: SourceNameType) -> bool:
        """Send a Next Track command to the source"""
        source: SourceDescription = self.get_source_by_name(source_name)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        _logger.info("[Controller Manager] Sending Play/Pause to %s", source.vnc_ip)
        sock.sendto("P".encode("ascii"), (str(source.vnc_ip), 9999))
        return True

    def update_sink_equalizer(self, sink_name: SinkNameType, equalizer: Equalizer) -> bool:
        """Set the equalizer for a sink or sink group"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        sink.equalizer = equalizer
        _logger.debug("Updating EQ for %s", sink_name)
        self.__reload_volume_eq_timeshift_delay_configuration()
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
        self.__reload_volume_eq_timeshift_delay_configuration()
        return True

    def update_sink_timeshift(self, sink_name: SinkNameType,
                                timeshift: TimeshiftType) -> bool:
        """Set the timeshift for a sink or sink group"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        sink.timeshift = timeshift
        self.__reload_volume_eq_timeshift_delay_configuration()
        return True

    def update_sink_delay(self, sink_name: SinkNameType, delay: DelayType) -> bool:
        """Set the delay for a sink or sink group"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        sink.delay = delay
        self.__reload_volume_eq_timeshift_delay_configuration()
        return True

    def update_route_equalizer(self, route_name: RouteNameType, equalizer: Equalizer) -> bool:
        """Set the equalizer for a route"""
        route: RouteDescription = self.get_route_by_name(route_name)
        route.equalizer = equalizer
        self.__reload_volume_eq_timeshift_delay_configuration()
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
        self.__reload_volume_eq_timeshift_delay_configuration()
        return True

    def update_route_timeshift(self, route_name: RouteNameType,
                                timeshift: TimeshiftType) -> bool:
        """Set the timeshift for a route"""
        route: RouteDescription = self.get_route_by_name(route_name)
        route.timeshift = timeshift
        self.__reload_volume_eq_timeshift_delay_configuration()
        return True

    def update_route_delay(self, route_name: RouteNameType, delay: DelayType) -> bool:
        """Set the delay for a route"""
        route: RouteDescription = self.get_route_by_name(route_name)
        route.delay = delay
        self.__reload_volume_eq_timeshift_delay_configuration()
        return True

    def stop(self) -> bool:
        """Stop all threads/processes"""
        _logger.debug("[Configuration Manager] Stopping webstream")
        self.__api_webstream.stop()
        _logger.debug("[Configuration Manager] Webstream stopped")
        _logger.debug("[Configuration Manager] Stopping receiver")
        self.scream_recevier.stop()
        self.multicast_scream_recevier.stop()
        self.rtp_receiver.stop()
        self.tcp_manager.stop()
        _logger.debug("[Configuration Manager] Receiver stopped")
        _logger.debug("[Configuration Manager] Stopping Plugin Manager")
        self.plugin_manager.stop_registered_plugins()
        _logger.debug("[Configuration Manager] Plugin Manager Stopped")
        _logger.debug("[Configuration Manager] Stopping audio controllers")
        for audio_controller in self.audio_controllers:
            audio_controller.stop()
        _logger.debug("[Configuration Manager] Audio controllers stopped")
        _logger.debug("[Configuration Manager] Stopping mDNS")
        self.mdns_responder.stop()
        self.mdns_pinger.stop()
        self.mdns_settings_pinger.stop()
        _logger.debug("[Configuration Manager] mDNS stopped")
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
        source_locator: List[SourceDescription] = [source for source in self.source_descriptions
                                                    if source.name == source_name]
        if len(source_locator) == 0:
            raise NameError(f"source {source_name} not found")
        return source_locator[0]

    def get_sink_by_name(self, sink_name: SinkNameType) -> SinkDescription:
        """Returns a SinkDescription by name"""
        sink_locator: List[SinkDescription] = [sink for sink in self.sink_descriptions
                                                    if sink.name == sink_name]
        if len(sink_locator) == 0:
            raise NameError(f"sink {sink_name} not found")
        return sink_locator[0]

    def get_route_by_name(self, route_name: RouteNameType) -> RouteDescription:
        """Returns a RouteDescription by name"""
        route_locator: List[RouteDescription] = [route for route in self.route_descriptions
                                                    if route.name == route_name]
        if len(route_locator) == 0:
            raise NameError(f"Route {route_name} not found")
        return route_locator[0]

    def get_website_context(self):
        """Returns a tuple containing (List[SourceDescription],
                                       List[SinkDescription],
                                       List[RouteDescription])
           with all of the known Sources, Sinks, and Routes
        """
        data: Tuple[List[SourceDescription], List[SinkDescription], List[RouteDescription]]
        data = (self.source_descriptions, self.sink_descriptions, self.route_descriptions)
        return data

    # Private Functions

    # Verification functions

    def __verify_new_sink(self, sink: SinkDescription) -> None:
        """Verifies all sink group members exist and the sink doesn't
           Throws exception on failure"""
        for _sink in self.sink_descriptions:
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

    def __verify_source_unused(self, source: SourceDescription) -> None:
        """Verifies a source is unused by any routes, throws exception if not"""
        groups: List[SourceDescription]
        groups = self.active_configuration.get_source_groups_from_member(source)
        if len(groups) > 0:
            group_names: List[str] = []
            for group in groups:
                group_names.append(group.name)
            raise InUseError(f"Source: {source.name} is in use by Groups {group_names}")
        routes: List[RouteDescription] = []
        routes = self.active_configuration.get_routes_by_source(source)
        for route in routes:
            if route.source == source.name and route.enabled:
                raise InUseError(f"Source: {source.name} is in use by Route {route.name}")

    def __verify_sink_unused(self, sink: SinkDescription) -> None:
        """Verifies a sink is unused by any routes, throws exception if not"""
        groups: List[SinkDescription] = self.active_configuration.get_sink_groups_from_member(sink)
        if len(groups) > 0:
            group_names: List[str] = []
            for group in groups:
                group_names.append(group.name)
            raise InUseError(f"Sink: {sink.name} is in use by Groups {group_names}")
        routes: List[RouteDescription] = []
        routes = self.active_configuration.get_routes_by_sink(sink)
        for route in routes:
            if route.sink == sink.name and route.enabled:
                raise InUseError(f"Sink: {sink.name} is in use by Route {route.name}")
            
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
        try:
            with open("config.yaml", "r", encoding="UTF-8") as f:
                savedata: dict = yaml.unsafe_load(f)
                self.sink_descriptions = savedata["sinks"]
                self.source_descriptions = savedata["sources"]
                self.route_descriptions = savedata["routes"]


                for sink in self.sink_descriptions:
                    for unset_field in [field for field in sink.model_fields if
                                        field not in sink.model_fields_set]:
                        _logger.warning(
                    "[Configuration Manager] Setting unset attribte %s on sink %s to default %s",
                    unset_field, sink.name, SinkDescription.model_fields[unset_field].default)
                        setattr(sink, unset_field,
                                SinkDescription.model_fields[unset_field].default)

                for route in self.route_descriptions:
                    for unset_field in [field for field in route.model_fields if
                                         field not in route.model_fields_set]:
                        _logger.warning(
                    "[Configuration Manager] Setting unset attribte %s on route %s to default %s",
                    unset_field, route.name, RouteDescription.model_fields[unset_field].default)
                        setattr(route, unset_field,
                                RouteDescription.model_fields[unset_field].default)

                for source in self.source_descriptions:
                    for unset_field in [field for field in source.model_fields if
                                        field not in source.model_fields_set]:
                        _logger.warning(
                    "[Configuration Manager] Setting unset attribte %s on source %s to default %s",
                    unset_field, source.name, SourceDescription.model_fields[unset_field].default)
                        setattr(source, unset_field,
                                SourceDescription.model_fields[unset_field].default)

        except FileNotFoundError:
            _logger.warning("[Configuration Manager] Configuration not found., making new config")
        except KeyError as exc:
            _logger.error("[Configuration Manager] Configuration key %s missing, exiting.", exc)
            raise exc
            #sys.exit(-1)

        asyncio.run(self.websocket_config.broadcast_config_update(self.source_descriptions,
                                                                self.sink_descriptions,
                                                                self.route_descriptions))

    def __multiprocess_save(self):
        """Saves the config to config.yaml"""
        save_data: dict = {"sinks": self.sink_descriptions, "sources": self.source_descriptions,
                           "routes": self.route_descriptions }
        with open('config.yaml', 'w', encoding="UTF-8") as yaml_file:
            yaml.dump(save_data, yaml_file)

    def __save_config(self) -> None:
        """Saves the config"""

        _logger.info("[Configuration Manager] Saving config")
        if not self.configuration_semaphore.acquire(timeout=1):
            raise TimeoutError("Failed to get configuration semaphore")
        proc = Process(target=self.__multiprocess_save)
        proc.start()
        proc.join()
        asyncio.run(self.websocket_config.broadcast_config_update(self.source_descriptions,
                                                                self.sink_descriptions,
                                                                self.route_descriptions))
        self.configuration_semaphore.release()

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

        # Add sinks requesting a reload
        for audio_controller in self.audio_controllers:
            if audio_controller.wants_reload():
                _logger.info("[Configuration Manager] Controller %s wants a reload",
                             audio_controller.sink_info.name)
                for sink in new_map.keys():
                    if sink.name == audio_controller.sink_info.name:
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

        # Get a new resolved configuration from the current state
        _logger.debug("[Configuration Manager] Solving Configuration")
        self.active_configuration = ConfigurationSolver(self.source_descriptions,
                                                        self.sink_descriptions,
                                                        self.route_descriptions)

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

    def __reload_configuration(self) -> None:
        """Notifies the configuration manager to reload the configuration"""
        asyncio.run(self.websocket_config.broadcast_config_update(self.source_descriptions,
                                                        self.sink_descriptions,
                                                        self.route_descriptions))
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

    def __reload_volume_eq_timeshift_delay_configuration(self) -> None:
        """Notifies the configuration manager to reload the volume/eq"""
        if not self.volume_eq_reload_condition.acquire(timeout=1):
            raise TimeoutError("Failed to get volume/eq configuration reload condition")
        try:
            self.volume_eq_reload_condition.notify()
        except RuntimeError:
            pass
        self.volume_eq_reload_condition.release()
        _logger.debug("[Configuration Manager] Marking volume/eq for reload")

    def __process_and_apply_volume_eq_delay_timeshift(self) -> None:
        """Process the volume/eq/delay/timeshift configuration, apply them to all audiocontrollers"""
        new_configuration: ConfigurationSolver = ConfigurationSolver(
                                                        self.source_descriptions,
                                                        self.sink_descriptions,
                                                        self.route_descriptions)
        new_map: dict[SinkDescription,List[SourceDescription]]
        new_map = new_configuration.real_sinks_to_real_sources
        for sink, sources in new_map.items():
            for audio_controller in self.audio_controllers:
                if audio_controller.sink_info.name == sink.name:
                    for source in sources:
                        audio_controller.update_volume(source.name, source.volume)
                        audio_controller.update_equalizer(source.name, source.equalizer)
                        audio_controller.update_delay(source.name, source.delay)
                        audio_controller.update_timeshift(source.name, source.timeshift)
        try:
            self.volume_eq_reload_condition.release()
        except:
            pass

        self.__save_config()

    def __process_and_apply_configuration(self) -> None:
        """Process the configuration, get which sinks have changed and need reloaded,
           then reload them."""
        _logger.debug("[Configuration Manager] Reloading configuration")
        # Process new config, store what's changed
        added_sinks: List[SinkDescription]
        removed_sinks: List[SinkDescription]
        changed_sinks: List[SinkDescription]
        added_sinks, removed_sinks, changed_sinks = self.__process_configuration()
        _logger.info("[Configuration Manager] Config Reload")
        _logger.info("[Configuration Manager] Enabled Sink Controllers: %s",
                     [sink.name for sink in added_sinks])
        _logger.info("[Configuration Manager] Changed Sink Controllers: %s",
                     [sink.name for sink in changed_sinks])
        _logger.info("[Configuration Manager] Disabled Sink Controllers: %s",
                     [sink.name for sink in removed_sinks])
        original_audio_controllers: List[AudioController] = copy(self.audio_controllers)

        _logger.debug("[Configuration Manager] Removing and re-adding changed sinks")

        # Controllers to be reloaded
        for sink in changed_sinks:
            # Unload the old controller
            _logger.debug("[Configuration Manager] Removing Audio Controller %s", sink.name)
            for audio_controller in original_audio_controllers:
                if audio_controller.sink_info.name == sink.name:
                    if audio_controller in self.audio_controllers:
                        audio_controller.stop()
                        self.audio_controllers.remove(audio_controller)
            # Load a new controller
            sources: List[SourceDescription]
            sources = self.active_configuration.real_sinks_to_real_sources[sink]
            audio_controller = AudioController(sink, sources,
                                               self.tcp_manager.get_fd(sink.ip),
                                               self.__api_webstream)
            _logger.debug("[Configuration Manager] Adding Audio Controller %s", sink.name)
            self.audio_controllers.append(audio_controller)

        _logger.debug("[Configuration Manager] Removing now unused sinks")

        # Controllers to be removed
        for sink in removed_sinks:
            # Unload the old controller
            _logger.debug("Removing Audio Controller %s", sink.name)
            for audio_controller in original_audio_controllers:
                if audio_controller.sink_info.name == sink.name:
                    audio_controller.stop()
                    self.audio_controllers.remove(audio_controller)

        _logger.debug("[Configuration Manager] Adding new sinks")

        # Controllers to be added
        for sink in added_sinks:
            # Load a new controller
            sources: List[SourceDescription]
            sources = self.active_configuration.real_sinks_to_real_sources[sink]
            audio_controller = AudioController(sink, sources,
                                               self.tcp_manager.get_fd(sink.ip),
                                               self.__api_webstream)
            _logger.debug("[Configuration Manager] Adding Audio Controller %s", sink.name)
            self.audio_controllers.append(audio_controller)

        source_write_fds: List[int] = []
        for audio_controller in self.audio_controllers:
            source_write_fds.extend([source.writer_write for
                                    source in audio_controller.sources.values()])

        old_scream_recevier: ScreamReceiver = self.scream_recevier
        old_scream_per_process_recevier: ScreamPerProcessReceiver = self.scream_per_process_recevier
        old_multicast_scream_recevier: MulticastScreamReceiver = self.multicast_scream_recevier
        old_rtp_receiver: RTPReceiver = self.rtp_receiver

        if len(changed_sinks) > 0 or len(removed_sinks) > 0 or len(added_sinks) > 0:
            self.scream_recevier = ScreamReceiver(source_write_fds)
            self.scream_per_process_recevier = ScreamPerProcessReceiver(source_write_fds)
            self.multicast_scream_recevier = MulticastScreamReceiver(source_write_fds)
            self.rtp_receiver = RTPReceiver(source_write_fds)
            controller_write_fds: List[int] = []
            for audio_controller in self.audio_controllers:
                controller_write_fds.append(audio_controller.controller_write_fd)
            self.tcp_manager.replace_mixers(self.audio_controllers)

        if len(changed_sinks) > 0 or len(removed_sinks) > 0 or len(added_sinks) > 0:
            old_scream_recevier.stop()
            old_scream_per_process_recevier.stop()
            old_multicast_scream_recevier.stop()
            old_rtp_receiver.stop()

        self.__save_config()

        if len(changed_sinks) > 0 or len(removed_sinks) > 0 or len(added_sinks) > 0:
            _logger.debug("[Configuration Manager] Notifying plugin manager")
            self.plugin_manager.load_registered_plugins(source_write_fds)
            _logger.debug("[Configuration Manager] Reload done")

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
        hostname: str = str(ip)
        try:
            hostname = socket.gethostbyaddr(str(ip))[0].split(".")[0]
            _logger.debug("[Configuration Manager] Adding source %s got hostname %s via DNS",
                          ip, hostname)
        except socket.herror:
            try:
                _logger.debug(
                    "[Configuration Manager] Adding source %s couldn't get DNS, trying mDNS",
                    ip)
                resolver = dns.resolver.Resolver()
                resolver.nameservers = [str(ip)]
                resolver.nameserver_ports = {str(ip): 5353}
                answer = resolver.resolve_address(str(ip))
                rrset: dns.rrset.RRset = answer.response.answer[0]
                if isinstance(rrset[0], dns.rdtypes.ANY.PTR.PTR):
                    ptr: dns.rdtypes.ANY.PTR.PTR = rrset[0] # type: ignore
                    hostname = str(ptr.target).split(".", maxsplit=1)[0]
                    _logger.debug(
                        "[Configuration Manager] Adding source %s got hostname %s via mDNS",
                        ip, hostname)
            except dns.resolver.LifetimeTimeout:
                _logger.debug(
                    "[Configuration Manager] Adding source %s couldn't get hostname, using IP",
                    ip)
        return hostname

    def auto_add_source(self, ip: IPAddressType):
        """Checks if VNC is available and adds a source by IP with the correct options"""
        hostname: str = self.get_hostname_by_ip(ip)
        try:
            original_hostname: str = hostname
            counter: int = 1
            while self.get_source_by_name(hostname):
                hostname = f"{original_hostname} ({counter})"
                counter += 1
        except NameError:
            pass
            
        # Try to query settings to get config_id
        config_id = None
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
        ip: IPAddressType = IPAddressType(tag[:15].strip())
        hostname: str = self.get_hostname_by_ip(ip)
        process: str = tag[15:]
        sourcename: str = f"{hostname} - {process}"
        try:
            original_sourcename: str = sourcename
            counter: int = 1
            while self.get_source_by_name(sourcename):
                sourcename = f"{original_sourcename} ({counter})"
                counter += 1
        except NameError:
            pass
        _logger.info("[Configuration Manager] Adding Source Process %s - %s", hostname, process)
        source: SourceDescription = SourceDescription(name=sourcename, tag=tag, is_process=True)
        source_group_search: List[SourceDescription]
        source_group_search = [source for source in self.source_descriptions if source.tag == f"{hostname} All Processes"]
        source_group: SourceDescription
        if not source_group_search:
            source_group = SourceDescription(name=f"{hostname} All Processes", tag=f"{hostname} All Processes", ip=source.ip, is_group=True)
            self.add_source(source_group)
        else:
            source_group = source_group_search[0]
        source_group.group_members.append(source.name)
        self.add_source(source)
        self.__reload_configuration()

    def auto_add_sink(self, ip: IPAddressType):
        """Adds a sink with settings queried from the device or defaults if query fails"""
        hostname: str = str(ip)
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
       
        # Default audio settings
        bit_depth: int = 16
        sample_rate: int = 48000
        channels: int = 2
        channel_layout: str = "stereo"
        
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
                     "bit_depth=%d, sample_rate=%d, channels=%d, channel_layout=%s",
                     ip, bit_depth, sample_rate, channels, channel_layout)
        
        sink_desc = SinkDescription(name=hostname,
                                   ip=ip,
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
        self.scream_recevier.check_known_ips()
        self.scream_per_process_recevier.check_known_sources()
        self.multicast_scream_recevier.check_known_ips()
        self.rtp_receiver.check_known_ips()
        known_source_tags: List[str] = [str(desc.tag) for desc in self.source_descriptions]
        known_source_ips: List[str] = [str(desc.ip) for desc in self.source_descriptions]
        known_sink_ips: List[str] = [str(desc.ip) for desc in self.sink_descriptions]
        known_sink_config_ids: List[str] = [str(desc.config_id) for desc in self.sink_descriptions]
        for ip in self.scream_recevier.known_ips:
            if not str(ip) in known_source_ips:
                _logger.info("[Configuration Manager] Adding new source from Scream port %s", ip)
                self.auto_add_source(ip)
        for ip in self.multicast_scream_recevier.known_ips:
            if not str(ip) in known_source_ips:
                _logger.info(
                    "[Configuration Manager] Adding new source from Multicast Scream port %s", ip)
                self.auto_add_source(ip)
        for ip in self.rtp_receiver.known_ips:
            if not str(ip) in known_source_ips:
                _logger.info("[Configuration Manager] Adding new source from RTP port %s", ip)
                self.auto_add_source(ip)
        for ip in self.mdns_pinger.get_source_ips():
            if not str(ip) in known_source_ips:
                _logger.info("[Configuration Manager] Adding new source from mDNS %s", ip)
                self.auto_add_source(ip)
        for ip in self.mdns_pinger.get_sink_ips():
            if not str(ip) in known_sink_ips:
                _logger.info("[Configuration Manager] Adding new sink from mDNS %s", ip)
                self.auto_add_sink(ip)
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
        for tag in self.scream_per_process_recevier.known_sources:
            if not str(tag) in known_source_tags:
                _logger.info("[Configuration Manager] Adding new per-process source %s", tag)
                self.auto_add_process_source(tag)
        
    def run(self):
        """Monitors for the reload condition to be set and reloads the config when it is set"""
        self.__process_and_apply_configuration()
        while self.running:
            try:
                if not self.reload_condition.acquire(timeout=1):
                    raise TimeoutError("Failed to get configuration reload condition")
                if self.reload_condition.wait(timeout=.3) or self.plugin_manager.wants_reload(False):
                    # This will get set to true if something else wants to reload the configuration
                    # while it's already reloading
                    if self.plugin_manager.wants_reload():
                        _logger.info("[Configuration Manager] Plugin Manager")
                        self.reload_config = True
                    for audio_controller in self.audio_controllers:
                        if audio_controller.wants_reload():
                            self.reload_config = True
                    if self.tcp_manager.wants_reload:
                        _logger.info("[Configuration Manager] TCP Manager Wants Reload")
                        self.tcp_manager.wants_reload = False
                        self.reload_config = True
                    if self.reload_config:
                        self.reload_config = False
                        _logger.info("[Configuration Manager] Reloading the configuration")
                        if not self.__process_and_apply_configuration_with_timeout():
                            _logger.error("Configuration reload aborted due to errors or timeout.")
                            continue  # Skip the rest of the loop iteration
                else:
                    try:
                        self.reload_condition.release()
                    except:
                        pass
                if not self.volume_eq_reload_condition.acquire(timeout=1):
                    raise TimeoutError("Failed to get configuration reload condition")
                if self.volume_eq_reload_condition.wait(timeout=.3):
                    self.__process_and_apply_volume_eq_delay_timeshift()
                self.check_autodetected_sinks_sources()
            except:
                pass
