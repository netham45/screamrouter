from typing import List
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from fastapi.templating import Jinja2Templates
from pydantic import BaseModel
import threading

import uvicorn
from controller import SinkDescription, SourceDescription, RouteDescription, Controller

from api_webstream import API_Webstream

templates = Jinja2Templates(directory="./")

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

class API_Controller(threading.Thread):

    def __init__(self, app: FastAPI, controller: Controller):
        super().__init__(name=f"API Thread")
        """Holds the active controller"""
        self._controller = controller
        self._app = app
        """FastAPI"""
        self._app.get("/", tags=["Site"])(self.read_index)
        self._app.get("/site.js", tags=["Site"])(self.read_javascript)
        self._app.get("/site.css", tags=["Site"])(self.read_css)
        
        self._app.get("/sinks", tags=["Sinks"])(self.get_sinks)
        self._app.post("/groups/sinks", tags=["Sinks"])(self.add_sink_group)
        self._app.post("/sinks", tags=["Sinks"])(self.add_sink)
        self._app.delete("/sinks/{sink_name}", tags=["Sinks"])(self.delete_sink)
        self._app.get("/sinks/{sink_name}/disable", tags=["Sinks"])(self.disable_sink)
        self._app.get("/sinks/{sink_name}/enable", tags=["Sinks"])(self.enable_sink)
        self._app.get("/sinks/{sink_name}/volume/{volume}", tags=["Sinks"])(self.set_sink_volume)
        
        self._app.get("/sources", tags=["Sources"])(self.get_sources)
        self._app.post("/groups/sources", tags=["Sources"])(self.add_source_group)
        self._app.post("/sources", tags=["Sources"])(self.add_source)
        self._app.delete("/sources/{source_name}", tags=["Sources"])(self.delete_source)
        self._app.get("/sources/{source_name}/disable", tags=["Sources"])(self.disable_source)
        self._app.get("/sources/{source_name}/enable", tags=["Sources"])(self.enable_source)
        self._app.get("/sources/{source_name}/volume/{volume}", tags=["Sources"])(self.set_source_volume)

        self._app.get("/routes", tags=["Routes"])(self.get_routes)
        self._app.get("/routes", tags=["Routes"])(self.get_routes)
        self._app.post("/routes", tags=["Routes"])(self.add_route)
        self._app.delete("/routes/{route_name}", tags=["Routes"])(self.delete_route)
        self._app.get("/routes/{route_name}/disable", tags=["Routes"])(self.disable_route)
        self._app.get("/routes/{route_name}/enable", tags=["Routes"])(self.enable_route)
        self._app.get("/routes/{route_name}/volume/{volume}", tags=["Routes"])(self.set_route_volume)
        self._app.add_exception_handler(Exception, self.__api_exception_handler)
        self.start()    

    def run(self):
        uvicorn.run(self._app, port=8080, host='0.0.0.0')

    def __api_exception_handler(self, request: Request, exception: Exception) -> JSONResponse:
        """Generic error handler so controller can throw generic exceptions and get useful messages returned to clients"""
        return JSONResponse(
            status_code = 500,
            content = {"error": str(exception)}
        )

    # Site resource endpoints
    def read_index(self, request: Request):
        """Index page"""
        return templates.TemplateResponse(
            request=request, name="index.html"
            )

    def read_javascript(self, request: Request):
        """Javascript page"""
        return templates.TemplateResponse(
            request=request, name="site.js"
            )

    def read_css(self, request: Request):
        """CSS page"""
        return templates.TemplateResponse(
            request=request, name="site.css"
            )

    # Sink Endpoints
    def set_sink_volume(self, sink_name: str, volume: float) -> bool:
        """Sets the volume for a sink"""
        return self._controller.update_sink_volume(sink_name, volume)

    def get_sinks(self) -> List[SinkDescription]:
        """Get all sinks"""
        return self._controller.get_sinks()

    def add_sink(self, sink: PostSink) -> bool:
        """Add a new sink"""
        return self._controller.add_sink(SinkDescription(sink.name, sink.ip, sink.port, False, True, [], 1))

    def add_sink_group(self, sink_group: PostSinkGroup) -> bool:
        """Add a new sink group"""
        return self._controller.add_sink(SinkDescription(sink_group.name, "", 0, True, True, sink_group.sinks, 1))

    def delete_sink(self, sink_name: str) -> bool:
        """Delete a sink group by ID"""
        return self._controller.delete_sink(sink_name)

    def disable_sink(self, sink_name: str) -> bool:
        """Disable a sink"""
        return self._controller.disable_sink(sink_name)

    def enable_sink(self, sink_name: str) -> bool:
        """Enable a sink"""
        return self._controller.enable_sink(sink_name)

    # Source Endpoints
    def set_source_volume(self, source_name: str, volume: float) -> bool:
        """Sets the volume for a source"""
        return self._controller.update_source_volume(source_name, volume)
    
    def get_sources(self) -> List[SourceDescription]:
        """Get all sources"""
        return self._controller.get_sources()

    def add_source(self, source: PostSource) -> bool:
        """Add a new source"""
        return self._controller.add_source(SourceDescription(source.name, source.ip, False, True, [], 1))

    def add_source_group(self, source_group: PostSourceGroup) -> bool:
        """Add a new source group"""
        return self._controller.add_source(SourceDescription(source_group.name, "", True, True, source_group.sources, 1))

    def delete_source(self, source_name: str) -> bool:
        """Delete a source group by name"""
        return self._controller.delete_source(source_name)

    def disable_source(self, source_name: str) -> bool:
        """Disable a source"""
        return self._controller.disable_source(source_name)

    def enable_source(self, source_name: str) -> bool:
        """Enable a source"""
        return self._controller.enable_source(source_name)

    # Route Endpoints
    def set_route_volume(self, route_name: str, volume: float) -> bool:
        """Sets the volume for a route"""
        return self._controller.update_route_volume(route_name, volume)
    
    def get_routes(self) -> List[RouteDescription]:
        """Get all routes"""
        return self._controller.get_routes()

    def add_route(self, route: PostRoute) -> bool:
        """Add a new route"""
        return self._controller.add_route(RouteDescription(route.name, route.sink, route.source, True, 1))

    def delete_route(self, route_name: str)  -> bool:
        """Delete a route by ID"""
        return self._controller.delete_route(route_name)

    def disable_route(self, route_name: str)  -> bool:
        """Disable a route"""
        return self._controller.disable_route(route_name)

    def enable_route(self, route_name: str)  -> bool:
        """Enable a route"""
        return self._controller.enable_route(route_name)