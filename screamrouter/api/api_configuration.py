"""API endpoints to the configuration manager"""
import traceback
from typing import Dict

from fastapi import FastAPI, HTTPException
from fastapi.responses import JSONResponse

from screamrouter.configuration.configuration_manager import ConfigurationManager
from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
from screamrouter.screamrouter_types.annotations import IPAddressType

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
            self._configuration_controller.get_permanent_sinks)
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
        self._app.get("/sinks/{sink_name}/volume_normalization/{volume_normalization}", tags=["Sink Configuration"])(
            self._configuration_controller.update_sink_volume_normalization)
        self._app.post("/sinks/{sink_name}/equalizer/", tags=["Sink Configuration"])(
            self._configuration_controller.update_sink_equalizer)
        self._app.get("/sinks/{sink_name}/reorder/{new_index}", tags=["Sink Configuration"])(
            self._configuration_controller.update_sink_position)
        self._app.get("/sinks/rtp_compatible", tags=["Sink Configuration"])(
            self.get_rtp_compatible_sinks)

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
        self._app.post("/sources/add-discovered", tags=["Source Configuration"])(
            self.add_discovered_source)

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
            self._configuration_controller.get_permanent_routes)
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
        self._app.post("/sinks/add-discovered", tags=["Sink Configuration"])(
            self.add_discovered_sink)
        self._app.get("/system_audio_devices", tags=["System Audio"])(
            self.get_system_audio_devices)
        self._app.add_exception_handler(Exception, self.__api_exception_handler)
        logger.info("[API] API loaded")

    def __api_exception_handler(self, _, exception: Exception) -> JSONResponse:
        """Error handler so controller can throw exceptions that get returned to clients"""
        return JSONResponse(
            status_code = 500,
            content = {"error": str(exception), "traceback": traceback.format_exc()}
        )

    async def add_discovered_source(self, payload: Dict[str, str]):
        """Add a discovered source to the configuration."""
        device_key = payload.get("device_key") if payload else None
        if not device_key:
            raise HTTPException(status_code=400, detail="device_key is required")

        device = self._configuration_controller.discovered_devices.get(device_key)
        if not device:
            raise HTTPException(status_code=404, detail="Discovered device not found")
        if device.role != "source":
            raise HTTPException(status_code=400, detail="Discovered device is not a source")

        try:
            if device.device_type == "per_process" or device.discovery_method == "per_process":
                if not device.tag:
                    raise HTTPException(status_code=400, detail="Per-process source missing tag")
                self._configuration_controller.auto_add_process_source(device.tag)
            else:
                target_ip = device.ip
                if device.discovery_method == "cpp_sap" and device.properties:
                    stream_ip = device.properties.get("stream_ip")
                    if isinstance(stream_ip, str) and stream_ip.strip():
                        target_ip = stream_ip.strip()

                if not target_ip:
                    raise HTTPException(status_code=400, detail="Discovered source missing IP")
                service_info = {
                    "name": device.name,
                    "port": device.port,
                    "properties": device.properties or {},
                }
                self._configuration_controller.auto_add_source(IPAddressType(target_ip), service_info)

            self._configuration_controller.discovered_devices.pop(device_key, None)
            return {"status": "ok"}
        except HTTPException:
            raise
        except Exception as exc:  # pylint: disable=broad-except
            logger.exception("Failed to add discovered source")
            raise HTTPException(status_code=500, detail="Failed to add discovered source") from exc

    async def add_discovered_sink(self, payload: Dict[str, str]):
        """Add a discovered sink to the configuration."""
        device_key = payload.get("device_key") if payload else None
        if not device_key:
            raise HTTPException(status_code=400, detail="device_key is required")

        device = self._configuration_controller.discovered_devices.get(device_key)
        if not device:
            raise HTTPException(status_code=404, detail="Discovered device not found")
        if device.role != "sink":
            raise HTTPException(status_code=400, detail="Discovered device is not a sink")

        if not device.ip:
            raise HTTPException(status_code=400, detail="Discovered sink missing IP")

        try:
            service_info = {
                "name": device.name,
                "port": device.port,
                "properties": device.properties or {},
            }
            self._configuration_controller.auto_add_sink(IPAddressType(device.ip), service_info)
            self._configuration_controller.discovered_devices.pop(device_key, None)
            return {"status": "ok"}
        except HTTPException:
            raise
        except Exception as exc:  # pylint: disable=broad-except
            logger.exception("Failed to add discovered sink")
            raise HTTPException(status_code=500, detail="Failed to add discovered sink") from exc

    def get_system_audio_devices(self):
        """Expose cached ALSA system devices for capture and playback."""
        snapshot = self._configuration_controller.get_system_audio_device_snapshot()
        return {
            "system_capture_devices": [device.model_dump(mode="json") for device in snapshot["system_capture_devices"]],
            "system_playback_devices": [device.model_dump(mode="json") for device in snapshot["system_playback_devices"]],
        }
    
    def get_rtp_compatible_sinks(self):
        """Get all configured sinks that are not groups and have protocol set to 'rtp'.
        This is used by the frontend to populate a dropdown of available receivers."""
        try:
            # Get all sinks from the configuration controller
            all_sinks = self._configuration_controller.get_sinks()
            
            # Filter for non-group sinks with RTP protocol
            rtp_compatible_sinks = [
                sink for sink in all_sinks
                if not sink.is_group and sink.protocol == "rtp"
            ]
            
            logger.info("[API] Returning %d RTP compatible sinks", len(rtp_compatible_sinks))
            return rtp_compatible_sinks
        except Exception as e:
            logger.error("[API] Error getting RTP compatible sinks: %s", str(e))
            raise
