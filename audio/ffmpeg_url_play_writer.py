"""Handles playing a URL and forwarding the output from it to the main receiver.
   Sinks can subscribe to it by having a corresponding SourceDescription made."""
import io
import os
import select
import subprocess
import threading
import fcntl
from typing import List
from pathlib import Path

from screamrouter_types import SinkDescription
from screamrouter_types import PlaybackURLType, VolumeType
from audio.receiver_thread import ReceiverThread
from audio.scream_header_parser import ScreamHeader, create_stream_info
from logger import get_logger

logger = get_logger(__name__)

url_playback_semaphore: threading.Semaphore = threading.Semaphore(5)
"""Max number of URLs playing at once, for limiting resource usage"""

class FFMpegURLPlayWriter(threading.Thread):
    """Handles playing a URL and forwarding the output from it to the main receiver"""
    def __init__(self, url: PlaybackURLType, volume: VolumeType, sink_info: SinkDescription,
                 fifo_in: Path, source_tag: str, receiver: ReceiverThread):
        """Plays a URL using ffmpeg and outputs it to a pipe name stored in fifo_in."""
        if not url_playback_semaphore.acquire(timeout=1):
            raise TimeoutError("Timed out waiting for available URL play slot")
        super().__init__(name=f"[Sink:{sink_info.ip}] ffmpeg Playback {url}")
        self._url: PlaybackURLType = url
        """URL for Playback"""
        self._volume: VolumeType = volume
        """Volume for playback (0.0-1.0)"""
        self.__sink_info: SinkDescription = sink_info
        """Sink info, needed for bitrate to transcode to"""
        self.__fifo_in_url: Path = fifo_in
        """ffmpeg fifo file"""
        self._fd: io.BufferedReader
        """File handle"""
        self.__source_tag = source_tag
        """Source tag that the sink queue will check against"""
        self.__receiver: ReceiverThread = receiver
        """Receiver to call back when data is available or playback is done"""
        self._make_ffmpeg_to_screamrouter_pipe()  # Make python -> ffmpeg fifo
        fd = os.open(self.__fifo_in_url, os.O_RDONLY | os.O_NONBLOCK)
        fcntl.fcntl(fd, 1031, 1024*1024*1024*64)
        self._fd = open(fd, 'rb', -1)
        self.__header: ScreamHeader = create_stream_info(self.__sink_info.bit_depth,
                                                       self.__sink_info.sample_rate,
                                                       2, "stereo")
        self.__ffmpeg: subprocess.Popen
        self.start()

    def __get_ffmpeg_command(self) -> List[str]:
        """Builds the ffmpeg playback command"""
        ffmpeg_command_parts: List[str] = ['ffmpeg', '-hide_banner']
        ffmpeg_command_parts.extend(["-i", f"{self._url}"])
        ffmpeg_command_parts.extend(["-filter_complex",
                                     f"arealtime,volume={self._volume},apad=pad_dur=3"])
        ffmpeg_command_parts.extend(["-avioflags", "direct",
                                     "-y",
                                     "-f", f"s{self.__sink_info.bit_depth}le",
                                     "-ac", "2",
                                     "-ar", f"{self.__sink_info.sample_rate}",
                                     f"{self.__fifo_in_url}"])  # ffmpeg PCM output
        return ffmpeg_command_parts

    def _make_ffmpeg_to_screamrouter_pipe(self) -> bool:
        """Makes fifo out for python sending to ffmpeg"""
        if os.path.exists(self.__fifo_in_url):
            os.remove(self.__fifo_in_url)
        os.mkfifo(self.__fifo_in_url)
        return True

    def _read_bytes(self, count: int) -> bytes:
        """Reads count bytes, blocks until self.__running goes false or count bytes are received."""
        dataout:bytearray = bytearray()  # Data to return
        while  len(dataout) < count and self.__ffmpeg.poll() is None:
            ready = select.select([self._fd], [], [], .005)
            if ready[0]:
                data: bytes = self._fd.read(count - len(dataout))
                if data:
                    dataout.extend(data)
        return dataout

    def run(self):
        """Wait for data to be available from ffmpeg to put into the sink queue
           when ffmpeg ends the thread can end"""
        self.__ffmpeg = subprocess.Popen(self.__get_ffmpeg_command(),
                                         shell=False,
                                         start_new_session=True,
                                         stdin=subprocess.PIPE,
                                         stdout=subprocess.DEVNULL,
                                         stderr=subprocess.DEVNULL)
        while self.__ffmpeg.poll() is None:
            data = self._read_bytes(1152)
            self.__receiver.add_packet_to_queue(self.__source_tag, self.__header.header + data)
        self.__receiver.notify_url_done_playing(self.__source_tag)
        self.__ffmpeg.wait()
        if os.path.exists(self.__fifo_in_url):
            os.remove(self.__fifo_in_url)
        url_playback_semaphore.release()
