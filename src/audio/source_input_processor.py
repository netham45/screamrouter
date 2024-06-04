"""Holds the Source Info and a thread for handling it's queue"""
import multiprocessing
import multiprocessing.sharedctypes
import os
import select
import time
from ctypes import c_bool, c_double
from subprocess import TimeoutExpired
from typing import Optional
import numpy

from src.audio.source_ffmpeg_processor import SourceFFMpegProcessor
from src.constants import constants
from src.audio.scream_header_parser import ScreamHeader
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import IPAddressType
from src.screamrouter_types.configuration import Equalizer, SourceDescription
from src.utils.utils import close_all_pipes, close_pipe, set_process_name

logger = get_logger(__name__)

class SourceInputProcessor(multiprocessing.Process):
    """Stores the status for a single Source to a single Sink
       Handles writing from a queue to an output processor pipe"""
    def __init__(self, tag: str,
                 sink_ip: Optional[IPAddressType],
                 source_info: SourceDescription,
                 sink_info: ScreamHeader):
        """Initializes a new Source object"""
        super().__init__(name=f"[Sink:{sink_ip}][Source:{tag}] Input Processor")
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
        self.running = multiprocessing.Value(c_bool, True)
        """Multiprocessing-passed flag to determine if the thread is running"""
        self.wants_restart = multiprocessing.Value(c_bool, False)
        """Set to true to indicate this thread wants a config reload due to a source change"""
        self.sink_info = sink_info
        """Holds the active sink info to see if ffmpeg needs to be used"""
        self.using_ffmpeg: bool = (
            self.stream_attributes.sample_rate != self.sink_info.sample_rate or
            self.stream_attributes.channels != self.sink_info.channels or
            self.stream_attributes.channel_layout != self.sink_info.channel_layout or
            self.source_info.equalizer != Equalizer() or
            self.source_info.delay != 0)

        if self.stream_attributes.sample_rate != self.sink_info.sample_rate:
            logger.debug("[Source %s] Using ffmpeg because sample rate", self.source_info.ip)
        if self.stream_attributes.channels != self.sink_info.channels:
            logger.debug("[Source %s] Using ffmpeg because channels", self.source_info.ip)
        if self.stream_attributes.channel_layout != self.sink_info.channel_layout:
            logger.debug("[Source %s] Using ffmpeg because channel layout", self.source_info.ip)
        if self.source_info.equalizer != Equalizer():
            logger.debug("[Source %s] Using ffmpeg because equalizer", self.source_info.ip)
        if self.source_info.delay != 0:
            logger.debug("[Source %s] Using ffmpeg because delay", self.source_info.ip)
        if self.stream_attributes.sample_rate != self.sink_info.sample_rate:
            print("Sample Rate Difference")
        if self.stream_attributes.channels != self.sink_info.channels:
            print("Channels Difference")
        if self.stream_attributes.channel_layout != self.sink_info.channel_layout:
            print("Channel Layout Difference")
        if self.source_info.equalizer != Equalizer():
            print("Equalizer Difference")
            print(self.source_info.equalizer)
            print(Equalizer())
        if self.source_info.delay != float(0):
            print("Delay Difference")
        self.ffmpeg_read: int
        self.ffmpeg_write: int
        self.ffmpeg_read, self.ffmpeg_write = os.pipe()
        self.ffmpeg_handler: Optional[SourceFFMpegProcessor] = None
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
                                    f"at {self.stream_attributes.sample_rate}k",
                                    f"{self.stream_attributes.channel_layout} layout is now ",
                                    f"{parsed_scream_header.bit_depth}-bit at ",
                                    f"{parsed_scream_header.sample_rate}k",
                                    f"{parsed_scream_header.channel_layout} layout."]))
                self.stream_attributes = parsed_scream_header
                self.wants_restart.value = c_bool(True)
                if self.using_ffmpeg:
                    if not self.ffmpeg_handler is None:
                        self.ffmpeg_handler.stop()
                    self.ffmpeg_handler = SourceFFMpegProcessor(
                        f"[Sink: {self.__sink_ip}][Source: {self.source_info.ip}]FFMpeg",
                        self.source_input_fd,
                        self.ffmpeg_read,
                        self.source_info,
                        self.stream_attributes,
                        self.sink_info)
            self.update_activity()
            self.is_open.value = c_bool(True)

    def check_if_inactive(self) -> None:
        """Looks for old pipes that are open and closes them"""
        if self.is_open.value and not self.is_active(constants.SOURCE_INACTIVE_TIME_MS):
            logger.info("[Sink:%s][Source:%s] Closing (Timeout = %sms)",
                        self.__sink_ip,
                        self.tag,
                        constants.SOURCE_INACTIVE_TIME_MS)
            self.is_open.value = c_bool(False)
            os.write(self.source_input_fd, bytes([0] * constants.PACKET_DATA_SIZE))

    def stop(self) -> None:
        """Fully stops and closes the source, closes fifo handles"""
        self.running.value = c_bool(False)
        if self.is_open.value:
            self.is_open.value = c_bool(False)
        logger.info("[Sink:%s][Source:%s] Stopping", self.__sink_ip, self.tag)
        if not self.ffmpeg_handler is None:
            self.ffmpeg_handler.stop()
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
        close_pipe(self.ffmpeg_read)
        close_pipe(self.ffmpeg_write)
        close_pipe(self.source_output_fd)
        close_pipe(self.source_input_fd)

    def write(self, data: bytes) -> None:
        """Writes data to this source's FIFO"""
        os.write(self.writer_write, data)
        self.update_activity()

    def equalizer(self, data: numpy.ndarray):
        """Equalizer"""
        return data

    def run(self) -> None:
        """This loop reads from the writer's pipe and writes it to the output processor"""
        set_process_name("Source Writer",
                         f"[Sink:{self.__sink_ip}][Source:{self.tag}] Input Processor")
        logger.debug("[Sink %s Source %s] Source Input Thread PID %s",
                     self.__sink_ip,
                     self.tag,
                     os.getpid())
        samples_left_over = numpy.array([], numpy.int32)

        if self.using_ffmpeg:
            self.ffmpeg_handler = SourceFFMpegProcessor(
                f"[Sink: {self.__sink_ip}][Source: {self.source_info.ip}] FFMpeg",
                self.source_input_fd,
                self.ffmpeg_read,
                self.source_info,
                self.stream_attributes,
                self.sink_info)
        while self.running.value:
            self.check_if_inactive()
            ready = select.select([self.writer_read], [], [], .01)
            if ready[0]:
                data: bytes = os.read(self.writer_read, constants.PACKET_SIZE)
                self.update_source_attributes_and_open_source(data[:constants.PACKET_HEADER_SIZE])
                if self.using_ffmpeg:
                    os.write(self.ffmpeg_write, data[constants.PACKET_HEADER_SIZE:])
                elif self.stream_attributes.bit_depth == 16:
                    pcm_data = numpy.frombuffer(data[constants.PACKET_HEADER_SIZE:], numpy.int16)
                    pcm_data = numpy.array(pcm_data * self.source_info.volume, numpy.int32)
                    pcm_data = numpy.left_shift(pcm_data, 16)
                    # Two 32-bit packets per upscaled 16-bit packet
                    pcm_data = self.equalizer(pcm_data)
                    os.write(self.source_input_fd,
                            pcm_data[:constants.PACKET_DATA_SIZE_INT32].tobytes())
                    os.write(self.source_input_fd,
                            pcm_data[constants.PACKET_DATA_SIZE_INT32:].tobytes())
                elif self.stream_attributes.bit_depth == 24:
                    # Pad 24-bit to make it 32-bit
                    pcm_data = numpy.frombuffer(data[constants.PACKET_HEADER_SIZE:], numpy.int8)
                    pcm_data = numpy.insert(pcm_data, range(0, len(pcm_data), 3), 0)
                    pcm_data = numpy.frombuffer(pcm_data, numpy.int32)
                    pcm_data = numpy.array(pcm_data * self.source_info.volume, numpy.int32)
                    pcm_data = numpy.insert(pcm_data, 0, samples_left_over)
                    os.write(self.source_input_fd,
                            pcm_data[:constants.PACKET_DATA_SIZE_INT32].tobytes())
                    if len(pcm_data) >= (constants.PACKET_DATA_SIZE_INT32 * 2):
                        os.write(self.source_input_fd,
                            pcm_data[constants.PACKET_DATA_SIZE_INT32:
                                    (constants.PACKET_DATA_SIZE_INT32*2)].tobytes())
                        samples_left_over = pcm_data[(constants.PACKET_DATA_SIZE_INT32 * 2):]
                    else:
                        samples_left_over = pcm_data[constants.PACKET_DATA_SIZE_INT32:]
                elif self.stream_attributes.bit_depth == 32:
                    pcm_data = numpy.frombuffer(data[constants.PACKET_HEADER_SIZE:], numpy.int32)
                    pcm_data = numpy.array(pcm_data * self.source_info.volume, numpy.int32)
                    os.write(self.source_input_fd, pcm_data.tobytes())
                self.update_activity()
        if not self.ffmpeg_handler is None:
            self.ffmpeg_handler.stop()
        close_all_pipes()
