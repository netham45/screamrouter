"""Holds the Source Info and a thread for handling it's queue"""
import multiprocessing
import multiprocessing.sharedctypes
import os
import queue
import sys
import time
from ctypes import c_bool, c_double
from typing import Optional

import src.constants.constants as constants
from src.audio.scream_header_parser import ScreamHeader
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import IPAddressType
from src.screamrouter_types.configuration import SourceDescription
from src.utils.utils import set_process_name

logger = get_logger(__name__)

class FFMpegInputWriter(multiprocessing.Process):
    """Stores the status for a single Source to a single Sink
       Handles writing from a queue to an ffmpeg pipe"""
    def __init__(self, tag: str,
                 sink_ip: Optional[IPAddressType], source_info: SourceDescription):
        """Initializes a new Source object"""
        super().__init__(name=f"[Sink:{sink_ip}][Source:{tag}] Pipe Writer")
        set_process_name("Source Writer", f"[Sink:{sink_ip}][Source:{tag}] Pipe Writer")
        self.source_info = source_info
        """Source Description for this source"""
        self.tag: str = tag
        """The source's tag, generally it's IP."""
        self.is_open: bool = False
        """Rather the Source is open for writing or not"""
        self.__last_data_time = multiprocessing.Value(c_double, 0)
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
        self.queue: multiprocessing.Queue = multiprocessing.Queue()
        """Holds the queue to read/write from"""
        logger.info("[Sink:%s][Source:%s] Queue %s", self.__sink_ip, self.tag, self.queue)
        self.running = multiprocessing.Value(c_bool, True)
        """Multiprocessing-passed flag to determine if the thread is running"""
        self.start()

    def is_active(self, active_time_ms: int = 200) -> bool:
        """Returns if the source has been active in the last active_time_ms ms"""
        now: float = time.time() * 1000
        if now - float(self.__last_data_time.value) > active_time_ms:
            return False
        return True

    def update_activity(self) -> None:
        """Sets the source last active time"""
        now: float = time.time() * 1000
        self.__last_data_time.value = now # type: ignore

    def stop(self) -> None:
        """Fully stops and closes the source, closes fifo handles"""
        self.running.value = c_bool(False)
        os.write(self.fifo_fd_write, bytes([0] * 1152))
        try:
            os.close(self.fifo_fd_read)
        except OSError:
            pass
        try:
            os.close(self.fifo_fd_write)
        except OSError:
            pass

        if self.is_open:
            self.is_open = False
            logger.info("[Sink:%s][Source:%s] Stopping", self.__sink_ip, self.tag)
        if constants.KILL_AT_CLOSE:
            try:
                self.kill()
            except AttributeError:
                pass
        if constants.WAIT_FOR_CLOSES:
            self.join()

    def write(self, data: bytes) -> None:
        """Writes data to this source's FIFO"""
        self.queue.put(data)
        self.update_activity()

    def run(self) -> None:
        logger.debug("[Sink %s Source %s] Source Input Thread PID %s",
                     self.__sink_ip,
                     self.tag,
                     os.getpid())
        while self.running.value:
            if os.getppid() == 1:
                logger.warning("Exiting because parent pid is 1")
                sys.exit(3)
            try:
                data: bytes = self.queue.get(timeout=.3)
                os.write(self.fifo_fd_write, data)
                self.update_activity()
            except ValueError:
                logger.warning("[Sink:%s][Source:%s] Failed to write to ffmpeg",
                                self.__sink_ip, self.tag)
            except queue.Empty:
                pass
