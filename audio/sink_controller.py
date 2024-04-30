"""Controls the sink, keeps track of associated sources, and holds the ffmpeg thread object"""
from typing import List, Optional

from audio.ffmpeg_process_handler import FFMpegHandler
from audio.ffmpeg_input_queue import FFMpegInputQueue, FFMpegInputQueueEntry
from audio.source_to_ffmpeg_writer import SourceToFFMpegWriter
from audio.stream_info import StreamInfo, create_stream_info
from audio.ffmpeg_output_threads import FFMpegMP3Thread, FFMpegPCMThread

from configuration.configuration_types import SinkDescription, SourceDescription

from api.api_webstream import APIWebStream
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
        self.__channels: int = sink_info.channels
        """Number of channels the sink is configured for"""
        self.__bit_depth: int = sink_info.bit_depth
        """Bit depth the sink is configured for"""
        self.__sample_rate: int = sink_info.sample_rate
        """Sample rate the sink is configured for"""
        self.__channel_layout: str = sink_info.channel_layout
        """Channel layout the sink is configured for"""
        self.__stream_info: StreamInfo = create_stream_info(self.__bit_depth,
                                                            self.__sample_rate,
                                                            self.__channels,
                                                            self.__channel_layout)
        """Output stream info"""
        self.sink_ip: str = sink_info.ip
        """Sink IP"""
        self._sink_port: int = sink_info.port
        """Sink Port"""
        self.name: str = sink_info.name
        """Sink Name"""
        self.__controller_sources: List[SourceDescription] = sources
        """Sources this Sink has"""
        self.sources: List[SourceToFFMpegWriter] = []
        """Sources this Sink is playing"""
        self.__pipe_path: str = f"./pipes/scream-{self.sink_ip}-"
        """Per-sink pipe path"""
        self.__fifo_in_pcm: str = self.__pipe_path + "in-pcm"
        """Input file from ffmpeg PCM output"""
        self.__fifo_in_mp3: str = self.__pipe_path + "in-mp3"
        """Input file from ffmpeg MP3 output"""
        self.__ffmpeg: FFMpegHandler = FFMpegHandler(self.sink_ip,
                                                     self.__fifo_in_pcm,
                                                     self.__fifo_in_mp3,
                                                     self.__get_open_sources(),
                                                     self.__stream_info)
        """ffmpeg handler"""
        self.__webstream: Optional[APIWebStream] = websocket
        """Holds the websock object to copy audio to, passed through to MP3 listener thread"""
        self.__pcm_thread: FFMpegPCMThread = FFMpegPCMThread(self.__fifo_in_pcm,
                                                             self.sink_ip,
                                                             self._sink_port,
                                                             self.__stream_info)
        """Holds the thread to listen to PCM output from ffmpeg"""
        self.__mp3_thread: FFMpegMP3Thread = FFMpegMP3Thread(self.__fifo_in_mp3,
                                                             self.sink_ip,
                                                             self.__webstream)
        """Holds the thread to listen to MP3 output from ffmpeg"""
        self.__queue_thread: FFMpegInputQueue = FFMpegInputQueue(self.process_packet_from_queue,
                                                                 self.sink_ip)
        """Holds the thread to listen to the input queue and send it to ffmpeg"""

        for source in self.__controller_sources:
            self.sources.append(SourceToFFMpegWriter(source.ip,
                                                     self.__pipe_path + source.ip,
                                                     self.sink_ip,
                                                     source.volume))

    def update_source_volume(self, controllersource: SourceDescription) -> None:
        """Updates the source volume to the specified volume.
           Does nothing if the source is not playing to this sink."""
        for source in self.sources:
            if source.tag == controllersource.ip:
                source.volume = controllersource.volume
                self.__ffmpeg.set_input_volume(source, controllersource.volume)

    def __get_open_sources(self) -> List[SourceToFFMpegWriter]:
        """Build a list of active IPs, exclude ones that aren't open"""
        active_sources: List[SourceToFFMpegWriter] = []
        for source in self.sources:
            if source.is_open():
                active_sources.append(source)
        return active_sources

    def __get_source_by_ip(self, ip: str) -> Optional[SourceToFFMpegWriter]:
        """Gets a SourceInfo by IP address"""
        for source in self.sources:
            if source.tag == ip:
                return source
        return None

    def __check_for_inactive_sources(self) -> None:
        """Looks for old pipes that are open and closes them"""
        for source in self.sources:
            if not source.is_active(100) and source.is_open():
                logger.info("[Sink:%s][Source:%s] Closing (Timeout = 100ms)",
                            self.sink_ip,
                            source.tag)
                source.close()
                self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())

    def __update_source_attributes_and_open_source(self,
                                                   source: SourceToFFMpegWriter,
                                                   header: bytes) -> None:
        """Opens and verifies the target pipe header matches what we have, updates it if not."""
        parsed_scream_header = StreamInfo(header)
        if not source.check_attributes(parsed_scream_header):
            logger.debug("".join([f"[Sink:{self.sink_ip}][Source:{source.tag}] ",
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
        source = self.__get_source_by_ip(entry.source_ip)
        if not source is None:
            self.__check_for_inactive_sources()
            self.__update_source_attributes_and_open_source(source, entry.data[:5])
            source.write(entry.data[5:])  # Write the data to the output fifo

    def add_packet_to_queue(self, source_ip: str, data: bytes) -> None:
        """Sends data from the main receiver to ffmpeg after verifying it's on our list"""
        self.__queue_thread.queue(FFMpegInputQueueEntry(source_ip, data))

    def stop(self) -> None:
        """Stops the Sink, closes all handles"""
        logger.debug("[Sink:%s] Stopping PCM thread", self.sink_ip)
        self.__pcm_thread.stop()
        logger.debug("[Sink:%s] Stopping MP3 thread", self.sink_ip)
        self.__mp3_thread.stop()
        logger.debug("[Sink:%s] Stopping Queue thread", self.sink_ip)
        self.__queue_thread.stop()
        logger.debug("[Sink:%s] Stopping ffmpeg thread", self.sink_ip)
        self.__ffmpeg.stop()
        logger.debug("[Sink:%s] Stopping sources", self.sink_ip)
        for source in self.sources:
            source.stop()

    def wait_for_threads_to_stop(self) -> None:
        """Waits for threads to stop"""
        self.__pcm_thread.join()
        logger.debug("[Sink:%s] PCM thread stopped", self.sink_ip)
        self.__mp3_thread.join()
        logger.debug("[Sink:%s] MP3 thread stopped", self.sink_ip)
        self.__queue_thread.join()
        logger.debug("[Sink  %s] Queue thread stopped", self.sink_ip)
        self.__ffmpeg.join()
        logger.debug("[Sink:%s] ffmpeg thread stopped", self.sink_ip)
        for source in self.sources:
            source.join()
        logger.debug("[Sink:%s] sources stopped", self.sink_ip)
        logger.info("[Sink:%s] Stopped", self.sink_ip)

    def url_playback_done_callback(self, tag: str):
        """Callback for ffmpeg to clean up when playback is done, tag is the Source tag"""
        for source in self.sources:
            if source.tag == tag:
                source.close()
                source.stop()
                self.sources.remove(source)
