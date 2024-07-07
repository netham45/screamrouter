"""Thread to mix each source together for the result
   Do mixing in process instead of forwarding to ffmpeg for latency"""
import ctypes
import multiprocessing
import multiprocessing.process
import os
import select
import socket
from ctypes import c_bool
from subprocess import TimeoutExpired
import time
from typing import List, Optional
import numpy
import ntplib

from src.screamrouter_types.configuration import SinkDescription
from src.audio.source_input_processor import SourceInputProcessor
import src.constants.constants as constants
from src.audio.scream_header_parser import ScreamHeader
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.utils.utils import close_all_pipes, set_process_name

logger = get_logger(__name__)

class SinkOutputMixer(multiprocessing.Process):
    """Handles listening for PCM output from sources and sends it to sinks"""
    def __init__(self, sink_info: SinkDescription,
                 output_info: ScreamHeader,
                 tcp_client_fd: Optional[int],
                 sources: List[SourceInputProcessor],
                 ffmpeg_mp3_write_fd: int):
        super().__init__(name="[Sink:{sink_ip}] PCM Thread")
        self.sink_info: SinkDescription = sink_info
        """SinkDescription holding sink properties"""
        self.running = multiprocessing.Value(c_bool, True)
        """Multiprocessing-passed flag to determine if the thread is running"""
        self.__sock: socket.socket
        """Output socket for sink"""
        self.tcp_client_fd: Optional[int] = tcp_client_fd
        if self.tcp_client_fd is not None:
            try:
                self.__sock = socket.socket(fileno=self.tcp_client_fd)
                self.__sock.setblocking(False)
                #self.__sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                self.__sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1152*4)
                self.__sock.setsockopt(socket.IPPROTO_IP, socket.IP_TOS, 0x28)
                self.__sock.settimeout(15)
            except OSError:
                self.tcp_client_fd = None
                self.__sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            except ValueError:
                self.tcp_client_fd = None
                self.__sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        else:
            self.__sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.__output_header: bytes = output_info.header
        self.__output_info: ScreamHeader = output_info
        """Holds the header added onto packets sent to Scream receivers"""
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
        set_process_name("PCMThread", f"[Sink {self.sink_info.ip}] Mixer")
        logger.debug("[Sink %s] Mixer Thread PID %s", self.sink_info.ip, os.getpid())
        output_data = numpy.array(constants.PACKET_DATA_SIZE, numpy.int64)
        output_buffer = numpy.array([], numpy.int8)
        ntp_offset: int = 0
        try:
            ntp_offset = int(ntplib.NTPClient().request('192.168.3.3',
                                                         version=3).offset)
        except ntplib.NTPException:
            pass
        packet_count: int = 0
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
                output_data = numpy.array(output_data, numpy.uint32)
                output_data = numpy.frombuffer(output_data, numpy.uint8)
                if self.__output_info.bit_depth == 24:
                    output_data = numpy.delete(output_data, range(0, len(output_data), 4))
                if self.__output_info.bit_depth == 16:
                    output_data = numpy.delete(output_data, range(0, len(output_data), 4))
                    output_data = numpy.delete(output_data, range(0, len(output_data), 3))
                output_buffer = numpy.insert(output_data, 0, output_buffer)

                if len(output_buffer) >= constants.PACKET_DATA_SIZE:
                    final_buffer: bytes
                    if self.sink_info.time_sync:
                        time_bytes: bytes = bytes(ctypes.c_uint64(int(time.time() * 1000) +
                                                                  ntp_offset +
                                                                  constants.SYNCED_TIME_BUFFER +
                                                                  self.sink_info.time_sync_delay))
                        #time_bytes: bytes = bytes(ctypes.c_uint64(int(packet_count)))
                        packet_count += 1
                        final_buffer = (bytes(self.__output_header) +
                                        time_bytes +
                                        output_buffer[:constants.PACKET_DATA_SIZE].tobytes())
                    else:
                        final_buffer = (self.__output_header +
                                        output_buffer[:constants.PACKET_DATA_SIZE].tobytes())
                    if (not self.sink_info.ip is None and
                        not self.sink_info.port is None):
                        if self.tcp_client_fd is None:
                            self.__sock.sendto(final_buffer,
                                        (str(self.sink_info.ip), int(self.sink_info.port)))
                        else:
                            try:
                                if len(final_buffer) in [constants.PACKET_SIZE,
                                                         constants.PACKET_SIZE + 8]:
                                    self.__sock.send(final_buffer[5:])
                            except ConnectionResetError:  # Lost TCP, go back to UDP
                                logger.warning("[Sink %s] Lost TCP (Reset)", self.sink_info.ip)
                                self.tcp_client_fd = None
                                self.__sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                            except BlockingIOError:
                                pass
                                #logger.warning("TCP BlockingIOError")
                                #self.tcp_client_fd = None
                                #self.__sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                            except BrokenPipeError:
                                logger.warning("[Sink %s] Lost TCP (BrokenPipeError)",
                                               self.sink_info.ip)
                                self.tcp_client_fd = None
                                self.__sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                            except socket.timeout:
                                logger.warning("[Sink %s] Lost TCP (Timeout)", self.sink_info.ip)
                                self.tcp_client_fd = None
                                self.__sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                            except TimeoutError:
                                logger.warning("[Sink %s] Lost TCP (Timeout)", self.sink_info.ip)
                                self.tcp_client_fd = None
                                self.__sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    output_buffer = output_buffer[constants.PACKET_DATA_SIZE:]
        logger.debug("[Sink:%s] Mixer thread exit", self.sink_info.ip)
        if not self.tcp_client_fd is None:
            self.__sock.detach()
        close_all_pipes()

    def stop(self, wait_for_close: bool = False) -> None:
        """Stop"""
        self.running.value = c_bool(False) # type: ignore
        if not self.tcp_client_fd is None:
            self.__sock.detach()
        if constants.KILL_AT_CLOSE or wait_for_close:
            self.kill()
        if constants.WAIT_FOR_CLOSES or wait_for_close:
            try:
                self.join(5)
            except TimeoutExpired:
                logger.warning("Mixer failed to close")
