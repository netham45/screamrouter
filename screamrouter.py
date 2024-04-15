#!/usr/bin/python3
from typing import List
from fastapi import FastAPI
from starlette.responses import FileResponse
import yaml
import socket
import threading
import uvicorn
from pydantic import BaseModel
from copy import copy
import time

from mixer import MasterReceiver, Receiver


app = FastAPI()

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

class PostSinkGroup(BaseModel):
    name: str
    sinks: List[str]

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

class PostSourceGroup(BaseModel):
    name: str
    sources: List[str]

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

class PostRoute(BaseModel):
    name: str
    source: str
    sink: str


LOCALPORT=16401

class UdpListener(threading.Thread):
    """Handles listening for inbound UDP messages and sending them back out based off of the sourcestosinks dict"""
    def __init__(self):
        super().__init__()

    def run(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", LOCALPORT))
        while True:
            data, addr = self.sock.recvfrom(9001)
            if addr[0] in sourcestosinks:
                for sink in sourcestosinks[addr[0]]:
                    sink.outsock.sendto(data, (sink.ip, sink.port))


sinks = []  # Holds a list of sinks and sink groups
sources = []  # Holds a list of sources and source groups
routes = []  # Holds a list of routes
threads = []  # Holds a pointer to the UdpListener thread
sourcestosinks = {}  # Holds a cached map of sources -> sinks
sinkstosources = {}  # Holds a cached map of sinks -> sources

def unique(list):
    _list = []
    for element in list:
        if not element in _list:
            _list.append(element)
    return _list

def load_yaml():
    """Loads the initial config"""
    with open("screamrouter.yaml", "r") as f:
        config = yaml.safe_load(f)
    for sinkEntry in config["sinks"]:
        sinks.append(Sink(sinkEntry["sink"]["name"], sinkEntry["sink"]["ip"], sinkEntry["sink"]["port"], False, []))
    for sourceEntry in config["sources"]:
        sources.append(Source(sourceEntry["source"]["name"], sourceEntry["source"]["ip"], False, []))
    for sinkGroup in config["groups"]["sinks"]:
        sinks.append(Sink(sinkGroup["sink"]["name"], "", 0, True, sinkGroup["sink"]["sinks"]))
    for sourceGroup in config["groups"]["sources"]:
        sources.append(Source(sourceGroup["source"]["name"], 0, True, sourceGroup["source"]["sources"]))
    routes.append(Route("Default", config["default_routing"]["sink"], config["default_routing"]["source"]))

# Sink Finders, used to build sourcetosink cache
def get_sinks_by_source(source: Source):
    """Returns all sinks for a given source"""
    _sinks = []
    _routes = get_routes_by_source(source)
    for route in _routes:
        _sinks.extend(get_all_real_sinks_by_route(route))
    return unique(_sinks)

def get_all_real_sinks_by_route(route: Route):
    """Returns all real (non-group) sinks for a given route"""
    return get_all_real_sinks_from_sink(get_sink_by_name(route.sink))

def get_sink_by_name(name: str):
    """Returns a sink by name"""
    for sink in sinks:
        if sink.name == name:
            return sink
    return 0

def get_all_real_sinks_from_sink(sink: Sink):
    """Work out sink groups and get all real sinks"""
    if sink.is_group:
        sinks = []
        for entry in sink.group_members:
            sinkEntry = get_sink_by_name(entry)
            if sinkEntry.is_group:
                sinks.extend(get_all_real_sinks_from_sink(sinkEntry))
            else:
                sinks.append(sinkEntry)
        return sinks
    else:
        return unique([sink])

# Source Finders
def get_sources_by_sink(sink: Sink):
    """Returns all sources for a given sink"""
    _sources = []
    _routes = get_routes_by_sink(sink)
    for route in _routes:
        _sources.extend(get_all_real_sources_by_route(route))
    return unique(_sources)

def get_all_real_sources_by_route(route: Route):
    """Returns all real (non-group) sources for a given route"""
    return get_all_real_sources_from_source(get_source_by_name(route.source))

def get_sinks_by_source(source: Source):
    """Returns all sinks for a given source"""
    _sinks = []
    _routes = get_routes_by_source(source)
    for route in _routes:
        _sinks.extend(get_all_real_sinks_by_route(route))
    return unique(_sinks)

def get_all_real_sinks_by_route(route: Route):
    """Returns all real (non-group) sinks for a given route"""
    return get_all_real_sinks_from_sink(get_sink_by_name(route.sink))
def get_source_by_name(name: str):
    """Get source by name"""
    for source in sources:
        if source.name == name:
            return source
    return 0

def getSourceByIP(ip: str):
    """Get source by IP"""
    for source in sources:
        if source.ip == ip:
            return source
    return 0

def get_all_real_sources_from_source(source: Source):
   """Work out source groups and get all real sources that belong to a source"""
   if source.is_group:
        _sources = []
        for entry in source.group_members:
            sourceEntry = get_source_by_name(entry)
            if sourceEntry.is_group:
                _sources.extend(get_all_real_sources_from_source(sourceEntry))
            else:
                _sources.append(sourceEntry)
        return _sources
   else:
        return unique([source])

def get_all_real_sources():
    """Get all real sources in the project"""
    _sources = []
    for routeEntry in routes:
        _sources.extend(get_all_real_sources_from_source(get_source_by_name(routeEntry.source)))
    return _sources

# Route Finders
def get_routes_by_source(source: Source):
    """Get all routes that use this source"""
    _routes = []
    for route in routes:
        _sources = get_all_real_sources_from_source(get_source_by_name(route.source))
        for _source in _sources:
            if _source == source:
                _routes.append(route)
    return unique(_routes)

def get_routes_by_sink(sink: Sink):
    """Get all routes that use this sink"""
    _routes = []
    for route in routes:
        _sinks = get_all_real_sinks_from_sink(get_sink_by_name(route.sink))
        for _sink in _sinks:
            if _sink == sink:
                _routes.append(route)
    return unique(_routes)

def build_sources_to_sinks():
    """Build source to sink cache"""
    global sourcestosinks
    sourcestosinks = {}
    for source in sources:
        sourcestosinks[source.ip] = get_sinks_by_source(source)

def build_sinks_to_sources():
    """Build sink to source cache"""
    global sinkstosources
    sinkstosources = {}
    for sink in sinks:
        sinkstosources[sink.ip] = get_sources_by_sink(sink)

master = False
masterset = False

def load_listeners():
    """Load the listener thread"""
    global master,masterset
    build_sources_to_sinks()
    build_sinks_to_sources()
    if masterset:
        print("Closing master!")
        master.sock.close()
        master.close()
        time.sleep(5)
    masterset = True
    master = MasterReceiver()
    for sink in sinkstosources.keys():
        sourceips = []
        for source in sinkstosources[sink]:
            sourceips.append(source.ip)
        if len(sourceips) > 0 and sink:
            print(f"Sink:{sink}, Sources: {sinkstosources[sink]}")
            receiver = Receiver(master, sink, sourceips)
#    newListener = UdpListener()
#    threads.append(newListener)
#    newListener.start()

# Index Endpoint
@app.get("/")
async def read_index():
    """Index page"""
    return FileResponse('index.html')

# Sink Endpoints
@app.get("/sinks")
async def get_sinks():
    """Get all sinks"""
    _sinks = []
    for sink in sinks:
        _sinks.append(copy(sink))
        _sinks[len(_sinks) - 1].outsock = ""
    return _sinks

@app.post("/sinks")
async def add_sink_group(sink: PostSinkGroup):
    """Add a new sink group"""
    for _sink in sinks:
        if sink.name == _sink.name:
            return False
    if sink.name.strip() == False:
        return False
    sinks.append(Sink(sink.name,"", 0, True, sink.sinks))
    load_listeners()
    return True

@app.delete("/sinks/{sink_id}")
async def delete_sink_group(sink_id: int):
    """Delete a sink group by ID"""
    if sinks[sink_id].is_group:
        for route in routes:
            if sinks[sink_id].name == route.sink:
                return False  # Can't remove a sink in use by a route
        sinks.pop(sink_id)
        load_listeners()
        return True
    return False

# Source Endpoints
@app.get("/sources")
async def get_sources():
    """Get all sources"""
    return sources

@app.post("/sources")
async def add_source_group(source: PostSourceGroup):
    """Add a new source group"""
    for _source in sources:
        if source.name == _source.name:
            return False
    if source.name.strip() == False:
        return False
    sources.append(Source(source.name, "", 0, True, source.sources))
    load_listeners()
    return True

@app.delete("/sources/{source_id}")
async def delete_source_group(source_id: int):
    """Delete a source group by ID"""
    if sources[source_id].is_group:
        for route in routes:
            if sources[source_id].name == route.source:
                return False  # Can't remove a source in use by a route
        sources.pop(source_id)
        load_listeners()
        return True
    return False

# Route Endpoints
@app.get("/routes")
async def get_routes():
    """Get all routes"""
    return routes

@app.post("/routes")
async def add_route(route: PostRoute):
    """Add a new route"""
    routes.append(Route(route.name, route.sink, route.source))
    load_listeners()
    return True

@app.delete("/routes/{route_id}")
async def delete_route(route_id: int):
    """Delete a route by ID"""
    routes.pop(route_id)
    load_listeners()
    return True

if __name__ == '__main__':
    load_yaml()
    load_listeners()
    uvicorn.run(app, port=8080, host='0.0.0.0')