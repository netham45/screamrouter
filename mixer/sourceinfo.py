import os
import time

import io
import traceback

from mixer.streaminfo import StreamInfo


class SourceInfo():
    """Stores the status for a single Source to a single Sink"""
    def __init__(self, ip: str, fifo_file_name: str, sink_ip: str, volume: float):
        """Initializes a new Source object"""
        
        self._ip: str = ip
        """The source's IP"""
        self.__open: bool = False
        """Rather the Source is open for writing or not"""
        self.__last_data_time: float = 0
        """The time in milliseconds we last received data"""
        self._stream_attributes: StreamInfo = StreamInfo([0, 0, 0, 0, 0])
        """The source stream attributes (bit depth, sample rate, channels)"""
        self._fifo_file_name: str = fifo_file_name
        """The named pipe that ffmpeg is using as an input for this source"""
        self.__fifo_file_handle: io.IOBase
        """The open handle to the ffmpeg named pipe"""
        self.__sink_ip: str = sink_ip
        """The sink that opened this source"""
        self.volume: float = volume
        """Holds the sink's volume. 0 = silent, 1 = 100% volume"""

    def check_attributes(self, stream_attributes: StreamInfo) -> bool:
        """Returns True if the source's stream attributes are the same, False if they're different."""
        return stream_attributes == self._stream_attributes

    def set_attributes(self, stream_attributes: StreamInfo) -> None:
        """Sets stream attributes for a source"""
        self._stream_attributes = stream_attributes

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

    def open(self) -> None:
        """Opens the source"""
        if not self.__open:
            try:
                try:
                    os.remove(self._fifo_file_name)
                except:
                    pass
                os.mkfifo(self._fifo_file_name)
                fd = os.open(self._fifo_file_name, os.O_RDWR | os.O_NONBLOCK)
                self.__fifo_file_handle = os.fdopen(fd, 'wb', 0)
            except:
                print(traceback.format_exc())
            self.__open = True
            self.update_activity()
            print(f"[Sink {self.__sink_ip} Source {self._ip}] Opened")

    def close(self) -> None:
        """Closes the source"""
        try:
            self.__fifo_file_handle.close()  # Close and remove the fifo handle so ffmpeg will stop trying to listen for it
        except:
            pass
        self.__open = False
        print(f"[Sink {self.__sink_ip} Source {self._ip}] Closed")

    def stop(self) -> None:
        """Fully stops and closes the source, closes fifo handles"""
        self.__open = False
        try:
            self.__fifo_file_handle.close()
        except:
            print(traceback.format_exc())

    def write(self, data: bytes) -> None:
        """Writes data to this source's FIFO
           Scream Source -> Receiver -> Sink Handler -> Sources -> Pipe -> FFMPEG -> Pipe -> Python -> Scream Sink
                                                           ^
                                                      You are here                       
        """
        self.__fifo_file_handle.write(data)
        self.update_activity()