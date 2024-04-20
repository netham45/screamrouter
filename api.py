from typing import List
from fastapi import FastAPI
from starlette.responses import FileResponse
from pydantic import BaseModel
import threading
import uvicorn

class PostSinkGroup(BaseModel):
    name: str
    sinks: List[str]

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
        self.app = FastAPI()
        self.controller = controller
        self.app.get("/")(self.read_index)
        self.app.get("/sinks")(self.get_sinks)
        self.app.post("/sinks")(self.add_sink_group)
        self.app.delete("/sinks/{sink_id}")(self.delete_sink_group)
        self.app.get("/sinks/{sink_id}/disable")(self.disable_sink)
        self.app.get("/sinks/{sink_id}/enable")(self.enable_sink)
        self.app.get("/sources")(self.get_sources)
        self.app.post("/sources")(self.add_source_group)
        self.app.delete("/sources/{source_id}")(self.delete_source_group)
        self.app.get("/sources/{source_id}/disable")(self.disable_source)
        self.app.get("/sources/{source_id}/enable")(self.enable_source)
        self.app.get("/routes")(self.get_routes)
        self.app.post("/routes")(self.add_route)
        self.app.delete("/routes/{route_id}")(self.delete_route)
        self.app.get("/routes/{route_id}/disable")(self.disable_route)
        self.app.get("/routes/{route_id}/enable")(self.enable_route)
        self.start()


    def run(self):
        uvicorn.run(self.app, port=8080, host='0.0.0.0')

    # Index Endpoint
    def read_index(self):
        """Index page"""
        return FileResponse('index.html')

    # Sink Endpoints
    def get_sinks(self):
        """Get all sinks"""
        return self.controller.get_sinks()

    def add_sink_group(self, sink: PostSinkGroup):
        """Add a new sink group"""
        return self.controller.add_sink_group(sink)

    def delete_sink_group(self, sink_id: int):
        """Delete a sink group by ID"""
        return self.controller.delete_sink_group(sink_id)

    def disable_sink(self, sink_id: int):
        """Disable a sink"""
        return self.controller.disable_sink(sink_id)

    def enable_sink(self, sink_id: int):
        """Enable a sink"""
        return self.controller.enable_sink(sink_id)

    # Source Endpoints
    def get_sources(self):
        """Get all sources"""
        return self.controller.get_sources()

    def add_source_group(self, source: PostSourceGroup):
        """Add a new source group"""
        return self.controller.add_source_group(PostSourceGroup)

    def delete_source_group(self, source_id: int):
        """Delete a source group by ID"""
        return self.controller.delete_source_group(source_id)

    def disable_source(self, source_id: int):
        """Disable a source"""
        return self.controller.disable_source(source_id)

    def enable_source(self, source_id: int):
        """Enable a source"""
        return self.controller.enable_source(source_id)

    # Route Endpoints
    def get_routes(self):
        """Get all routes"""
        return self.controller.get_routes()

    def add_route(self, route: PostRoute):
        """Add a new route"""
        return self.controller.add_route(route)

    def delete_route(self, route_id: int):
        """Delete a route by ID"""
        return self.controller.delete_route(route_id)

    def disable_route(self, route_id: int):
        """Disable a route"""
        return self.controller.disable_route(route_id)

    def enable_route(self, route_id: int):
        """Enable a route"""
        return self.controller.enable_route(route_id)