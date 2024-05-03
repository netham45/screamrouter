"""Plugin to use ffmpeg to play a url"""

import io
import multiprocessing
import os
from pathlib import Path
import select
import socket
import subprocess
from typing import List

from pydantic import BaseModel
from audio.scream_header_parser import ScreamHeader, create_stream_info
from plugin_manager.screamrouter_plugin import ScreamRouterPlugin
from screamrouter_types.annotations import PlaybackURLType, SinkNameType, VolumeType
import constants

from logger import get_logger

logger = get_logger(__name__)

class URL(BaseModel):
    """Post data containing a URL"""
    url: PlaybackURLType
    """URL to play back"""

class FFMpegPlayURL(ScreamRouterPlugin):
    """This implements an ffmpeg thread to play a URL and capture it's PCM"""
    def initialize_api(self):
        """Add play_url endpoint"""
        self.api.post("/sinks/{sink_name}/play/{volume}",
            tags=["Sink Playback"])(self.play_url)

    def play_url(self, url: URL, sink_name: SinkNameType, volume: VolumeType):
        """Plays a URL"""
        tag: str = self.add_one_time_source([sink_name])
        PlayURL(self, tag, url.url, volume)

class PlayURL(multiprocessing.Process):
    """Plays a URL"""
    def __init__(self, plugin: ScreamRouterPlugin,
                 tag: str,
                 url: PlaybackURLType,
                 volume: VolumeType):
        super().__init__(name=f"[{tag}] URL Playback")
        self.tag = tag
        """Tag for the URL playback"""
        self.fifo_out_path: Path = Path(f"pipeFFMpegPlayURL_{self.tag}")
        """FIFO path for ffmpeg to write to"""
        self.url: PlaybackURLType = url
        """URL to play back"""
        self.volume: VolumeType = volume
        """Volume to play back at"""        
        self.ffmpeg: subprocess.Popen
        """ffmpeg command process"""
        self.file: io.BufferedReader
        """File handle to read from ffmpeg"""
        self.fd: int = 0
        """File descriptor"""
        self.bytes_to_write = bytearray([])
        self.plugin: ScreamRouterPlugin = plugin
        self.stream_info: ScreamHeader = create_stream_info(bit_depth=16,
                                              sample_rate=44100,
                                              channels=2,
                                              channel_layout="stereo")
        self.ffmpeg_command: List[str] = self.get_ffmpeg_command(self.url,
                                                                 self.volume)
        """ffmpeg command to run"""
        self.sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        """Socket to communicate with the main thread through"""
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.last_write_time: int = 0
        """Last write time"""
        print(f"Sock port: {self.sock.getsockname()}")
        self.start()

    def get_ffmpeg_command(self, url: PlaybackURLType, volume: VolumeType) -> List[str]:
        """Builds the ffmpeg playback command"""
        ffmpeg_command_parts: List[str] = []
        ffmpeg_command_parts.extend(['ffmpeg'])#, '-hide_banner'])
        ffmpeg_command_parts.extend(["-blocksize", "1",
                                    "-max_delay", "0",
                                    "-audio_preload", "0",
                                    "-max_probe_packets", "0",
                                    "-rtbufsize", "0",
                                    "-analyzeduration", "0",
                                    "-probesize", "32",
                                    "-fflags", "discardcorrupt",
                                    "-flags", "low_delay",
                                    "-fflags", "nobuffer",
                                    "-thread_queue_size", "128",
                                      "-i", f"{url}"] 
                                     )
        ffmpeg_command_parts.extend(["-filter_complex",
                                     f"volume={volume}"])
        ffmpeg_command_parts.extend([
                                     "-y",
                                     "-f", f"s{self.stream_info.bit_depth}le",
                                     "-ac", f"{self.stream_info.channels}",
                                     "-channel_layout", f"{self.stream_info.channel_layout}",
                                     "-ar", f"{self.stream_info.sample_rate}",
                                     f"{self.fifo_out_path}"
                                     ])  # ffmpeg PCM output
        return ffmpeg_command_parts

    def make_ffmpeg_output_pipe(self, pipe: Path) -> bool:
        """Makes fifo out for python sending to ffmpeg"""
        if os.path.exists(pipe):
            os.remove(pipe)
        os.mkfifo(pipe)
        return True

    def open_ffmpeg_pipe(self, fifo_out_path: Path) -> None:
        """Opens the ffmpeg output pipe"""
        self.make_ffmpeg_output_pipe(fifo_out_path)
        self.fd = os.open(fifo_out_path, os.O_RDONLY, os.O_NONBLOCK)

    def _read_bytes(self, count: int, firstread: bool = False) -> bytes:
        """Reads count bytes, blocks until self.__running goes false or count bytes are received.
        firstread indicates it should return the first read regardless of how many bytes it read
        as long as it read something."""
        dataout:bytearray = bytearray() # Data to return
        while len(dataout) < count:
            ready = select.select([self.fd], [], [], .1)
            if ready[0]:
                data: bytes = self.file.read(count - len(dataout))
                if data:
                    if firstread:
                        return data
                    dataout.extend(data)
        return dataout

    def run(self):
        """Wait for data to be available from ffmpeg to put into the sink queue
           when ffmpeg ends the thread can end"""
        os.nice(-15)
        logger.info("[URL Playback:%s] Playing back", self.tag)
        print(self.ffmpeg_command)
        self.ffmpeg = subprocess.Popen(self.ffmpeg_command,
                                                    shell=False,
                                                    stdin=subprocess.PIPE)#,
                                                    #stdout=subprocess.DEVNULL,
                                                    #stderr=subprocess.DEVNULL)
        self.open_ffmpeg_pipe(self.fifo_out_path)
        while self.ffmpeg.poll() is None:  # Goes to a value when ffmpeg ends
            data = os.read(self.fd, constants.PACKET_DATA_SIZE)
            self.plugin.write_bytes(self.stream_info.header + data)

        self.plugin.notify_one_time_source_done(self.tag)
        self.ffmpeg.wait()
        if os.path.exists(self.fifo_out_path):
            os.remove(self.fifo_out_path)
        logger.debug("[URL Playback:%s] Done", self.tag)
