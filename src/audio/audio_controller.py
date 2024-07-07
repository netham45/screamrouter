"""One audio controller per sink, handles taking in packets and distributing them to sources"""
import multiprocessing
import os
import select
import threading
from ctypes import c_bool
from queue import Empty
from subprocess import TimeoutExpired
from typing import Dict, List, Optional

from src.audio.mp3_ffmpeg_process import MP3FFMpegProcess
from src.audio.sink_mp3_processor import SinkMP3Processor
import src.constants.constants as constants
from src.api.api_webstream import APIWebStream
from src.audio.source_input_processor import SourceInputProcessor
from src.audio.sink_output_mixer import SinkOutputMixer
from src.audio.scream_header_parser import ScreamHeader, create_stream_info
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.configuration import (SinkDescription,
                                                  SourceDescription)
from src.utils.utils import close_all_pipes, close_pipe, set_process_name

logger = get_logger(__name__)

class AudioController(multiprocessing.Process):
    """Handles a sink, keeps a list of sources, sends passed data to the appropriate pipe
       One AudioController per active real output sink
       This thread listens to the input queue and passes it to each Source processor"""

    def __init__(self, sink_info: SinkDescription,
                 sources: List[SourceDescription],
                 tcp_fd: Optional[int],
                 websocket: APIWebStream):
        """Initialize a sink queue"""
        super().__init__(name=f"[Sink {sink_info.name}] Audio Controller")
        logger.info("[Sink %s] Loading audio controller", sink_info.name)
        logger.info("[Sink %s] Sources:", sink_info.name)

        for source in sources:
            logger.info("[Sink %s] %s:%s", sink_info.name, source.name, source.ip)
        logger.info("[Sink %s] Stream mode: Bit Depth      : %s", sink_info.name,
                    sink_info.bit_depth)
        logger.info("[Sink %s]              Sample Rate    : %s", sink_info.name,
                    sink_info.sample_rate)
        logger.info("[Sink %s]              Channel Layout : %s", sink_info.name,
                    sink_info.channel_layout)
        logger.info("[Sink %s]              Channel Count  : %s", sink_info.name,
                    sink_info.channels)
        logger.info("[Sink %s] Outputting to: %s:%s", sink_info.name, sink_info.ip,
                    sink_info.port)
        self.sink_info = sink_info
        """Sink Info"""
        self.stream_info: ScreamHeader = create_stream_info(sink_info.bit_depth,
                                                              sink_info.sample_rate,
                                                              sink_info.channels,
                                                              sink_info.channel_layout)
        """Output stream info"""
        if self.sink_info.ip is None:
            raise ValueError("Running Sink Controller IP can't be None")
        if self.sink_info.port is None:
            raise ValueError("Running Sink Controller Port can't be None")
        self.__controller_sources: List[SourceDescription] = sources
        """Sources this Sink has"""
        self.sources: Dict[str, SourceInputProcessor] = {}
        """Sources this Sink is playing"""
        self.webstream: APIWebStream = websocket
        """Holds the websock object to copy audio to, passed through to MP3 listener thread"""
        self.sources_lock: threading.Lock = threading.Lock()
        """This lock ensures the Sources list is only accessed by one thread at a time"""
        self.controller_read_fd: int
        """Controller input write file descriptor"""
        self.controller_write_fd: int
        """Controller input read file descriptor"""
        self.controller_read_fd, self.controller_write_fd = os.pipe()
        self.running = multiprocessing.Value(c_bool, True)
        """Multiprocessing-passed flag to determine if the thread is running"""
        self.request_restart = multiprocessing.Value(c_bool, False)
        """Set true if we want a config reload"""
        logger.info("[Sink:%s] Queue %s", self.sink_info.ip, self.controller_write_fd)
        if self.sources_lock.acquire(timeout=1):
            for source in self.__controller_sources:
                tag: str = ""
                if source.ip is None:
                    if not source.tag is None:
                        tag = source.tag
                else:
                    tag = str(source.ip)
                self.sources[tag] = SourceInputProcessor(tag, self.sink_info.ip, source,
                                                 create_stream_info(self.sink_info.bit_depth,
                                                                    self.sink_info.sample_rate,
                                                                    self.sink_info.channels,
                                                                    self.sink_info.channel_layout))
            self.sources_lock.release()
        else:
            raise TimeoutError("Failed to acquire sources lock")
        self.mp3_ffmpeg_input_read: int
        self.mp3_ffmpeg_input_write: int
        self.mp3_ffmpeg_output_read: int
        self.mp3_ffmpeg_output_write: int
        self.mp3_ffmpeg_input_read, self.mp3_ffmpeg_input_write = os.pipe()
        self.mp3_ffmpeg_output_read, self.mp3_ffmpeg_output_write = os.pipe()
        self.pcm_thread: SinkOutputMixer
        """Holds the thread to listen to PCM output from a Source"""

        self.mp3_ffmpeg_processor = MP3FFMpegProcess(f"[Sink {self.sink_info.ip}] MP3 Process",
                                                       self.mp3_ffmpeg_output_write,
                                                       self.mp3_ffmpeg_input_read,
                                                       self.sink_info
                                                       )

        self.mp3_thread: SinkMP3Processor = SinkMP3Processor(self.sink_info.ip,
                                                           self.mp3_ffmpeg_output_read,
                                                           self.webstream.queue)
        """Holds the thread to generaet MP3 output from a PCM reader"""
        self.pcm_thread = SinkOutputMixer(self.sink_info,
                                          self.stream_info,
                                          tcp_fd,
                                          list(self.sources.values()),
                                          self.mp3_ffmpeg_input_write)
        self.start()

    def restart_mixer(self, tcp_fd: int):
        """(Re)starts the mixer"""
        logger.info("[Audio Controller] Requesting config reloaed")
        self.pcm_thread.tcp_client_fd = tcp_fd
        self.pcm_thread.update_active_sources()
        self.request_restart = c_bool(True)

    def get_open_sources(self) -> List[SourceInputProcessor]:
        """Build a list of active IPs, exclude ones that aren't open"""
        active_sources: List[SourceInputProcessor] = []
        for _, source in self.sources.items():
            if source.is_open.value:
                active_sources.append(source)
        return active_sources

    def process_packet_from_queue(self, entry: bytes) -> None:
        """Callback for the queue thread to pass packets for processing"""
        try:
            tag = entry[:constants.TAG_MAX_LENGTH].decode("ascii").split("\x00")[0]
        except UnicodeDecodeError as exc:
            logger.debug("Error decoding packet, discarding. Exception: %s", exc)
            return
        self.pcm_thread.update_active_sources()
        data: bytes = entry[constants.TAG_MAX_LENGTH:]
        if tag in self.sources:
            source = self.sources[tag]
            source.write(data)  # Write the data to the output fifo

    def stop(self) -> None:
        """Stops the Sink, closes all handles"""
        logger.debug("[Sink:%s] Stopping PCM thread", self.sink_info.ip)
        self.pcm_thread.stop()
        logger.debug("[Sink:%s] Stopping MP3 Receiver", self.sink_info.ip)
        self.mp3_thread.stop()
        logger.debug("[Sink:%s] Stopping ffmpeg MP3 Converter", self.sink_info.ip)
        self.mp3_ffmpeg_processor.stop()
        logger.debug("[Sink:%s] Stopping sources", self.sink_info.ip)
        for _, source in self.sources.items():
            logger.debug("[Sink:%s] Stopping source %s", self.sink_info.ip, source.name)
            source.stop()
            logger.debug("[Sink:%s] Stopped source %s", self.sink_info.ip, source.name)
        logger.debug("[Sink:%s] Stopping Audio Controller", self.sink_info.ip)
        self.running.value = c_bool(False) # type: ignore

        if constants.WAIT_FOR_CLOSES:
            logger.debug("[Sink:%s] Waiting for Audio Controller Stop", self.sink_info.ip)
            try:
                self.join(5)
            except TimeoutExpired:
                logger.warning("Audio Controller failed to close")
            logger.debug("[Sink:%s] Audio Controller stopped", self.sink_info.ip)
            logger.info("[Sink:%s] Stopped", self.sink_info.ip)
        close_pipe(self.controller_read_fd)
        close_pipe(self.controller_write_fd)

    def wants_reload(self) -> bool:
        """Returns true of any of the sources want a restart"""
        flag: bool = False
        if self.request_restart.value:
            return True
        for source in self.sources.values():
            if source.wants_restart.value:
                source.wants_restart.value = c_bool(False) # type: ignore
                flag = True
        return flag

    def run(self) -> None:
        """This loop checks the queue
            notifies the Sink Controller callback when there's something in the queue"""
        logger.debug("[Sink:%s] PID %s Write fd %s ",
                     self.sink_info.ip, os.getpid(), self.controller_write_fd)
        set_process_name(f"Sink{self.sink_info.name}",
                         f"[Sink {self.sink_info.name}] Audio Controller")

        while self.running.value:
            ready = select.select([self.controller_read_fd], [], [], .05)
            if ready[0]:
                try:
                    data = os.read(self.controller_read_fd,
                                   constants.PACKET_SIZE + constants.TAG_MAX_LENGTH)
                    self.process_packet_from_queue(data)
                except Empty:
                    pass
        close_all_pipes()
