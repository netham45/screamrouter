"""This manages the target state of sinks, sources, and routes
   then runs audio controllers for each source"""
from multiprocessing import Process
import os
import sys
from copy import copy, deepcopy
import threading
import time
from pathlib import Path
from typing import List, Optional, Tuple
import yaml
from configuration.configuration_solver import ConfigurationSolver
from screamrouter_types import URL, SinkDescription, SourceDescription
from screamrouter_types import RouteDescription, InUseError
from screamrouter_types import DelayType, Equalizer, PortType, RouteNameType
from screamrouter_types import SinkNameType, SourceNameType, VolumeType
from api.api_webstream import APIWebStream
from audio.ffmpeg_url_play_writer import FFMpegURLPlayWriter
from audio.receiver_thread import ReceiverThread
from audio.audio_controller import AudioController
from audio.source_input_writer import SourceInputThread

import logger

_logger = logger.get_logger(__name__)

class ConfigurationManager:
    """Tracks configuration and loading the main receiver/sinks based off of it"""
    def __init__(self, websocket: Optional[APIWebStream]):
        """Initialize an empty controller"""
        self.sink_descriptions: List[SinkDescription] = []
        """List of Sinks the controller knows of"""
        self.source_descriptions:  List[SourceDescription] = []
        """List of Sources the controller knows of"""
        self.route_descriptions: List[RouteDescription] = []
        """List of Routes the controller knows of"""
        self.__loaded: bool = False
        """Holds rather the config is loaded"""
        self.audio_controllers: List[AudioController] = []
        """Holds a list of active Audio Controllers"""
        self.__url_play_counter: int = 0
        """Holds URL play counter so ffmpeg pipes can have unique names"""
        self.__api_webstream: Optional[APIWebStream] = websocket
        """Holds the WebStream API for streaming MP3s to browsers"""
        self.api_port: PortType = 8080
        """Port for Uvicorn API to listen on, can be changed by load_yaml"""
        self.receiver_port: PortType = 16401
        """Port for receiver to listen on, can be changed by load_yaml"""
        self.pipes_dir: Path = Path("./pipes/")
        """Folder for pipes to be stored in, can be changed by load_yaml"""
        self.__starting = False
        """Holds rather start_receiver is running so multiple instances don't go off at once"""
        self.url_playback_lock: threading.Lock = threading.Lock()
        """Lock to ensure URL playback is only started by one thread at a time"""
        self.configuration_solver: ConfigurationSolver
        """Holds the solved configuration"""
        self.old_configuration_solver: ConfigurationSolver = ConfigurationSolver([], [], [])
        """Holds the previously solved configuration to compare against for changes"""
        self.save_semaphore: threading.Semaphore = threading.Semaphore(1)
        """Semaphore so only one thread can save to YAML at a time"""
        self.__load_config()

        # Needs config loaded
        self.__receiver: ReceiverThread =  ReceiverThread(self.receiver_port)
        """Main receiver, handles receiving data from sources"""
        self.__process_and_apply_configuration()

        print( "------------------------------------------------------------------------")
        print( "  ScreamRouter")
        print(f"     Console Log level: {logger.CONSOLE_LOG_LEVEL}")
        print(f"     Log Dir: {os.path.realpath(logger.LOGS_DIR)}")
        print(f"     Pipe Dir: {os.path.realpath(self.pipes_dir)}")
        print(f"     API listening on: http://0.0.0.0:{self.api_port}")
        print(f"     Input Sink active at 0.0.0.0:{self.receiver_port}")
        print( "------------------------------------------------------------------------")

    # Public functions

    def get_sinks(self) -> List[SinkDescription]:
        """Returns a list of all sinks"""
        _sinks: List[SinkDescription] = []
        for sink in self.sink_descriptions:
            _sinks.append(copy(sink))
        return _sinks

    def add_sink(self, sink: SinkDescription) -> bool:
        """Adds a sink or sink group"""
        self.__verify_sink(sink)
        self.sink_descriptions.append(sink)
        self.__process_and_apply_configuration()
        return True

    def update_sink(self, sink: SinkDescription) -> bool:
        """Updates a sink"""
        original_sink: SinkDescription = self.configuration_solver.get_sink_from_name(sink.name)
        existing_sink_index: int = self.sink_descriptions.index(original_sink)
        self.sink_descriptions[existing_sink_index] = sink
        self.__process_and_apply_configuration()
        return True

    def delete_sink(self, sink_name: SinkNameType) -> bool:
        """Deletes a sink by name"""
        sink: SinkDescription = self.configuration_solver.get_sink_from_name(sink_name)
        self.__verify_sink_unused(sink)
        self.sink_descriptions.remove(sink)
        self.__process_and_apply_configuration()
        return True

    def enable_sink(self, sink_name: SinkNameType) -> bool:
        """Enables a sink by name"""
        sink: SinkDescription = self.configuration_solver.get_sink_from_name(sink_name)
        sink.enabled = True
        self.__process_and_apply_configuration()
        return True

    def disable_sink(self, sink_name: SinkNameType) -> bool:
        """Disables a sink by name"""
        sink: SinkDescription = self.configuration_solver.get_sink_from_name(sink_name)
        sink.enabled = False
        self.__process_and_apply_configuration()
        return True

    def get_sources(self) -> List[SourceDescription]:
        """Get a list of all sources"""
        return copy(self.source_descriptions)

    def add_source(self, source: SourceDescription) -> bool:
        """Add a source or source group"""
        self.__verify_source(source)
        self.source_descriptions.append(source)
        self.__process_and_apply_configuration()
        return True

    def update_source(self, source: SourceDescription) -> bool:
        """Updates a source"""
        existing_source: SourceDescription
        existing_source = self.configuration_solver.get_source_from_name(source.name)
        existing_source_index: int = self.source_descriptions.index(existing_source)
        self.source_descriptions[existing_source_index] = source
        self.__process_and_apply_configuration()
        return True

    def delete_source(self, source_name: SourceNameType) -> bool:
        """Deletes a source by name"""
        source: SourceDescription = self.configuration_solver.get_source_from_name(source_name)
        self.__verify_source_unused(source)
        self.source_descriptions.remove(source)
        self.__process_and_apply_configuration()
        return True

    def enable_source(self, source_name: SourceNameType) -> bool:
        """Enables a source by index"""
        source: SourceDescription = self.configuration_solver.get_source_from_name(source_name)
        source.enabled = True
        self.__process_and_apply_configuration()
        return True

    def disable_source(self, source_name: SourceNameType) -> bool:
        """Disables a source by index"""
        source: SourceDescription = self.configuration_solver.get_source_from_name(source_name)
        source.enabled = False
        self.__process_and_apply_configuration()
        return True

    def get_routes(self) -> List[RouteDescription]:
        """Returns a list of all routes"""
        return copy(self.route_descriptions)

    def add_route(self, route: RouteDescription) -> bool:
        """Adds a route"""
        self.__verify_route(route)
        self.route_descriptions.append(route)
        self.__process_and_apply_configuration()
        return True

    def update_route(self, route: RouteDescription) -> bool:
        """Updates a route"""
        existing_route: RouteDescription = self.configuration_solver.get_route_from_name(route.name)
        existing_route_index: int = self.route_descriptions.index(existing_route)
        self.route_descriptions[existing_route_index] = route
        self.__process_and_apply_configuration()
        return True

    def delete_route(self, route_name: RouteNameType) -> bool:
        """Deletes a route by name"""
        route: RouteDescription = self.configuration_solver.get_route_from_name(route_name)
        self.route_descriptions.remove(route)
        self.__process_and_apply_configuration()
        return True

    def enable_route(self, route_name: RouteNameType) -> bool:
        """Enables a route by name"""
        route: RouteDescription = self.configuration_solver.get_route_from_name(route_name)
        route.enabled = True
        self.__process_and_apply_configuration()
        return True

    def disable_route(self, route_name: RouteNameType) -> bool:
        """Disables a route by name"""
        route: RouteDescription = self.configuration_solver.get_route_from_name(route_name)
        route.enabled = False
        self.__process_and_apply_configuration()
        return True

    def update_source_equalizer(self, source_name: SourceNameType, equalizer: Equalizer) -> bool:
        """Set the equalizer for a source or source group"""
        source: SourceDescription = self.configuration_solver.get_source_from_name(source_name)
        source.equalizer = equalizer
        self.__process_and_apply_configuration()
        return True

    def update_source_volume(self, source_name: SourceNameType, volume: VolumeType) -> bool:
        """Set the volume for a source or source group"""
        source: SourceDescription = self.configuration_solver.get_source_from_name(source_name)
        source.volume = volume
        self.__apply_volume_change()
        return True

    def update_sink_equalizer(self, sink_name: SinkNameType, equalizer: Equalizer) -> bool:
        """Set the equalizer for a sink or sink group"""
        sink: SinkDescription = self.configuration_solver.get_sink_from_name(sink_name)
        sink.equalizer = equalizer
        self.__process_and_apply_configuration()
        return True

    def update_sink_volume(self, sink_name: SinkNameType, volume: VolumeType) -> bool:
        """Set the volume for a sink or sink group"""
        sink: SinkDescription = self.configuration_solver.get_sink_from_name(sink_name)
        sink.volume = volume
        self.__apply_volume_change()
        return True

    def update_sink_delay(self, sink_name: SinkNameType, delay: DelayType) -> bool:
        """Set the delay for a sink"""
        sink: SinkDescription = self.configuration_solver.get_sink_from_name(sink_name)
        sink.delay = delay
        self.__process_and_apply_configuration()
        return True

    def update_route_equalizer(self, route_name: RouteNameType, equalizer: Equalizer) -> bool:
        """Set the equalizer for a route"""
        route: RouteDescription = self.configuration_solver.get_route_from_name(route_name)
        route.equalizer = equalizer
        self.__process_and_apply_configuration()
        return True

    def update_route_volume(self, route_name: RouteNameType, volume: VolumeType) -> bool:
        """Set the volume for a route"""
        route: RouteDescription = self.configuration_solver.get_route_from_name(route_name)
        route.volume = volume
        self.__apply_volume_change()
        return True

    def play_url(self, sink_name: SinkNameType, url: URL, volume: VolumeType) -> bool:
        """Play a URL on a sink or group of sinks"""
        self.url_playback_lock.acquire()
        sink: SinkDescription = self.configuration_solver.get_sink_from_name(sink_name)
        all_child_sinks: List[SinkDescription] = self.configuration_solver.get_real_sinks_from_sink(
                                                                                            sink,
                                                                                            True,
                                                                                            volume)
        found: bool = False
        for sink_description in all_child_sinks:
            for sink_controller in self.audio_controllers:
                if sink_description.name == sink_controller.sink_info.name:
                    found = True
                    tag: str = f"ffmpeg{self.__url_play_counter}"
                    pipe_name: str = f"{self.pipes_dir}scream-{sink_description.ip}-{tag}"
                    pipe_path: Path = Path(pipe_name)
                    ffmpeg_source_info: SourceInputThread
                    ffmpeg_source_info = SourceInputThread(
                                            tag, pipe_path,
                                            sink_description.ip,
                                            SourceDescription(
                                                name=tag,
                                                volume=sink_description.volume * volume))
                    sink_controller.sources_lock.acquire()
                    sink_controller.sources[tag] = ffmpeg_source_info
                    sink_controller.sources_lock.release()
        if found:  # If at least one sink is found start playback
            tag: str = f"ffmpeg{self.__url_play_counter}"
            pipe_name: str = f"{self.pipes_dir}ffmpeg{self.__url_play_counter}"
            pipe_path: Path = Path(pipe_name)
            FFMpegURLPlayWriter(url.url, 1, all_child_sinks[0], pipe_path, tag, self.__receiver)
            self.__url_play_counter = self.__url_play_counter + 1
        self.url_playback_lock.release()
        return found

    def stop(self) -> bool:
        """Stop the receiver, this stops all ffmepg processes and all sinks."""
        self.__receiver.stop()
        self.__receiver.join()
        return True

    def set_webstream(self, webstream: APIWebStream) -> None:
        """Sets the webstream"""
        self.__api_webstream = webstream
        self.__process_and_apply_configuration()

    # Private Functions

    # Verification functions

    def __verify_sink(self, sink: SinkDescription) -> None:
        """Verifies all sink group members exist, throws exception if not"""
        for _sink in self.sink_descriptions:
            if sink.name == _sink.name:
                raise ValueError(f"Sink name '{sink.name}' already in use")
        for member in sink.group_members:
            self.configuration_solver.get_sink_from_name(member)
        if sink.is_group:
            for member in sink.group_members:
                self.configuration_solver.get_sink_from_name(member)

    def __verify_source(self, source: SourceDescription) -> None:
        """Verifies all source group members exist, throws exception if not"""
        for _source in self.source_descriptions:
            if source.name == _source.name:
                raise ValueError(f"Source name '{source.name}' already in use")
        if source.is_group:
            for member in source.group_members:
                self.configuration_solver.get_source_from_name(member)

    def __verify_route(self, route: RouteDescription) -> None:
        """Verifies route sink and source exist, throws exception if not"""
        self.configuration_solver.get_sink_from_name(route.sink)
        self.configuration_solver.get_source_from_name(route.source)

    def __verify_source_unused(self, source: SourceDescription) -> None:
        """Verifies a source is unused, throws exception if not"""
        groups: List[SourceDescription]
        groups = self.configuration_solver.get_source_groups_from_member(source)
        if len(groups) > 0:
            group_names: List[str] = []
            for group in groups:
                group_names.append(group.name)
            raise InUseError(f"Source:{source.name} is in use by Groups {group_names}")
        routes: List[RouteDescription] = []
        routes = self.configuration_solver.get_routes_by_source(source)
        for route in routes:
            if route.source == source.name:
                raise InUseError(f"Source:{source.name} is in use by Route {route.name}")

    def __verify_sink_unused(self, sink: SinkDescription) -> None:
        """Verifies a sink is unused, throws exception if not"""
        groups: List[SinkDescription] = self.configuration_solver.get_sink_groups_from_member(sink)
        if len(groups) > 0:
            group_names: List[str] = []
            for group in groups:
                group_names.append(group.name)
            raise InUseError(f"Sink:{sink.name} is in use by Groups {group_names}")
        routes: List[RouteDescription] = []
        routes = self.configuration_solver.get_routes_by_sink(sink)
        for route in routes:
            if route.sink == sink.name:
                raise InUseError(f"Sink:{sink.name} is in use by Route {route.name}")

# Adjust Audio Function

    def __apply_volume_change(self) -> None:
        """Calculates the volume for each Source on each Sink and tells the Sink Controllers"""
        self.__process_configuration()
        for sink_name, sources in self.configuration_solver.real_sinks_to_real_sources.items():
            for _sink in self.audio_controllers:
                if _sink.sink_info.name == sink_name:
                    for source in sources:
                        _sink.update_source_volume(source)
        self.__save_config()

# Configuration functions

    def __load_config(self) -> None:
        """Loads the config"""
        try:
            with open("config.yaml", "r", encoding="UTF-8") as f:
                savedata: dict = yaml.unsafe_load(f)
                self.sink_descriptions = savedata["sinks"]
                self.source_descriptions = savedata["sources"]
                self.route_descriptions = savedata["routes"]
                serverinfo = savedata["serverinfo"]
                self.api_port = serverinfo["api_port"]
                self.receiver_port = serverinfo["receiver_port"]
                self.pipes_dir = serverinfo["pipes_dir"]
        except FileNotFoundError:
            _logger.warning("[Controller] Configuration not found, starting with a blank config")
        except KeyError as exc:
            _logger.error("[Controller] Configuration key %s missing, exiting.", exc)
            sys.exit(-1)
        self.__loaded = True

    def __multiprocess_save(self):
        """Saves the config to config.yaml"""
        self.save_semaphore.acquire()
        serverinfo: dict = {"api_port": self.api_port, "receiver_port": self.receiver_port,
                        "logs_dir": logger.LOGS_DIR, "pipes_dir": self.pipes_dir,
                        "console_log_level": logger.CONSOLE_LOG_LEVEL}
        save_data: dict = {"sinks": self.sink_descriptions, "sources": self.source_descriptions,
                           "routes": self.route_descriptions, "serverinfo": serverinfo}
        with open('config.yaml', 'w', encoding="UTF-8") as yaml_file:
            yaml.dump(save_data, yaml_file)
        self.save_semaphore.release()

    def __save_config(self) -> None:
        """Saves the config"""
        proc = Process(target=self.__multiprocess_save)
        proc.start()

# Configuration processing functions

    def __find_changed_sinks(self) -> Tuple[List[SinkDescription], List[SinkDescription],
                                            List[SinkDescription], List[SinkDescription]]:
        """Finds changed sinks, returns a list of the new sinks"""
        added_sinks: List[SinkDescription] = []
        unchanged_sinks: List[SinkDescription] = []
        changed_sinks: List[SinkDescription] = []
        removed_sinks: List[SinkDescription] = []

        new_map: dict[SinkDescription,List[SourceDescription]]
        new_map = self.configuration_solver.real_sinks_to_real_sources

        old_map: dict[SinkDescription,List[SourceDescription]]
        old_map = self.old_configuration_solver.real_sinks_to_real_sources

        for sink, sources in new_map.items():
            if not sink in old_map.keys():
                if sink.name in [old_sink.name for old_sink in old_map.keys()]:
                    changed_sinks.append(sink)
                else:
                    added_sinks.append(sink)
                continue
            old_sources: List[SourceDescription] = old_map[sink]
            for source in sources:
                if source not in old_sources:
                    changed_sinks.append(sink)

        for old_sink, old_sources in old_map.items():
            if not old_sink in new_map:
                if old_sink.name in [sink.name for sink in new_map.keys()]:
                    sink: SinkDescription = [sink for sink in new_map.keys()
                                             if sink.name == old_sink.name][0]
                    changed_sinks.append(sink)
                else:
                    removed_sinks.append(old_sink)
                continue
            sources: List[SourceDescription] = new_map[old_sink]
            for source in old_sources:
                if source not in sources:
                    sink: SinkDescription = [sink for sink in new_map.keys()
                                             if sink.name == old_sink.name][0]
                    changed_sinks.append(sink)


        return added_sinks, removed_sinks, changed_sinks, unchanged_sinks

    def __process_configuration(self) -> Tuple[List[SinkDescription], List[SinkDescription],
                                               List[SinkDescription], List[SinkDescription]]:
        """Rebuilds the configuration solver and updates the configuration"""
        self.configuration_solver = ConfigurationSolver(self.source_descriptions,
                                                        self.sink_descriptions,
                                                        self.route_descriptions)
        added_sinks: List[SinkDescription]
        removed_sinks: List[SinkDescription]
        changed_sinks: List[SinkDescription]
        unchanged_sinks: List[SinkDescription]
        added_sinks, removed_sinks, changed_sinks, unchanged_sinks = self.__find_changed_sinks()
        self.old_configuration_solver = deepcopy(self.configuration_solver)
        return added_sinks, removed_sinks, changed_sinks, unchanged_sinks

    def __process_and_apply_configuration(self) -> None:
        """Start or restart ScreamRouter engine"""
        if not self.__loaded:
            return
        while self.__starting:
            time.sleep(.01)
        self.__starting = True
        added_sinks: List[SinkDescription]
        removed_sinks: List[SinkDescription]
        changed_sinks: List[SinkDescription]
        unchanged_sinks: List[SinkDescription]
        added_sinks, removed_sinks, changed_sinks, unchanged_sinks = self.__process_configuration()
        _logger.debug("[Controller] Config Reload")
        _logger.debug("New Sink Controllers: %s", [sink.name for sink in added_sinks])
        _logger.debug("Unchanged Sink Controllers: %s", [sink.name for sink in unchanged_sinks])
        _logger.debug("Changed Sink Controllers: %s", [sink.name for sink in changed_sinks])
        _logger.debug("Removed Sink Controllers: %s", [sink.name for sink in removed_sinks])
        original_audio_controllers: List[AudioController] = copy(self.audio_controllers)

        for sink in changed_sinks:
            _logger.debug("Removing Audio Controller %s", sink.name)
            self.__receiver.unregister_audio_controller_by_sink(sink)
            for audio_controller in original_audio_controllers:
                if audio_controller.sink_info.name == sink.name:
                    if audio_controller in self.audio_controllers:
                        self.audio_controllers.remove(audio_controller)
            sources: List[SourceDescription]
            sources = self.configuration_solver.real_sinks_to_real_sources[sink]
            audio_controller = AudioController(sink, sources, self.__api_webstream)
            _logger.debug("Adding Audio Controller %s", sink.name)
            self.__receiver.register_audio_controller(audio_controller)
            self.audio_controllers.append(audio_controller)

        for sink in removed_sinks:
            _logger.debug("Removing Audio Controller %s", sink.name)
            self.__receiver.unregister_audio_controller_by_sink(sink)
            for audio_controller in original_audio_controllers:
                if audio_controller.sink_info.name == sink.name:
                    self.audio_controllers.remove(audio_controller)

        for sink in added_sinks:
            sources: List[SourceDescription]
            sources = self.configuration_solver.real_sinks_to_real_sources[sink]
            audio_controller = AudioController(sink, sources, self.__api_webstream)
            _logger.debug("Adding Audio Controller %s", sink.name)
            self.__receiver.register_audio_controller(audio_controller)
            self.audio_controllers.append(audio_controller)
        self.__starting = False
        self.__save_config()
