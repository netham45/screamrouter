"""
VNC WebSocket Proxy for FastAPI

This module implements a native WebSocket proxy for VNC connections,
replacing the websockify subprocess-based approach with FastAPI's
built-in WebSocket support.
"""

import asyncio
import socket
import traceback
from typing import Optional, Dict, Any
from fastapi import WebSocket, WebSocketDisconnect
from starlette.websockets import WebSocketState
from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
logger = get_logger(__name__)


class VNCWebSocketProxy:
    """
    Handles WebSocket to VNC TCP socket proxying for a single connection.
    
    This class manages:
    - TCP connection to VNC server
    - Bidirectional data forwarding between WebSocket and VNC
    - Connection lifecycle and error handling
    """
    
    def __init__(self, websocket: WebSocket, vnc_host: str, vnc_port: int):
        """
        Initialize the VNC WebSocket proxy.
        
        Args:
            websocket: FastAPI WebSocket connection
            vnc_host: VNC server hostname/IP
            vnc_port: VNC server port
        """
        self.websocket = websocket
        self.vnc_host = vnc_host
        self.vnc_port = vnc_port
        self.vnc_reader: Optional[asyncio.StreamReader] = None
        self.vnc_writer: Optional[asyncio.StreamWriter] = None
        self.running = False
        self.tasks: list[asyncio.Task] = []
        
    async def start(self):
        """
        Start the proxy connection and begin data forwarding.
        """
        try:
            # Accept the WebSocket connection
            logger.info(f"[VNC_PROXY] Attempting to accept WebSocket connection for {self.vnc_host}:{self.vnc_port}")
            await self.websocket.accept()
            logger.info(f"[VNC_PROXY] ✓ WebSocket connection accepted for VNC proxy to {self.vnc_host}:{self.vnc_port}")
            
            # Connect to VNC server
            logger.info(f"[VNC_PROXY] Initiating connection to VNC server...")
            await self._connect_to_vnc()
            
            # Start bidirectional forwarding
            self.running = True
            logger.info(f"[VNC_PROXY] Starting bidirectional data forwarding...")
            await self._start_forwarding()
            
        except Exception as e:
            logger.error(f"[VNC_PROXY] Error starting VNC WebSocket proxy: {e}")
            logger.error(f"[VNC_PROXY] Stack trace:\n{traceback.format_exc()}")
            await self._cleanup()
            raise
    
    async def _connect_to_vnc(self):
        """
        Establish TCP connection to VNC server.
        """
        try:
            logger.info(f"[VNC_PROXY] Connecting to VNC server at {self.vnc_host}:{self.vnc_port}")
            logger.debug(f"[VNC_PROXY] Host type: {type(self.vnc_host)}, Port type: {type(self.vnc_port)}")
            
            # Ensure host is a string and port is an integer
            host_str = str(self.vnc_host) if not isinstance(self.vnc_host, str) else self.vnc_host
            port_int = int(self.vnc_port) if not isinstance(self.vnc_port, int) else self.vnc_port
            
            logger.debug(f"[VNC_PROXY] Converted - Host: {host_str} (type: {type(host_str)}), Port: {port_int} (type: {type(port_int)})")
            
            self.vnc_reader, self.vnc_writer = await asyncio.open_connection(
                host_str,
                port_int
            )
            logger.info(f"[VNC_PROXY] ✓ Successfully connected to VNC server at {host_str}:{port_int}")
        except Exception as e:
            logger.error(f"[VNC_PROXY] Failed to connect to VNC server: {e}")
            logger.error(f"[VNC_PROXY] Connection details - Host: {self.vnc_host} (type: {type(self.vnc_host)}), Port: {self.vnc_port} (type: {type(self.vnc_port)})")
            raise ConnectionError(f"Could not connect to VNC server at {self.vnc_host}:{self.vnc_port}: {e}")
    
    async def _start_forwarding(self):
        """
        Start bidirectional data forwarding tasks.
        """
        try:
            logger.info(f"[VNC_PROXY] Creating bidirectional forwarding tasks...")
            
            # Create tasks for bidirectional forwarding
            ws_to_vnc_task = asyncio.create_task(
                self._forward_websocket_to_vnc(),
                name="ws_to_vnc"
            )
            vnc_to_ws_task = asyncio.create_task(
                self._forward_vnc_to_websocket(),
                name="vnc_to_ws"
            )
            
            self.tasks = [ws_to_vnc_task, vnc_to_ws_task]
            logger.info(f"[VNC_PROXY] ✓ Forwarding tasks created, starting data transfer...")
            
            # Wait for either task to complete (usually due to disconnection)
            done, pending = await asyncio.wait(
                self.tasks,
                return_when=asyncio.FIRST_COMPLETED
            )
            
            logger.info(f"[VNC_PROXY] One of the forwarding tasks completed")
            
            # Cancel any remaining tasks
            for task in pending:
                logger.debug(f"[VNC_PROXY] Cancelling pending task: {task.get_name()}")
                task.cancel()
                try:
                    await task
                except asyncio.CancelledError:
                    pass
            
            # Check if any task failed with an exception
            for task in done:
                if task.exception():
                    logger.error(f"[VNC_PROXY] Task {task.get_name()} failed: {task.exception()}")
                else:
                    logger.info(f"[VNC_PROXY] Task {task.get_name()} completed successfully")
                    
        except Exception as e:
            logger.error(f"[VNC_PROXY] Error in forwarding tasks: {e}")
            logger.error(f"[VNC_PROXY] Stack trace:\n{traceback.format_exc()}")
        finally:
            logger.info(f"[VNC_PROXY] Cleaning up connection...")
            await self._cleanup()
    
    async def _forward_websocket_to_vnc(self):
        """
        Forward data from WebSocket client to VNC server.
        """
        logger.info(f"[VNC_PROXY_WS→VNC] Starting WebSocket to VNC forwarding task")
        bytes_transferred = 0
        try:
            while self.running:
                # Receive binary data from WebSocket
                data = await self.websocket.receive_bytes()
                
                if not data:
                    logger.debug("[VNC_PROXY_WS→VNC] Received empty data from WebSocket, closing connection")
                    break
                
                # Forward to VNC server
                if self.vnc_writer:
                    self.vnc_writer.write(data)
                    await self.vnc_writer.drain()
                    bytes_transferred += len(data)
                    logger.debug(f"[VNC_PROXY_WS→VNC] Forwarded {len(data)} bytes (total: {bytes_transferred})")
                else:
                    logger.error("[VNC_PROXY_WS→VNC] VNC writer is not available")
                    break
                    
        except WebSocketDisconnect:
            logger.info(f"[VNC_PROXY_WS→VNC] WebSocket client disconnected (transferred {bytes_transferred} bytes total)")
        except Exception as e:
            logger.error(f"[VNC_PROXY_WS→VNC] Error forwarding WebSocket to VNC: {e}")
            logger.debug(f"[VNC_PROXY_WS→VNC] Stack trace:\n{traceback.format_exc()}")
        finally:
            logger.info(f"[VNC_PROXY_WS→VNC] Stopping WebSocket to VNC forwarding (transferred {bytes_transferred} bytes total)")
            self.running = False
    
    async def _forward_vnc_to_websocket(self):
        """
        Forward data from VNC server to WebSocket client.
        """
        logger.info(f"[VNC_PROXY_VNC→WS] Starting VNC to WebSocket forwarding task")
        bytes_transferred = 0
        try:
            while self.running:
                if not self.vnc_reader:
                    logger.error("[VNC_PROXY_VNC→WS] VNC reader is not available")
                    break
                
                # Read data from VNC server (up to 64KB at a time)
                data = await self.vnc_reader.read(65536)
                
                if not data:
                    logger.info("[VNC_PROXY_VNC→WS] VNC server closed connection")
                    break
                
                # Forward to WebSocket client
                if self.websocket.client_state == WebSocketState.CONNECTED:
                    await self.websocket.send_bytes(data)
                    bytes_transferred += len(data)
                    logger.debug(f"[VNC_PROXY_VNC→WS] Forwarded {len(data)} bytes (total: {bytes_transferred})")
                else:
                    logger.warning(f"[VNC_PROXY_VNC→WS] WebSocket is not in connected state: {self.websocket.client_state}")
                    break
                    
        except Exception as e:
            logger.error(f"[VNC_PROXY_VNC→WS] Error forwarding VNC to WebSocket: {e}")
            logger.debug(f"[VNC_PROXY_VNC→WS] Stack trace:\n{traceback.format_exc()}")
        finally:
            logger.info(f"[VNC_PROXY_VNC→WS] Stopping VNC to WebSocket forwarding (transferred {bytes_transferred} bytes total)")
            self.running = False
    
    async def _cleanup(self):
        """
        Clean up connections and resources.
        """
        logger.info(f"[VNC_PROXY_CLEANUP] Starting cleanup process...")
        self.running = False
        
        # Close VNC connection
        if self.vnc_writer:
            try:
                logger.debug("[VNC_PROXY_CLEANUP] Closing VNC connection...")
                self.vnc_writer.close()
                await self.vnc_writer.wait_closed()
                logger.info("[VNC_PROXY_CLEANUP] ✓ VNC connection closed")
            except Exception as e:
                logger.error(f"[VNC_PROXY_CLEANUP] Error closing VNC connection: {e}")
        
        # Close WebSocket if still open
        if self.websocket.client_state == WebSocketState.CONNECTED:
            try:
                logger.debug("[VNC_PROXY_CLEANUP] Closing WebSocket connection...")
                await self.websocket.close()
                logger.info("[VNC_PROXY_CLEANUP] ✓ WebSocket connection closed")
            except Exception as e:
                logger.error(f"[VNC_PROXY_CLEANUP] Error closing WebSocket: {e}")
        
        # Clear references
        self.vnc_reader = None
        self.vnc_writer = None
        
        logger.info(f"[VNC_PROXY_CLEANUP] ✓ Cleaned up VNC WebSocket proxy for {self.vnc_host}:{self.vnc_port}")


async def handle_vnc_websocket(
    websocket: WebSocket,
    source_name: str,
    vnc_config: Dict[str, Any]
) -> None:
    """
    Handle a VNC WebSocket connection for a specific source.
    
    Args:
        websocket: FastAPI WebSocket connection
        source_name: Name of the source requesting VNC access
        vnc_config: VNC configuration containing host and port
    """
    vnc_host = vnc_config.get("host", "localhost")
    vnc_port = vnc_config.get("port", 5900)
    
    logger.info(f"[VNC_HANDLER] Starting VNC WebSocket proxy for source '{source_name}'")
    logger.info(f"[VNC_HANDLER] VNC Config: host={vnc_config.get('host')} (type: {type(vnc_config.get('host'))}), port={vnc_config.get('port')} (type: {type(vnc_config.get('port'))})")
    
    proxy = VNCWebSocketProxy(websocket, vnc_host, vnc_port)
    
    try:
        await proxy.start()
        logger.info(f"[VNC_HANDLER] VNC WebSocket proxy completed for source '{source_name}'")
    except Exception as e:
        logger.error(f"[VNC_HANDLER] VNC WebSocket proxy error for source '{source_name}': {e}")
        logger.error(f"[VNC_HANDLER] Stack trace:\n{traceback.format_exc()}")
        # Ensure WebSocket is closed on error
        if websocket.client_state == WebSocketState.CONNECTED:
            await websocket.close(code=1011, reason=str(e))