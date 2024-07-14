"""Holds the Source Info and a thread for handling it's queue"""
import multiprocessing
import multiprocessing.sharedctypes
import os
import subprocess
from typing import List, Optional

from src.audio.scream_header_parser import ScreamHeader
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import IPAddressType
from src.screamrouter_types.configuration import SourceDescription
from src.utils.utils import close_pipe

logger = get_logger(__name__)

class SourceInputProcessor():
    """Stores the status for a single Source to a single Sink
       Handles writing from a queue to an output processor pipe"""
    def __init__(self, tag: str,
                 sink_ip: Optional[IPAddressType],
                 source_info: SourceDescription,
                 sink_info: ScreamHeader):
        """Initializes a new Source object"""
        self.source_info = source_info
        """Source Description for this source"""
        self.tag: str = tag
        """The source's tag, generally it's IP."""
        self.source_output_fd: int
        """Passed to the source output handler at start"""
        self.source_input_fd: int
        """Written to by the input processor to write to the output processor"""
        self.source_output_fd, self.source_input_fd = os.pipe()
        self.__sink_ip: Optional[IPAddressType] = sink_ip
        """The sink that opened this source, used for logs"""
        self.writer_read: int
        """Holds the pipe for the Audio Controller to write to the writer"""
        self.writer_write: int
        """Holds the pipe for the writer to read input from the Audio Controller"""
        self.writer_read, self.writer_write = os.pipe()
        self.sink_info = sink_info
        """Holds the active sink info to see if ffmpeg needs to be used"""
        self.__processor: Optional[subprocess.Popen] = None
        """Processor process"""
        self.start()

    def stop(self) -> None:
        """Fully stops and closes the source, closes fifo handles"""
        logger.info("[Sink:%s][Source:%s] Stopping", self.__sink_ip, self.tag)
        if self.__processor is not None:
                self.__processor.kill()
                self.__processor.wait()
        close_pipe(self.writer_read)
        close_pipe(self.writer_write)
        close_pipe(self.source_output_fd)
        close_pipe(self.source_input_fd)

    def write(self, data: bytes) -> None:
        """Writes data to this source's FIFO"""
        os.write(self.writer_write, data)

    def __build_command(self) -> List[str]:
        """Builds Command to run"""
        command: List[str] = []
        command.extend(["c_utils/bin/source_input_processor",
                        str(self.writer_read),
                        str(self.source_input_fd),
                        str(self.sink_info.channels),
                        str(self.sink_info.sample_rate),
                        str(self.sink_info.header[3]),
                        str(self.sink_info.header[4]),
                        str(self.source_info.volume),
                        str(self.source_info.equalizer.b1),
                        str(self.source_info.equalizer.b2),
                        str(self.source_info.equalizer.b3),
                        str(self.source_info.equalizer.b4),
                        str(self.source_info.equalizer.b5),
                        str(self.source_info.equalizer.b6),
                        str(self.source_info.equalizer.b7),
                        str(self.source_info.equalizer.b8),
                        str(self.source_info.equalizer.b9),
                        str(self.source_info.equalizer.b10),
                        str(self.source_info.equalizer.b11),
                        str(self.source_info.equalizer.b12),
                        str(self.source_info.equalizer.b13),
                        str(self.source_info.equalizer.b14),
                        str(self.source_info.equalizer.b15),
                        str(self.source_info.equalizer.b16),
                        str(self.source_info.equalizer.b17),
                        str(self.source_info.equalizer.b18),
                        str(self.source_info.delay)
                        ])
        print(command)
        return command

    def start(self) -> None:
        """Starts the source_input_processor process"""
        pass_fds: List[int] = [self.writer_read, self.source_input_fd]
        self.__processor = subprocess.Popen(self.__build_command(),
                                            shell=False,
                                            start_new_session=True,
                                            pass_fds=pass_fds,
                                            stdin=subprocess.PIPE,
                                            )
