"""Holds the Source Info and a thread for handling it's queue"""
import multiprocessing
import multiprocessing.sharedctypes
import os
import select
from subprocess import TimeoutExpired
import time
from ctypes import c_bool, c_double
from typing import Optional

import src.constants.constants as constants
from src.audio.scream_header_parser import ScreamHeader
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import IPAddressType
from src.screamrouter_types.configuration import SourceDescription
from src.utils.utils import close_all_pipes, close_pipe, set_process_name

logger = get_logger(__name__)

class FFMpegInputWriter(multiprocessing.Process):
    """Stores the status for a single Source to a single Sink
       Handles writing from a queue to an ffmpeg pipe"""
    def __init__(self, tag: str,
                 sink_ip: Optional[IPAddressType], source_info: SourceDescription):
        """Initializes a new Source object"""
        super().__init__(name=f"[Sink:{sink_ip}][Source:{tag}] Pipe Writer")
        self.source_info = source_info
        """Source Description for this source"""
        self.tag: str = tag
        """The source's tag, generally it's IP."""
        self.is_open = multiprocessing.Value(c_bool, False)
        """Rather the Source is open for writing or not"""
        self.__last_data_time = multiprocessing.Value(c_double, 0)
        """The time in milliseconds we last received data"""
        self.stream_attributes: ScreamHeader = ScreamHeader(bytearray([0, 32, 2, 0, 0]))
        """The source stream attributes (bit depth, sample rate, channels)"""
        self.ffmpeg_read_fd: int
        """Passed to ffmpeg at start"""
        self.ffmpeg_write_fd: int
        """output to ffmpeg for encoding and mixing"""
        self.ffmpeg_read_fd, self.ffmpeg_write_fd = os.pipe()
        self.__sink_ip: Optional[IPAddressType] = sink_ip
        """The sink that opened this source, used for logs"""
        self.writer_read: int
        """Holds the pipe for the Audio Controller to write to the writer"""
        self.writer_write: int
        """Holds the pipe for the writer to read input from the Audio Controller"""
        self.writer_read, self.writer_write = os.pipe()
        self.running = multiprocessing.Value(c_bool, True)
        """Multiprocessing-passed flag to determine if the thread is running"""
        self.wants_restart = multiprocessing.Value(c_bool, False)
        """Set to true to indicate this thread wants an ffmpeg reload due to a source change"""
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

    def update_source_attributes_and_open_source(self,
                                                header: bytes) -> None:
        """Opens and verifies the target pipe header matches what we have, updates it if not."""
        if not self.is_open.value:
            parsed_scream_header = ScreamHeader(header)
            if self.stream_attributes != parsed_scream_header:
                logger.debug("".join([f"[Sink:{self.__sink_ip}][Source:{self.tag}] ",
                                    "Closing source, stream attribute change detected. ",
                                    f"Was: {self.stream_attributes.bit_depth}-bit ",
                                    f"at {self.stream_attributes.sample_rate}kHz ",
                                    f"{self.stream_attributes.channel_layout} layout is now ",
                                    f"{parsed_scream_header.bit_depth}-bit at ",
                                    f"{parsed_scream_header.sample_rate}kHz ",
                                    f"{parsed_scream_header.channel_layout} layout."]))
                self.stream_attributes = parsed_scream_header
            self.update_activity()
            self.is_open.value = c_bool(True)
            self.wants_restart.value = c_bool(True)

    def check_if_inactive(self) -> None:
        """Looks for old pipes that are open and closes them"""
        if self.is_open.value and not self.is_active(constants.SOURCE_INACTIVE_TIME_MS):
            logger.info("[Sink:%s][Source:%s] Closing (Timeout = %sms)",
                        self.__sink_ip,
                        self.tag,
                        constants.SOURCE_INACTIVE_TIME_MS)
            self.is_open.value = c_bool(False)
            self.wants_restart.value = c_bool(True)

    def stop(self) -> None:
        """Fully stops and closes the source, closes fifo handles"""
        self.running.value = c_bool(False)
        if self.is_open.value:
            self.is_open.value = c_bool(False)
            logger.info("[Sink:%s][Source:%s] Stopping", self.__sink_ip, self.tag)
        if constants.KILL_AT_CLOSE:
            try:
                self.kill()
            except AttributeError:
                pass
        if constants.WAIT_FOR_CLOSES:
            try:
                self.join(5)
            except TimeoutExpired:
                logger.warning("Input writer failed to close")

        close_pipe(self.ffmpeg_read_fd)
        close_pipe(self.ffmpeg_write_fd)

    def write(self, data: bytes) -> None:
        """Writes data to this source's FIFO"""
        os.write(self.writer_write, data)
        self.update_activity()

    def run(self) -> None:
        """This loop reads from the writer's pipe and writes it to ffmpeg"""
        set_process_name("Source Writer", f"[Sink:{self.__sink_ip}][Source:{self.tag}] Pipe Writer")
        logger.debug("[Sink %s Source %s] Source Input Thread PID %s",
                     self.__sink_ip,
                     self.tag,
                     os.getpid())
        while self.running.value:
            self.check_if_inactive()
            ready = select.select([self.writer_read], [], [], .3)
            if ready[0]:
                data: bytes = os.read(self.writer_read, constants.PACKET_SIZE)
                self.update_source_attributes_and_open_source(data[:5])
                os.write(self.ffmpeg_write_fd, data[5:])
                self.update_activity()
        close_all_pipes()
