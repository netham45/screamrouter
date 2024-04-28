from typing import List
import threading
import uvicorn

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from api.api_configuration_types import PostRoute, PostSink, PostSinkGroup, PostSource, PostSourceGroup, PostURL
from configuration.configuration_controller import SinkDescription, SourceDescription, RouteDescription, ConfigurationController

class API_Configuration(threading.Thread):

    def __init__(self, app: FastAPI, configuration_controller: ConfigurationController):
        super().__init__(name=f"API Thread")
        """Holds the active controller"""
        self._configuration_controller = configuration_controller
        """Configuration controller"""
        self._app = app
        """FastAPI"""
        
        self._app.get("/sinks", tags=["Sink Configuration"])(self.get_sinks)
        self._app.post("/groups/sinks", tags=["Sink Configuration"])(self.add_sink_group)
        self._app.post("/sinks", tags=["Sink Configuration"])(self.add_sink)
        self._app.put("/sinks", tags=["Sink Configuration"])(self.update_sink)
        self._app.delete("/sinks/{sink_name}", tags=["Sink Configuration"])(self.delete_sink)
        self._app.get("/sinks/{sink_name}/disable", tags=["Sink Configuration"])(self.disable_sink)
        self._app.get("/sinks/{sink_name}/enable", tags=["Sink Configuration"])(self.enable_sink)
        self._app.get("/sinks/{sink_name}/volume/{volume}", tags=["Sink Configuration"])(self.set_sink_volume)
        self._app.post("/sinks/{sink_name}/play/{volume}", tags=["Sink Playback"])(self.sink_play)
        
        self._app.get("/sources", tags=["Source Configuration"])(self.get_sources)
        self._app.post("/groups/sources", tags=["Source Configuration"])(self.add_source_group)
        self._app.post("/sources", tags=["Source Configuration"])(self.add_source)
        self._app.put("/sources", tags=["Source Configuration"])(self.update_source)
        self._app.delete("/sources/{source_name}", tags=["Source Configuration"])(self.delete_source)
        self._app.get("/sources/{source_name}/disable", tags=["Source Configuration"])(self.disable_source)
        self._app.get("/sources/{source_name}/enable", tags=["Source Configuration"])(self.enable_source)
        self._app.get("/sources/{source_name}/volume/{volume}", tags=["Source Configuration"])(self.set_source_volume)

        self._app.get("/routes", tags=["Route Configuration"])(self.get_routes)
        self._app.get("/routes", tags=["Route Configuration"])(self.get_routes)
        self._app.post("/routes", tags=["Route Configuration"])(self.add_route)
        self._app.put("/routes", tags=["Route Configuration"])(self.update_route)
        self._app.delete("/routes/{route_name}", tags=["Route Configuration"])(self.delete_route)
        self._app.get("/routes/{route_name}/disable", tags=["Route Configuration"])(self.disable_route)
        self._app.get("/routes/{route_name}/enable", tags=["Route Configuration"])(self.enable_route)
        self._app.get("/routes/{route_name}/volume/{volume}", tags=["Route Configuration"])(self.set_route_volume)
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

    # Sink Endpoints
    def set_sink_volume(self, sink_name: str, volume: float) -> bool:
        """Sets the volume for a sink"""
        return self._configuration_controller.update_sink_volume(sink_name, volume)
    

    def sink_play(self, url: PostURL, sink_name: str, volume: float):
        """Plays a URL"""
        return self._configuration_controller.play_url(sink_name, url.url, volume)

    def get_sinks(self) -> List[SinkDescription]:
        """Get all sinks"""
        return self._configuration_controller.get_sinks()

    def add_sink(self, sink: PostSink) -> bool:
        """Add a new sink"""
        return self._configuration_controller.add_sink(SinkDescription(sink.name, sink.ip, sink.port, False, True, [], 1, sink.bit_depth, sink.sample_rate, sink.channels, sink.channel_layout))
    
    def update_sink(self, sink: PostSink) -> bool:
        """Updaet a sink"""
        return self._configuration_controller.update_sink(SinkDescription(sink.name, sink.ip, sink.port, False, True, [], 1, sink.bit_depth, sink.sample_rate, sink.channels, sink.channel_layout))

    def add_sink_group(self, sink_group: PostSinkGroup) -> bool:
        """Add a new sink group"""
        return self._configuration_controller.add_sink(SinkDescription(sink_group.name, "", 0, True, True, sink_group.sinks, 1))

    def delete_sink(self, sink_name: str) -> bool:
        """Delete a sink group by ID"""
        return self._configuration_controller.delete_sink(sink_name)

    def disable_sink(self, sink_name: str) -> bool:
        """Disable a sink"""
        return self._configuration_controller.disable_sink(sink_name)

    def enable_sink(self, sink_name: str) -> bool:
        """Enable a sink"""
        return self._configuration_controller.enable_sink(sink_name)

    # Source Endpoints
    def set_source_volume(self, source_name: str, volume: float) -> bool:
        """Sets the volume for a source"""
        return self._configuration_controller.update_source_volume(source_name, volume)
    
    def get_sources(self) -> List[SourceDescription]:
        """Get all sources"""
        return self._configuration_controller.get_sources()

    def add_source(self, source: PostSource) -> bool:
        """Add a new source"""
        return self._configuration_controller.add_source(SourceDescription(source.name, source.ip, False, True, [], 1))

    def update_source(self, source: PostSource) -> bool:
        """Update an existing source"""
        return self._configuration_controller.update_source(SourceDescription(source.name, source.ip, False, True, [], 1))
    
    def add_source_group(self, source_group: PostSourceGroup) -> bool:
        """Add a new source group"""
        return self._configuration_controller.add_source(SourceDescription(source_group.name, "", True, True, source_group.sources, 1))

    def delete_source(self, source_name: str) -> bool:
        """Delete a source group by name"""
        return self._configuration_controller.delete_source(source_name)

    def disable_source(self, source_name: str) -> bool:
        """Disable a source"""
        return self._configuration_controller.disable_source(source_name)

    def enable_source(self, source_name: str) -> bool:
        """Enable a source"""
        return self._configuration_controller.enable_source(source_name)

    # Route Endpoints
    def set_route_volume(self, route_name: str, volume: float) -> bool:
        """Sets the volume for a route"""
        return self._configuration_controller.update_route_volume(route_name, volume)
    
    def get_routes(self) -> List[RouteDescription]:
        """Get all routes"""
        return self._configuration_controller.get_routes()

    def add_route(self, route: PostRoute) -> bool:
        """Add a new route"""
        return self._configuration_controller.add_route(RouteDescription(route.name, route.sink, route.source, True, 1))
    
    def update_route(self, route: PostRoute) -> bool:
        """Update a route"""
        return self._configuration_controller.update_route(RouteDescription(route.name, route.sink, route.source, True, 1))

    def delete_route(self, route_name: str)  -> bool:
        """Delete a route by ID"""
        return self._configuration_controller.delete_route(route_name)

    def disable_route(self, route_name: str)  -> bool:
        """Disable a route"""
        return self._configuration_controller.disable_route(route_name)

    def enable_route(self, route_name: str)  -> bool:
        """Enable a route"""
        return self._configuration_controller.enable_route(route_name)