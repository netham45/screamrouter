"""Manages the C++ program that mixes audio streams"""
from copy import copy
import subprocess
from typing import List, Optional

from src.screamrouter_types.configuration import SinkDescription
from src.audio.source_input_processor import SourceInputProcessor
from src.audio.scream_header_parser import ScreamHeader
from src.screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)

class SinkOutputMixer():
    """Handles listening for PCM output from sources and sends it to sinks"""
    def __init__(self, sink_info: SinkDescription,
                 output_info: ScreamHeader,
                 tcp_client_fd: Optional[int],
                 sources: List[SourceInputProcessor],
                 ffmpeg_mp3_write_fd: int):
        self.sink_info: SinkDescription = sink_info
        """SinkDescription holding sink properties"""
        self.tcp_client_fd: Optional[int] = tcp_client_fd
        self.__output_info: ScreamHeader = output_info
        """Holds the header added onto packets sent to Scream receivers"""
        self.read_fds: List[int] = []
        """List of source FDs to read from"""
        self.sources: List[SourceInputProcessor] = sources
        """List of all sources"""
        self.ffmpeg_mp3_write_fd = ffmpeg_mp3_write_fd
        """FD for ffmpeg mp3 conversion"""
        self.__mixer: Optional[subprocess.Popen] = None
        """Mixer process"""
        self.update_active_sources()

    def start(self):
        """Starts the sink mixer"""
        if len(self.read_fds) == 0:
            logger.info("[Audio Mixer: %s] Started mixer with no active sources",
                           self.sink_info.ip)
            return
        pass_fds: List[int] = copy(self.read_fds)
        if self.tcp_client_fd is not None:
            pass_fds.append(self.tcp_client_fd)
        pass_fds.append(self.ffmpeg_mp3_write_fd)
        self.__mixer = subprocess.Popen(self.__build_mixer_command(),
                                        shell=False,
                                        start_new_session=True,
                                        pass_fds=pass_fds,
                                        stdin=subprocess.PIPE,
                                        )

    def stop(self):
        """Stops the sink mixer"""
        if self.__mixer is not None:
            self.__mixer.kill()
            self.__mixer.wait()

    def update_active_sources(self):
        """Updates active sources"""
        original_fds: List[int] = self.read_fds
        self.read_fds = []
        for source in self.sources:
            #if source.is_open.value:
            self.read_fds.append(source.source_output_fd)
        if original_fds != self.read_fds:
            self.stop()
            self.start()

    def __build_mixer_command(self) -> List[str]:
        """Builds Command to run"""
        command: List[str] = []
        command.extend([#"/usr/bin/valgrind", "--tool=callgrind", "--dump-instr=yes",
                        "c_utils/bin/sink_audio_mixer",
                        str(self.sink_info.ip),
                        str(self.sink_info.port),
                        str(self.sink_info.bit_depth),
                        str(self.__output_info.sample_rate),
                        str(self.__output_info.channels),
                        str(self.__output_info.header[3]),
                        str(self.__output_info.header[4]),
                        str(0 if self.tcp_client_fd is None else self.tcp_client_fd),
                        str(self.ffmpeg_mp3_write_fd)])
        for fd in self.read_fds:
            command.append(str(fd))
        return command
