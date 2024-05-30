"""Thread to mix each source together for the result
   Do mixing in process instead of forwarding to ffmpeg for latency"""
import multiprocessing
import multiprocessing.process
import os
import select
import socket
from ctypes import c_bool
from subprocess import TimeoutExpired
import time
from typing import List
import numpy

from src.audio.source_input_processor import SourceInputProcessor
import src.constants.constants as constants
from src.audio.scream_header_parser import ScreamHeader
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import IPAddressType, PortType
from src.utils.utils import close_all_pipes, set_process_name

logger = get_logger(__name__)

class SinkOutputMixer(multiprocessing.Process):
    """Handles listening for PCM output from sources and sends it to sinks"""
    def __init__(self, sink_ip: IPAddressType,
                 sink_port: PortType, output_info: ScreamHeader,
                 sources: List[SourceInputProcessor],
                 ffmpeg_mp3_write_fd: int):
        super().__init__(name="[Sink:{sink_ip}] PCM Thread")
        self._sink_ip: IPAddressType = sink_ip
        """Holds the sink IP for the web api to filter based on"""
        self.running = multiprocessing.Value(c_bool, True)
        """Multiprocessing-passed flag to determine if the thread is running"""
        self.__sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        """Output socket for sink"""
        self._sink_port: PortType = sink_port
        self.__output_header: bytes = output_info.header
        self.__output_info: ScreamHeader = output_info
        """Holds the header added onto packets sent to Scream receivers"""
        self._sink_ip = sink_ip
        self.read_fds: List[int] = []
        self.sources_open: List = []
        for source in sources:
            self.read_fds.append(source.source_output_fd)
            self.sources_open.append(source.is_open)
        self.ffmpeg_mp3_write_fd = ffmpeg_mp3_write_fd
        """FD for ffmpeg mp3 conversion"""
        self.start()

    def read_bytes(self, fd: int, count: int, timeout: float = 0, first: bool = False) -> bytes:
        """Reads count bytes, blocks until self.__running goes false or count bytes are received.
        firstread indicates it should return the first read regardless of how many bytes it read
        as long as it read something."""
        start_time = time.time()
        dataout:bytearray = bytearray() # Data to return
        while (self.running.value and len(dataout) < count and
               ((time.time() - timeout) < start_time or timeout == 0)):
            ready = select.select([fd], [], [], .2)
            if ready[0]:
                try:
                    data: bytes = os.read(fd, count - len(dataout))
                    if data:
                        if first:
                            return data
                        dataout.extend(data)
                except ValueError:
                    pass
        return bytes(dataout)

    def run(self) -> None:
        """Reads PCM output from sources, mixes it together, attaches a header, sends it to sink."""
        set_process_name("PCMThread", f"[Sink {self._sink_ip}] Mixer")
        logger.debug("[Sink %s] Mixer Thread PID %s", self._sink_ip, os.getpid())
        self.__sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, constants.PACKET_SIZE * 65535)
        output_data = numpy.array(constants.PACKET_DATA_SIZE, numpy.int64)
        output_buffer = numpy.array([], numpy.int8)
        while self.running.value:
            first_value: bool = True
            for index, fd in enumerate(self.read_fds):
                is_open = self.sources_open[index]
                if is_open.value:
                    ready = select.select([fd], [], [], .05)
                    if ready[0]:
                        stream_data = numpy.frombuffer(self.read_bytes(fd,
                                                                       constants.PACKET_DATA_SIZE),
                                                       numpy.int32)
                        if first_value:
                            first_value = False
                            output_data = stream_data
                        else:
                            output_data = stream_data + output_data
            if first_value:  # No active source, sleep for 100ms to not burn CPU.
                time.sleep(.1)
            else:
                os.write(
                        self.ffmpeg_mp3_write_fd,
                        numpy.array(output_data, numpy.int32).tobytes())
                if self.__output_info.bit_depth == 32:
                    output_data = numpy.array(output_data, numpy.uint32)
                    output_buffer = numpy.insert(
                                        numpy.frombuffer(output_data, numpy.uint8), 0, output_buffer)
                elif self.__output_info.bit_depth == 24:
                    output_data = numpy.array(output_data, numpy.uint32)
                    output_data = numpy.frombuffer(output_data, numpy.uint8)
                    output_data = numpy.delete(output_data, range(0, len(output_data), 4))
                    output_buffer = numpy.insert(output_data, 0, output_buffer)
                elif self.__output_info.bit_depth == 16:
                    output_data = numpy.array(output_data, numpy.uint16)
                    output_buffer = numpy.insert(
                                        numpy.frombuffer(output_data, numpy.uint8), 0, output_buffer)
                if len(output_buffer) >= constants.PACKET_DATA_SIZE:
                    self.__sock.sendto(
                        self.__output_header + output_buffer[:constants.PACKET_DATA_SIZE].tobytes(),
                        (str(self._sink_ip), int(self._sink_port)))
                    output_buffer = output_buffer[constants.PACKET_DATA_SIZE:]
        logger.debug("[Sink:%s] Mixer thread exit", self._sink_ip)
        close_all_pipes()

    def stop(self) -> None:
        """Stop"""
        self.running.value = c_bool(False)
        if constants.KILL_AT_CLOSE:
            self.kill()
        if constants.WAIT_FOR_CLOSES:
            try:
                self.join(5)
            except TimeoutExpired:
                logger.warning("Mixer failed to close")
