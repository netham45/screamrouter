"""This manages the target state of sinks, sources, and routes
   then runs audio controllers for each source"""
import multiprocessing
import os
import sys
import threading
from copy import copy, deepcopy
from multiprocessing import Process
from typing import List, Tuple

import yaml

import src.constants.constants as constants
from src.plugin_manager.plugin_manager import PluginManager
import src.screamrouter_logger.screamrouter_logger as screamrouter_logger
from src.api.api_webstream import APIWebStream
from src.audio.audio_controller import AudioController
from src.audio.receiver_thread import ReceiverThread
from src.configuration.configuration_solver import ConfigurationSolver
from src.screamrouter_types.annotations import (DelayType, RouteNameType,
                                                SinkNameType, SourceNameType,
                                                VolumeType)
from src.screamrouter_types.configuration import (Equalizer, RouteDescription,
                                                  SinkDescription,
                                                  SourceDescription)
from src.screamrouter_types.exceptions import InUseError

_logger = screamrouter_logger.get_logger(__name__)

class ConfigurationManager(threading.Thread):
    """Tracks configuration and loading the main receiver/sinks based off of it"""
    def __init__(self, websocket: APIWebStream, plugin_manager: PluginManager):
        """Initialize the controller"""
        super().__init__(name="Configuration Manager")
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
        self.running: bool = True
        """Rather the thread is running or not"""
        self.receiver: ReceiverThread = ReceiverThread([])
        """Holds the thread that receives UDP packets from Scream"""
        self.reload_config: bool = False
        """Set to true to reload the config. Used so config
           changes during another config reload still trigger
           a reload"""
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

    def update_sink(self, new_sink: SinkDescription) -> bool:
        """Updates a sink"""
        sink: SinkDescription = self.get_sink_by_name(new_sink.name)
        for field in new_sink.model_fields_set:
            setattr(sink, field, getattr(new_sink, field))
        self.__reload_configuration()
        return True

    def delete_sink(self, sink_name: SinkNameType) -> bool:
        """Deletes a sink by name"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        self.__verify_sink_unused(sink)
        self.sink_descriptions.remove(sink)
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

    def add_source(self, source: SourceDescription) -> bool:
        """Add a source or source group"""
        self.__verify_new_source(source)
        self.source_descriptions.append(source)
        self.__reload_configuration()
        return True

    def update_source(self, new_source: SourceDescription) -> bool:
        """Updates a source"""
        source: SourceDescription = self.get_source_by_name(new_source.name)
        for field in new_source.model_fields_set:
            setattr(source, field, getattr(new_source, field))
        self.__reload_configuration()
        return True

    def delete_source(self, source_name: SourceNameType) -> bool:
        """Deletes a source by name"""
        source: SourceDescription = self.active_configuration.get_source_from_name(source_name)
        self.__verify_source_unused(source)
        self.source_descriptions.remove(source)
        self.__reload_configuration()
        return True

    def enable_source(self, source_name: SourceNameType) -> bool:
        """Enables a source by index"""
        source: SourceDescription = self.active_configuration.get_source_from_name(source_name)
        source.enabled = True
        self.__reload_configuration()
        return True

    def disable_source(self, source_name: SourceNameType) -> bool:
        """Disables a source by index"""
        source: SourceDescription = self.active_configuration.get_source_from_name(source_name)
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

    def update_route(self, new_route: RouteDescription) -> bool:
        """Updates a route"""
        route: RouteDescription = self.get_route_by_name(new_route.name)
        for field in new_route.model_fields_set:
            setattr(route, field, getattr(new_route, field))
        self.__reload_configuration()
        return True

    def delete_route(self, route_name: RouteNameType) -> bool:
        """Deletes a route by name"""
        route: RouteDescription = self.active_configuration.get_route_from_name(route_name)
        self.route_descriptions.remove(route)
        self.__reload_configuration()
        return True

    def enable_route(self, route_name: RouteNameType) -> bool:
        """Enables a route by name"""
        route: RouteDescription = self.active_configuration.get_route_from_name(route_name)
        route.enabled = True
        self.__reload_configuration()
        return True

    def disable_route(self, route_name: RouteNameType) -> bool:
        """Disables a route by name"""
        route: RouteDescription = self.active_configuration.get_route_from_name(route_name)
        route.enabled = False
        self.__reload_configuration()
        return True

    def update_source_equalizer(self, source_name: SourceNameType, equalizer: Equalizer) -> bool:
        """Set the equalizer for a source or source group"""
        source: SourceDescription = self.active_configuration.get_source_from_name(source_name)
        source.equalizer = equalizer
        self.__reload_configuration()
        return True

    def update_source_volume(self, source_name: SourceNameType, volume: VolumeType) -> bool:
        """Set the volume for a source or source group"""
        source: SourceDescription = self.active_configuration.get_source_from_name(source_name)
        source.volume = volume
        self.__reload_configuration()
        return True

    def update_sink_equalizer(self, sink_name: SinkNameType, equalizer: Equalizer) -> bool:
        """Set the equalizer for a sink or sink group"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        sink.equalizer = equalizer
        self.__reload_configuration()
        return True

    def update_sink_volume(self, sink_name: SinkNameType, volume: VolumeType) -> bool:
        """Set the volume for a sink or sink group"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        sink.volume = volume
        self.__reload_configuration()
        return True

    def update_sink_delay(self, sink_name: SinkNameType, delay: DelayType) -> bool:
        """Set the delay for a sink"""
        sink: SinkDescription = self.get_sink_by_name(sink_name)
        sink.delay = delay
        self.__reload_configuration()
        return True

    def update_route_equalizer(self, route_name: RouteNameType, equalizer: Equalizer) -> bool:
        """Set the equalizer for a route"""
        route: RouteDescription = self.active_configuration.get_route_from_name(route_name)
        route.equalizer = equalizer
        self.__reload_configuration()
        return True

    def update_route_volume(self, route_name: RouteNameType, volume: VolumeType) -> bool:
        """Set the volume for a route"""
        route: RouteDescription = self.active_configuration.get_route_from_name(route_name)
        route.volume = volume
        self.__reload_configuration()
        return True

    def stop(self) -> bool:
        """Stop all threads/processes"""
        _logger.debug("Stopping receiver")
        self.receiver.stop()
        _logger.debug("Receiver stopped")
        _logger.debug("Stopping audio controllers")
        for audio_controller in self.audio_controllers:
            audio_controller.stop()
        _logger.debug("Audio controllers stopped")

        _logger.debug("Stopping webstream")
        self.__api_webstream.stop()
        _logger.debug("Webstream stopped")
        _logger.debug("Stopping Plugin Manager")
        self.plugin_manager.stop_registered_plugins()
        _logger.debug("Plugin Manager Stopped")
        self.running = False
        if constants.WAIT_FOR_CLOSES:
            self.join()
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
                self.active_configuration.get_source_from_name(member)

    def __verify_new_route(self, route: RouteDescription) -> None:
        """Verifies route sink and source exist, throws exception if not"""
        self.get_sink_by_name(route.sink)
        self.active_configuration.get_source_from_name(route.source)

    def __verify_source_unused(self, source: SourceDescription) -> None:
        """Verifies a source is unused by any routes, throws exception if not"""
        groups: List[SourceDescription]
        groups = self.active_configuration.get_source_groups_from_member(source)
        if len(groups) > 0:
            group_names: List[str] = []
            for group in groups:
                group_names.append(group.name)
            raise InUseError(f"Source:{source.name} is in use by Groups {group_names}")
        routes: List[RouteDescription] = []
        routes = self.active_configuration.get_routes_by_source(source)
        for route in routes:
            if route.source == source.name:
                raise InUseError(f"Source:{source.name} is in use by Route {route.name}")

    def __verify_sink_unused(self, sink: SinkDescription) -> None:
        """Verifies a sink is unused by any routes, throws exception if not"""
        groups: List[SinkDescription] = self.active_configuration.get_sink_groups_from_member(sink)
        if len(groups) > 0:
            group_names: List[str] = []
            for group in groups:
                group_names.append(group.name)
            raise InUseError(f"Sink:{sink.name} is in use by Groups {group_names}")
        routes: List[RouteDescription] = []
        routes = self.active_configuration.get_routes_by_sink(sink)
        for route in routes:
            if route.sink == sink.name:
                raise InUseError(f"Sink:{sink.name} is in use by Route {route.name}")

# Configuration load/save functions

    def __load_config(self) -> None:
        """Loads the config"""
        try:
            with open("config.yaml", "r", encoding="UTF-8") as f:
                savedata: dict = yaml.unsafe_load(f)
                self.sink_descriptions = savedata["sinks"]
                self.source_descriptions = savedata["sources"]
                self.route_descriptions = savedata["routes"]
        except FileNotFoundError:
            _logger.warning("[Controller] Configuration not found, starting with a blank config")
        except KeyError as exc:
            _logger.error("[Controller] Configuration key %s missing, exiting.", exc)
            sys.exit(-1)

    def __multiprocess_save(self):
        """Saves the config to config.yaml"""
        save_data: dict = {"sinks": self.sink_descriptions, "sources": self.source_descriptions,
                           "routes": self.route_descriptions }
        with open('config.yaml', 'w', encoding="UTF-8") as yaml_file:
            yaml.dump(save_data, yaml_file)

    def __save_config(self) -> None:
        """Saves the config"""
        if not self.configuration_semaphore.acquire(timeout=1):
            raise TimeoutError("Failed to get configuration semaphore")
        proc = Process(target=self.__multiprocess_save)
        proc.start()
        proc.join()
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

        self.active_configuration = ConfigurationSolver(self.source_descriptions,
                                                        self.sink_descriptions,
                                                        self.route_descriptions)

        # Add temporary plugin sources
        temporary_sources: dict[SinkNameType, List[SourceDescription]]
        temporary_sources = self.plugin_manager.get_temporary_sources()
        for plugin_sink_name, plugin_sources in temporary_sources.items():
            found: bool = False
            for sink, sources in self.active_configuration.real_sinks_to_real_sources.items():
                if plugin_sink_name == sink.name:
                    _logger.info("Adding temporary sources to existing sink %s", sink.name)
                    found = True
                    sources.extend(plugin_sources)
            if not found:
                sink: SinkDescription = self.get_sink_by_name(plugin_sink_name)
                if sink.enabled:
                    self.active_configuration.real_sinks_to_real_sources[sink] = plugin_sources
                    _logger.info("Adding temporary sources to new sink %s", sink.name)

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
        if not self.reload_condition.acquire(timeout=1):
            raise TimeoutError("Failed to get configuration reload condition")
        self.reload_condition.notify()
        self.reload_condition.release()
        self.reload_config = True
        _logger.debug("Marking config for reload")

    def __process_and_apply_configuration(self) -> None:
        """Process the configuration, get which sinks have changed and need reloaded,
           then reload them."""
        # Process new config, store what's changed
        added_sinks: List[SinkDescription]
        removed_sinks: List[SinkDescription]
        changed_sinks: List[SinkDescription]
        added_sinks, removed_sinks, changed_sinks = self.__process_configuration()
        _logger.info("[Controller] Config Reload")
        _logger.info("[Controller] Enabled Sink Controllers: %s",
                     [sink.name for sink in added_sinks])
        _logger.info("[Controller] Changed Sink Controllers: %s",
                     [sink.name for sink in changed_sinks])
        _logger.info("[Controller] Disabled Sink Controllers: %s",
                     [sink.name for sink in removed_sinks])
        original_audio_controllers: List[AudioController] = copy(self.audio_controllers)

        if len(changed_sinks) > 0 or len(removed_sinks) > 0 or len(added_sinks) > 0:
            self.receiver.stop()

        # Controllers to be reloaded
        for sink in changed_sinks:
            # Unload the old controller
            _logger.debug("Removing Audio Controller %s", sink.name)
            for audio_controller in original_audio_controllers:
                if audio_controller.sink_info.name == sink.name:
                    if audio_controller in self.audio_controllers:
                        audio_controller.stop()
                        self.audio_controllers.remove(audio_controller)
            # Load a new controller
            sources: List[SourceDescription]
            sources = self.active_configuration.real_sinks_to_real_sources[sink]
            audio_controller = AudioController(sink, sources, self.__api_webstream)
            _logger.debug("Adding Audio Controller %s", sink.name)
            self.audio_controllers.append(audio_controller)

        # Controllers to be removed
        for sink in removed_sinks:
            # Unload the old controller
            _logger.debug("Removing Audio Controller %s", sink.name)
            for audio_controller in original_audio_controllers:
                if audio_controller.sink_info.name == sink.name:
                    audio_controller.stop()
                    self.audio_controllers.remove(audio_controller)

        # Controllers to be added
        for sink in added_sinks:
            # Load a new controller
            sources: List[SourceDescription]
            sources = self.active_configuration.real_sinks_to_real_sources[sink]
            audio_controller = AudioController(sink, sources, self.__api_webstream)
            _logger.debug("Adding Audio Controller %s", sink.name)
            self.audio_controllers.append(audio_controller)

        # Check if there was a change before reloading or saving
        if len(changed_sinks) > 0 or len(removed_sinks) > 0 or len(added_sinks) > 0:
            self.receiver = ReceiverThread([audio_controller.queue for
                                            audio_controller in self.audio_controllers])
            self.__save_config()
            queues: List[multiprocessing.Queue] = []
            for audio_controller in self.audio_controllers:
                queues.append(audio_controller.queue)
            self.plugin_manager.load_registered_plugins(queues)

    def run(self):
        self.__process_and_apply_configuration()
        if not self.reload_condition.acquire(timeout=1):
            raise TimeoutError("Failed to get configuration reload condition")
        while self.running:
            if self.reload_condition.wait(timeout=.1) or self.plugin_manager.wants_reload:
                # This will get set to true if something else wants to reload the configuration
                # while it's already reloading
                if self.plugin_manager.wants_reload():
                    self.reload_config = True
                while self.reload_config:
                    self.reload_config = False
                    _logger.info("Reloading the configuration")
                    self.__process_and_apply_configuration()
        self.reload_condition.release()
