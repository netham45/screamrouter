import yaml
from copy import copy

from typing import List, Optional

import mixer.receiver
import mixer.sink_controller

from configuration_controller_types import SinkDescription, SourceDescription, RouteDescription, InUseException

from api.api_webstream import API_Webstream

# Helper functions
#def unique[T](list: List[T]) -> List[T]:  # One day
def unique(list: List) -> List:
    """Returns a list with duplicates filtered out"""
    _list = []
    for element in list:
        if not element in _list: 
            _list.append(element)
    return _list


class ConfigurationController:
    """The controller handles tracking configuration and loading the main receiver/sinks based off of it"""
    def __init__(self, websocket: Optional[API_Webstream]):
        """Initialize an empty controller"""
        self.__sink_objects: List[mixer.sink_controller.SinkController] = []
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
        self.__loaded: bool = False
        """Holds rather the config is loaded"""
        self.__api_websocket: Optional[API_Webstream] = websocket
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
        self.__verify_sink(sink)
        self.__sink_descriptions.append(sink)
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

    def update_sink_volume(self, sink_name: str, volume: float) -> bool:
        """Sets the volume for sink sink_id to volume"""
        sink: SinkDescription = self.__get_sink_by_name(sink_name)
        sink.set_volume(volume)
        self.__apply_volume_change()
        return True

    def update_route_volume(self, route_name: str, volume: float) -> bool:
        """Sets the volume for route route_id to volume"""
        route: RouteDescription = self.__get_route_by_name(route_name)
        route.set_volume(volume)
        self.__apply_volume_change()
        return True
    
    def stop(self) -> bool:
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
            raise InUseException(f"Source {source.name} is in use by Groups {group_names}")
        routes: List[RouteDescription] = []
        try:
             routes = self.__get_routes_by_source(source)
        except:  # Failed to find source, it must be unused.
            print(f"Failed to get routes for source {source.name}")
            return
        for route in routes:
            if route.source == source.name:
                raise InUseException(f"Source {source.name} is in use by Route {route.name}")

    def __verify_sink_unused(self, sink: SinkDescription) -> None:
        """Verifies a sink is unused, throws exception if not"""
        groups: List[SinkDescription] = self.__get_sink_groups_from_member(sink)
        if len(groups) > 0:
            group_names: List[str] = []
            for group in groups:
                group_names.append(group.name)
            raise InUseException(f"Sink {sink.name} is in use by Groups {group_names}")
        routes: List[RouteDescription] = []
        try:
             routes = self.__get_routes_by_sink(sink)
        except:  # Failed to find sink, it must be unused.
            print(f"Failed to get routes for sink {sink.name}")
            return
        for route in routes:
            if route.sink == sink.name:
                raise InUseException(f"Sink {sink.name} is in use by Route {route.name}")
                                          
    def __verify_sink_index(self, sink_index: int) -> None:
        """Verifies sink index is >= 0 and < len(self.__sink_descriptions), throws exception if not"""
        if sink_index < 0 or sink_index >= len(self.__sink_descriptions):
            raise IndexError(f"Invalid sink index {sink_index}, max index is {len(self.__sink_descriptions)}")
        
    def __verify_source_index(self, source_index: int) -> None:
        """Verifies source index is >= 0 and < len(self.__source_descriptions), throws exception if not"""
        if source_index < 0 or source_index >= len(self.__source_descriptions):
            raise IndexError(f"Invalid source index {source_index}, max index is {len(self.__source_descriptions)}")

    def __verify_route_index(self, route_index: int) -> None:
        """Verifies route index is >= 0 and < len(self.__route_descriptions), throws exception if not"""
        if route_index < 0 or route_index >= len(self.__route_descriptions):
            raise IndexError(f"Invalid route index {route_index}, max index is {len(self.__route_descriptions)}")

    def __apply_volume_change(self) -> None:
        """Applies the current controller volume to the running ffmpeg instances"""
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
            self.add_sink(SinkDescription(sinkEntry["name"], sinkEntry["ip"], sinkEntry["port"], False, sinkEntry["enabled"], [], sinkEntry["volume"]))
        for sourceEntry in config["sources"]:
            self.add_source(SourceDescription(sourceEntry["name"], sourceEntry["ip"], False, sourceEntry["enabled"], [], sourceEntry["volume"]))
        for sinkGroup in config["groups"]["sinks"]:
            self.add_sink(SinkDescription(sinkGroup["name"], "", 0, True, sinkGroup["enabled"], sinkGroup["sinks"], sinkGroup["volume"]))
        for sourceGroup in config["groups"]["sources"]:
            self.add_source(SourceDescription(sourceGroup["name"], "", True, sourceGroup["enabled"], sourceGroup["sources"], sourceGroup["volume"]))
        for routeEntry in config["routes"]:
            self.add_route(RouteDescription(routeEntry["name"], routeEntry["sink"], routeEntry["source"], routeEntry["enabled"], routeEntry["volume"]))

        self.__loaded = True

    def __save_yaml(self) -> None:
        """Saves the config to config.yaml"""
        sinks: List[dict] = []
        sink_groups: List[dict] = []
        sources: List[dict] = []
        source_groups: List[dict] = []
        routes: List[dict] = []
        for sink in self.__sink_descriptions:
            if not sink.is_group:
                _newsink = {"name": sink.name, "ip": sink.ip, "port": sink.port, "enabled": sink.enabled, "volume": sink.volume}
                sinks.append(_newsink)
            else:
                _newsink = {"name": sink.name, "sinks": sink.group_members, "enabled": sink.enabled, "volume": sink.volume}
                sink_groups.append(_newsink)
        for source in self.__source_descriptions:
            if not source.is_group:
                _newsource = {"name": source.name, "ip": source.ip, "enabled": source.enabled, "volume": source.volume}
                sources.append(_newsource)
            else:
                _newsource = {"name": source.name, "sources": source.group_members, "enabled": source.enabled, "volume": source.volume}
                source_groups.append(_newsource)        
        for route in self.__route_descriptions:
            _newroute = {"name": route.name, "source": route.source, "sink": route.sink, "enabled": route.enabled, "volume": route.volume}
            routes.append(_newroute)
        groups = {"sinks": sink_groups, "sources": source_groups}
        save_data = {"sinks": sinks, "sources": sources, "routes": routes, "groups": groups}
        with open('config.yaml', 'w') as yaml_file:
            yaml.dump(save_data, yaml_file)

    def __start_receiver(self) -> None:
        """Start or restart the receiver"""
        if not self.__loaded:
            return
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
                sink = mixer.sink_controller.SinkController(sink_ip, self.__sinks_to_sources[sink_ip], self.__api_websocket)
                self.__receiver.register_sink(sink)
                self.__sink_objects.append(sink)

    # Sink Finders
    def __get_sink_by_name(self, name: str) -> SinkDescription:
        """Returns a sink by name"""
        for sink in self.__sink_descriptions:
            if sink.name == name:
                return sink
        raise NameError(f"Sink not found by name {name}")

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
        
    def __get_real_sinks_and_groups_from_sink(self, sink: SinkDescription) -> List[SinkDescription]:
        """Recursively work through sink groups to get all real sinks and sink groups, for name comparisons
        """
        if sink.is_group:
            sinks: List[SinkDescription] = []
            for entry in sink.group_members:
                sinkEntry = self.__get_sink_by_name(entry)
                sink_entry_copy: SinkDescription = copy(sinkEntry)
                sinks.append(sink_entry_copy)
                if sinkEntry.is_group:
                    sinks.extend(self.__get_real_sinks_and_groups_from_sink(sinkEntry))
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
            sources: List[SourceDescription] = self.__get_real_sources_from_source(source, route.volume * source.volume)
            return sources
        return []

    def __get_real_sources_and_groups_from_source(self, source: SourceDescription) -> List[SourceDescription]:
        """Recursively work through source groups to get all real sources and source groups, for name comparisons
        """
        if source.is_group:
            sources: List[SourceDescription] = []
            for entry in source.group_members:
                sourceEntry = self.__get_source_by_name(entry)
                source_entry_copy: SourceDescription = copy(sourceEntry)
                sources.append(source_entry_copy)
                if sourceEntry.is_group:
                    sources.extend(self.__get_real_sources_and_groups_from_source(sourceEntry))
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
    
    def __get_routes_by_enabled_source(self, source: SourceDescription) -> List[RouteDescription]:
        """Get all routes that use this source
           Volume levels for the returned routes will be adjusted based off the source levels
        """
        _routes: List[RouteDescription] = []
        for route in self.__route_descriptions:
            _parentsource = self.__get_source_by_name(route.source)
            if _parentsource.enabled and route.enabled:
                _sources = self.__get_real_sources_from_source(_parentsource, _parentsource.volume)
                for _source in _sources:
                    if _source.enabled and _source.ip == source.ip:
                        route_copy = copy(route)
                        route_copy.volume = route_copy.volume * _source.volume
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
        sources: List[SourceDescription] = self.__get_real_sources_and_groups_from_source(source)
        sources.append(source)
        for route in self.__route_descriptions:        
                for _source in sources:
                    if _source.ip == source.ip:
                        _routes.append(copy(route))
        return unique(_routes)

    # Sink IP -> Source maps
    def __build_real_sinks_to_real_sources(self):
        """Build sink to source cache {"sink_ip": ["source1", "source_2", ...]}"""
        self.__sinks_to_sources = {}
        for sink in self.__sink_descriptions:
            if sink.enabled:
                self.__sinks_to_sources[sink.ip] = self.__get_real_sources_by_sink(sink)