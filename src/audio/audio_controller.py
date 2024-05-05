"""One audio controller per sink, handles taking in sources and tracking ffmpeg"""
import multiprocessing
import os
import queue
import sys
import threading
from ctypes import c_bool
from queue import Empty
from typing import Dict, List

import src.constants.constants as constants
from src.api.api_webstream import APIWebStream
from src.audio.ffmpeg_process_handler import FFMpegHandler
from src.audio.output_threads import MP3OutputThread, PCMOutputThread
from src.audio.scream_header_parser import ScreamHeader, create_stream_info
from src.audio.source_input_writer import SourceInputThread
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.configuration import (SinkDescription,
                                                  SourceDescription)
from src.screamrouter_types.packets import FFMpegInputQueueEntry

logger = get_logger(__name__)

class AudioController(multiprocessing.Process):
    """Handles ffmpeg, keeps a list of sources, sends passed data to the appropriate pipe
       One AudioController per active real output sink
       This thread listens to the input queue and sends it to ffmpeg"""

    def __init__(self, sink_info: SinkDescription,
                 sources: List[SourceDescription], websocket: APIWebStream):
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
        self.__stream_info: ScreamHeader = create_stream_info(sink_info.bit_depth,
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
        self.sources: Dict[str, SourceInputThread] = {}
        """Sources this Sink is playing"""
        self.__fifo_in_pcm_read: int
        """ffmpeg PCM output read file descriptor"""
        self.__fifo_in_pcm_write: int
        """ffmpeg PCM output write file descriptor"""
        self.__fifo_in_pcm_read, self.__fifo_in_pcm_write = os.pipe()
        self.__fifo_in_mp3_read: int
        """ffmpeg MP3 output read file descriptor"""
        self.__fifo_in_mp3_write: int
        """ffmpeg MP3 output write file descriptor"""
        self.__fifo_in_mp3_read, self.__fifo_in_mp3_write = os.pipe()
        self.__ffmpeg: FFMpegHandler = FFMpegHandler(self.sink_info.ip,
                                                     self.__fifo_in_pcm_write,
                                                     self.__fifo_in_mp3_write,
                                                     self.__get_open_sources(),
                                                     self.__stream_info,
                                                     self.sink_info.equalizer)
        """ffmpeg handler"""
        self.__webstream: APIWebStream = websocket
        """Holds the websock object to copy audio to, passed through to MP3 listener thread"""
        self.__pcm_thread: PCMOutputThread = PCMOutputThread(self.__fifo_in_pcm_read,
                                                             self.__fifo_in_pcm_write,
                                                             self.sink_info.ip,
                                                             self.sink_info.port,
                                                             self.__stream_info)
        """Holds the thread to listen to PCM output from ffmpeg"""

        self.__mp3_thread: MP3OutputThread = MP3OutputThread(self.__fifo_in_mp3_read,
                                                             self.__fifo_in_mp3_write,
                                                             self.sink_info.ip,
                                                             self.__webstream.queue)
        """Holds the thread to listen to MP3 output from ffmpeg"""
        self.sources_lock: threading.Lock = threading.Lock()
        """This lock ensures the Sources list is only accessed by one thread at a time"""
        self.running = multiprocessing.Value(c_bool, True)
        """Multiprocessing-passed flag to determine if the thread is running"""
        self.queue: multiprocessing.Queue = multiprocessing.Queue()

        if self.sources_lock.acquire(timeout=1):
            for source in self.__controller_sources:
                self.sources[str(source.ip)] = SourceInputThread(str(source.ip),  # Tag, not IP
                                                                    self.sink_info.ip,
                                                                    source)
            self.sources_lock.release()
        else:
            raise TimeoutError("Failed to acquire sources lock")
        self.start()

    def __get_open_sources(self) -> List[SourceInputThread]:
        """Build a list of active IPs, exclude ones that aren't open"""
        active_sources: List[SourceInputThread] = []
        for _, source in self.sources.items():
            if source.is_open:
                active_sources.append(source)
        return active_sources

    def __check_for_inactive_sources(self) -> None:
        """Looks for old pipes that are open and closes them"""
        if self.sources_lock.acquire(timeout=.2):
            for _, source in self.sources.items():
                if source.is_open and not source.is_active(constants.SOURCE_INACTIVE_TIME_MS):
                    logger.info("[Sink:%s][Source:%s] Closing (Timeout = %sms)",
                                self.sink_info.ip,
                                source.tag,
                                constants.SOURCE_INACTIVE_TIME_MS)
                    source.is_open = False
                    self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())
            self.sources_lock.release()

    def __update_source_attributes_and_open_source(self,
                                                   source: SourceInputThread,
                                                   header: bytes) -> None:
        """Opens and verifies the target pipe header matches what we have, updates it if not."""
        if not source.is_open:
            parsed_scream_header = ScreamHeader(header)
            if not source.stream_attributes == parsed_scream_header:
                logger.debug("".join([f"[Sink:{self.sink_info.ip}][Source:{source.tag}] ",
                                    "Closing source, stream attribute change detected. ",
                                    f"Was: {source.stream_attributes.bit_depth}-bit ",
                                    f"at {source.stream_attributes.sample_rate}kHz ",
                                    f"{source.stream_attributes.channel_layout} layout is now ",
                                    f"{parsed_scream_header.bit_depth}-bit at ",
                                    f"{parsed_scream_header.sample_rate}kHz ",
                                    f"{parsed_scream_header.channel_layout} layout."]))
                source.stream_attributes = parsed_scream_header
            source.update_activity()
            source.is_open = True
            self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())

    def process_packet_from_queue(self, entry: FFMpegInputQueueEntry) -> None:
        """Callback for the queue thread to pass packets for processing"""
        if entry.tag in self.sources:
            source = self.sources[entry.tag]
            self.__update_source_attributes_and_open_source(source, entry.data[:5])
            self.__check_for_inactive_sources()
            source.write(entry.data[5:])  # Write the data to the output fifo

    def stop(self) -> None:
        """Stops the Sink, closes all handles"""
        logger.debug("[Sink:%s] Stopping PCM thread", self.sink_info.ip)
        self.__pcm_thread.stop()
        logger.debug("[Sink:%s] Stopping MP3 thread", self.sink_info.ip)
        self.__mp3_thread.stop()
        logger.debug("[Sink:%s] Stopping ffmpeg thread", self.sink_info.ip)
        self.__ffmpeg.stop()
        logger.debug("[Sink:%s] Stopping sources", self.sink_info.ip)
        for _, source in self.sources.items():
            logger.debug("[Sink:%s] Stopping source %s", self.sink_info.ip, source.name)
            source.stop()
            logger.debug("[Sink:%s] Stopped source %s", self.sink_info.ip, source.name)
        self.running.value = c_bool(False)
        logger.debug("[Sink:%s] Stopping Audio Controller", self.sink_info.ip)
        while True:
            try:
                self.queue.get_nowait()
            except queue.Empty:
                break
            except ValueError:
                break
        self.wait_for_threads_to_stop()

    def wait_for_threads_to_stop(self) -> None:
        """Waits for threads to stop"""
        if constants.WAIT_FOR_CLOSES:
            logger.debug("[Sink:%s] Waiting for Audio Controller Stop", self.sink_info.ip)
            self.join()
            self.close()
            logger.debug("[Sink:%s] Audio Controller stopped", self.sink_info.ip)
            logger.info("[Sink:%s] Stopped", self.sink_info.ip)

    def run(self) -> None:
        """Constantly checks the queue
            notifies the Sink Controller callback when there's something in the queue"""
        logger.debug("[Sink:%s] PID %s", self.sink_info.ip, os.getpid())
        while self.running.value:
            if os.getppid() == 1:
                logger.warning("Exiting because parent pid is 1")
                self.stop()
                sys.exit(3)
            try:
                self.process_packet_from_queue(self.queue.get(timeout=.1))
            except Empty:
                pass
        while True:
            try:
                self.queue.get_nowait()
            except queue.Empty:
                break
            except ValueError:
                break
        self.queue.close()
        self.queue.join_thread()
