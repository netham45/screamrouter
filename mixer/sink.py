import asyncio
import os
import queue
import select
import tempfile

import threading
import socket

import io
import traceback

from typing import List, Optional

from mixer.ffmpeg import ffmpeg
from mixer.sinkinputqueue import SinkInputQueue, SinkInputQueueEntry
from mixer.sourceinfo import SourceInfo
from mixer.streaminfo import StreamInfo

from controller_types import SourceDescription as ControllerSource

from api_webstream import API_webstream

import mixer.mp3 as mp3


class sink_mp3_thread(threading.Thread):
    """Handles listening for MP3 output from ffmpeg"""
    def __init__(self, fifo_in: str, sink_ip: str, webstream: Optional[API_webstream]):
        super().__init__(name=f"[{sink_ip}] MP3 Thread")
        self.__fifo_in: str = fifo_in
        self.__sink_ip: str = sink_ip
        self.__webstream: Optional[API_webstream] = webstream
        self.__running: bool = True
        self.__fd: io.BufferedReader
        self.__make_in_fifo()  # Make python -> ffmpeg fifo fifo
        self.start()

    def __make_in_fifo(self) -> bool:
        """Makes fifo in for ffmpeg to send back to python"""
        try:
            try:
                os.remove(self.__fifo_in)
            except:
                pass
            os.mkfifo(self.__fifo_in)
            return True
        except:
            print(traceback.format_exc())
            return False
        
    def stop(self) -> None:
        """Stop"""
        self.__running = False
        try:
            self.__fd.close()
        except:
            pass
        try:
            os.remove(self.__fifo_in)
        except:
            pass

    def run(self) -> None:
        fd = os.open(self.__fifo_in, os.O_RDONLY| os.O_NONBLOCK)
        self.__fd = open(fd, 'rb')
        header_length:int = 4
        junk = self.__fd.read(45)
        output = bytearray()
        targetcount: int = 1
        count: int = 0
        while self.__running:
            ready = select.select([self.__fd], [], [], .2)
            try:
                header: bytes = self.__fd.read(header_length)
            except ValueError:
                return
            if header == None:
                continue
            if len(header) == 0:
                continue
            tag: bytes = header[0:3]
            if tag == "ID3".encode():
                bytes_to_read: int  = 41
                while bytes_to_read > 0:
                    junkdata: bytes = self.__fd.read(bytes_to_read)
                    bytes_to_read = bytes_to_read - len(junkdata)
                header = self.__fd.read(header_length)
                if header == None:
                    continue
            output.extend(header)
            try:
                header_data: mp3.MP3header = mp3.MP3header(header)
            except Exception as e:
                if self.__running:
                    print(f"Got bad header {e}")
                return
            data: bytearray = bytearray()
            framelength = int(header_data.framelength)
            while len(data) < framelength:
                datain = self.__fd.read(framelength)
                if datain != None:
                    data.extend(datain)
                framelength = framelength - len(data)
            output.extend(data)
            count = count + 1
            if self.__webstream and count == targetcount:
                count = 0
                self.__webstream.sink_callback(self.__sink_ip, output)
                output = bytearray()
        print(f"[Sink {self.__sink_ip}] MP3 thread exit")

class sink_pcm_thread(threading.Thread):
    """Handles listening for PCM output from ffmpeg"""
    def __init__(self, fifo_in: str, sink_ip: str):
        super().__init__()
        self.__fifo_in: str = fifo_in
        self._sink_ip: str = sink_ip
        self.__running: bool = True
        self.__output_header = bytes([0x01, 0x20, 0x02, 0x03, 0x00])  # 48khz, 32-bit, stereo
        self.__sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.__fd: io.BufferedReader
        """Output socket for sink"""
        """Holds the header added onto packets sent to Scream receivers"""
        self.__make_in_fifo()  # Make python -> ffmpeg fifo fifo
        self.start()

    def __make_in_fifo(self) -> bool:
        """Makes fifo in for ffmpeg to send back to python"""
        try:
            try:
                os.remove(self.__fifo_in)
            except:
                pass
            os.mkfifo(self.__fifo_in)
            return True
        except:
            print(traceback.format_exc())
            return False
        
    def stop(self) -> None:
        self.__sock.close()
        try:
            self.__fd.close()
        except:
            pass
        try:
            os.remove(self.__fifo_in)
        except:
            pass
        self.__running = False

    def __check_ffmpeg_packet(self, data: bytes) -> bool:
        """Verifies a packet is the right length"""
        if len(data) != 1152:
            #print(f"[Sink {self._sink_ip}] Got bad packet length {len(data)} != 1152 from source")
            return False
        return True

    def run(self) -> None:
        """This thread implements listening to self.fifoin and sending it out to dest_ip
           Scream Source -> Receiver -> Sink Handler -> Sources -> Pipe -> FFMPEG -> Pipe -> Python -> Scream Sink
                                                                                               ^
                                                                                          You are here                   
        """
        fd = os.open(self.__fifo_in, os.O_RDONLY | os.O_NONBLOCK)
        self.__fd = open(fd, 'rb')
        
        while self.__running:
            data = bytearray()
            try:
                try:
                    ready = select.select([self.__fd], [], [], .1)
                    if ready[0]:
                        target = 1152
                        while target > 0:
                            datain = self.__fd.read(target)
                            if datain is None:
                                continue
                            data.extend(datain)  # Read 1152 bytes from ffmpeg
                            target = target - len(data)
                    else:
                        continue
                except:
                    continue
                if self.__check_ffmpeg_packet(data):
                    sendbuf = self.__output_header + data  # Add the header to the data
                    self.__sock.sendto(sendbuf, (self._sink_ip, 4010))  # Send it to the sink
            except Exception as e:
                print(traceback.format_exc())
        print(f"[Sink {self._sink_ip}] PCM thread exit")
    

class Sink():
    """Handles ffmpeg, keeps a list of it's own sources, sends passed data to the appropriate pipe"""
    def __init__(self, sink_ip: str, sources: List[ControllerSource], websocket: Optional[API_webstream]):
        """Initialize a sink"""
        
        super().__init__()
        self._sink_ip: str = sink_ip
        """Sink IP"""
        self.__controller_sources: List[ControllerSource] = sources
        """Sources this Sink has"""
        self.__sources: List[SourceInfo] = []
        """Sources this Sink is playing"""
        self.__temp_path: str = tempfile.gettempdir() + f"/scream-{self._sink_ip}-"
        """Per-sink temp path"""
        self.__fifo_in_pcm: str = self.__temp_path + "in-pcm"
        self.__fifo_in_mp3: str = self.__temp_path + "in-mp3"
        """Input file from ffmpeg"""
        self.__ffmpeg: ffmpeg = ffmpeg(self._sink_ip, self.__fifo_in_pcm, self.__fifo_in_mp3, self.__get_open_sources())
        """ffmpeg handler"""
        self.__webstream: Optional[API_webstream] = websocket
        """Holds the websock object to copy audio to, passed through to MP3 listener thread"""
        self.__pcm_thread: sink_pcm_thread = sink_pcm_thread(self.__fifo_in_pcm, self._sink_ip)
        """Holds the thread to listen to PCM output from ffmpeg"""
        self.__mp3_thread: sink_mp3_thread = sink_mp3_thread(self.__fifo_in_mp3, self._sink_ip, self.__webstream)
        """Holds the thread to listen to MP3 output from ffmpeg"""
        self.__queue_thread: SinkInputQueue = SinkInputQueue(self.process_packet_from_queue)
        """Holds the thread to listen to the input queue and send it to ffmpeg"""

        for source in self.__controller_sources:
            self.__sources.append(SourceInfo(source.ip, self.__temp_path + source.ip, self._sink_ip, source.volume))
    
    def update_source_volume(self, controllersource: ControllerSource) -> None:
        """Updates the source volume to the specified volume, does nothing if the source is not playing to this sink."""
        for source in self.__sources:
            if source._ip == controllersource.ip:
                source.volume = controllersource.volume
                self.__ffmpeg.set_source_volume(source, controllersource.volume)

    def __get_open_sources(self) -> List[SourceInfo]:
        """Build a list of active IPs, exclude ones that aren't open"""
        active_sources: List[SourceInfo] = []
        for source in self.__sources:
            if source.is_open():
                active_sources.append(source)
        return active_sources
    
    def __get_source_by_ip(self, ip: str) -> tuple[SourceInfo, bool]:
        """Gets a SourceInfo by IP address"""
        for source in self.__sources:
            if source._ip == ip:
                return (source, True)
        return (None, False)  # type: ignore

    def __check_for_inactive_sources(self) -> None:
        """Looks for old pipes that are open and closes them"""
        for source in self.__sources:
            active_time: int = 500  # Time in MS  300ms
            if len(self.__get_open_sources()) == 1:  # Don't close the last pipe until more time has passed
                active_time = 2000000  # 1s
            if not source.is_active(active_time) and source.is_open():
                print(f"[Sink {self._sink_ip} Source {source._ip}] Closing (Timeout = {active_time}ms)")
                source.close()
                #self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())

    def __update_source_attributes_and_open_source(self, source: SourceInfo, header: bytes) -> None:
        """Verifies the target pipe header matches what we have, updates it if not. Also opens the pipe."""
        try:
            parsed_scream_header = StreamInfo(header)
        except:
            # Got an invalid header. This can just be ignored for now.
            return

        if not source.check_attributes(parsed_scream_header):
            print(f"[Sink {self._sink_ip} Source {source._ip}] Closing (Stream attribute change detected. Was: {source._stream_attributes.bit_depth}-bit at {source._stream_attributes.sample_rate}kHz, is now {parsed_scream_header.bit_depth}-bit at {parsed_scream_header.sample_rate}kHz.)")
            source.set_attributes(parsed_scream_header)
            source.close()
        if not source.is_open():
            source.open()
            self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())

    def process_packet_from_queue(self, entry: SinkInputQueueEntry) -> None:
        source: SourceInfo
        result: bool
        source, result = self.__get_source_by_ip(entry.source_ip)
        if result:
            self.__check_for_inactive_sources()
            self.__update_source_attributes_and_open_source(source, entry.data[:5])
            source.write(entry.data[5:])  # Write the data to the output fifo

    def process_packet_from_receiver(self, source_ip: str, data: bytes) -> None:
        """Sends data from the main receiver to ffmpeg after verifying it's on our list
           Scream Source -> Receiver -> Sink Handler -> Sources -> Pipe -> FFMPEG -> Pipe -> Python -> Scream Sink
                                             ^
                                        You are here                   
        """
        self.__queue_thread.queue(SinkInputQueueEntry(source_ip, data))

    def stop(self) -> None:
        """Stops the Sink, closes all handles"""
        print(f"[Sink {self._sink_ip}] Stopping PCM")
        self.__pcm_thread.stop()
        self.__pcm_thread.join()
        print(f"[Sink {self._sink_ip}] Stopping MP3")
        self.__mp3_thread.stop()
        self.__mp3_thread.join()
        print(f"[Sink {self._sink_ip}] Stopping Queue")
        self.__queue_thread.stop()
        self.__queue_thread.join()
        print(f"[Sink {self._sink_ip}] Stopping ffmpeg")
        self.__ffmpeg.stop()
        self.__ffmpeg.join()
        print(f"[Sink {self._sink_ip}] Stopped")