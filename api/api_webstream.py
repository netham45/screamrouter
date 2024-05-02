"""Holds the web stream queue and API endpoints, manages serving MP3s."""
import asyncio
from typing import List, Optional

from fastapi import FastAPI, WebSocket

from fastapi.responses import StreamingResponse
from screamrouter_types import IPAddressType
from logger import get_logger

logger = get_logger(__name__)

class Listener():
    """Holds info on a single listener to send streams to"""
    def __init__(self, sink_ip: IPAddressType):
        self._sink_ip: IPAddressType = sink_ip
        """Sink IP the listener is listening to"""
        self._active: bool =  True
        """Rather the listener is active or not"""

    def send(self, sink_ip: IPAddressType, data: bytes) -> bool:
        """Implemented by classes that extend Listener"""
        # Junk return so pylint quits complaining about unused args
        return str(sink_ip) + str(len(data)) == 9001

    async def open(self) -> None:
        """Implemented by classes that extend Listener"""

class WebsocketListener(Listener):
    """Holds a single instance of an active websocket sink listener"""
    def __init__(self, sink_ip: IPAddressType, client: Optional[WebSocket]):
        super().__init__(sink_ip)
        self.__client: Optional[WebSocket] = client

    async def open(self) -> None:
        """Opens the websocket, to be called right after the listener is made"""
        if self.__client:
            await self.__client.accept()

    def send(self, sink_ip: IPAddressType, data: bytes) -> bool:
        """Send data to the websocket, returns false if the socket is dead"""
        if self._active:
            if sink_ip == self._sink_ip:
                if self.__client:
                    asyncio.run(self.__client.send_bytes(data))
        return self._active

class HTTPListener(Listener):
    """Holds a single instance of an active websocket sink listener"""
    def __init__(self, sink_ip: IPAddressType):
        super().__init__(sink_ip)
        self._queue = asyncio.Queue(200)

    async def open(self) -> None:
        """Doesn't do anything for http"""

    def send(self, sink_ip: IPAddressType, data: bytes) -> bool:
        """Send data to the websocket, returns false if the socket is dead"""
        if self._active:
            if sink_ip == self._sink_ip:
                try:
                    self._queue.put_nowait(data)
                except asyncio.queues.QueueFull:
                    self._active = False
                    logger.debug("[%s] HTTP queue full, assuming client disconnect", self._sink_ip)
        return self._active

    async def get_queue(self):
        """Returns when a queue option is available"""
        while self._active:
            data = await self._queue.get()
            yield bytes(data)

class APIWebStream():
    """Holds the main websocket controller for the API that distributes messages to listeners"""
    def __init__(self, app: FastAPI):
        self._listeners: List[Listener] = []
        app.websocket("/ws/{sink_ip}/")(self.websocket_mp3_stream)
        app.get("/stream/{sink_ip}/", tags=["Stream"])(self.http_mp3_stream)

    def sink_callback(self, sink_ip: IPAddressType, data: bytes) -> None:
        """Callback for sinks to have data sent out to websockets"""
        for _, listener in enumerate(self._listeners):
            if not listener.send(sink_ip, data):  # Returns false on receive error
                #self._listeners.remove(self._listeners[idx])
                pass

    async def websocket_mp3_stream(self, websocket: WebSocket, sink_ip: IPAddressType):
        """FastAPI handler"""
        listener: WebsocketListener = WebsocketListener(sink_ip, websocket)
        await listener.open()
        self._listeners.append(listener)
        while True:  # Keep the connection open until something external closes it.
            await asyncio.sleep(10000)

    async def http_mp3_stream(self, sink_ip: IPAddressType):
        """Streams MP3 frames from ScreamRouter"""
        listener: HTTPListener = HTTPListener(sink_ip)
        await listener.open()
        self._listeners.append(listener)
        return StreamingResponse(listener.get_queue(), media_type="audio/mpeg")
