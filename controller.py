import yaml
from copy import copy

from typing import List

import mixer.receiver
import mixer.sink

from controller_types import SinkDescription
from controller_types import SourceDescription
from controller_types import RouteDescription

# Helper functions
#def unique[T](list: List[T]) -> List[T]:  # One day
def unique(list: List) -> List:
    """Returns a list with duplicates filtered out"""
    _list = []
    for element in list:
        if not element in _list: 
            _list.append(element)
    return _list

class Controller:
    """The controller handles tracking configuration and loading the main receiver/sinks based off of it"""
    def __init__(self):
        """Initialize an empty controller"""
        self.__sink_objects: List[mixer.sink.Sink] = []
        """List of Sink objects the receiver is using"""
        self.__sink_descriptions: List[SinkDescription] = []
        """List of Sinks the controller knows of"""
        self.__source_descriptions:  List[SourceDescription] = []
        """List of Sources the controller knows of"""
        self.__route_descriptions: List[RouteDescription] = []
        """List of Routes the controller knows of"""
        self.__sinks_to_sources = {}
        """Dict mapping all sink IPs to the source descriptions playing to them"""
        self.__receiver: mixer.receiver.Receiver
        """Main receiver, handles receiving data from sources"""
        self.__receiverset: bool = False
        """Rather the recevier has been set"""
        self.__load_yaml()
        self.__start_receiver()

    # Public functions

    def get_sinks(self) -> List[SinkDescription]:
        """Returns a copy of the list holding all sinks"""
        _sinks = []
        for sink in self.__sink_descriptions:
            _sinks.append(copy(sink))
        return _sinks

    def add_sink(self, sink: SinkDescription) -> bool:
        """Adds a sink"""
        for _sink in self.__sink_descriptions:
            if sink.name == _sink.name:
                return False
        if sink.name.strip() == False:
            return False
        self.__sink_descriptions.append(sink)
        self.__start_receiver()
        return True

    def delete_sink(self, sink_id: int) -> bool:
        """Deletes a sink by index"""
        for route in self.__route_descriptions:
            if self.__sink_descriptions[sink_id].name == route.sink:
                return False  # Can't remove a sink in use by a route
        self.__sink_descriptions.pop(sink_id)
        self.__start_receiver()
        return True
        return False

    def enable_sink(self, sink_id: int) -> bool:
        """Enables a sink by index"""
        if 0 <= sink_id < len(self.__sink_descriptions):
            self.__sink_descriptions[sink_id].enabled = True
            self.__start_receiver()
            return True
        return False

    def disable_sink(self, sink_id: int) -> bool:
        """Disables a sink by index"""
        if 0 <= sink_id < len(self.__sink_descriptions):
            self.__sink_descriptions[sink_id].enabled = False
            self.__start_receiver()
            return True
        return False

    def get_sources(self) -> List[SourceDescription]:
        """Returns a copy of the sources list"""
        return copy(self.__source_descriptions)

    def add_source(self, source: SourceDescription) -> bool:
        """Adds a source"""
        for _source in self.__source_descriptions:
            if source.name == _source.name:
                return False
        if source.name.strip() == False:
            return False
        self.__source_descriptions.append(source)
        self.__start_receiver()
        return True

    def delete_source(self, source_id: int) -> bool:
        """Deletes a source by index"""
        for route in self.__route_descriptions:
            if self.__source_descriptions[source_id].name == route.source:
                return False  # Can't remove a source in use by a route
        self.__source_descriptions.pop(source_id)
        self.__start_receiver()
        return True

    def enable_source(self, source_id: int) -> bool:
        """Enables a source by index"""
        if 0 <= source_id < len(self.__source_descriptions):
            self.__source_descriptions[source_id].enabled = True
            self.__start_receiver()
            return True
        return False

    def disable_source(self, source_id: int) -> bool:
        """Disables a source by index"""
        if 0 <= source_id < len(self.__source_descriptions):
            self.__source_descriptions[source_id].enabled = False
            self.__start_receiver()
            return True
        return False

    def get_routes(self) -> List[RouteDescription]:
        """Returns a copy of the routes list"""
        return copy(self.__route_descriptions)

    def add_route(self, route: RouteDescription) -> bool:
        """Adds a route"""
        sinkFound: bool = False
        sourceFound: bool = False

        for sink in self.__sink_descriptions:
            if sink.name == route.sink:
                sinkFound = True
                break

        for source in self.__source_descriptions:
            if source.name == route.source:
                sourceFound = True
                break
        
        if not sinkFound or not sourceFound:
            return False

        self.__route_descriptions.append(route)
        self.__start_receiver()
        return True

    def delete_route(self, route_id: int) -> bool:
        """Deletes a route by index"""
        self.__route_descriptions.pop(route_id)
        self.__start_receiver()
        return True

    def enable_route(self, route_id: int) -> bool:
        """Enables a route by index"""
        if 0 <= route_id < len(self.__route_descriptions):
            self.__route_descriptions[route_id].enabled = True
            self.__start_receiver()
            return True
        return False

    def disable_route(self, route_id: int) -> bool:
        """Disables a route by index"""
        if 0 <= route_id < len(self.__route_descriptions):
            self.__route_descriptions[route_id].enabled = False
            self.__start_receiver()
            return True
        return False
    
    def update_source_volume(self, source_idx: int, volume: float) -> None:
        """Sets the volume for source source_idx to volume"""
        for idx, source in enumerate(self.__source_descriptions):
            if idx == source_idx:
                source.volume = volume
        self.__apply_volume_change()

    def update_sink_volume(self, sink_idx: int, volume: float) -> None:
        """Sets the volume for sink sink_idx to volume"""
        for idx, sink in enumerate(self.__sink_descriptions):
            if idx == sink_idx:
                sink.volume = volume
        self.__apply_volume_change()

    def update_route_volume(self, route_idx: int, volume: float) -> None:
        """Sets the volume for route route_idx to volume"""
        for idx, route in enumerate(self.__route_descriptions):
            if idx == route_idx:
                route.volume = volume
        self.__apply_volume_change()

    # Private Functions

    def __apply_volume_change(self) -> None:
        self.__build_real_sinks_to_real_sources()
        for sink_ip, sources in self.__sinks_to_sources.items():
            for _sink in self.__sink_objects:
                if _sink._sink_ip == sink_ip:
                    for source in sources:
                        _sink.update_source_volume(source)
        self.__save_yaml()

    def __load_yaml(self) -> None:
        """Loads the initial config"""
        with open("config.yaml", "r") as f:
            config = yaml.safe_load(f)
        for sinkEntry in config["sinks"]:
            self.__sink_descriptions.append(SinkDescription(sinkEntry["name"], sinkEntry["ip"], sinkEntry["port"], False, sinkEntry["enabled"], [], sinkEntry["volume"]))
        for sourceEntry in config["sources"]:
            self.__source_descriptions.append(SourceDescription(sourceEntry["name"], sourceEntry["ip"], False, sourceEntry["enabled"], [], sourceEntry["volume"]))
        for sinkGroup in config["groups"]["sinks"]:
            self.__sink_descriptions.append(SinkDescription(sinkGroup["name"], "", 0, True, sinkGroup["enabled"], sinkGroup["sinks"], sinkGroup["volume"]))
        for sourceGroup in config["groups"]["sources"]:
            self.__source_descriptions.append(SourceDescription(sourceGroup["name"], "", True, sourceGroup["enabled"], sourceGroup["sources"], sourceGroup["volume"]))
        for routeEntry in config["routes"]:
            self.__route_descriptions.append(RouteDescription(routeEntry["name"], routeEntry["sink"], routeEntry["source"], routeEntry["enabled"], routeEntry["volume"]))

    def __save_yaml(self) -> None:
        """Saves the config to config.yaml"""
        sinks = []
        for sink in self.__sink_descriptions:
            if not sink.is_group:
                _newsink = {"name": sink.name, "ip": sink.ip, "port": sink.port, "enabled": sink.enabled, "volume": sink.volume}
                sinks.append(_newsink)
        sources = []
        for source in self.__source_descriptions:
            if not source.is_group:
                _newsource = {"name": source.name, "ip": source.ip, "enabled": source.enabled, "volume": source.volume}
                sources.append(_newsource)
        routes = []
        for route in self.__route_descriptions:
            _newroute = {"name": route.name, "source": route.source, "sink": route.sink, "enabled": route.enabled, "volume": route.volume}
            routes.append(_newroute)
        sink_groups = []
        for sink in self.__sink_descriptions:
            if sink.is_group:
                _newsink = {"name": sink.name, "sinks": sink.group_members, "enabled": sink.enabled, "volume": sink.volume}
                sink_groups.append(_newsink)
        source_groups = []
        for source in self.__source_descriptions:
            if source.is_group:
                _newsource = {"name": source.name, "sources": source.group_members, "enabled": source.enabled, "volume": source.volume}
                source_groups.append(_newsource)
        groups = {"sinks": sink_groups, "sources": source_groups}
        save_data = {"sinks": sinks, "sources": sources, "routes": routes, "groups": groups}
        with open('config.yaml', 'w') as yaml_file:
            yaml.dump(save_data, yaml_file)

    def __start_receiver(self) -> None:
        """Start or restart the receiver"""
        self.__save_yaml()
        self.__build_real_sinks_to_real_sources()
        if self.__receiverset:
            print("[Controller] Closing receiver!")
            self.__receiver.stop()
            self.__receiver.join()
            print("[Controller] Receiver closed!")
        self.__receiverset = True
        self.__receiver = mixer.receiver.Receiver()
        self.__sink_objects = []
        for sink_ip in self.__sinks_to_sources.keys():
            if sink_ip != "":
                sink = mixer.sink.Sink(sink_ip, self.__sinks_to_sources[sink_ip])
                self.__receiver.register_sink(sink)
                self.__sink_objects.append(sink)

    # Sink Finders
    def __get_sink_by_name(self, name: str) -> SinkDescription:
        """Returns a sink by name"""
        for sink in self.__sink_descriptions:
            if sink.name == name:
                return sink
        raise Exception(f"Sink not found by name {name}")

    def __get_real_sinks_from_sink(self, sink: SinkDescription, volume_multiplier: float) -> List[SinkDescription]:
        """Recursively work through sink groups to get all real sinks
           Volume levels for the returned sinks will be adjusted based off parent sink group levels
        """
        if sink.is_group:
            sinks: List[SinkDescription] = []
            for entry in sink.group_members:
                sinkEntry = self.__get_sink_by_name(entry)
                if not sinkEntry.enabled:
                    return []
                if sinkEntry.is_group:
                    sinks.extend(self.__get_real_sinks_from_sink(sinkEntry, volume_multiplier * sink.volume))
                else:
                    sink_entry_copy: SinkDescription = copy(sinkEntry)
                    sink_entry_copy.volume = sink_entry_copy.volume * volume_multiplier
                    sinks.append(sink_entry_copy)
            return sinks
        else:
            return [sink]

    # Source Finders
    def __get_source_by_name(self, name: str) -> SourceDescription:
        """Get source by name"""
        for source in self.__source_descriptions:
            if source.name == name:
                return source
        raise Exception(f"No source found by name {name}")

    def __get_real_sources_from_source(self, source: SourceDescription, volume_multiplier: float) -> List[SourceDescription]:
        """Recursively work through source groups to get all real sources
           Volume levels for the returned sources will be adjusted based off parent source group levels
        """
        if not source.enabled:
            return []
        if source.is_group:
                _sources: List[SourceDescription] = []
                for entry in source.group_members:
                    source_entry = self.__get_source_by_name(entry)
                    if source_entry.enabled:
                        if source_entry.is_group:
                            _sources.extend(self.__get_real_sources_from_source(source_entry, volume_multiplier * source_entry.volume))
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
            _routes = self.__get_routes_by_sink(sink)
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
            print(f"Processing route {route.name}, source {route.source}")
            sources: List[SourceDescription] = self.__get_real_sources_from_source(source, route.volume * source.volume)
            print(sources)
            return sources
        return []

    # Route Finders
    def __get_routes_by_sink(self, sink: SinkDescription) -> List[RouteDescription]:
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

    # Sink IP -> Source maps
    def __build_real_sinks_to_real_sources(self):
        """Build sink to source cache {"sink_ip": ["source1", "source_2", ...]}"""
        self.__sinks_to_sources = {}
        for sink in self.__sink_descriptions:
            if sink.enabled:
                self.__sinks_to_sources[sink.ip] = self.__get_real_sources_by_sink(sink)