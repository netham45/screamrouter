"""API endpoints for proxying websocket connections to webpack dev server"""
import asyncio
from typing import Dict

import websockets
from fastapi import FastAPI, WebSocket

from screamrouter.screamrouter_logger.screamrouter_logger import get_logger

_logger = get_logger(__name__)

class APIWebsocketDebug():
    """Proxies websocket connections to webpack dev server"""
    def __init__(self, app: FastAPI):
        """Initialize the websocket debug proxy
        
        Args:
            app (FastAPI): The FastAPI application instance
        """
        self._active_connections: Dict[WebSocket, websockets.WebSocketClientProtocol] = {}
        """Dictionary mapping frontend connections to webpack connections"""

        app.websocket("/ws")(self._debug_websocket)
        _logger.info("[WebSocket Debug] Debug WebSocket proxy endpoint registered")

    async def _connect_to_webpack(self, websocket: WebSocket) -> websockets.WebSocketClientProtocol:
        """Connect to webpack dev server websocket
        
        Args:
            websocket: The client WebSocket connection to get headers from
            
        Returns:
            websockets.WebSocketClientProtocol: Connection to webpack dev server
        """
        headers = dict(websocket.headers)
#        _logger.debug("[WebSocket Debug] Forwarding headers: %s", headers)
        return await websockets.connect('ws://localhost:8080/ws', extra_headers=headers)

    async def _proxy_messages(self, client: WebSocket, webpack: websockets.WebSocketClientProtocol):
        """Proxy messages between client and webpack
        
        Args:
            client (WebSocket): Frontend client connection
            webpack (websockets.WebSocketClientProtocol): Webpack dev server connection
        """
        try:
            while True:
                # Forward messages in both directions
                client_msg = asyncio.create_task(client.receive_text())
                webpack_msg = asyncio.create_task(webpack.recv())
                
                done, pending = await asyncio.wait(
                    [client_msg, webpack_msg],
                    return_when=asyncio.FIRST_COMPLETED
                )

                for task in pending:
                    task.cancel()

                for task in done:
                    if task is client_msg:
                        msg = await task
                        await webpack.send(msg)
                    else:
                        msg = await task
                        await client.send_text(msg)

        except Exception as exc:
            pass
            #_logger.debug("[WebSocket Debug] Connection closed: %s", str(exc))
        finally:
            # Clean up both connections
            if client in self._active_connections:
                del self._active_connections[client]
            await webpack.close()

    async def _debug_websocket(self, websocket: WebSocket) -> None:
        """Handle websocket proxy connection lifecycle
        
        Args:
            websocket (WebSocket): The frontend websocket connection to handle
        """
        await websocket.accept()
        try:
            # Connect to webpack dev server
            webpack = await self._connect_to_webpack(websocket)
            self._active_connections[websocket] = webpack
            
            # Start proxying messages
            await self._proxy_messages(websocket, webpack)

        except Exception as exc:
            #_logger.error("[WebSocket Debug] Error in proxy: %s", str(exc))
            pass
        finally:
            if websocket in self._active_connections:
                webpack = self._active_connections[websocket]
                del self._active_connections[websocket]
                await webpack.close()
