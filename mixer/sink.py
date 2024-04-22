import os
import tempfile

import threading
import socket

import io
import traceback

from typing import List

from mixer.ffmpeg import ffmpeg
from mixer.sourceinfo import SourceInfo
from mixer.streaminfo import StreamInfo

from controller_types import SourceDescription as ControllerSource

class Sink(threading.Thread):
    """Handles ffmpeg, keeps a list of it's own sources, sends passed data to the appropriate pipe"""
    def __init__(self, sink_ip: str, sources: List[ControllerSource]):
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
        self.__fifo_in: str = self.__temp_path + "in"
        """Input file from ffmpeg"""
        self.__running: bool = True
        """Rather the Sink is running, when set to false the thread ends and the sink is done"""
        self.__ffmpeg: ffmpeg = ffmpeg(self._sink_ip, self.__fifo_in, self.__get_open_sources())
        """ffmpeg handler"""
        self.__sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        """Output socket for sink"""
        self.__fd: io.IOBase
        """Holds the ffmpeg output pipe handle"""

        self.__make_in_fifo()  # Make python -> ffmpeg fifo fifo

        for source in self.__controller_sources:  # Initialize dicts based off of source ips
            self.__sources.append(SourceInfo(source.ip, self.__temp_path + source.ip, self._sink_ip, source.volume))

        self.start()  # Start our listener thread
    
    def update_source_volume(self, controllersource: ControllerSource):
        """Updates the source volume to the specified volume, does nothing if the source is not playing to this sink."""
        for source in self.__sources:
            if source._ip == controllersource.ip:
                source.volume = controllersource.volume
                self.__ffmpeg.set_source_volume(source, controllersource.volume)

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

    def __get_open_sources(self) -> List[SourceInfo]:
        """Build a list of active IPs, exclude ones that aren't open"""
        active_sources: List[SourceInfo] = []
        for source in self.__sources:
            if source.is_open():
                active_sources.append(source)
        return active_sources
    
    def __get_source_by_ip(self, ip: str) -> tuple[SourceInfo, bool]:
        for source in self.__sources:
            if source._ip == ip:
                return (source, True)
        return (None, False)  # type: ignore

    def __check_for_inactive_sources(self) -> None:
        """Looks for old pipes that are open and closes them"""
        if len(self.__get_open_sources()) < 2:  # Don't close the last pipe
            return

        for source in self.__sources:
            if not source.is_active() and source.is_open():
                print(f"[Sink {self._sink_ip} Source {source._ip}] Closing (Timeout)")
                source.close()
                self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())

    def __update_source_attributes_and_open_source(self, source: SourceInfo, header: bytearray) -> None:
        """Verifies the target pipe header matches what we have, updates it if not. Also opens the pipe."""
        parsed_scream_header = StreamInfo(header)
        if not source.check_attributes(parsed_scream_header):
            print(f"[Sink {self._sink_ip} Source {source._ip}] Closing (Stream attribute change detected. Was: {source._stream_attributes.bit_depth}-bit at {source._stream_attributes.sample_rate}kHz, is now {parsed_scream_header.bit_depth}-bit at {parsed_scream_header.sample_rate}kHz.)")
            source.set_attributes(parsed_scream_header)
            source.close()
        if not source.is_open():
            source.open()
            self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())

    def __check_packet(self, source: SourceInfo, data: bytearray) -> bool:
        """Verifies a packet is the right length"""
        if len(data) != 1157:
            raise Exception(f"[Sink {self._sink_ip} Source {source._ip}] Got bad packet length {len(data)} != 1157 from source")
        return True

    def process_packet_from_receiver(self, source_ip, data) -> None:
        """Sends data from the main receiver to ffmpeg after verifying it's on our list
           Scream Source -> Receiver -> Sink Handler -> Sources -> Pipe -> FFMPEG -> Pipe -> Python -> Scream Sink
                                             ^
                                        You are here                   
        """
        try:
            source: SourceInfo
            result: bool
            source, result = self.__get_source_by_ip(source_ip)
            if result:
                if self.__check_packet(source, data):
                    self.__update_source_attributes_and_open_source(source, data[:5])
                    source.write(data[5:])  # Write the data to the output fifo
            self.__check_for_inactive_sources()
        except:
            print(traceback.format_exc())

    def stop(self) -> None:
        """Stops the Sink, closes all handles"""
        self.__running = False
        self.__sock.close()
        self.__fd.close()
        try:
            os.remove(self.__fifo_in)
        except:
            pass

    def run(self) -> None:
        """This thread implements listening to self.fifoin and sending it out to dest_ip
           Scream Source -> Receiver -> Sink Handler -> Sources -> Pipe -> FFMPEG -> Pipe -> Python -> Scream Sink
                                                                                               ^
                                                                                          You are here                   
        """
        self.__fd = open(self.__fifo_in, "rb")
        while self.__running:
            try:
                header = bytes([0x01, 0x20, 0x02, 0x03, 0x00])  # 48khz, 32-bit, stereo
                data = self.__fd.read(1152)  # Read 1152 bytes from ffmpeg
                if len(data) == 1152:
                    sendbuf = header + data  # Add the header to the data
                    self.__sock.sendto(sendbuf, (self._sink_ip, 4010))  # Send it to the sink
            except Exception as e:
                print(traceback.format_exc())
        print(f"[Sink {self._sink_ip}] Stopping")
        self.__ffmpeg.stop()
        for source in self.__sources:
            try:
                source.close()
            except:
                print(traceback.format_exc())