"""This is the main controller that holds the configuration and spawns the receiver/sink handlers"""
import os
import sys
from copy import copy

import time
from typing import List, Optional

import yaml

from api.api_types import Equalizer
from api.api_webstream import APIWebStream
from audio.ffmpeg_url_play_thread import FFMpegPlayURL
from audio.receiver import Receiver
import audio.sink_controller
from audio.source_to_ffmpeg_writer import SourceToFFMpegWriter

from configuration.configuration_types import SinkDescription, SourceDescription
from configuration.configuration_types import RouteDescription, InUseError

from logger import get_logger, LOGS_DIR, CONSOLE_LOG_LEVEL

logger = get_logger(__name__)

# Helper functions
#def unique[T](list: List[T]) -> List[T]:  # One day
def unique(list_in: List) -> List:
    """Returns a list with duplicates filtered out"""
    return_list = []
    for element in list_in:
        if not element in return_list:
            return_list.append(element)
    return return_list


class ConfigurationController:
    """Tracks configuration and loading the main receiver/sinks based off of it"""
    def __init__(self, websocket: Optional[APIWebStream]):
        """Initialize an empty controller"""
        self.__sink_objects: List[audio.sink_controller.SinkController] = []
        """List of Sink objects the receiver is using"""
        self.__sink_descriptions: List[SinkDescription] = []
        """List of Sinks the controller knows of"""
        self.__source_descriptions:  List[SourceDescription] = []
        """List of Sources the controller knows of"""
        self.__route_descriptions: List[RouteDescription] = []
        """List of Routes the controller knows of"""
        self.sinks_to_sources = {}
        """Dict mapping all sink IPs to the source descriptions playing to them"""
        self.__receiver: Receiver
        """Main receiver, handles receiving data from sources"""
        self.__receiverset: bool = False
        """Rather the recevier has been set"""
        self.__loaded: bool = False
        """Holds rather the config is loaded"""
        self.__url_play_counter: int = 0
        """Holds URL play counter so ffmpeg pipes can have unique names"""
        self.__api_webstream: Optional[APIWebStream] = websocket
        """Holds the WebStream API for streaming MP3s to browsers"""
        self.api_port: int = 8080
        """Port for Uvicorn API to listen on, can be changed by load_yaml"""
        self.receiver_port: int = 16401
        """Port for receiver to listen on, can be changed by load_yaml"""
        self.pipes_dir: str = "./pipes/"
        """Folder for pipes to be stored in, can be changed by load_yaml"""
        self.__starting = False
        """Holds rather start_receiver is running so multiple instances don't go off at once"""
        self.__load_yaml()
        self.__start_receiver()
        print( "------------------------------------------------------------------------")
        print( "  ScreamRouter")
        print(f"     Console Log level: {CONSOLE_LOG_LEVEL}")
        print(f"     Log Dir: {os.path.realpath(LOGS_DIR)}")
        print(f"     Pipe Dir: {os.path.realpath(self.pipes_dir)}")
        print(f"     API listening on: http://0.0.0.0:{self.api_port}")
        print(f"     Input Sink active at 0.0.0.0:{self.receiver_port}")
        print( "------------------------------------------------------------------------")

    # Public functions

    def get_sinks(self) -> List[SinkDescription]:
        """Returns a copy of the list holding all sinks"""
        _sinks = []
        for sink in self.__sink_descriptions:
            _sinks.append(copy(sink))
        return _sinks

    def add_sink(self, sink: SinkDescription) -> bool:
        """Adds a sink"""
        self.__verify_sink(sink)
        self.__sink_descriptions.append(sink)
        self.__start_receiver()
        return True

    def update_sink(self, sink: SinkDescription) -> bool:
        """Updates a sink"""
        original_sink: SinkDescription = self.__get_sink_by_name(sink.name)
        existing_sink_index: int = self.__sink_descriptions.index(original_sink)
        self.__sink_descriptions[existing_sink_index] = sink
        self.__start_receiver()
        return True

    def delete_sink(self, sink_name: str) -> bool:
        """Deletes a sink by index"""
        sink: SinkDescription = self.__get_sink_by_name(sink_name)
        self.__verify_sink_unused(sink)
        self.__sink_descriptions.remove(sink)
        self.__start_receiver()
        return True

    def enable_sink(self, sink_name: str) -> bool:
        """Enables a sink by index"""
        sink: SinkDescription = self.__get_sink_by_name(sink_name)
        sink.enabled = True
        self.__start_receiver()
        return True

    def disable_sink(self, sink_name: str) -> bool:
        """Disables a sink by index"""
        sink: SinkDescription = self.__get_sink_by_name(sink_name)
        sink.enabled = False
        self.__start_receiver()
        return True

    def get_sources(self) -> List[SourceDescription]:
        """Returns a copy of the sources list"""
        return copy(self.__source_descriptions)

    def add_source(self, source: SourceDescription) -> bool:
        """Adds a source"""
        self.__verify_source(source)
        self.__source_descriptions.append(source)
        self.__start_receiver()
        return True

    def update_source(self, source: SourceDescription) -> bool:
        """Updates a source"""
        existing_source: SourceDescription = self.__get_source_by_name(source.name)
        existing_source_index: int = self.__source_descriptions.index(existing_source)
        self.__source_descriptions[existing_source_index] = source
        self.__start_receiver()
        return True

    def delete_source(self, source_name: str) -> bool:
        """Deletes a source by index"""
        source: SourceDescription = self.__get_source_by_name(source_name)
        self.__verify_source_unused(source)
        self.__source_descriptions.remove(source)
        self.__start_receiver()
        return True

    def enable_source(self, source_name: str) -> bool:
        """Enables a source by index"""
        source: SourceDescription = self.__get_source_by_name(source_name)
        source.enabled = True
        self.__start_receiver()
        return True

    def disable_source(self, source_name: str) -> bool:
        """Disables a source by index"""
        source: SourceDescription = self.__get_source_by_name(source_name)
        source.enabled = False
        self.__start_receiver()
        return True

    def get_routes(self) -> List[RouteDescription]:
        """Returns a copy of the routes list"""
        return copy(self.__route_descriptions)

    def add_route(self, route: RouteDescription) -> bool:
        """Adds a route"""
        self.__verify_route(route)
        self.__route_descriptions.append(route)
        self.__start_receiver()
        return True

    def update_route(self, route: RouteDescription) -> bool:
        """Updates a route"""
        existing_route: RouteDescription = self.__get_route_by_name(route.name)
        existing_route_index: int = self.__route_descriptions.index(existing_route)
        self.__route_descriptions[existing_route_index] = route
        self.__start_receiver()
        return True

    def delete_route(self, route_name: str) -> bool:
        """Deletes a route by index"""
        route: RouteDescription = self.__get_route_by_name(route_name)
        self.__route_descriptions.remove(route)
        self.__start_receiver()
        return True

    def enable_route(self, route_name: str) -> bool:
        """Enables a route by index"""
        route: RouteDescription = self.__get_route_by_name(route_name)
        route.enabled = True
        self.__start_receiver()
        return True

    def disable_route(self, route_name: str) -> bool:
        """Disables a route by index"""
        route: RouteDescription = self.__get_route_by_name(route_name)
        route.enabled = False
        self.__start_receiver()
        return True

    def update_source_volume(self, source_name: str, volume: float) -> bool:
        """Sets the volume for source source_id to volume"""
        source: SourceDescription = self.__get_source_by_name(source_name)
        source.set_volume(volume)
        self.__apply_volume_change()
        return True

    def update_sink_equalizer(self, sink_name: str, equalizer: Equalizer) -> bool:
        """Sets the volume for sink sink_id to volume"""
        sink: SinkDescription = self.__get_sink_by_name(sink_name)
        sink.equalizer = equalizer
        self.__start_receiver()
        return True

    def update_sink_volume(self, sink_name: str, volume: float) -> bool:
        """Sets the volume for sink sink_id to volume"""
        sink: SinkDescription = self.__get_sink_by_name(sink_name)
        sink.set_volume(volume)
        self.__apply_volume_change()
        return True

    def update_sink_delay(self, sink_name: str, delay: int) -> bool:
        """Sets the delay for sink sink_id to delay"""
        sink: SinkDescription = self.__get_sink_by_name(sink_name)
        sink.set_delay(delay)
        self.__apply_delay_change()
        return True

    def update_route_volume(self, route_name: str, volume: float) -> bool:
        """Sets the volume for route route_id to volume"""
        route: RouteDescription = self.__get_route_by_name(route_name)
        route.set_volume(volume)
        self.__apply_volume_change()
        return True

    def play_url(self, sink_name: str, url: str, volume: float) -> bool:
        """Plays a URL on the sink or all children sinks"""
        sink: SinkDescription = self.__get_sink_by_name(sink_name)
        all_child_sinks: List[SinkDescription] = self.__get_real_sinks_from_sink(sink, sink.volume)
        found: bool = False
        for sink_description in all_child_sinks:
            for sink_controller in self.__sink_objects:
                if sink_description.name == sink_controller.name:
                    found = True
                    tag: str = f"ffmpeg{self.__url_play_counter}"
                    pipe_name: str = f"{self.pipes_dir}scream-{sink_description.ip}-{tag}"
                    ffmpeg_source_info: SourceToFFMpegWriter
                    ffmpeg_source_info= SourceToFFMpegWriter(tag, pipe_name,
                                                             sink_description.ip,
                                                             sink_description.volume * volume)
                    sink_controller.sources.append(ffmpeg_source_info)
        if found:
            tag: str = f"ffmpeg{self.__url_play_counter}"
            pipe_name: str = f"{self.pipes_dir}ffmpeg{self.__url_play_counter}"
            FFMpegPlayURL(url, 1, all_child_sinks[0], pipe_name, tag, self.__receiver)
            self.__url_play_counter = self.__url_play_counter + 1
        return found

    def stop(self) -> bool:
        """Stop the receiver, this stops all ffmepg processes and all sinks."""
        self.__receiver.stop()
        self.__receiver.join()
        return True

    # Private Functions

    def __verify_sink(self, sink: SinkDescription) -> None:
        """Verifies all sink group members exist, throws exception if not"""
        for _sink in self.__sink_descriptions:
            if sink.name == _sink.name:
                raise ValueError(f"Sink name '{sink.name}' already in use")
        for member in sink.group_members:
            self.__get_sink_by_name(member)
        if sink.is_group:
            for member in sink.group_members:
                self.__get_sink_by_name(member)

    def __verify_source(self, source: SourceDescription) -> None:
        """Verifies all source group members exist, throws exception if not"""
        for _source in self.__source_descriptions:
            if source.name == _source.name:
                raise ValueError(f"Source name '{source.name}' already in use")
        if source.is_group:
            for member in source.group_members:
                self.__get_source_by_name(member)

    def __verify_route(self, route: RouteDescription) -> None:
        """Verifies route sink and source exist, throws exception if not"""
        self.__get_sink_by_name(route.sink)
        self.__get_source_by_name(route.source)

    def __verify_source_unused(self, source: SourceDescription) -> None:
        """Verifies a source is unused, throws exception if not"""
        groups: List[SourceDescription] = self.__get_source_groups_from_member(source)
        if len(groups) > 0:
            group_names: List[str] = []
            for group in groups:
                group_names.append(group.name)
            raise InUseError(f"Source:{source.name} is in use by Groups {group_names}")
        routes: List[RouteDescription] = []
        routes = self.__get_routes_by_source(source)
        for route in routes:
            if route.source == source.name:
                raise InUseError(f"Source:{source.name} is in use by Route {route.name}")

    def __verify_sink_unused(self, sink: SinkDescription) -> None:
        """Verifies a sink is unused, throws exception if not"""
        groups: List[SinkDescription] = self.__get_sink_groups_from_member(sink)
        if len(groups) > 0:
            group_names: List[str] = []
            for group in groups:
                group_names.append(group.name)
            raise InUseError(f"Sink:{sink.name} is in use by Groups {group_names}")
        routes: List[RouteDescription] = []
        routes = self.__get_routes_by_sink(sink)
        for route in routes:
            if route.sink == sink.name:
                raise InUseError(f"Sink:{sink.name} is in use by Route {route.name}")

    def __apply_volume_change(self) -> None:
        """Applies the current controller volume to the running ffmpeg instances"""
        self.__build_real_sinks_to_real_sources()
        for sink_ip, sources in self.sinks_to_sources.items():
            for _sink in self.__sink_objects:
                if _sink.sink_ip == sink_ip:
                    for source in sources:
                        _sink.update_source_volume(source)
        self.__save_yaml()

    def __apply_delay_change(self) -> None:
        """Applies the current controller delay to the running ffmpeg instances"""
        for sink in self.__sink_objects:
            for _sink in self.__sink_descriptions:
                if sink.name == _sink.name:
                    sink.update_delay(_sink.delay)
        self.__save_yaml()

    def __load_yaml(self) -> None:
        """Loads the initial config"""
        try:
            with open("config.yaml", "r", encoding="UTF-8") as f:
                config = yaml.safe_load(f)
            for sink_entry in config["sinks"]:
                equalizer: Equalizer = Equalizer(b1 = sink_entry["equalizer"]["b1"],
                                                 b2 = sink_entry["equalizer"]["b2"],
                                                 b3 = sink_entry["equalizer"]["b3"],
                                                 b4 = sink_entry["equalizer"]["b4"],
                                                 b5 = sink_entry["equalizer"]["b5"],
                                                 b6 = sink_entry["equalizer"]["b6"],
                                                 b7 = sink_entry["equalizer"]["b7"],
                                                 b8 = sink_entry["equalizer"]["b8"],
                                                 b9 = sink_entry["equalizer"]["b9"],
                                                 b10 = sink_entry["equalizer"]["b10"],
                                                 b11 = sink_entry["equalizer"]["b11"],
                                                 b12 = sink_entry["equalizer"]["b12"],
                                                 b13 = sink_entry["equalizer"]["b13"],
                                                 b14 = sink_entry["equalizer"]["b14"],
                                                 b15 = sink_entry["equalizer"]["b15"],
                                                 b16 = sink_entry["equalizer"]["b16"],
                                                 b17 = sink_entry["equalizer"]["b17"],
                                                 b18 = sink_entry["equalizer"]["b18"],
                )
                self.add_sink(SinkDescription(sink_entry["name"], sink_entry["ip"],
                                              sink_entry["port"], False,
                                              sink_entry["enabled"], [],
                                              sink_entry["volume"], sink_entry["bitdepth"],
                                              sink_entry["samplerate"], sink_entry["channels"],
                                              sink_entry["channel_layout"], sink_entry["delay"],
                                              equalizer=equalizer
                                              ))
            for source_entry in config["sources"]:
                self.add_source(SourceDescription(source_entry["name"], source_entry["ip"],
                                                  False, source_entry["enabled"],
                                                  [], source_entry["volume"]))
            for sink_group in config["groups"]["sinks"]:
                self.add_sink(SinkDescription(sink_group["name"], "",
                                              0, True,
                                              sink_group["enabled"], sink_group["sinks"],
                                              sink_group["volume"]))
            for source_group in config["groups"]["sources"]:
                self.add_source(SourceDescription(source_group["name"], "",
                                                  True, source_group["enabled"],
                                                  source_group["sources"], source_group["volume"]))
            for route_entry in config["routes"]:
                self.add_route(RouteDescription(route_entry["name"], route_entry["sink"],
                                                route_entry["source"], route_entry["enabled"],
                                                route_entry["volume"]))
            self.api_port = config["server"]["api_port"]
            self.receiver_port = config["server"]["receiver_port"]
            self.pipes_dir = config["server"]["pipes_dir"]
        except FileNotFoundError:
            logger.warning("[Controller] Configuration not found, starting with a blank config")
        except KeyError as exc:
            logger.error("[Controller] Failed to load config.yaml. Aborting load.",
                          exc_info = exc)
            sys.exit(-1)
        except IndexError as exc:
            logger.error("[Controller] Failed to load config.yaml. Aborting load.",
                         exc_info = exc)
            sys.exit(-1)

        if not os.path.exists(self.pipes_dir):
            os.mkdir(self.pipes_dir)
        self.__loaded = True

    def __save_yaml(self) -> None:
        """Saves the config to config.yaml"""
        sinks: List[dict] = []
        sink_groups: List[dict] = []
        sources: List[dict] = []
        source_groups: List[dict] = []
        routes: List[dict] = []
        serverinfo: dict = {"api_port": self.api_port, "receiver_port": self.receiver_port,
                             "logs_dir": LOGS_DIR, "pipes_dir": self.pipes_dir,
                             "console_log_level": CONSOLE_LOG_LEVEL}

        for sink in self.__sink_descriptions:
            if not sink.is_group:
                if sink.equalizer is not None:
                    equalizer: dict = { "b1": sink.equalizer.b1,
                                        "b2": sink.equalizer.b2,
                                        "b3": sink.equalizer.b3,
                                        "b4": sink.equalizer.b4,
                                        "b5": sink.equalizer.b5,
                                        "b6": sink.equalizer.b6,
                                        "b7": sink.equalizer.b7,
                                        "b8": sink.equalizer.b8,
                                        "b9": sink.equalizer.b9,
                                        "b10": sink.equalizer.b10,
                                        "b11": sink.equalizer.b11,
                                        "b12": sink.equalizer.b12,
                                        "b13": sink.equalizer.b13,
                                        "b14": sink.equalizer.b14,
                                        "b15": sink.equalizer.b15,
                                        "b16": sink.equalizer.b16,
                                        "b17": sink.equalizer.b17,
                                        "b18": sink.equalizer.b18}
                else:
                    equalizer: dict = { "b1": 1,
                                        "b2": 1,
                                        "b3": 1,
                                        "b4": 1,
                                        "b5": 1,
                                        "b6": 1,
                                        "b7": 1,
                                        "b8": 1,
                                        "b9": 1,
                                        "b10": 1,
                                        "b11": 1,
                                        "b12": 1,
                                        "b13": 1,
                                        "b14": 1,
                                        "b15": 1,
                                        "b16": 1,
                                        "b17": 1,
                                        "b18": 1}
                _newsink = {"name": sink.name, "ip": sink.ip,
                            "port": sink.port, "enabled": sink.enabled,
                            "volume": sink.volume, "bitdepth": sink.bit_depth,
                            "samplerate": sink.sample_rate, "channels": sink.channels,
                            "channel_layout": sink.channel_layout, "delay": sink.delay,
                            "equalizer": equalizer}
                sinks.append(_newsink)
            else:
                _newsink = {"name": sink.name, "sinks": sink.group_members,
                            "enabled": sink.enabled, "volume": sink.volume}
                sink_groups.append(_newsink)
        for source in self.__source_descriptions:
            if not source.is_group:
                _newsource = {"name": source.name, "ip": source.ip,
                              "enabled": source.enabled, "volume": source.volume}
                sources.append(_newsource)
            else:
                _newsource = {"name": source.name, "sources": source.group_members,
                              "enabled": source.enabled, "volume": source.volume}
                source_groups.append(_newsource)
        for route in self.__route_descriptions:
            _newroute = {"name": route.name, "source": route.source,
                         "sink": route.sink, "enabled": route.enabled,
                         "volume": route.volume}
            routes.append(_newroute)
        groups = {"sinks": sink_groups, "sources": source_groups}
        save_data = {"sinks": sinks, "sources": sources,
                     "routes": routes, "groups": groups, "server": serverinfo}
        with open('config.yaml', 'w', encoding="UTF-8") as yaml_file:
            yaml.dump(save_data, yaml_file)

    def __start_receiver(self) -> None:
        """Start or restart the receiver"""
        if not self.__loaded:
            return
        while self.__starting:
            time.sleep(.1)
        self.__starting = True
        self.__save_yaml()
        self.__build_real_sinks_to_real_sources()
        if self.__receiverset:
            logger.debug("[Controller] Closing receiver!")
            self.__receiver.stop()
            self.__receiver.join()
            logger.info("[Controller] Receiver closed!")
        self.__receiverset = True
        self.__receiver = Receiver(self.receiver_port)
        self.__sink_objects = []
        for sink_ip, _ in self.sinks_to_sources.items():
            if sink_ip != "":
                sink_info: SinkDescription
                for sink_description in self.__sink_descriptions:
                    if sink_description.ip == sink_ip:
                        sink_info = sink_description
                        sink = audio.sink_controller.SinkController(sink_info,
                                                                    self.sinks_to_sources[sink_ip],
                                                                    self.__api_webstream)
                        self.__receiver.register_sink(sink)
                        self.__sink_objects.append(sink)
                        break
        self.__starting = False
    # Sink Finders
    def __get_sink_by_name(self, name: str) -> SinkDescription:
        """Returns a sink by name"""
        for sink in self.__sink_descriptions:
            if sink.name == name:
                return sink
        raise NameError(f"Sink not found by name {name}")

    def __get_real_sinks_from_sink(self, sink: SinkDescription,
                                   volume_multiplier: float) -> List[SinkDescription]:
        """Recursively work through sink groups to get all real sinks
           Volume levels for the returned sinks will be adjusted based off parent sink group levels
        """
        if sink.is_group:
            sinks: List[SinkDescription] = []
            for entry in sink.group_members:
                sink_entry = self.__get_sink_by_name(entry)
                if not sink_entry.enabled:
                    return []
                if sink_entry.is_group:
                    sinks.extend(self.__get_real_sinks_from_sink(sink_entry,
                                                                 volume_multiplier * sink.volume))
                else:
                    sink_entry_copy: SinkDescription = copy(sink_entry)
                    sink_entry_copy.volume = sink_entry_copy.volume * volume_multiplier
                    sinks.append(sink_entry_copy)
            return sinks
        else:
            return [sink]

    def __get_real_sinks_and_groups_from_sink(self, sink: SinkDescription) -> List[SinkDescription]:
        """Recursively work through sink groups to get all real sinks and sink groups"""
        if sink.is_group:
            sinks: List[SinkDescription] = []
            for entry in sink.group_members:
                sink_entry = self.__get_sink_by_name(entry)
                sink_entry_copy: SinkDescription = copy(sink_entry)
                sinks.append(sink_entry_copy)
                if sink_entry.is_group:
                    sinks.extend(self.__get_real_sinks_and_groups_from_sink(sink_entry))
            return sinks
        else:
            return [sink]

    def __get_sink_groups_from_member(self, sink: SinkDescription) -> List[SinkDescription]:
        sink_groups: List[SinkDescription] = []
        for _sink in self.__sink_descriptions:
            if sink.name in _sink.group_members:
                sink_groups.append(_sink)
                sink_groups.extend(self.__get_sink_groups_from_member(_sink))
        return sink_groups

    # Source Finders
    def __get_source_by_name(self, name: str) -> SourceDescription:
        """Get source by name"""
        for source in self.__source_descriptions:
            if source.name == name:
                return source
        raise NameError(f"No source found by name {name}")

    def __get_real_sources_from_source(self, source: SourceDescription,
                                       volume_multiplier: float) -> List[SourceDescription]:
        """Recursively work through source groups to get all real sources
           Volume levels for the returned sources will be adjusted for parent source group levels
        """
        if not source.enabled:
            return []
        if source.is_group:
            _sources: List[SourceDescription] = []
            for entry in source.group_members:
                source_entry = self.__get_source_by_name(entry)
                if source_entry.enabled:
                    if source_entry.is_group:
                        new_volume: float = volume_multiplier * source_entry.volume
                        group_sources: List[SourceDescription]
                        group_sources = self.__get_real_sources_from_source(source_entry,
                                                                            new_volume)
                        _sources.extend(group_sources)
                    else:
                        source_entry_copy = copy(source_entry)
                        source_entry_copy.volume = source_entry_copy.volume * volume_multiplier
                        _sources.append(source_entry_copy)
            return _sources
        else:
            source_entry_copy: SourceDescription = copy(source)
            source_entry_copy.volume = source_entry_copy.volume * volume_multiplier
            return [source_entry_copy]

    def __get_real_sources_by_sink(self, sink: SinkDescription) -> List[SourceDescription]:
        """Returns real (non-group) sources for a given sink"""
        _sources: List[SourceDescription] = []
        if sink.enabled:
            _routes = self.__get_routes_by_enabled_sink(sink)
            for route in _routes:
                if route.enabled:
                    sources = self.__get_real_sources_by_route(route)
                    _sources.extend(sources)
        return unique(_sources)

    def __get_real_sources_by_route(self, route: RouteDescription) -> List[SourceDescription]:
        """Returns real (non-group) sources for a given route
           Volume levels for the returned sources will be adjusted based off the route levels
        """
        source = self.__get_source_by_name(route.source)
        if source.enabled and route.enabled:
            new_volume: float = route.volume * source.volume
            sources: List[SourceDescription] = self.__get_real_sources_from_source(source,
                                                                                   new_volume)
            return sources
        return []

    def __get_sources_and_groups_from_source(self,
                                             source: SourceDescription) -> List[SourceDescription]:
        """Recursively work through source groups to get all real sources and source groups
        """
        if source.is_group:
            sources: List[SourceDescription] = []
            for entry in source.group_members:
                source_entry = self.__get_source_by_name(entry)
                source_entry_copy: SourceDescription = copy(source_entry)
                sources.append(source_entry_copy)
                if source_entry.is_group:
                    sources.extend(self.__get_sources_and_groups_from_source(source_entry))
            return sources
        else:
            return [source]

    def __get_source_groups_from_member(self, source: SourceDescription) -> List[SourceDescription]:
        source_groups: List[SourceDescription] = []
        for _source in self.__source_descriptions:
            if source.name in _source.group_members:
                source_groups.append(_source)
                source_groups.extend(self.__get_source_groups_from_member(_source))
        return source_groups

    # Route Finders
    def __get_route_by_name(self, name: str) -> RouteDescription:
        """Get route by name"""
        for route in self.__route_descriptions:
            if route.name == name:
                return route
        raise NameError(f"No route found by name {name}")

    def __get_routes_by_enabled_sink(self, sink: SinkDescription) -> List[RouteDescription]:
        """Get all routes that use this sink
           Volume levels for the returned routes will be adjusted based off the sink levels
        """
        _routes: List[RouteDescription] = []
        for route in self.__route_descriptions:
            _parentsink = self.__get_sink_by_name(route.sink)
            if _parentsink.enabled and route.enabled:
                _sinks = self.__get_real_sinks_from_sink(_parentsink, _parentsink.volume)
                for _sink in _sinks:
                    if _sink.enabled and _sink.ip == sink.ip:
                        route_copy = copy(route)
                        route_copy.volume = route_copy.volume * _sink.volume
                        _routes.append(route_copy)
        return unique(_routes)

    def __get_routes_by_sink(self, sink: SinkDescription) -> List[RouteDescription]:
        """Get all routes that use this sink
           Volume levels for the returned routes will be adjusted based off the sink levels
        """
        _routes: List[RouteDescription] = []
        sinks: List[SinkDescription] = self.__get_real_sinks_and_groups_from_sink(sink)
        sinks.append(sink)
        for route in self.__route_descriptions:
            for _sink in sinks:
                if _sink.name == route.sink:
                    _routes.append(copy(route))
        return unique(_routes)

    def __get_routes_by_source(self, source: SourceDescription) -> List[RouteDescription]:
        """Get all routes that use this source
           Volume levels for the returned routes will be adjusted based off the source levels
        """
        _routes: List[RouteDescription] = []
        sources: List[SourceDescription] = self.__get_sources_and_groups_from_source(source)
        sources.append(source)
        for route in self.__route_descriptions:
            for _source in sources:
                if _source.ip == source.ip:
                    _routes.append(copy(route))
        return unique(_routes)

    # Sink IP -> Source maps
    def __build_real_sinks_to_real_sources(self):
        """Build sink to source cache {"sink_ip": [source1, source_2, ...]}"""
        self.sinks_to_sources = {}
        for sink in self.__sink_descriptions:
            if sink.enabled:
                self.sinks_to_sources[sink.ip] = self.__get_real_sources_by_sink(sink)
