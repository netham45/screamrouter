

import io
import os
import select
import subprocess
import threading
import traceback
from typing import List

from audio.stream_info import StreamInfo, create_stream_info
from configuration.configuration_controller_types import SinkDescription


class ffmpegPlayURL(threading.Thread):
    def __init__(self, url: str, volume: float, sink_info: SinkDescription, fifo_in_url: str, callback):
        super().__init__(name=f"ffmpeg Playback {url}")
        self._url: str = url
        self._volume: float = volume
        self.__sink_info: SinkDescription = sink_info
        self.__fifo_in_url: str = fifo_in_url
        self._fd: io.BufferedReader
        """File handle"""
        self.callback = callback
        self._make_ffmpeg_to_screamrouter_pipe()  # Make python -> ffmpeg fifo
        fd = os.open(self.__fifo_in_url, os.O_RDONLY | os.O_NONBLOCK)
        self._fd = open(fd, 'rb')
        self.__ffmpeg: subprocess.Popen = subprocess.Popen(self.__get_ffmpeg_command(), preexec_fn = self.ffmpeg_preopen_hook, shell=False, stdin=subprocess.PIPE)#, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.__header: StreamInfo = create_stream_info(self.__sink_info.bit_depth, self.__sink_info.sample_rate, 2, "stereo")
        self.start()

    def ffmpeg_preopen_hook(self): 
        """Don't forward signals. It's lifecycle is managed."""
        os.setpgrp()
        
    def __get_ffmpeg_command(self) -> List[str]:
        """Builds the ffmpeg playback command"""
        ffmpeg_command_parts: List[str] = ['ffmpeg', '-hide_banner']
        ffmpeg_command_parts.extend(["-i", f"{self._url}"])
        ffmpeg_command_parts.extend(["-filter_complex", f"arealtime,volume={self._volume}"])
        ffmpeg_command_parts.extend(["-avioflags", "direct", "-y", "-f", f"s{self.__sink_info.bit_depth}le", "-ac", f"{self.__sink_info.channels}", "-ar", f"{self.__sink_info.sample_rate}", f"{self.__fifo_in_url}"])  # ffmpeg PCM output
        return ffmpeg_command_parts

    def _make_ffmpeg_to_screamrouter_pipe(self) -> bool:
        """Makes fifo out for python sending to ffmpeg"""
        try:
            try:
                os.remove(self.__fifo_in_url)
            except:
                pass
            os.mkfifo(self.__fifo_in_url)
            return True
        except:
            print(traceback.format_exc())
            return False
        
    def _read_bytes(self, count: int) -> bytes:
        """Reads count bytes, blocks until self.__running goes false or 'count' bytes are received."""
        dataout:bytearray = bytearray()
        """Data to return"""
        while  len(dataout) < count and self.__ffmpeg.poll() == None:
            ready = select.select([self._fd], [], [], .1)
            if ready[0]:
                data: bytes = self._fd.read(count - len(dataout))
                if (data):
                    dataout.extend(data)
        return dataout

    def run(self):
        while self.__ffmpeg.poll() == None:
            data = self._read_bytes(1152)
            callback = self.callback
            callback("ffmpeg", self.__header.header + data)
        self.__ffmpeg.wait()

        