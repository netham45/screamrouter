import yaml
import socket
import time
import mixer
from copy import copy
from typing import List, Type, Annotated
from pydantic import BaseModel, Field

LOCALPORT=16401


"""
Class Sink

name: A string name for the sink.
ip: A string IP address for the sink.
port: An integer port number for the sink.
is_group: A boolean indicating whether this sink is a group or not.
group_members: A list of Sink objects that are members of this sink if it is a group, otherwise an empty list.
"""
class Sink(BaseModel):
    name: str
    ip: str
    port: int
    is_group: bool
    enabled: bool
    group_members: List[str]
    def __init__(self, name: str, ip: str, port: int, is_group: bool, enabled: bool, group_members: List[str] = []):
        super().__init__(name = name, ip = ip, port = port, is_group = is_group, enabled = enabled, group_members = group_members)

"""
Class Source

name: A string name for the sink.
ip: A string IP address for the sink.
is_group: A boolean indicating whether this sink is a group or not.
group
"""

class Source(BaseModel):
    name: str
    ip: str
    is_group: bool
    enabled: bool
    group_members: List[str]
    def __init__(self, name: str, ip: str, is_group: bool, enabled: bool, group_members: List[str] = []):
        super().__init__(name = name, ip = ip, is_group = is_group, enabled = enabled, group_members = group_members)

"""
Class Route

name: A string name for the route.
sink: A Sink object that represents the sink that this route is connected to.
source: A Source object that represents the source that this route is connected to.
"""

class Route(BaseModel):
    name: str
    sink: str
    source: str
    enabled: bool
    def __init__(self, name: str, sink: Sink, source: Source, enabled: bool):
        super().__init__(name = name, sink = sink, source = source, enabled = enabled)

# Helper functions
#def unique[T](list: List[T]) -> List[T]:  # One day
def unique(list: []) -> []:
    _list = []
    for element in list:
        if not element in _list:
            _list.append(element)
    return _list

class Controller:
    def __init__(self):
        self.__sinks: List[Sink] = []
        self.__sources:  List[Source] = []
        self.__routes: List[Route] = []
        self.__sources_to_sinks = {}
        self.__sinks_to_sources = {}
        self.__receiver: mixer.Receiver = None
        self.__receiverset: bool = False
        self.__load_yaml()
        self.__start_receiver()

    # Public functions

    def get_sinks(self) -> List[Sink]:
        _sinks = []
        for sink in self.__sinks:
            _sinks.append(copy(sink))
        return _sinks

    def add_sink(self, sink: Sink) -> bool:
        for _sink in self.__sinks:
            if sink.name == _sink.name:
                return False
        if sink.name.strip() == False:
            return False
        self.__sinks.append(Sink(sink))
        self.__start_receiver()
        return True

    def delete_sink(self, sink_id: int) -> bool:
        if self.__sinks[sink_id].is_group:
            for route in self.__routes:
                if self.__sinks[sink_id].name == route.sink:
                    return False  # Can't remove a sink in use by a route
            self.__sinks.pop(sink_id)
            self.__start_receiver()
            return True
        return False

    def enable_sink(self, sink_id: int) -> bool:
        if 0 <= sink_id < len(self.__sinks):
            self.__sinks[sink_id].enabled = True
            self.__start_receiver()
            return True
        return False

    def disable_sink(self, sink_id: int) -> bool:
        if 0 <= sink_id < len(self.__sinks):
            self.__sinks[sink_id].enabled = False
            self.__start_receiver()
            return True
        return False

    def get_sources(self) -> List[Sink]:
        return copy(self.__sources)

    def add_source(self, source: Source) -> bool:
        for _source in self.__sources:
            if source.name == _source.name:
                return False
        if source.name.strip() == False:
            return False
        self.__sources.append(source)
        self.__start_receiver()
        return True

    def delete_source(self, source_id: int) -> bool:
        if self.__sources[source_id].is_group:
            for route in self.__routes:
                if self.__sources[source_id].name == route.source:
                    return False  # Can't remove a source in use by a route
            self.__sources.pop(source_id)
            self.__start_receiver()
            return True
        return False

    def enable_source(self, source_id: int) -> bool:
        if 0 <= source_id < len(self.__sources):
            self.__sources[source_id].enabled = True
            self.__start_receiver()
            return True
        return False

    def disable_source(self, source_id: int) -> bool:
        if 0 <= source_id < len(self.__sources):
            self.__sources[source_id].enabled = False
            self.__start_receiver()
            return True
        return False

    def get_routes(self) -> List[Route]:
        return copy(self.__routes)

    def add_route(self, route: Route) -> bool:
        self.__routes.append(route)
        self.__start_receiver()
        return True

    def delete_route(self, route_id: int) -> bool:
        self.__routes.pop(route_id)
        self.__start_receiver()
        return True

    def enable_route(self, route_id: int) -> bool:
        if 0 <= route_id < len(self.__routes):
            self.__routes[route_id].enabled = True
            self.__start_receiver()
            return True
        return False

    def disable_route(self, route_id: int) -> bool:
        if 0 <= route_id < len(self.__routes):
            self.__routes[route_id].enabled = False
            self.__start_receiver()
            return True
        return False

    # Private Functions

    def __load_yaml(self) -> None:
        """Loads the initial config"""
        with open("config.yaml", "r") as f:
            config = yaml.safe_load(f)
        for sinkEntry in config["sinks"]:
            self.__sinks.append(Sink(sinkEntry["name"], sinkEntry["ip"], sinkEntry["port"], False, sinkEntry["enabled"], []))
        for sourceEntry in config["sources"]:
            self.__sources.append(Source(sourceEntry["name"], sourceEntry["ip"], False, sourceEntry["enabled"], []))
        for sinkGroup in config["groups"]["sinks"]:
            self.__sinks.append(Sink(sinkGroup["name"], "", 0, True, sinkGroup["enabled"], sinkGroup["sinks"]))
        for sourceGroup in config["groups"]["sources"]:
            self.__sources.append(Source(sourceGroup["name"], "", True, sourceGroup["enabled"], sourceGroup["sources"]))
        for routeEntry in config["routes"]:
            self.__routes.append(Route(routeEntry["name"], routeEntry["sink"], routeEntry["source"], routeEntry["enabled"]))

    def __save_yaml(self) -> None:
        """Saves the config to config.yaml"""
        sinks = []
        for sink in self.__sinks:
            if not sink.is_group:
                _newsink = {"name": sink.name, "ip": sink.ip, "port": sink.port, "enabled": sink.enabled}
                sinks.append(_newsink)
        sources = []
        for source in self.__sources:
            if not source.is_group:
                _newsource = {"name": source.name, "ip": source.ip, "enabled": source.enabled}
                sources.append(_newsource)
        routes = []
        for route in self.__routes:
            _newroute = {"name": route.name, "source": route.source, "sink": route.sink, "enabled": route.enabled}
            routes.append(_newroute)
        sink_groups = []
        for sink in self.__sinks:
            if sink.is_group:
                _newsink = {"name": sink.name, "sinks": sink.group_members, "enabled": sink.enabled}
                sink_groups.append(_newsink)
        source_groups = []
        for source in self.__sources:
            if source.is_group:
                _newsource = {"name": source.name, "sources": source.group_members, "enabled": source.enabled}
                source_groups.append(_newsource)
        groups = {"sinks": sink_groups, "sources": source_groups}
        save_data = {"sinks": sinks, "sources": sources, "routes": routes, "groups": groups}
        with open('config.yaml', 'w') as yaml_file:
            yaml.dump(save_data, yaml_file)

    def __start_receiver(self) -> None:
        """Start or restart the receiver"""
        self.__save_yaml()
        self.__build_real_sources_to_real_sinks()
        self.__build_real_sinks_to_real_sources()
        if self.__receiverset:
            print("Closing receiver!")
            self.__receiver.stop()
            self.__receiver.join()
        self.__receiverset = True
        self.__receiver = mixer.Receiver()
        for sink_ip in self.__sinks_to_sources.keys():
            sourceips: List[str] = []
            for source in self.__sinks_to_sources[sink_ip]:
                sourceips.append(source.ip)
            if len(sourceips) > 0 and sink_ip:
                sink = mixer.Sink(self.__receiver, sink_ip, sourceips)
                self.__receiver.register_sink(sink)

    # Sink Finders, used to build sourcetosink cache
    def __get_real_sinks_by_source(self, source: Source) -> List[Sink]:
        """Returns real (non-group) sinks for a given source"""
        if source.enabled:
            _sinks: List[Sink] = []
            _routes = self.__get_routes_by_source(source)
            for route in _routes:
                if route.enabled:
                    _sinks.extend(self.__get_real_sinks_by_route(route))
        return unique(_sinks)

    def __get_real_sinks_by_route(self, route: Route) -> List[Sink]:
        """Returns real (non-group) sinks for a given route"""
        return self.__get_real_sinks_from_sink(self.__get_sink_by_name(route.sink))

    def __get_sink_by_name(self, name: str) -> List[Sink]:
        """Returns a sink by name"""
        for sink in self.__sinks:
            if sink.name == name:
                return sink
        return 0

    def __get_real_sinks_from_sink(self, sink: Sink) -> List[Sink]:
        """Recursively work through sink groups to get all real sinks"""
        if sink.is_group:
            sinks: List[sink] = []
            for entry in sink.group_members:
                sinkEntry = self.__get_sink_by_name(entry)
                if not sinkEntry.enabled:
                    return []
                if sinkEntry.is_group:
                    sinks.extend(self.__get_real_sinks_from_sink(sinkEntry))
                else:
                    sinks.append(sinkEntry)
            return sinks
        else:
            return unique([sink])

    def __get_real_sinks_by_source(self, source: Source) -> List[Sink]:
        """Returns real (non-group) sinks for a given source"""
        _sinks: List[Sink] = []
        _routes = self.__get_routes_by_source(source)
        for route in _routes:
            if route.enabled and source.enabled:
                _sinks.extend(self.__get_real_sinks_by_route(route))
        return unique(_sinks)

    def __get_real_sinks_by_route(self, route: Route) -> List[Sink]:
        """Returns real (non-group) sinks for a given route"""
        sink = self.__get_sink_by_name(route.sink)
        if sink.enabled and route.enabled:
            return self.__get_real_sinks_from_sink(sink)
        return []

    # Source Finders
    def __get_real_sources_by_sink(self, sink: Sink) -> List[Source]:
        """Returns real (non-group) sources for a given sink"""
        if sink.enabled:
            _sources: List[Source] = []
            _routes = self.__get_routes_by_sink(sink)
            for route in _routes:
                if route.enabled:
                    _sources.extend(self.__get_real_sources_by_route(route))
        return unique(_sources)

    def __get_real_sources_by_route(self, route: Route) -> List[Source]:
        """Returns real (non-group) sources for a given route"""
        source = self.__get_source_by_name(route.source)
        if source.enabled and route.enabled:
            return self.__get_real_sources_from_source(source)
        return []

    def __get_source_by_name(self, name: str) -> Source:
        """Get source by name"""
        for source in self.__sources:
            if source.name == name:
                return source
        return 0

    def __getSourceByIP(self, ip: str) -> Source:
        """Get source by IP"""
        for source in self.__sources:
            if source.ip == ip:
                return source
        return 0

    def __get_real_sources_from_source(self, source: Source) -> List[Source]:
        """Recursively work through source groups to get all real sources"""
        if not source.enabled:
            return []
        if source.is_group:
                _sources: List[Source] = []
                for entry in source.group_members:
                    sourceEntry = self.__get_source_by_name(entry)
                    if sourceEntry.enabled:
                        if sourceEntry.is_group:
                            _sources.extend(self.__get_real_sources_from_source(sourceEntry))
                        else:
                            _sources.append(sourceEntry)
                return _sources
        else:
                return unique([source])

    def __get_all_real_sources(self) -> List[Source]:
        """Get all real sources in the project"""
        _sources: List[Source] = []
        for routeEntry in self.__routes:
            if routeEntry.enabled:
                _sources.extend(self.__get_real_sources_from_source(self.__get_source_by_name(routeEntry.source)))
        return _sources

    # Route Finders
    def __get_routes_by_source(self, source: Source):
        """Get all routes that use this source"""
        _routes: List[Route] = []
        for route in self.__routes:
            if route.enabled:
                _sources = self.__get_real_sources_from_source(self.__get_source_by_name(route.source))
                for _source in _sources:
                    if _source.enabled and _source == source:
                        _routes.append(route)
        return unique(_routes)

    def __get_routes_by_sink(self, sink: Sink):
        """Get all routes that use this sink"""
        _routes: List[Route] = []
        for route in self.__routes:
            _parentsink = self.__get_sink_by_name(route.sink)
            if _parentsink.enabled and route.enabled:
                _sinks = self.__get_real_sinks_from_sink(_parentsink)
                for _sink in _sinks:
                    if _sink.enabled and _sink == sink:
                        _routes.append(route)
        return unique(_routes)

    def __build_real_sources_to_real_sinks(self):
        """Build source to sink cache"""
        self.__sources_to_sinks = {}
        for source in self.__sources:
            if source.enabled:
                self.__sources_to_sinks[source.ip] = self.__get_real_sinks_by_source(source)

    def __build_real_sinks_to_real_sources(self):
        """Build sink to source cache"""
        self.__sinks_to_sources = {}
        for sink in self.__sinks:
            if sink.enabled:
                self.__sinks_to_sources[sink.ip] = self.__get_real_sources_by_sink(sink)