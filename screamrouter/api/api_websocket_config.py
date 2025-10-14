"""API endpoints for websocket configuration updates that notify clients of changes"""
import asyncio
from copy import deepcopy
from typing import Dict, List, Set

from fastapi import FastAPI, WebSocket

from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
from screamrouter.screamrouter_types.configuration import (RouteDescription,
                                                  SinkDescription,
                                                  SourceDescription)

_logger = get_logger(__name__)

class APIWebsocketConfig():
    """Manages websocket connections and broadcasts configuration updates to clients"""
    def __init__(self, app: FastAPI):
        """Initialize the websocket configuration manager
        
        Args:
            app (FastAPI): The FastAPI application instance
        """
        self._active_connections: Set[WebSocket] = set()
        """Set of active websocket connections"""
        self._last_sources: Dict[str, SourceDescription] = {}
        """Last known state of sources for change detection"""
        self._last_sinks: Dict[str, SinkDescription] = {}
        """Last known state of sinks for change detection"""
        self._last_routes: Dict[str, RouteDescription] = {}
        """Last known state of routes for change detection"""

        app.websocket("/ws/config")(self._config_websocket)
        _logger.info("[WebSocket Config] Configuration WebSocket endpoint registered")

    async def _connect(self, websocket: WebSocket) -> None:
        """Accept a new websocket connection
        
        Args:
            websocket (WebSocket): The websocket connection to accept
        """
        await websocket.accept()
        self._active_connections.add(websocket)
        _logger.debug(f"[WebSocket Config] New client connected: {websocket.client.host if websocket.client else 'Unknown'}")

    def _disconnect(self, websocket: WebSocket) -> None:
        """Remove a websocket connection
        
        Args:
            websocket (WebSocket): The websocket connection to remove
        """
        self._active_connections.remove(websocket)
        _logger.debug(f"[WebSocket Config] Client disconnected: {websocket.client.host if websocket.client else 'Unknown'}")

    async def broadcast_config_update(self, sources: List[SourceDescription], 
                                    sinks: List[SinkDescription],
                                    routes: List[RouteDescription]) -> None:
        """Compare current config with last known state and broadcast changes
        
        Args:
            sources (List[SourceDescription]): Current list of sources
            sinks (List[SinkDescription]): Current list of sinks
            routes (List[RouteDescription]): Current list of routes
        """
        updates: Dict = {"sources": {}, "sinks": {}, "routes": {}, "removals": {"sources": [], "sinks": [], "routes": []}}
        do_update: bool = False

        # Convert current lists to dictionaries for easier comparison
        current_sources = {s.name: s for s in sources}
        current_sinks = {s.name: s for s in sinks if not s.is_temporary}
        current_routes = {r.name: r for r in routes if not r.is_temporary}

        # Check for additions and modifications
        for idx, source in current_sources.items():
            if self._last_sources.get(idx, None) != source:
                updates["sources"].update({idx: source.model_dump(mode='json')})
                do_update = True

        for idx, sink in current_sinks.items():
        
            if self._last_sinks.get(idx, None)  != sink:
                updates["sinks"].update({idx: sink.model_dump(mode='json')})
                do_update = True

        for idx, route in current_routes.items():
            if self._last_routes.get(idx, None) != route:
                updates["routes"].update({idx: route.model_dump(mode='json')})
                do_update = True

        # Check for removals
        for idx in self._last_sources:
            if idx not in current_sources:
                updates["removals"]["sources"].append(idx)
                do_update = True

        for idx in self._last_sinks:
            if idx not in current_sinks:
                updates["removals"]["sinks"].append(idx)
                do_update = True

        for idx in self._last_routes:
            if idx not in current_routes:
                updates["removals"]["routes"].append(idx)
                do_update = True

        # Update last known state
        self._last_sources = deepcopy(current_sources)
        self._last_sinks = deepcopy(current_sinks)
        self._last_routes = deepcopy(current_routes)

        # Only broadcast if there are changes
        if do_update:
            _logger.debug("[WebSocket Config] Broadcasting updates: %s", updates)
            disconnected: Set[WebSocket] = set()
            for connection in self._active_connections:
                try:
                    await connection.send_json(updates)
                    _logger.debug("[WebSocket Config] sent mesage")
                except Exception as exc:
                    _logger.error("[WebSocket Config] Failed to send update: %s", str(exc))
                    disconnected.add(connection)

            # Clean up disconnected clients
            for connection in disconnected:
                self._disconnect(connection)
        else:
            _logger.debug("[WebSocket Config] NOT Broadcasting updates")

    async def _config_websocket(self, websocket: WebSocket) -> None:
        """Handle websocket connection lifecycle
        
        Args:
            websocket (WebSocket): The websocket connection to handle
        """
        await self._connect(websocket)
        try:
            while True:
                # Keep connection alive
                await websocket.receive_text()
        except Exception as exc:
            _logger.debug("[WebSocket Config] Client disconnected: %s", str(exc))
        finally:
            self._disconnect(websocket)
