"""Threads to handle the PCM and MP3 output from ffmpeg"""
import os
import pathlib
import select
import threading
import socket
import io
import fcntl

from typing import Optional, Tuple

from screamrouter_types import IPAddressType, PortType
from api.api_webstream import APIWebStream

from audio.mp3_header_parser import MP3Header, InvalidHeaderException
from audio.stream_info import StreamInfo
from logger import get_logger

logger = get_logger(__name__)

class FFMpegOutputThread(threading.Thread):
    """Handles listening for output from ffmpeg, extended by codec-specific classes"""
    def __init__(self, fifo_in: pathlib.Path, sink_ip: IPAddressType, threadname: str):
        super().__init__(name = threadname)
        self._fifo_in: pathlib.Path = fifo_in
        """Holds the ffmpeg output pipe name this sink will use as a source"""
        self._sink_ip: IPAddressType = sink_ip
        """Holds the sink IP for the web api to filter based on"""
        self._running: bool = True
        """Rather this thread is running"""
        self._fd: io.BufferedReader
        """File handle"""
        self._make_ffmpeg_to_screamrouter_pipe()  # Make python -> ffmpeg fifo
        fd = os.open(self._fifo_in, os.O_RDONLY | os.O_NONBLOCK)
        fcntl.fcntl(fd, 1031, 1024*1024*1024*64)
        self._fd = open(fd, 'rb', -1)
        self.start()

    def _make_ffmpeg_to_screamrouter_pipe(self) -> bool:
        """Makes fifo out for python sending to ffmpeg"""
        if os.path.exists(self._fifo_in):
            os.remove(self._fifo_in)
        os.mkfifo(self._fifo_in)
        return True

    def stop(self) -> None:
        """Stop"""
        self._running = False
        self._fd.close()
        if os.path.exists(self._fifo_in):
            os.remove(self._fifo_in)

    def _read_bytes(self, count: int) -> bytes:
        """Reads count bytes, blocks until self.__running goes false or count bytes are received."""
        dataout:bytearray = bytearray() # Data to return
        while self._running and len(dataout) < count:
            ready = select.select([self._fd], [], [], .1)
            if ready[0]:
                try:
                    data: bytes = self._fd.read(count - len(dataout))
                    if data:
                        dataout.extend(data)
                except ValueError:
                    logger.warning("[Sink:%s] Can't read ffmpeg output", self._sink_ip)
        return dataout


class FFMpegMP3Thread(FFMpegOutputThread):
    """Handles listening for MP3 output from ffmpeg"""
    def __init__(self, fifo_in: pathlib.Path, sink_ip: IPAddressType,
                 webstream: Optional[APIWebStream]):
        super().__init__(fifo_in=fifo_in, sink_ip=sink_ip,
                         threadname=f"[Sink:{sink_ip}] MP3 Thread")

        self.__webstream: Optional[APIWebStream] = webstream
        """Holds the Webstream queue object to dump to"""

    def __read_header(self) -> Tuple[MP3Header, bytes]:
        """Returns a tuple of the parsed header and raw header data. Skips ID3 headers if found."""
        header_length: int = 4  # Length of an MP3 header
        header: bytearray = bytearray(self._read_bytes(header_length))  # Header bytes
        header_parsed: MP3Header  # Parsed header object
        max_bytes_to_search: int = 250
        # Byte count to search for an MP3 header tag after finding an ID3 header
        bytes_searched: int = 0  # Number of bytes searched so far

        if header[0:3] == "ID3".encode():  # Found ID3 header, search for start of MP3 header
            while bytes_searched < max_bytes_to_search:
                bytes_searched = bytes_searched + 1
                bytesin: bytearray = bytearray(self._read_bytes(1))
                if len(bytesin) == 0:
                    raise InvalidHeaderException(
                        f"[Sink:{self._sink_ip}] Couldn't read from ffmpeg")
                if bytesin[0] == 255:
                    header = bytesin + self._read_bytes(header_length - 1)
                    try:
                        header_parsed: MP3Header = MP3Header(header)
                        return (header_parsed, header)
                    except InvalidHeaderException as exc:
                        logger.warning("[Sink:%s] Bad MP3 Header: %s", self._sink_ip, exc)
            if bytes_searched == max_bytes_to_search:
                raise InvalidHeaderException(
                    f"[Sink:{self._sink_ip}] Couldn't find MP3 header after ID3 header")
        else:
            header_parsed: MP3Header = MP3Header(header)
            return (header_parsed, header)
        raise InvalidHeaderException("Invalid header")

    def run(self) -> None:
        target_frames_per_packet: int = 1
        # Send data to the web handler when it gets this many frames buffered up
        target_bytes_per_packet: int = 1500
        # Send data to the web handler when it gets this many bytes buffered up
        available_data = bytearray()  # Holds the available frames
        available_frame_count: int = 0  # Holds the number of available frames
        while self._running:
            mp3_header_parsed: MP3Header  # Holds the parsed header object
            mp3_header_raw: bytes  # Holds the raw header bytes
            mp3_frame: bytes  # Holds the currently processing MP3 frame
            try:
                mp3_header_parsed, mp3_header_raw = self.__read_header()
            except InvalidHeaderException as exc:
                logger.debug("[Sink:%s] Failed processing MP3 header: %s",  self._sink_ip, exc)
                logger.debug("[Sink:%s] This is probably because ffmpeg quit", self._sink_ip)
                continue
            available_data.extend(mp3_header_raw)
            mp3_frame = self._read_bytes(mp3_header_parsed.framelength)
            available_data.extend(mp3_frame)
            available_frame_count = available_frame_count + 1
            if self.__webstream and (available_frame_count == target_frames_per_packet or
                                     len(available_data) >= target_bytes_per_packet):
            # Send the buffered data when target frames per packet or target bytes per packet hit
                self.__webstream.sink_callback(self._sink_ip, available_data)
                available_frame_count = 0
                available_data = bytearray()
        logger.debug("[Sink:%s] MP3 thread exit", self._sink_ip)


class FFMpegPCMThread(FFMpegOutputThread):
    """Handles listening for PCM output from ffmpeg"""
    def __init__(self, fifo_in: pathlib.Path, sink_ip: IPAddressType,
                 sink_port: PortType, output_info: StreamInfo):

        self.__sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        """Output socket for sink"""
        self._sink_port: PortType = sink_port
        self.__output_header: bytes = output_info.header
        """Holds the header added onto packets sent to Scream receivers"""

        super().__init__(fifo_in=fifo_in,sink_ip=sink_ip,
                         threadname=f"[Sink:{sink_ip}] PCM Thread")

    def run(self) -> None:
        """This thread implements listening to self.fifoin and sending it out to dest_ip"""
        self.__sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1157 * 65535)
        while self._running:
            # Send data from ffmpeg to the Scream receiver
            self.__sock.sendto(self.__output_header + self._read_bytes(1152),
                               (str(self._sink_ip), int(self._sink_port)))
        logger.debug("[Sink:%s] PCM thread exit", self._sink_ip)
