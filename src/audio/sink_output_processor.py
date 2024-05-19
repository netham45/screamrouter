"""Threads to handle the PCM stream from each source"""
import multiprocessing
import multiprocessing.process
import os
import socket
from ctypes import c_bool
from subprocess import TimeoutExpired
from typing import List
import numpy

from src.audio.source_input_processor import SourceInputProcessor
import src.constants.constants as constants
from src.audio.scream_header_parser import ScreamHeader
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import IPAddressType, PortType
from src.utils.utils import close_all_pipes, set_process_name

logger = get_logger(__name__)

class PCMOutputReader(multiprocessing.Process):
    """Handles listening for PCM output from sources and sends it to sinks"""
    def __init__(self, sink_ip: IPAddressType,
                 sink_port: PortType, output_info: ScreamHeader,
                 sources: List[SourceInputProcessor]):
        super().__init__(name="[Sink:{sink_ip}] PCM Thread")
        self._sink_ip: IPAddressType = sink_ip
        """Holds the sink IP for the web api to filter based on"""
        self.running = multiprocessing.Value(c_bool, True)
        """Multiprocessing-passed flag to determine if the thread is running"""
        self.__sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        """Output socket for sink"""
        self._sink_port: PortType = sink_port
        self.__output_header: bytes = output_info.header
        """Holds the header added onto packets sent to Scream receivers"""
        self._sink_ip = sink_ip
        self.read_fds: List[int] = []
        self.sources_open: List = []
        for source in sources:
            self.read_fds.append(source.source_output_fd)
            self.sources_open.append(source.is_open)
        self.start()

    def run(self) -> None:
        """Reads PCM output from sources, mixes it together, attaches a header, sends it to the sink."""
        set_process_name("PCMThread", f"[Sink {self._sink_ip}] PCM Writer Thread")
        logger.debug("[Sink %s] PCM Thread PID %s", self._sink_ip, os.getpid())
        self.__sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, constants.PACKET_SIZE * 65535)
        while self.running.value:
            output_data = numpy.array(constants.PACKET_DATA_SIZE, numpy.int64)
            for index, fd in enumerate(self.read_fds):
                is_open = self.sources_open[index]
                if is_open.value:
                    stream_data = numpy.frombuffer(os.read(fd, constants.PACKET_DATA_SIZE),
                                                   numpy.int32)
                    output_data = stream_data + output_data
            output_data = numpy.clip(output_data, -2147483648, 2147483647)
            self.__sock.sendto(
                self.__output_header + numpy.int32(output_data).tobytes(),
                (str(self._sink_ip), int(self._sink_port)))
        logger.debug("[Sink:%s] PCM thread exit", self._sink_ip)
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
                logger.warning("PCM Output Reader failed to close")
