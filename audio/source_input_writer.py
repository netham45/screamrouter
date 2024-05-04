"""Holds the Source Info and a thread for handling it's queue"""
import fcntl
import multiprocessing
import multiprocessing.sharedctypes
import os
import queue
import time
from ctypes import c_bool
from typing import Optional

import constants
from audio.scream_header_parser import ScreamHeader
from logger import get_logger
from screamrouter_types.annotations import IPAddressType
from screamrouter_types.configuration import SourceDescription

logger = get_logger(__name__)

class SourceInputThread(multiprocessing.Process):
    """Stores the status for a single Source to a single Sink
       Handles writing from a queue to an ffmpeg pipe"""
    def __init__(self, tag: str,
                 sink_ip: Optional[IPAddressType], source_info: SourceDescription):
        """Initializes a new Source object"""
        super().__init__(name=f"[Sink:{sink_ip}][Source:{tag}] Pipe Writer")
        self.source_info = source_info
        """Source Description for this source"""
        self.tag: str = tag
        """The source's tag, generally it's IP."""
        self.is_open: bool = False
        """Rather the Source is open for writing or not"""
        self.__last_data_time: float = 0
        """The time in milliseconds we last received data"""
        self.stream_attributes: ScreamHeader = ScreamHeader(bytearray([0, 32, 2, 0, 0]))
        """The source stream attributes (bit depth, sample rate, channels)"""
        self.fifo_fd_read: int
        """Passed to ffmpeg at start"""
        self.fifo_fd_write: int
        """output to ffmpeg for encoding and mixing"""
        self.fifo_fd_read, self.fifo_fd_write = os.pipe()

        self.__sink_ip: Optional[IPAddressType] = sink_ip
        """The sink that opened this source"""
        self._queue: multiprocessing.Queue = multiprocessing.Queue()
        """Holds the queue to read/write from"""
        self.running = multiprocessing.Value(c_bool, True)
        """Multiprocessing-passed flag to determine if the thread is running"""
        self.start()

    def is_active(self, active_time_ms: int = 200) -> bool:
        """Returns if the source has been active in the last active_time_ms ms"""
        now: float = time.time() * 1000
        if now - self.__last_data_time > active_time_ms:
            return False
        return True

    def update_activity(self) -> None:
        """Sets the source last active time"""
        now: float = time.time() * 1000
        self.__last_data_time = now

    def stop(self) -> None:
        """Fully stops and closes the source, closes fifo handles"""
        self.running.value = c_bool(False)
        if constants.WAIT_FOR_CLOSES:
            self.join()
        if self.is_open:
            self.is_open = False
            logger.info("[Sink:%s][Source:%s] Stopping", self.__sink_ip, self.tag)
        logger.debug("Ended Source %s", self.source_info.name)

    def write(self, data: bytes) -> None:
        """Writes data to this source's FIFO"""
        self._queue.put(data)
        self.update_activity()

    def run(self) -> None:
        logger.debug("[Sink %s Source %s] Source Input Thread PID %s",
                     self.__sink_ip,
                     self.tag,
                     os.getpid())
        fcntl.fcntl(self.fifo_fd_write, 1031, 1024*1024*1024*64)
        fifo_file_handle = open(self.fifo_fd_write, 'wb', -1)
        while self.running.value:
            try:
                data: bytes = self._queue.get(timeout=.1)
                fifo_file_handle.write(data)
            except ValueError:
                logger.warning("[Sink:%s][Source:%s] Failed to write to ffmpeg",
                                self.__sink_ip, self.tag)
            except queue.Empty:
                pass
