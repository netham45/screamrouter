"""Threads to handle the PCM and MP3 output from ffmpeg
   Currently defunct"""
import multiprocessing
import multiprocessing.process
import os
import select
import time
from ctypes import c_bool
from subprocess import TimeoutExpired
from typing import Tuple

import src.constants.constants as constants
from src.audio.mp3_header_parser import InvalidHeaderException, MP3Header
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import IPAddressType
from src.screamrouter_types.packets import WebStreamFrames
from src.utils.utils import close_all_pipes, set_process_name

logger = get_logger(__name__)

class MP3OutputReader(multiprocessing.Process):
    """Handles listening for MP3 output from ffmpeg and sends it to the WebStream handler"""
    def __init__(self, sink_ip: IPAddressType,
                 webstream_queue: multiprocessing.Queue):
        super().__init__(name=f"[Sink:{sink_ip}] MP3 Thread")
        self.__webstream_queue: multiprocessing.Queue = webstream_queue
        """webstream queue to write frames to"""
        self._sink_ip: IPAddressType = sink_ip
        """Holds the sink IP for the web api to filter based on"""
        self.running = multiprocessing.Value(c_bool, True)
        """Multiprocessing-passed flag to determine if the thread is running"""
        #self.start()

    def _read_bytes(self, count: int, timeout: float, firstread: bool = False) -> bytes:
        """Reads count bytes, blocks until self.__running goes false or count bytes are received.
        firstread indicates it should return the first read regardless of how many bytes it read
        as long as it read something."""
        start_time = time.time()
        dataout:bytearray = bytearray() # Data to return
        while (self.running.value and len(dataout) < count and
               ((time.time() - timeout) < start_time or timeout == 0)):
            ready = select.select([0], [], [], .2)
            if ready[0]:
                try:
                    data: bytes = os.read(0, count - len(dataout))
                    if data:
                        if firstread:
                            return data
                        dataout.extend(data)
                except ValueError:
                    pass
        return bytes(dataout)

    def __read_header(self) -> Tuple[MP3Header, bytes]:
        """Returns a tuple of the parsed headr and raw header data. Skips ID3 headers if found."""
        header: bytearray = bytearray(self._read_bytes(constants.MP3_HEADER_LENGTH, 0))  # Header
        header_parsed: MP3Header  # Parsed header object
        max_bytes_to_search: int = 250
        # Byte count to search for an MP3 header tag after finding an ID3 header
        bytes_searched: int = 0  # Number of bytes searched so far

        if header[0:3] == "ID3".encode():  # Found ID3 header, search for start of MP3 header
            while bytes_searched < max_bytes_to_search:
                bytes_searched = bytes_searched + 1
                bytesin: bytearray = bytearray(self._read_bytes(1, 0))
                if len(bytesin) == 0:
                    raise InvalidHeaderException(
                        f"[Sink: {self._sink_ip}] Couldn't read from ffmpeg")
                if bytesin[0] == 255:
                    header = bytesin + self._read_bytes(constants.MP3_HEADER_LENGTH - 1, 0)
                    try:
                        header_parsed: MP3Header = MP3Header(header)
                        return (header_parsed, header)
                    except InvalidHeaderException as exc:
                        logger.warning("[Sink:%s] Bad MP3 Header: %s", self._sink_ip, exc)
            if bytes_searched == max_bytes_to_search:
                raise InvalidHeaderException(
                    f"[Sink: {self._sink_ip}] Couldn't find MP3 header after ID3 header")
        else:
            header_parsed: MP3Header = MP3Header(header)
            return (header_parsed, header)
        raise InvalidHeaderException("Invalid header")

    def run(self) -> None:
        """This look reads MP3 output from ffmpeg and passes MP3 frames to the WebStream handler"""
        logger.debug("[Sink %s] MP3 Thread PID %s", self._sink_ip, os.getpid())
        set_process_name("PCMThread", f"[Sink {self._sink_ip}] MP3 Writer Thread")
        target_frames_per_packet: int = 1
        # Send data to the web handler when it gets this many frames buffered up
        target_bytes_per_packet: int = 1500
        # Send data to the web handler when it gets this many bytes buffered up
        available_data = bytearray()  # Holds the available frames
        available_frame_count: int = 0  # Holds the number of available frames
        while self.running.value:
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
            mp3_frame = self._read_bytes(mp3_header_parsed.framelength, .1)
            available_data.extend(mp3_frame)
            available_frame_count = available_frame_count + 1
            if (available_frame_count == target_frames_per_packet or
                                    len(available_data) >= target_bytes_per_packet):
            # Send the buffered data when target frames per packet or target bytes per packet hit
                self.__webstream_queue.put(WebStreamFrames(sink_ip=self._sink_ip,
                                                           data=available_data))
                available_frame_count = 0
                available_data = bytearray()
        logger.debug("[Sink:%s] MP3 thread exit", self._sink_ip)
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
                logger.warning("FFMpeg Output Reader failed to close")
