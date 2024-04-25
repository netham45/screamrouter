import asyncio
from typing import List, Optional

from fastapi import WebSocket

import queue

from fastapi.responses import StreamingResponse

def genHeader(sampleRate, bitsPerSample, channels, samples):
    datasize = samples * channels * bitsPerSample // 8
    o = bytes("RIFF",'ascii')                                               # (4byte) Marks file as RIFF
    o += (datasize + 36).to_bytes(4,'little')                               # (4byte) File size in bytes excluding this and RIFF marker
    o += bytes("WAVE",'ascii')                                              # (4byte) File type
    o += bytes("fmt ",'ascii')                                              # (4byte) Format Chunk Marker
    o += (16).to_bytes(4,'little')                                          # (4byte) Length of above format data
    o += (1).to_bytes(2,'little')                                           # (2byte) Format type (1 - PCM)
    o += (channels).to_bytes(2,'little')                                    # (2byte)
    o += (sampleRate).to_bytes(4,'little')                                  # (4byte)
    o += (sampleRate * channels * bitsPerSample // 8).to_bytes(4,'little')  # (4byte)
    o += (channels * bitsPerSample // 8).to_bytes(2,'little')               # (2byte)
    o += (bitsPerSample).to_bytes(2,'little')                               # (2byte)
    o += bytes("data",'ascii')                                              # (4byte) Data Chunk Marker
    o += (datasize).to_bytes(4,'little')                                    # (4byte) Data size in bytes
    return o

class Listener():
    """Holds a single instance of an active websocket sink listener"""
    def __init__(self, sink_ip: str, client: Optional[WebSocket]):
        # TODO: Validate parameters
        self.__sink_ip: str = sink_ip
        self.__client: Optional[WebSocket] = client
        self.__active: bool =  True


    async def open_client(self):
        """Opens the websocket, to be called right after the listener is made"""
        if self.__client:
            await self.__client.accept()

    def send(self, sink_ip: str, data: bytes) -> bool:
        """Send data to the websocket, returns false if the socket is dead"""
        if self.__active:
            if sink_ip == self.__sink_ip:
                try:
                    if self.__client:
                        asyncio.run(self.__client.send_bytes(data))
                except:
                    self.__active = False
        return self.__active
    
class HTTPListener(Listener):
    """Holds a single instance of an active websocket sink listener"""
    def __init__(self, sink_ip: str):
        # TODO: Validate parameters
        super().__init__(sink_ip, None)
        self.__queue = queue.Queue()
        self.__active = True
        self.__sink_ip = sink_ip

    async def open_client(self):
        """Opens the websocket, to be called right after the listener is made"""
        pass

    def send(self, sink_ip: str, data: bytes) -> bool:
        """Send data to the websocket, returns false if the socket is dead"""
        if self.__active:
            if sink_ip == self.__sink_ip:
                self.__queue.put(data)
        return self.__active

    async def getQueue(self):
        while self.__active:
            while not self.__queue.empty():
                yield bytes(self.__queue.get())
            await asyncio.sleep(.01)
        

    
    

class API_webstream():
    """Holds the main websocket controller for the API that distributes messages to listeners"""
    def __init__(self):
        self.__listeners: List[Listener] = []
        pass

    def sink_callback(self, sink_ip: str, data: bytes) -> None:
        """Callback for sinks to have data sent out to websockets"""
        for listener in self.__listeners:
            if not listener.send(sink_ip, data):  # Returns false on receive error
                self.__listeners.remove(listener)
    
    def add_listener(self, sink_ip: str, client) -> None:
        """Add a new websocket to send data to"""
        self.__listeners.append(Listener(sink_ip, client))

    async def websocket_api_handler(self, websocket: WebSocket, sink_ip: str):
        """FastAPI handler"""
        listener: Listener = Listener(sink_ip, websocket)
        await listener.open_client()
        self.__listeners.append(listener)
        while True:
            await asyncio.sleep(100)

    async def http_api_handler(self, sink_ip: str):
        """FastAPI handler"""
        listener: HTTPListener = HTTPListener(sink_ip)
        await listener.open_client()
        self.__listeners.append(listener)
        return StreamingResponse(listener.getQueue(), media_type="audio/mpeg")