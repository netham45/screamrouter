"""API endpoints for mDNS discovery information."""
import asyncio

from fastapi import FastAPI, HTTPException, Query

from screamrouter.configuration.configuration_manager import ConfigurationManager
from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
from screamrouter.utils.mdns_router_service_browser import discover_router_services

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
        self._app.add_api_route(
            "/discovery/unmatched",
            self.get_unmatched_discovered_devices,
            methods=["GET"],
            tags=["mDNS"],
        )
        self._app.add_api_route(
            "/mdns/router-services",
            self.get_router_services,
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

    async def get_unmatched_discovered_devices(self):
        """Return discovered devices that are not yet mapped to configured entities."""
        try:
            return self._configuration_manager.get_unmatched_discovered_devices()
        except Exception as exc:  # pylint: disable=broad-except
            logger.exception("Failed to retrieve unmatched discovery list")
            raise HTTPException(status_code=500, detail="Failed to retrieve unmatched discovery list") from exc

    async def get_router_services(self, timeout: float = Query(2.0, ge=0.5, le=10.0)):
        """Active scan for `_screamrouter._tcp` services and return the results."""
        try:
            services = await asyncio.to_thread(discover_router_services, timeout)
            return {"timeout": timeout, "services": services}
        except Exception as exc:  # pylint: disable=broad-except
            logger.exception("Failed to discover router services")
            raise HTTPException(status_code=500, detail="Failed to discover router services") from exc
