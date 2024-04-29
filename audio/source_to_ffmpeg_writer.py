"""Holds the Source Info and a thread for handling it's queue"""
import os
import threading
import time
import io

import collections

from audio.stream_info import StreamInfo


class SourceToFFMpegWriter(threading.Thread):
    """Stores the status for a single Source to a single Sink"""
    def __init__(self, tag: str, fifo_file_name: str, sink_ip: str, volume: float):
        """Initializes a new Source object"""
        super().__init__(name=f"[Sink {sink_ip} Source {tag}] Pipe Writer")
        self.tag: str = tag
        """The source's tag, generally it's IP."""
        self.__open: bool = False
        """Rather the Source is open for writing or not"""
        self.__last_data_time: float = 0
        """The time in milliseconds we last received data"""
        self.stream_attributes: StreamInfo = StreamInfo(bytearray([0, 0, 0, 0, 0]))
        """The source stream attributes (bit depth, sample rate, channels)"""
        self.fifo_file_name: str = fifo_file_name
        """The named pipe that ffmpeg is using as an input for this source"""
        self.__fifo_file_handle: io.IOBase
        """The open handle to the ffmpeg named pipe"""
        self.__sink_ip: str = sink_ip
        """The sink that opened this source"""
        self.volume: float = volume
        """Holds the sink's volume. 0 = silent, 1 = 100% volume"""
        self.__running = True
        """Rather the thread is running"""
        self._queue: collections.deque = collections.deque([bytes([] * 1152)])
        """Holds the queue to read/write from"""
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
        now: float = time.time() * 1000
        if now - self.__last_data_time > active_time_ms:
            return False
        return True

    def update_activity(self) -> None:
        """Sets the source last active time"""
        now: float = time.time() * 1000
        self.__last_data_time = now

    def is_open(self) -> bool:
        """Returns if the source is open"""
        return self.__open

    def __make_screamrouter_to_ffmpeg_pipe(self):
        if os.path.exists(self.fifo_file_name):
            os.remove(self.fifo_file_name)
        os.mkfifo(self.fifo_file_name)

    def open(self) -> None:
        """Makes the pipe to pass this source to FFMPEG, opens the source"""
        if not self.__open:
            #self.__make_screamrouter_to_ffmpeg_pipe()
            self.update_activity()
            fd = os.open(self.fifo_file_name, os.O_RDWR)
            self.__fifo_file_handle = os.fdopen(fd, 'wb', 0)
            self.__open = True
            print(f"[Sink {self.__sink_ip} Source {self.tag}] Opened")

    def close(self) -> None:
        """Closes the source"""
        if self.is_open():
            self.__fifo_file_handle.close()
            self.__open = False
            print(f"[Sink {self.__sink_ip} Source {self.tag}] Closed")

    def stop(self) -> None:
        """Fully stops and closes the source, closes fifo handles"""
        if self.is_open():
            self.__open = False
            self.__fifo_file_handle.close()
        if os.path.exists(self.fifo_file_name):
            os.remove(self.fifo_file_name)
        self.__running = False

    def write(self, data: bytes) -> None:
        """Writes data to this source's FIFO"""
        self._queue.append(data)
        self.update_activity()

    def run(self) -> None:
        while self.__running:
            while len(self._queue) > 0 and self.is_open():
                data: bytes = self._queue.popleft()
                try:
                    self.__fifo_file_handle.write(data)
                except ValueError:
                    print(f"[Sink {self.__sink_ip} Source {self.tag}] Failed to write to ffmpeg")
            time.sleep(.0001)
        print(f"[Sink {self.__sink_ip}] Queue thread exit")
