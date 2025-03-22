"""Holds the Source Info and a thread for handling it's queue"""
import os
import subprocess
import threading
import time
from typing import List, Optional

from src.audio.scream_header_parser import ScreamHeader
from src.constants import constants
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import (DelayType, IPAddressType,
                                                TimeshiftType, VolumeType)
from src.screamrouter_types.configuration import Equalizer, SourceDescription
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
        self.data_output_fd: int
        """Listened to for new IP addresses to consider connected"""
        self.data_input_fd: int
        """Passed to the listener for it to send data back to Python"""
        self.data_output_fd, self.data_input_fd = os.pipe()
        os.set_blocking(self.data_input_fd, False)
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

        self.running: bool = True
        """Whether or not the source is currently running"""
        self.logging_thread = threading.Thread(target=self.__log_output)
        """Thread to log output from process"""
        self.logging_thread.start()

    def __log_output(self):
        try:
            while self.running:
                if self.__processor is not None:
                    data = self.__processor.stdout.readline().decode('utf-8').strip()
                    if not data:
                        break
                    logger.info("[Source: %s] %s", self.tag, data)
                else:
                    time.sleep(1)
        except OSError as e:
            logger.error("Error in logging thread for source %s: %s", self.tag, e)

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
        self.running = False
        self.logging_thread.join()

    def __build_command(self) -> List[str]:
        """Builds Command to run"""
        command: List[str] = []
        command.extend([#"/usr/bin/valgrind", "--tool=callgrind",
                        "c_utils/bin/source_input_processor",
                        str(self.source_info.tag if self.source_info.tag is not None
                            else self.source_info.ip),
                        str(self.writer_read),
                        str(self.source_input_fd),
                        str(self.data_output_fd),
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
                        str(self.source_info.delay),
                        str(constants.TIMESHIFT_DURATION)
                        ])
        return command

    def update_equalizer(self, equalizer: Equalizer) -> None:
        """Updates the equalizer for this source"""
        self.source_info.equalizer = equalizer
        self.send_command("b1", equalizer.b1)
        self.send_command("b2", equalizer.b2)
        self.send_command("b3", equalizer.b3)
        self.send_command("b4", equalizer.b4)
        self.send_command("b5", equalizer.b5)
        self.send_command("b6", equalizer.b6)
        self.send_command("b7", equalizer.b7)
        self.send_command("b8", equalizer.b8)
        self.send_command("b9", equalizer.b9)
        self.send_command("b10", equalizer.b10)
        self.send_command("b11", equalizer.b11)
        self.send_command("b12", equalizer.b12)
        self.send_command("b13", equalizer.b13)
        self.send_command("b14", equalizer.b14)
        self.send_command("b15", equalizer.b15)
        self.send_command("b16", equalizer.b16)
        self.send_command("b17", equalizer.b17)
        self.send_command("b18", equalizer.b18)
        self.send_command("a") # Apply
        self.source_info.equalizer = equalizer

    def update_volume(self, volume: VolumeType):
        """Updates the volume for this source"""
        self.send_command("v", volume)
        self.source_info.volume = volume

    def update_delay(self, delay: DelayType):
        """Updates the volume for this source"""
        self.send_command("d", delay)
        self.source_info.delay = delay

    def update_timeshift(self, timeshift: TimeshiftType):
        """Updates the volume for this source"""
        self.send_command("t", timeshift)
        self.source_info.timeshift = timeshift

    def send_command(self, command, value=None) -> None:
        """Sends a command to the source_input_processor"""
        message: str = command
        if value is not None:
            message += " " + str(value)
        message += "\n"
        logger.debug("Sending command %s value %s", command, value)
        os.write(self.data_input_fd, message.encode())

    def start(self) -> None:
        """Starts the source_input_processor process"""
        pass_fds: List[int] = [self.writer_read, self.source_input_fd, self.data_output_fd]
        self.__processor = subprocess.Popen(self.__build_command(),
                                            shell=False,
                                            start_new_session=True,
                                            pass_fds=pass_fds,
                                            stdin=subprocess.PIPE,
                                            stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT,
                                            )
