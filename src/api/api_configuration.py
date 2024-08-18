"""API endpoints to the configuration manager"""
import traceback

from fastapi import FastAPI
from fastapi.responses import JSONResponse

from src.configuration.configuration_manager import ConfigurationManager
from src.screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)

class APIConfiguration():
    """API endpoints to the configuration manager"""
    def __init__(self, app: FastAPI, configuration_controller: ConfigurationManager):
        """Holds the active configuration"""
        self._configuration_controller = configuration_controller
        """Configuration manager"""
        self._app = app
        """FastAPI"""

        # Sink Configuration
        self._app.get("/sinks", tags=["Sink Configuration"])(
            self._configuration_controller.get_sinks)
        self._app.post("/sinks", tags=["Sink Configuration"])(
            self._configuration_controller.add_sink)
        self._app.put("/sinks/{old_sink_name}", tags=["Sink Configuration"])(
            self._configuration_controller.update_sink)
        self._app.delete("/sinks/{sink_name}", tags=["Sink Configuration"])(
            self._configuration_controller.delete_sink)
        self._app.get("/sinks/{sink_name}/disable", tags=["Sink Configuration"])(
            self._configuration_controller.disable_sink)
        self._app.get("/sinks/{sink_name}/enable", tags=["Sink Configuration"])(
            self._configuration_controller.enable_sink)
        self._app.get("/sinks/{sink_name}/volume/{volume}", tags=["Sink Configuration"])(
            self._configuration_controller.update_sink_volume)
        self._app.get("/sinks/{sink_name}/delay/{delay}", tags=["Sink Configuration"])(
            self._configuration_controller.update_sink_delay)
        self._app.get("/sinks/{sink_name}/timeshift/{timeshift}", tags=["Sink Configuration"])(
            self._configuration_controller.update_sink_timeshift)
        self._app.post("/sinks/{sink_name}/equalizer/", tags=["Sink Configuration"])(
            self._configuration_controller.update_sink_equalizer)
        self._app.get("/sinks/{sink_name}/reorder/{new_index}", tags=["Sink Configuration"])(
            self._configuration_controller.update_sink_position)

        # Source Configuration
        self._app.get("/sources", tags=["Source Configuration"])(
            self._configuration_controller.get_sources)
        self._app.post("/sources", tags=["Source Configuration"])(
            self._configuration_controller.add_source)
        self._app.put("/sources/{old_source_name}", tags=["Source Configuration"])(
            self._configuration_controller.update_source)
        self._app.delete("/sources/{source_name}", tags=["Source Configuration"])(
            self._configuration_controller.delete_source)
        self._app.get("/sources/{source_name}/disable", tags=["Source Configuration"])(
            self._configuration_controller.disable_source)
        self._app.get("/sources/{source_name}/enable", tags=["Source Configuration"])(
            self._configuration_controller.enable_source)
        self._app.get("/sources/{source_name}/volume/{volume}", tags=["Source Configuration"])(
            self._configuration_controller.update_source_volume)
        self._app.get("/sources/{source_name}/delay/{delay}", tags=["Source Configuration"])(
            self._configuration_controller.update_source_delay)
        self._app.get("/sources/{source_name}/timeshift/{timeshift}", tags=["Source Configuration"])(
            self._configuration_controller.update_source_timeshift)
        self._app.post("/sources/{source_name}/equalizer", tags=["Source Configuration"])(
            self._configuration_controller.update_source_equalizer)
        self._app.get("/sources/{source_name}/play", tags=["Source Configuration"])(
            self._configuration_controller.source_play)
        self._app.get("/sources/{source_name}/nexttrack", tags=["Source Configuration"])(
            self._configuration_controller.source_next_track)
        self._app.get("/sources/{source_name}/prevtrack", tags=["Source Configuration"])(
            self._configuration_controller.source_previous_track)
        self._app.get("/sources/{source_name}/reorder/{new_index}", tags=["Source Configuration"])(
            self._configuration_controller.update_source_position)

        # Self Source Configuration
        self._app.get("/sources_self/volume/{volume}", tags=["Source Configuration"])(
            self._configuration_controller.update_self_source_volume)
        self._app.get("/sources_self/play", tags=["Source Configuration"])(
            self._configuration_controller.source_self_play)
        self._app.get("/sources_self/nexttrack", tags=["Source Configuration"])(
            self._configuration_controller.source_self_next_track)
        self._app.get("/sources_self/prevtrack", tags=["Source Configuration"])(
            self._configuration_controller.source_self_previous_track)

        # Route Configuration
        self._app.get("/routes", tags=["Route Configuration"])(
            self._configuration_controller.get_routes)
        self._app.post("/routes", tags=["Route Configuration"])(
            self._configuration_controller.add_route)
        self._app.put("/routes/{old_route_name}", tags=["Route Configuration"])(
            self._configuration_controller.update_route)
        self._app.delete("/routes/{route_name}", tags=["Route Configuration"])(
            self._configuration_controller.delete_route)
        self._app.get("/routes/{route_name}/disable", tags=["Route Configuration"])(
            self._configuration_controller.disable_route)
        self._app.get("/routes/{route_name}/enable", tags=["Route Configuration"])(
            self._configuration_controller.enable_route)
        self._app.get("/routes/{route_name}/volume/{volume}", tags=["Route Configuration"])(
            self._configuration_controller.update_route_volume)
        self._app.get("/routes/{route_name}/delay/{delay}", tags=["Route Configuration"])(
            self._configuration_controller.update_route_delay)
        self._app.get("/routes/{route_name}/timeshift/{timeshift}", tags=["Route Configuration"])(
            self._configuration_controller.update_route_timeshift)
        self._app.post("/routes/{route_name}/equalizer/", tags=["Route Configuration"])(
            self._configuration_controller.update_route_equalizer)
        self._app.get("/routes/{route_name}/reorder/{new_index}", tags=["Route Configuration"])(
            self._configuration_controller.update_route_position)
        self._app.add_exception_handler(Exception, self.__api_exception_handler)
        logger.info("[API] API loaded")

    def __api_exception_handler(self, _, exception: Exception) -> JSONResponse:
        """Error handler so controller can throw exceptions that get returned to clients"""
        return JSONResponse(
            status_code = 500,
            content = {"error": str(exception.args[1]), "traceback": traceback.format_exc()}
        )
