"""Holds the Source Info and a thread for handling it's queue"""
import fcntl
import os
import threading
import time
import io
import pathlib

import collections
from typing import Optional

from screamrouter_types import IPAddressType, VolumeType
from audio.stream_info import StreamInfo
from logger import get_logger

logger = get_logger(__name__)

class SourceToFFMpegWriter(threading.Thread):
    """Stores the status for a single Source to a single Sink
       Handles writing from a queue to an ffmpeg pipe"""
    def __init__(self, tag: str, fifo_file_name: pathlib.Path,
                 sink_ip: Optional[IPAddressType], volume: VolumeType):
        """Initializes a new Source object"""
        if sink_ip is None:
            raise ValueError("Can't start FFMPEG Writer without a Sink IP")
        self.lock: threading.Lock = threading.Lock()
        """Lock to ensure the controller is only accessed by one other thread at a time"""
        self.lock.acquire()
        super().__init__(name=f"[Sink:{sink_ip}][Source:{tag}] Pipe Writer")
        self.tag: str = tag
        """The source's tag, generally it's IP."""
        self.__open: bool = False
        """Rather the Source is open for writing or not"""
        self.__last_data_time: float = 0
        """The time in milliseconds we last received data"""
        self.stream_attributes: StreamInfo = StreamInfo(bytearray([0, 32, 2, 0, 0]))
        """The source stream attributes (bit depth, sample rate, channels)"""
        self.fifo_file_name: pathlib.Path = fifo_file_name
        """The named pipe that ffmpeg is using as an input for this source"""
        self.__fifo_file_handle: io.IOBase
        """The open handle to the ffmpeg named pipe"""
        self.__sink_ip: IPAddressType = sink_ip
        """The sink that opened this source"""
        self.volume: VolumeType = volume
        """Holds the sink's volume. 0 = silent, 1 = 100% volume"""
        self.__running = True
        """Rather the thread is running"""
        self._queue: collections.deque = collections.deque([bytes([] * 1152)])
        """Holds the queue to read/write from"""
        self._condition: threading.Condition = threading.Condition()
        self.__make_screamrouter_to_ffmpeg_pipe()
        self.start()

    def check_attributes(self, stream_attributes: StreamInfo) -> bool:
        """Returns True if the source's stream attributes are the same."""
        return stream_attributes == self.stream_attributes

    def set_attributes(self, stream_attributes: StreamInfo) -> None:
        """Sets stream attributes for a source"""
        self.stream_attributes = stream_attributes

    def is_active(self, active_time_ms: int = 200) -> bool:
        """Returns if the source has been active in the last active_time_ms ms"""
        if not self.__running:
            return False
        now: float = time.time() * 1000
        if now - self.__last_data_time > active_time_ms:
            return False
        return True

    def update_activity(self) -> None:
        """Sets the source last active time"""
        if not self.__running:
            return
        now: float = time.time() * 1000
        self.__last_data_time = now

    def is_open(self) -> bool:
        """Returns if the source is open"""
        if not self.__running:
            return False
        return self.__open

    def __make_screamrouter_to_ffmpeg_pipe(self):
        if os.path.exists(self.fifo_file_name):
            os.remove(self.fifo_file_name)
        os.mkfifo(self.fifo_file_name)

    def open(self) -> None:
        """Makes the pipe to pass this source to FFMPEG, opens the source"""
        self.lock.acquire()
        if not self.__running:
            return
        if not self.__open:
            self.update_activity()
            fd = os.open(self.fifo_file_name, os.O_RDWR | os.O_NONBLOCK)
            fcntl.fcntl(fd, 1031, 1024*1024*1024*64)
            self.__fifo_file_handle = os.fdopen(fd, 'wb', -1)
            self.__open = True
            logger.info("[Sink:%s][Source:%s] Opened", self.__sink_ip, self.tag)
        self._condition.acquire()
        self._condition.notify_all()
        self._condition.release()
        self.lock.release()

    def close(self) -> None:
        """Closes the source"""
        if not self.__running:
            return
        self.lock.acquire()
        if self.is_open():
            if not self.__fifo_file_handle.closed:
                try:
                    self.__fifo_file_handle.close()
                except BlockingIOError:
                    pass
            self.__open = False
            logger.info("[Sink:%s][Source:%s] Closed", self.__sink_ip, self.tag)
        self.lock.release()

    def stop(self) -> None:
        """Fully stops and closes the source, closes fifo handles"""
        if not self.__running:
            return
        self.lock.acquire()
        if self.is_open():
            self.__open = False
            try:
                self.__fifo_file_handle.close()
            except BlockingIOError:
                pass
            logger.info("[Sink:%s][Source:%s] Closed", self.__sink_ip, self.tag)
            logger.debug("[Sink:%s][Source:%s] Stopping", self.__sink_ip, self.tag)
        if os.path.exists(self.fifo_file_name):
            os.remove(self.fifo_file_name)
        self.__running = False
        self.lock.release()

    def write(self, data: bytes) -> None:
        """Writes data to this source's FIFO"""
        if not self.__running:
            return
        self._queue.append(data)
        self.update_activity()
        self._condition.acquire()
        self._condition.notify_all()
        self._condition.release()

    def run(self) -> None:
        self.lock.release()
        self._condition.acquire()
        while self.__running:
            while self.is_open():
                try:
                    data: bytes = self._queue.popleft()
                    self.__fifo_file_handle.write(data)
                except ValueError:
                    logger.warning("[Sink:%s][Source:%s] Failed to write to ffmpeg",
                                    self.__sink_ip, self.tag)
                except IndexError:
                    self._condition.wait(timeout=.1)
                except BlockingIOError:
                    pass
            self._condition.wait(timeout=.1)
        self._condition.release()

        logger.debug("[Sink:%s] Source Queue thread exit", self.__sink_ip)
