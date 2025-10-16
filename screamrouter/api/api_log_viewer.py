"""API endpoints for log file viewing and streaming"""
import asyncio
import os
import json
from pathlib import Path
from typing import Dict, Set, Optional, List
from datetime import datetime

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.responses import JSONResponse
from starlette.websockets import WebSocketState

from screamrouter.constants import constants
from screamrouter.screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)


class LogFileWatcher:
    """Watches a log file for new content and notifies listeners"""
    
    def __init__(self, filepath: str):
        """Initialize the log file watcher
        
        Args:
            filepath: Path to the log file to watch
        """
        self.filepath = filepath
        self.position = 0
        self.listeners: Set[WebSocket] = set()
        self.watching = False
        self._watch_task: Optional[asyncio.Task] = None
        
    async def add_listener(self, websocket: WebSocket, initial_lines: int):
        """Add a WebSocket listener and send initial content
        
        Args:
            websocket: WebSocket connection to add
            initial_lines: Number of initial lines to send (-1 for all)
        """
        self.listeners.add(websocket)
        
        # Send initial lines
        try:
            initial_content = self._read_initial_lines(initial_lines)
            await websocket.send_json({
                "type": "initial",
                "lines": initial_content
            })
        except Exception as e:
            logger.error(f"Error sending initial lines: {e}")
            
        # Start watching if not already watching
        if not self.watching:
            self.watching = True
            self._watch_task = asyncio.create_task(self._watch_file())
            
    def remove_listener(self, websocket: WebSocket):
        """Remove a WebSocket listener
        
        Args:
            websocket: WebSocket connection to remove
        """
        self.listeners.discard(websocket)
        
        # Stop watching if no more listeners
        if not self.listeners and self.watching:
            self.watching = False
            if self._watch_task:
                self._watch_task.cancel()
                
    def _read_initial_lines(self, line_count: int) -> List[str]:
        """Read initial lines from the file
        
        Args:
            line_count: Number of lines to read (-1 for all)
            
        Returns:
            List of lines from the file
        """
        lines = []
        try:
            with open(self.filepath, 'r', encoding='utf-8', errors='replace') as f:
                if line_count == -1:
                    # Read all lines
                    lines = f.readlines()
                else:
                    # Read file backwards to get last N lines efficiently
                    f.seek(0, 2)  # Go to end of file
                    file_size = f.tell()
                    
                    # Estimate bytes to read (assuming ~100 chars per line)
                    bytes_to_read = min(file_size, line_count * 100)
                    f.seek(max(0, file_size - bytes_to_read))
                    
                    # Read and get last N lines
                    content = f.read()
                    all_lines = content.splitlines()
                    lines = all_lines[-line_count:] if len(all_lines) > line_count else all_lines
                    
                # Update position to end of file
                f.seek(0, 2)
                self.position = f.tell()
                
        except FileNotFoundError:
            logger.warning(f"Log file not found: {self.filepath}")
        except Exception as e:
            logger.error(f"Error reading initial lines from {self.filepath}: {e}")
            
        return [line.rstrip() for line in lines]
        
    async def _watch_file(self):
        """Watch the file for new content"""
        logger.info(f"Starting to watch file: {self.filepath}")
        
        while self.watching:
            try:
                # Check if file exists
                if not os.path.exists(self.filepath):
                    await asyncio.sleep(1)
                    continue
                    
                with open(self.filepath, 'r', encoding='utf-8', errors='replace') as f:
                    # Check if file was truncated/rotated
                    f.seek(0, 2)
                    current_size = f.tell()
                    
                    if current_size < self.position:
                        # File was truncated or rotated, reset position
                        logger.info(f"File {self.filepath} was truncated/rotated")
                        self.position = 0
                        
                    # Seek to last position
                    f.seek(self.position)
                    
                    # Read new lines
                    new_lines = []
                    for line in f:
                        new_lines.append(line.rstrip())
                        
                    # Update position
                    self.position = f.tell()
                    
                    # Send new lines to all listeners
                    if new_lines:
                        await self._broadcast_lines(new_lines)
                        
            except Exception as e:
                logger.error(f"Error watching file {self.filepath}: {e}")
                
            # Wait before checking again
            await asyncio.sleep(0.5)
            
        logger.info(f"Stopped watching file: {self.filepath}")
        
    async def _broadcast_lines(self, lines: List[str]):
        """Broadcast new lines to all listeners
        
        Args:
            lines: Lines to broadcast
        """
        disconnected = set()
        
        for websocket in self.listeners:
            try:
                if websocket.client_state == WebSocketState.CONNECTED:
                    for line in lines:
                        await websocket.send_json({
                            "type": "append",
                            "line": line
                        })
                else:
                    disconnected.add(websocket)
            except Exception as e:
                logger.error(f"Error sending to websocket: {e}")
                disconnected.add(websocket)
                
        # Remove disconnected listeners
        for ws in disconnected:
            self.remove_listener(ws)


class APILogViewer:
    """API endpoints for log file viewing and streaming"""
    
    def __init__(self, app: FastAPI):
        """Initialize the log viewer API
        
        Args:
            app: FastAPI application instance
        """
        self.app = app
        self.file_watchers: Dict[str, LogFileWatcher] = {}
        self.paused_connections: Set[WebSocket] = set()
        
        # Register endpoints
        app.get("/api/logs", tags=["Logs"])(self.list_logs)
        app.websocket("/ws/logs/{filename}/{initial_lines}")(self.websocket_handler)
        
        logger.info("[LogViewer] API endpoints registered")
        
    async def list_logs(self) -> JSONResponse:
        """List available log files
        
        Returns:
            JSON response with list of log files and metadata
        """
        logs = []
        logs_dir = constants.LOGS_DIR
        
        # Debug logging to see what path we're actually using
        logger.info(f"[LogViewer] Listing logs from directory: {logs_dir}")
        logger.info(f"[LogViewer] Directory exists: {os.path.exists(logs_dir)}")
        
        try:
            if os.path.exists(logs_dir):
                logger.info(f"[LogViewer] Found {len(os.listdir(logs_dir))} files in {logs_dir}")
                for filename in os.listdir(logs_dir):
                    if filename.endswith('.log'):
                        filepath = os.path.join(logs_dir, filename)
                        try:
                            stat = os.stat(filepath)
                            
                            # Count lines (efficiently for small files)
                            line_count = 0
                            if stat.st_size < 10 * 1024 * 1024:  # Only count lines for files < 10MB
                                with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
                                    line_count = sum(1 for _ in f)
                                    
                            logs.append({
                                "filename": filename,
                                "size": stat.st_size,
                                "modified": datetime.fromtimestamp(stat.st_mtime).isoformat(),
                                "lines": line_count
                            })
                        except Exception as e:
                            logger.error(f"Error getting stats for {filename}: {e}")
                            
                # Sort by modified time (newest first)
                logs.sort(key=lambda x: x["modified"], reverse=True)
                
        except Exception as e:
            logger.error(f"Error listing log files: {e}")
            
        return JSONResponse(content={"logs": logs})
        
    async def websocket_handler(self, websocket: WebSocket, filename: str, initial_lines: str):
        """Handle WebSocket connections for log streaming
        
        Args:
            websocket: WebSocket connection
            filename: Name of the log file to stream
            initial_lines: Number of initial lines to send (100, 500, 2000, or -1 for all)
        """
        # Validate filename to prevent directory traversal
        if "/" in filename or "\\" in filename or ".." in filename:
            await websocket.close(code=1008, reason="Invalid filename")
            return
            
        # Validate and parse initial_lines
        try:
            lines_count = int(initial_lines)
            if lines_count not in [100, 500, 2000, -1]:
                lines_count = 100  # Default to 100 if invalid
        except ValueError:
            lines_count = 100
            
        filepath = os.path.join(constants.LOGS_DIR, filename)
        
        # Check if file exists
        if not os.path.exists(filepath):
            await websocket.close(code=1008, reason=f"Log file not found: {filename}")
            return
            
        await websocket.accept()
        logger.info(f"[LogViewer] WebSocket connected for file: {filename}, initial_lines: {lines_count}")
        
        # Get or create file watcher
        if filepath not in self.file_watchers:
            self.file_watchers[filepath] = LogFileWatcher(filepath)
            
        watcher = self.file_watchers[filepath]
        
        try:
            # Add this connection as a listener
            await watcher.add_listener(websocket, lines_count)
            
            # Handle incoming messages (pause/resume)
            while True:
                try:
                    message = await websocket.receive_json()
                    
                    if message.get("action") == "pause":
                        self.paused_connections.add(websocket)
                        watcher.remove_listener(websocket)
                        logger.debug(f"[LogViewer] Paused streaming for {filename}")
                        
                    elif message.get("action") == "resume":
                        self.paused_connections.discard(websocket)
                        await watcher.add_listener(websocket, 0)  # Don't resend initial lines
                        logger.debug(f"[LogViewer] Resumed streaming for {filename}")
                        
                except WebSocketDisconnect:
                    break
                except json.JSONDecodeError:
                    logger.warning("[LogViewer] Received invalid JSON from client")
                except Exception as e:
                    logger.error(f"[LogViewer] Error handling message: {e}")
                    break
                    
        except Exception as e:
            logger.error(f"[LogViewer] WebSocket error: {e}")
            
        finally:
            # Clean up
            watcher.remove_listener(websocket)
            self.paused_connections.discard(websocket)
            
            # Remove watcher if no more listeners
            if not watcher.listeners:
                del self.file_watchers[filepath]
                
            logger.info(f"[LogViewer] WebSocket disconnected for file: {filename}")
            
            # Ensure websocket is closed
            if websocket.client_state == WebSocketState.CONNECTED:
                await websocket.close()