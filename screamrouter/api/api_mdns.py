"""API endpoints for mDNS discovery information."""
from fastapi import FastAPI, HTTPException

from screamrouter.configuration.configuration_manager import ConfigurationManager
from screamrouter.screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)


class APIMdns:
    """Expose mDNS discovery data via the REST API."""

    def __init__(self, app: FastAPI, configuration_manager: ConfigurationManager):
        self._app = app
        self._configuration_manager = configuration_manager
        self._app.add_api_route(
            "/mdns/devices",
            self.get_devices,
            methods=["GET"],
            tags=["mDNS"],
        )
        self._app.add_api_route(
            "/discovery/snapshot",
            self.get_discovery_snapshot,
            methods=["GET"],
            tags=["mDNS"],
        )

    async def get_devices(self):
        """Return the current mDNS discovery snapshot."""
        try:
            return self._configuration_manager.get_mdns_snapshot()
        except Exception as exc:  # pylint: disable=broad-except
            logger.exception("Failed to retrieve mDNS snapshot")
            raise HTTPException(status_code=500, detail="Failed to retrieve mDNS snapshot") from exc

    async def get_discovery_snapshot(self):
        """Return the unified discovery snapshot across discovery methods."""
        try:
            return self._configuration_manager.get_discovery_snapshot()
        except Exception as exc:  # pylint: disable=broad-except
            logger.exception("Failed to retrieve discovery snapshot")
            raise HTTPException(status_code=500, detail="Failed to retrieve discovery snapshot") from exc
