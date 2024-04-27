import asyncio
from typing import List, Optional

from fastapi import FastAPI, WebSocket

from fastapi.responses import StreamingResponse

class Listener():
    def __init__(self, sink_ip: str):
        # TODO: Validate parameters
        self._sink_ip: str = sink_ip
        """Sink IP the listener is listening to"""
        self._active: bool =  True
        """Rather the listener is active or not"""

    async def open(self) -> None:
        """Opens the Listener"""
        pass

    def send(self, sink_ip: str, data: bytes) -> bool:
        """Sends data to a listener for processing"""
        return False

class WebsocketListener(Listener):
    """Holds a single instance of an active websocket sink listener"""
    def __init__(self, sink_ip: str, client: Optional[WebSocket]):
        super().__init__(sink_ip)
        self.__client: Optional[WebSocket] = client

    async def open(self) -> None:
        """Opens the websocket, to be called right after the listener is made"""
        if self.__client:
            await self.__client.accept()

    def send(self, sink_ip: str, data: bytes) -> bool:
        """Send data to the websocket, returns false if the socket is dead"""
        if self._active:
            if sink_ip == self._sink_ip:
                try:
                    if self.__client:
                        asyncio.run(self.__client.send_bytes(data))
                except:
                    self._active = False
        return self._active
    
class HTTPListener(Listener):
    """Holds a single instance of an active websocket sink listener"""
    def __init__(self, sink_ip: str):
        super().__init__(sink_ip)
        self._queue = asyncio.Queue(200)

    async def open(self) -> None:
        """Doesn't do anything for http"""
        pass

    def send(self, sink_ip: str, data: bytes) -> bool:
        """Send data to the websocket, returns false if the socket is dead"""
        if self._active:
            if sink_ip == self._sink_ip:
                try:
                    self._queue.put_nowait(data)
                except asyncio.queues.QueueFull as exception:
                    self._active = False
                    print(f"[{self._sink_ip}] HTTP queue full, assuming client disconnected")
        return self._active

    async def getQueue(self):
        """Returns when a queue option is available"""
        while self._active:
            data = await self._queue.get()
            yield bytes(data)

class API_Webstream():
    """Holds the main websocket controller for the API that distributes messages to listeners"""
    def __init__(self, app: FastAPI):
        self._listeners: List[Listener] = []
        app.websocket("/ws/{sink_ip}/")(self.websocket_mp3_stream)
        app.get("/stream/{sink_ip}/", tags=["Stream"])(self.http_mp3_stream)

    def sink_callback(self, sink_ip: str, data: bytes) -> None:
        """Callback for sinks to have data sent out to websockets"""
        for idx, listener in enumerate(self._listeners):
            if not listener.send(sink_ip, data):  # Returns false on receive error
                self._listeners.remove(self._listeners[idx])

    async def websocket_mp3_stream(self, websocket: WebSocket, sink_ip: str):
        """FastAPI handler"""
        listener: WebsocketListener = WebsocketListener(sink_ip, websocket)
        await listener.open()
        self._listeners.append(listener)
        while True:  # Keep the connection open until something external closes it.
            await asyncio.sleep(100)

    async def http_mp3_stream(self, sink_ip: str):
        """Streams MP3 frames from ScreamRouter"""
        listener: HTTPListener = HTTPListener(sink_ip)
        await listener.open()
        self._listeners.append(listener)
        return StreamingResponse(listener.getQueue(), media_type="audio/mpeg")