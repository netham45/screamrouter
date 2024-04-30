"""Controls the sink, keeps track of associated sources, and holds the ffmpeg thread object"""
import pathlib
import threading
from typing import List, Optional

from api.api_webstream import APIWebStream
from screamrouter_types import DelayType
from screamrouter_types import SinkDescription, SourceDescription
from audio.ffmpeg_process_handler import FFMpegHandler
from audio.ffmpeg_input_queue import FFMpegInputQueue, FFMpegInputQueueEntry
from audio.source_to_ffmpeg_writer import SourceToFFMpegWriter
from audio.stream_info import StreamInfo, create_stream_info
from audio.ffmpeg_output_threads import FFMpegMP3Thread, FFMpegPCMThread


from logger import get_logger

logger = get_logger(__name__)

class SinkController():
    """Handles ffmpeg, keeps a list of sources, sends passed data to the appropriate pipe"""
    def __init__(self, sink_info: SinkDescription,
                 sources: List[SourceDescription], websocket: Optional[APIWebStream]):
        """Initialize a sink"""
        super().__init__()
        self.sink_info = sink_info
        """Sink Info"""
        self.__stream_info: StreamInfo = create_stream_info(sink_info.bit_depth,
                                                            sink_info.sample_rate,
                                                            sink_info.channels,
                                                            sink_info.channel_layout)
        """Output stream info"""
        if self.sink_info.ip is None:
            raise ValueError("Running Sink Controller IP can't be null")
        if self.sink_info.port is None:
            raise ValueError("Running Sink Controller Port can't be null")
        self.__controller_sources: List[SourceDescription] = sources
        """Sources this Sink has"""
        self.sources: List[SourceToFFMpegWriter] = []
        """Sources this Sink is playing"""
        self.__pipe_path_base: str = f"./pipes/scream-{self.sink_info.ip}-"
        """Per-sink pipe path"""
        self.__fifo_in_pcm: pathlib.Path = pathlib.Path(self.__pipe_path_base + "in-pcm")
        """Input file from ffmpeg PCM output"""
        self.__fifo_in_mp3: pathlib.Path = pathlib.Path(self.__pipe_path_base + "in-mp3")
        """Input file from ffmpeg MP3 output"""
        self.__ffmpeg: FFMpegHandler = FFMpegHandler(self.sink_info.ip,
                                                     pathlib.Path(self.__fifo_in_pcm),
                                                     pathlib.Path(self.__fifo_in_mp3),
                                                     self.__get_open_sources(),
                                                     self.__stream_info,
                                                     self.sink_info.equalizer,
                                                     self.sink_info.delay)
        """ffmpeg handler"""
        self.__webstream: Optional[APIWebStream] = websocket
        """Holds the websock object to copy audio to, passed through to MP3 listener thread"""
        self.__pcm_thread: FFMpegPCMThread = FFMpegPCMThread(pathlib.Path(self.__fifo_in_pcm),
                                                             self.sink_info.ip,
                                                             self.sink_info.port,
                                                             self.__stream_info)
        """Holds the thread to listen to PCM output from ffmpeg"""
        self.__mp3_thread: FFMpegMP3Thread = FFMpegMP3Thread(pathlib.Path(self.__fifo_in_mp3),
                                                             self.sink_info.ip,
                                                             self.__webstream)
        """Holds the thread to listen to MP3 output from ffmpeg"""
        self.__queue_thread: FFMpegInputQueue = FFMpegInputQueue(self.process_packet_from_queue,
                                                                 self.sink_info.ip)
        """Holds the thread to listen to the input queue and send it to ffmpeg"""
        self.lock: threading.Lock = threading.Lock()
        """Lock to ensure the controller is only accessed by one other thread at a time"""

        for source in self.__controller_sources:
            pipename: pathlib.Path = pathlib.Path(self.__pipe_path_base + str(source.ip))
            self.sources.append(SourceToFFMpegWriter(str(source.ip),  # Tag, not IP
                                                     pipename,
                                                     self.sink_info.ip,
                                                     source.volume))

    def update_source_volume(self, controllersource: SourceDescription) -> None:
        """Updates the source volume to the specified volume.
           Does nothing if the source is not playing to this sink."""
        for source in self.sources:
            if source.tag == str(controllersource.ip):
                source.volume = controllersource.volume
                self.__ffmpeg.set_input_volume(source, controllersource.volume)

    def update_delay(self, delay: DelayType) -> None:
        """Updates the ffmpeg delay to the specified delay."""
        self.lock.acquire()
        self.__ffmpeg.set_delay(delay)
        self.lock.release()

    def __get_open_sources(self) -> List[SourceToFFMpegWriter]:
        """Build a list of active IPs, exclude ones that aren't open"""
        active_sources: List[SourceToFFMpegWriter] = []
        for source in self.sources:
            if source.is_open():
                active_sources.append(source)
        return active_sources

    def __get_source_by_tag(self, tag: str) -> Optional[SourceToFFMpegWriter]:
        """Gets a SourceInfo by IP address"""
        for source in self.sources:
            if source.tag == tag:
                return source
        return None

    def __check_for_inactive_sources(self) -> None:
        """Looks for old pipes that are open and closes them"""
        for source in self.sources:
            if not source.is_active(300) and source.is_open():
                logger.info("[Sink:%s][Source:%s] Closing (Timeout = 300ms)",
                            self.sink_info.ip,
                            source.tag)
                source.close()
                self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())

    def __update_source_attributes_and_open_source(self,
                                                   source: SourceToFFMpegWriter,
                                                   header: bytes) -> None:
        """Opens and verifies the target pipe header matches what we have, updates it if not."""
        parsed_scream_header = StreamInfo(header)
        if not source.check_attributes(parsed_scream_header):
            logger.debug("".join([f"[Sink:{self.sink_info.ip}][Source:{source.tag}] ",
                                  "Closing source, stream attribute change detected. ",
                                 f"Was: {source.stream_attributes.bit_depth}-bit ",
                                 f"at {source.stream_attributes.sample_rate}kHz ",
                                 f"{source.stream_attributes.channel_layout} layout is now ",
                                 f"{parsed_scream_header.bit_depth}-bit at ",
                                 f"{parsed_scream_header.sample_rate}kHz ",
                                 f"{parsed_scream_header.channel_layout} layout."]))
            source.set_attributes(parsed_scream_header)
            source.close()
        if not source.is_open():
            source.open()
            self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())

    def process_packet_from_queue(self, entry: FFMpegInputQueueEntry) -> None:
        """Callback for the queue thread to pass packets for processing"""
        source: Optional[SourceToFFMpegWriter]
        source = self.__get_source_by_tag(entry.tag)
        if not source is None:
            self.__check_for_inactive_sources()
            self.__update_source_attributes_and_open_source(source, entry.data[:5])
            source.write(entry.data[5:])  # Write the data to the output fifo

    def add_packet_to_queue(self, tag: str, data: bytes) -> None:
        """Sends data from the main receiver to ffmpeg after verifying it's on our list"""
        self.__queue_thread.queue(FFMpegInputQueueEntry(tag, data))

    def stop(self) -> None:
        """Stops the Sink, closes all handles"""
        self.lock.acquire()
        logger.debug("[Sink:%s] Stopping PCM thread", self.sink_info.ip)
        self.__pcm_thread.stop()
        logger.debug("[Sink:%s] Stopping MP3 thread", self.sink_info.ip)
        self.__mp3_thread.stop()
        logger.debug("[Sink:%s] Stopping Queue thread", self.sink_info.ip)
        self.__queue_thread.stop()
        logger.debug("[Sink:%s] Stopping ffmpeg thread", self.sink_info.ip)
        self.__ffmpeg.stop()
        logger.debug("[Sink:%s] Stopping sources", self.sink_info.ip)
        for source in self.sources:
            source.stop()
        self.lock.release()

    def wait_for_threads_to_stop(self) -> None:
        """Waits for threads to stop"""
        self.lock.acquire()
        self.__pcm_thread.join()
        logger.debug("[Sink:%s] PCM thread stopped", self.sink_info.ip)
        self.__mp3_thread.join()
        logger.debug("[Sink:%s] MP3 thread stopped", self.sink_info.ip)
        self.__queue_thread.join()
        logger.debug("[Sink  %s] Queue thread stopped", self.sink_info.ip)
        self.__ffmpeg.join()
        logger.debug("[Sink:%s] ffmpeg thread stopped", self.sink_info.ip)
        for source in self.sources:
            source.join()
        logger.debug("[Sink:%s] sources stopped", self.sink_info.ip)
        logger.info("[Sink:%s] Stopped", self.sink_info.ip)
        self.lock.release()

    def url_playback_done_callback(self, tag: str):
        """Callback for ffmpeg to clean up when playback is done, tag is the Source tag"""
        for source in self.sources:
            if source.tag == tag:
                source.close()
                source.stop()
                self.sources.remove(source)
