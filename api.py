from typing import List
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from starlette.responses import FileResponse
from pydantic import BaseModel
import threading
import uvicorn
from controller import SinkDescription, SourceDescription, RouteDescription, Controller

class PostSink(BaseModel):
    name: str
    """Sink Name"""
    ip: str
    """Sink IP"""
    port: int
    """Sink Port"""

class PostSinkGroup(BaseModel):
    name: str
    """Sink Group Name"""
    sinks: List[str]
    """List of names of grouped sinks"""

class PostSource(BaseModel):
    name: str
    """Source Name"""
    ip: str
    """Source IP"""

class PostSourceGroup(BaseModel):
    name: str
    """Source Group Name"""
    sources: List[str]
    """List of names of grouped Sources"""

class PostRoute(BaseModel):
    name: str
    """Route Name"""
    source: str
    """Route Source"""
    sink: str
    """Route Sink"""

class API(threading.Thread):

    def __init__(self, controller):
        super().__init__()
        self.controller: Controller = controller
        """Holds the active controller"""
        tags_metadata = [
            {
                "name": "Sinks",
                "description": "API endpoints for managing Sinks"
            },
            {
                "name": "Sources",
                "description": "API endpoints for managing Sources"
            },
            {
                "name": "Routes",
                "description": "API endpoints for managing Routes"
            },
            {
                "name": "Site",
                "description": "File handlers for the site interface"
            }
        ]
        self.app: FastAPI = FastAPI( title="ScreamRouter",
            description = "Routes PCM audio around for Scream sinks and sources",
#            summary = "Routes PCM audio around for Scream sinks and sources",
            version="0.0.1",
            contact={
                "name": "ScreamRouter",
                "url": "http://github.com/netham45/screamrouter",
            },
            license_info={
                "name": "No license chosen yet, all rights reserved",
            },
            openapi_tags=tags_metadata
        )
        """FastAPI"""
        self.app.get("/", tags=["Site"])(self.read_index)
        self.app.get("/site.js", tags=["Site"])(self.read_javascript)
        self.app.get("/site.css", tags=["Site"])(self.read_css)
        
        self.app.get("/sinks", tags=["Sinks"])(self.get_sinks)
        self.app.post("/groups/sinks", tags=["Sinks"])(self.add_sink_group)
        self.app.post("/sinks", tags=["Sinks"])(self.add_sink)
        self.app.delete("/sinks/{sink_id}", tags=["Sinks"])(self.delete_sink)
        self.app.get("/sinks/{sink_id}/disable", tags=["Sinks"])(self.disable_sink)
        self.app.get("/sinks/{sink_id}/enable", tags=["Sinks"])(self.enable_sink)
        self.app.get("/sinks/{sink_id}/volume/{volume}", tags=["Sinks"])(self.set_sink_volume)
        
        self.app.get("/sources", tags=["Sources"])(self.get_sources)
        self.app.post("/groups/sources", tags=["Sources"])(self.add_source_group)
        self.app.post("/sources", tags=["Sources"])(self.add_source)
        self.app.delete("/sources/{source_id}", tags=["Sources"])(self.delete_source)
        self.app.get("/sources/{source_id}/disable", tags=["Sources"])(self.disable_source)
        self.app.get("/sources/{source_id}/enable", tags=["Sources"])(self.enable_source)
        self.app.get("/sources/{source_id}/volume/{volume}", tags=["Sources"])(self.set_source_volume)

        self.app.get("/routes", tags=["Routes"])(self.get_routes)
        self.app.get("/routes", tags=["Routes"])(self.get_routes)
        self.app.post("/routes", tags=["Routes"])(self.add_route)
        self.app.delete("/routes/{route_id}", tags=["Routes"])(self.delete_route)
        self.app.get("/routes/{route_id}/disable", tags=["Routes"])(self.disable_route)
        self.app.get("/routes/{route_id}/enable", tags=["Routes"])(self.enable_route)
        self.app.get("/routes/{route_id}/volume/{volume}", tags=["Routes"])(self.set_route_volume)
        self.start()

    def run(self) -> None:
        self.app.add_exception_handler(Exception, self.__api_exception_handler)
        uvicorn.run(self.app, port=8080, host='0.0.0.0')

    def __api_exception_handler(self, request: Request, exception: Exception) -> JSONResponse:
        """Generic error handler so controller can throw generic exceptions and get useful messages returned to clients"""
        return JSONResponse(
            status_code = 500,
            content = {"error": str(exception)}
        )

    # Site resource endpoints
    def read_index(self) -> FileResponse:
        """Index page"""
        return FileResponse('index.html')

    def read_javascript(self) -> FileResponse:
        """Javascript page"""
        return FileResponse('site.js')

    def read_css(self) -> FileResponse:
        """CSS page"""
        return FileResponse('site.css')

    # Sink Endpoints
    def set_sink_volume(self, sink_id: int, volume: float) -> bool:
        """Sets the volume for a sink"""
        return self.controller.update_sink_volume(sink_id, volume)

    def get_sinks(self) -> List[SinkDescription]:
        """Get all sinks"""
        return self.controller.get_sinks()

    def add_sink(self, sink: PostSink) -> bool:
        """Add a new sink"""
        return self.controller.add_sink(SinkDescription(sink.name, sink.ip, sink.port, False, True, [], 1))

    def add_sink_group(self, sink_group: PostSinkGroup) -> bool:
        """Add a new sink group"""
        return self.controller.add_sink(SinkDescription(sink_group.name, "", 0, True, True, sink_group.sinks, 1))

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
    def set_source_volume(self, source_id: int, volume: float) -> bool:
        """Sets the volume for a source"""
        return self.controller.update_source_volume(source_id, volume)
    
    def get_sources(self) -> List[SourceDescription]:
        """Get all sources"""
        return self.controller.get_sources()

    def add_source(self, source: PostSource) -> bool:
        """Add a new source"""
        return self.controller.add_source(SourceDescription(source.name, source.ip, False, True, [], 1))

    def add_source_group(self, source_group: PostSourceGroup) -> bool:
        """Add a new source group"""
        return self.controller.add_source(SourceDescription(source_group.name, "", True, True, source_group.sources, 1))

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
    def set_route_volume(self, route_id: int, volume: float) -> bool:
        """Sets the volume for a route"""
        return self.controller.update_route_volume(route_id, volume)
    
    def get_routes(self) -> List[RouteDescription]:
        """Get all routes"""
        return self.controller.get_routes()

    def add_route(self, route: PostRoute) -> bool:
        """Add a new route"""
        return self.controller.add_route(RouteDescription(route.name, route.sink, route.source, True, 1))

    def delete_route(self, route_id: int)  -> bool:
        """Delete a route by ID"""
        return self.controller.delete_route(route_id)

    def disable_route(self, route_id: int)  -> bool:
        """Disable a route"""
        return self.controller.disable_route(route_id)

    def enable_route(self, route_id: int)  -> bool:
        """Enable a route"""
        return self.controller.enable_route(route_id)