from typing import List
from fastapi import FastAPI
from starlette.responses import FileResponse
from pydantic import BaseModel
import threading
import uvicorn
from controller import Sink, Source, Route, Controller

class PostSink(BaseModel):
    name: str
    ip: str
    port: int

class PostSinkGroup(BaseModel):
    name: str
    sinks: List[str]

class PostSource(BaseModel):
    name: str
    ip: str

class PostSourceGroup(BaseModel):
    name: str
    sources: List[str]

class PostRoute(BaseModel):
    name: str
    source: str
    sink: str

class API(threading.Thread):

    def __init__(self, controller):
        super().__init__()
        self.controller: Controller = controller
        self.app: FastAPI = FastAPI()
        self.app.get("/")(self.read_index)
        self.app.get("/sinks")(self.get_sinks)
        self.app.post("/groups/sinks")(self.add_sink_group)
        self.app.post("/sinks")(self.add_sink)
        self.app.delete("/sinks/{sink_id}")(self.delete_sink)
        self.app.get("/sinks/{sink_id}/disable")(self.disable_sink)
        self.app.get("/sinks/{sink_id}/enable")(self.enable_sink)
        self.app.get("/sources")(self.get_sources)
        self.app.post("/groups/sources")(self.add_source_group)
        self.app.post("/sources")(self.add_source)
        self.app.delete("/sources/{source_id}")(self.delete_source)
        self.app.get("/sources/{source_id}/disable")(self.disable_source)
        self.app.get("/sources/{source_id}/enable")(self.enable_source)
        self.app.get("/routes")(self.get_routes)
        self.app.post("/routes")(self.add_route)
        self.app.delete("/routes/{route_id}")(self.delete_route)
        self.app.get("/routes/{route_id}/disable")(self.disable_route)
        self.app.get("/routes/{route_id}/enable")(self.enable_route)
        self.start()


    def run(self) -> None:
        uvicorn.run(self.app, port=8080, host='0.0.0.0')

    # Index Endpoint
    def read_index(self) -> FileResponse:
        """Index page"""
        return FileResponse('index.html')

    # Sink Endpoints
    def get_sinks(self) -> List[Sink]:
        """Get all sinks"""
        return self.controller.get_sinks()

    def add_sink(self, sink: PostSink) -> bool:
        return self.controller.add_sink(Sink(sink.name, sink.ip, sink.port, False, True, []))

    def add_sink_group(self, sink_group: PostSinkGroup) -> bool:
        """Add a new sink group"""
        return self.controller.add_sink(Sink(sink_group.name, "", "", True, True, sink_group.sinks))

    def delete_sink(self, sink_id: int) -> bool:
        """Delete a sink group by ID"""
        return self.controller.delete_sink(sink_id)

    def disable_sink(self, sink_id: int) -> bool:
        """Disable a sink"""
        return self.controller.disable_sink(sink_id)

    def enable_sink(self, sink_id: int) -> bool:
        """Enable a sink"""
        return self.controller.enable_sink(sink_id)

    # Source Endpoints
    def get_sources(self) -> List[Source]:
        """Get all sources"""
        return self.controller.get_sources()

    def add_source(self, source: PostSource) -> bool:
        return self.controller.add_source(Source(source.name, source.ip, False, True, []))

    def add_source_group(self, source_group: PostSourceGroup) -> bool:
        """Add a new source group"""
        return self.controller.add_source(Source(source_group.name, "", "", True, True, source_group.sources))

    def delete_source(self, source_id: int) -> bool:
        """Delete a source group by ID"""
        return self.controller.delete_source(source_id)

    def disable_source(self, source_id: int) -> bool:
        """Disable a source"""
        return self.controller.disable_source(source_id)

    def enable_source(self, source_id: int) -> bool:
        """Enable a source"""
        return self.controller.enable_source(source_id)

    # Route Endpoints
    def get_routes(self) -> List[Route]:
        """Get all routes"""
        return self.controller.get_routes()

    def add_route(self, route: PostRoute) -> bool:
        """Add a new route"""
        return self.controller.add_route(Route(route.name, route.sink, route.source,"True"))

    def delete_route(self, route_id: int)  -> bool:
        """Delete a route by ID"""
        return self.controller.delete_route(route_id)

    def disable_route(self, route_id: int)  -> bool:
        """Disable a route"""
        return self.controller.disable_route(route_id)

    def enable_route(self, route_id: int)  -> bool:
        """Enable a route"""
        return self.controller.enable_route(route_id)