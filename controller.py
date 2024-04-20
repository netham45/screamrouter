import yaml
import socket
import time
import mixer
from copy import copy
from api import PostSinkGroup, PostSourceGroup, PostRoute

LOCALPORT=16401


"""
Class Sink

name: A string name for the sink.
ip: A string IP address for the sink.
port: An integer port number for the sink.
is_group: A boolean indicating whether this sink is a group or not.
group_members: A list of Sink objects that are members of this sink if it is a group, otherwise an empty list.
"""
class Sink:
    def __init__(self, name: str, ip: str, port: int, is_group: bool, group_members=None):
        self.name = name
        self.ip = ip
        self.port = port
        self.is_group = is_group
        self.group_members = group_members or []
        self.outsock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) if not self.is_group else None
        self.enabled = True

"""
Class Source

name: A string name for the sink.
ip: A string IP address for the sink.
is_group: A boolean indicating whether this sink is a group or not.
group
"""

class Source:
    def __init__(self, name: str, ip: str, is_group: bool, group_members=None):
        self.name = name
        self.ip = ip
        self.is_group = is_group
        self.group_members = group_members or []
        self.enabled = True

"""
Class Route

name: A string name for the route.
sink: A Sink object that represents the sink that this route is connected to.
source: A Source object that represents the source that this route is connected to.
"""

class Route:
    def __init__(self, name: str, sink: Sink, source: Source):
        self.name = name
        self.sink = sink
        self.source = source
        self.enabled = True

# Helper functions
def unique(list):
    _list = []
    for element in list:
        if not element in _list:
            _list.append(element)
    return _list

class Controller:
    def __init__(self):
        self.__sinks = []  # Holds a list of sinks and sink groups
        self.__sources = []  # Holds a list of sources and source groups
        self.__routes = []  # Holds a list of routes
        self.__sourcestosinks = {}  # Holds a cached map of sources -> sinks
        self.__sinkstosources = {}  # Holds a cached map of sinks -> sources
        self.__receiver = None  # Holds the master object so it can be destroyed later
        self.__receiverset = False  # Flag if the master is set
        self.__load_yaml()
        self.__start_receiver()

    # Public functions

    def get_sinks(self):
        _sinks = []
        for sink in self.__sinks:
            _sinks.append(copy(sink))
            _sinks[len(_sinks) - 1].outsock = ""
        return _sinks

    def add_sink_group(self, sink: PostSinkGroup):
        for _sink in self.__sinks:
            if sink.name == _sink.name:
                return False
        if sink.name.strip() == False:
            return False
        self.__sinks.append(Sink(sink.name,"", 0, True, sink.sinks))
        self.__start_receiver()
        return True

    def delete_sink_group(self, sink_id):
        if self.__sinks[sink_id].is_group:
            for route in self.__routes:
                if self.__sinks[sink_id].name == route.sink:
                    return False  # Can't remove a sink in use by a route
            self.__sinks.pop(sink_id)
            self.__start_receiver()
            return True
        return False

    def enable_sink(self, sink_id):
        if 0 <= sink_id < len(self.__sinks):
            self.__sinks[sink_id].enabled = True
            self.__start_receiver()
            return True
        return False

    def disable_sink(self, sink_id):
        if 0 <= sink_id < len(self.__sinks):
            self.__sinks[sink_id].enabled = False
            self.__start_receiver()
            return True
        return False

    def get_sources(self):
        return copy(self.__sources)

    def add_source_group(self, source: PostSourceGroup):
        for _source in self.__sources:
            if source.name == _source.name:
                return False
        if source.name.strip() == False:
            return False
        self.__sources.append(Source(source.name, "", 0, True, source.sources))
        self.__start_receiver()
        return True

    def delete_source_group(self, source_id):
        if self.__sources[source_id].is_group:
            for route in self.__routes:
                if self.__sources[source_id].name == route.source:
                    return False  # Can't remove a source in use by a route
            self.__sources.pop(source_id)
            self.__start_receiver()
            return True
        return False

    def enable_source(self, source_id):
        if 0 <= source_id < len(self.__sources):
            self.__sources[source_id].enabled = True
            self.__start_receiver()
            return True
        return False

    def disable_source(self, source_id):
        if 0 <= source_id < len(self.__sources):
            self.__sources[source_id].enabled = False
            self.__start_receiver()
            return True
        return False

    def get_routes(self):
        return copy(self.__routes)

    def add_route(self, route: PostRoute):
        self.__routes.append(Route(route.name, route.sink, route.source))
        self.__start_receiver()
        return True

    def delete_route(self, route_id):
        self.__routes.pop(route_id)
        self.__start_receiver()
        return True

    def enable_route(self, route_id):
        if 0 <= route_id < len(self.__routes):
            self.__routes[route_id].enabled = True
            self.__start_receiver()
            return True
        return False

    def disable_route(self, route_id):
        if 0 <= route_id < len(self.__routes):
            self.__routes[route_id].enabled = False
            self.__start_receiver()
            return True
        return False

    # Private Functions

    def __load_yaml(self):
        """Loads the initial config"""
        with open("screamrouter.yaml", "r") as f:
            config = yaml.safe_load(f)
        for sinkEntry in config["sinks"]:
            self.__sinks.append(Sink(sinkEntry["sink"]["name"], sinkEntry["sink"]["ip"], sinkEntry["sink"]["port"], False, []))
        for sourceEntry in config["sources"]:
            self.__sources.append(Source(sourceEntry["source"]["name"], sourceEntry["source"]["ip"], False, []))
        for sinkGroup in config["groups"]["sinks"]:
            self.__sinks.append(Sink(sinkGroup["sink"]["name"], "", 0, True, sinkGroup["sink"]["sinks"]))
        for sourceGroup in config["groups"]["sources"]:
            self.__sources.append(Source(sourceGroup["source"]["name"], 0, True, sourceGroup["source"]["sources"]))
        for routeEntry in config["routes"]:
            self.__routes.append(Route(routeEntry["route"]["name"], routeEntry["route"]["sink"], routeEntry["route"]["source"]))

    def __start_receiver(self):
        """Start or restart the receiver"""
        self.__build_sources_to_sinks()
        self.__build_sinks_to_sources()
        if self.__receiverset:
            print("Closing receiver!")
            self.__receiver.sock.close()
            self.__receiver.close()
            time.sleep(.5)
        self.__receiverset = True
        self.__receiver = mixer.Receiver()
        for sink_ip in self.__sinkstosources.keys():
            sourceips = []
            for source in self.__sinkstosources[sink_ip]:
                sourceips.append(source.ip)
            if len(sourceips) > 0 and sink_ip:
                sink = mixer.Sink(self.__receiver, sink_ip, sourceips)

    # Sink Finders, used to build sourcetosink cache
    def __get_sinks_by_source(self, source: Source):
        """Returns all sinks for a given source"""
        if not source.enabled:
            return []
        _sinks = []
        _routes = self.__get_routes_by_source(source)
        for route in _routes:
            if not route.enabled and not source.enabled:
                continue
            _sinks.extend(self.__get_all_real_sinks_by_route(route))
        return unique(_sinks)

    def __get_all_real_sinks_by_route(self, route: Route):
        """Returns all real (non-group) sinks for a given route"""
        return self.__get_all_real_sinks_from_sink(self.__get_sink_by_name(route.sink))

    def __get_sink_by_name(self, name: str):
        """Returns a sink by name"""
        for sink in self.__sinks:
            if sink.name == name:
                return sink
        return 0

    def __get_all_real_sinks_from_sink(self, sink: Sink):
        """Work out sink groups and get all real sinks"""
        if sink.is_group:
            sinks = []
            for entry in sink.group_members:
                sinkEntry = self.__get_sink_by_name(entry)
                if not sinkEntry.enabled:
                    return []
                if sinkEntry.is_group:
                    sinks.extend(self.__get_all_real_sinks_from_sink(sinkEntry))
                else:
                    sinks.append(sinkEntry)
            return sinks
        else:
            return unique([sink])

    # Source Finders
    def __get_sources_by_sink(self, sink: Sink):
        """Returns all sources for a given sink"""
        if not sink.enabled:
            return []
        _sources = []
        _routes = self.__get_routes_by_sink(sink)
        for route in _routes:
            if not route.enabled or not sink.enabled:
                continue
            _sources.extend(self.__get_all_real_sources_by_route(route))
        return unique(_sources)

    def __get_all_real_sources_by_route(self, route: Route):
        """Returns all real (non-group) sources for a given route"""
        if not route.enabled:
            return []
        return self.__get_all_real_sources_from_source(self.__get_source_by_name(route.source))

    def __get_sinks_by_source(self, source: Source):
        """Returns all sinks for a given source"""
        _sinks = []
        _routes = self.__get_routes_by_source(source)
        for route in _routes:
            if not route.enabled or not source.enabled:
                continue
            _sinks.extend(self.__get_all_real_sinks_by_route(route))
        return unique(_sinks)

    def __get_all_real_sinks_by_route(self, route: Route):
        """Returns all real (non-group) sinks for a given route"""
        if not route.enabled:
            return []
        sink = self.__get_sink_by_name(route.sink)
        if not sink.enabled:
            return []
        return self.__get_all_real_sinks_from_sink(sink)

    def __get_source_by_name(self, name: str):
        """Get source by name"""
        for source in self.__sources:
            if source.name == name:
                return source
        return 0

    def __getSourceByIP(self, ip: str):
        """Get source by IP"""
        for source in self.__sources:
            if source.ip == ip:
                return source
        return 0

    def __get_all_real_sources_from_source(self, source: Source):
        """Work out source groups and get all real sources that belong to a source"""
        if not source.enabled:
            return []
        if source.is_group:
                _sources = []
                for entry in source.group_members:
                    sourceEntry = self.__get_source_by_name(entry)
                    if sourceEntry.enabled:
                        if sourceEntry.is_group:
                            _sources.extend(self.__get_all_real_sources_from_source(sourceEntry))
                        else:
                            _sources.append(sourceEntry)
                return _sources
        else:
                return unique([source])

    def __get_all_real_sources(self):
        """Get all real sources in the project"""
        _sources = []
        for routeEntry in self.__routes:
            if routeEntry.enabled:
                _sources.extend(self.__get_all_real_sources_from_source(self.__get_source_by_name(routeEntry.source)))
        return _sources

    # Route Finders
    def __get_routes_by_source(self, source: Source):
        """Get all routes that use this source"""
        _routes = []
        for route in self.__routes:
            if not route.enabled:
                continue
            _sources = self.__get_all_real_sources_from_source(self.__get_source_by_name(route.source))
            for _source in _sources:
                if not _source.enabled:
                    continue
                if _source == source:
                    _routes.append(route)
        return unique(_routes)

    def __get_routes_by_sink(self, sink: Sink):
        """Get all routes that use this sink"""
        _routes = []
        for route in self.__routes:
            if not route.enabled:
                continue
            _parentsink = self.__get_sink_by_name(route.sink)
            if not _parentsink.enabled:
                continue
            _sinks = self.__get_all_real_sinks_from_sink(_parentsink)
            for _sink in _sinks:
                if not _sink.enabled:
                    continue
                if _sink == sink:
                    _routes.append(route)
        return unique(_routes)

    def __build_sources_to_sinks(self):
        """Build source to sink cache"""
        self.__sourcestosinks = {}
        for source in self.__sources:
            if source.enabled:
                self.__sourcestosinks[source.ip] = self.__get_sinks_by_source(source)

    def __build_sinks_to_sources(self):
        """Build sink to source cache"""
        self.__sinkstosources = {}
        for sink in self.__sinks:
            if sink.enabled:
                self.__sinkstosources[sink.ip] = self.__get_sources_by_sink(sink)