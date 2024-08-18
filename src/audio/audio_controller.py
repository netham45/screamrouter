"""One audio controller per sink, handles taking in packets and distributing them to sources"""
import os
import threading
from typing import Dict, List, Optional

from src.screamrouter_types.annotations import DelayType, SourceNameType, TimeshiftType, VolumeType
from src.api.api_webstream import APIWebStream
from src.audio.scream_header_parser import ScreamHeader, create_stream_info
from src.audio.sink_mp3_processor import SinkMP3Processor
from src.audio.sink_output_mixer import SinkOutputMixer
from src.audio.source_input_processor import SourceInputProcessor
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.configuration import (Equalizer, SinkDescription,
                                                  SourceDescription)

logger = get_logger(__name__)

class AudioController():
    """Handles a sink, keeps a list of sources, sends passed data to the appropriate pipe
       One AudioController per active real output sink
       This thread listens to the input queue and passes it to each Source processor"""

    def __init__(self, sink_info: SinkDescription,
                 sources: List[SourceDescription],
                 tcp_fd: Optional[int],
                 websocket: APIWebStream):
        """Initialize a sink queue"""
        logger.info("[Sink %s] Loading audio controller", sink_info.name)
        logger.info("[Sink %s] Sources:", sink_info.name)

        for source in sources:
            logger.info("[Sink %s] %s:%s:%s", sink_info.name,
                        source.name,
                        source.ip,
                        source.tag if 'tag' in source.model_fields_set else 'No Tag')
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
        self.request_restart: bool = False
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

        self.mp3_thread: SinkMP3Processor = SinkMP3Processor(self.sink_info.ip,
                                                           self.mp3_ffmpeg_output_read,
                                                           self.webstream.queue,
                                                           self.webstream)
        """Holds the thread to generaet MP3 output from a PCM reader"""
        self.pcm_thread = SinkOutputMixer(self.sink_info,
                                          self.stream_info,
                                          tcp_fd,
                                          list(self.sources.values()),
                                          self.mp3_ffmpeg_output_write)

    def restart_mixer(self, tcp_fd: int):
        """(Re)starts the mixer"""
        logger.info("[Audio Controller] Requesting config reloaed")
        self.pcm_thread.tcp_client_fd = tcp_fd
        self.pcm_thread.update_active_sources()
        self.request_restart = True

    def update_equalizer(self, source_name: SourceNameType, equalizer: Equalizer):
        """Update the equalizer for a source"""
        for source in self.sources.values():
            if source.source_info.name == source_name:
                source.update_equalizer(equalizer)

    def update_volume(self, source_name: SourceNameType, volume: VolumeType):
        """Update the volume for a source"""
        for source in self.sources.values():
            if source.source_info.name == source_name:
                source.update_volume(volume)

    def update_delay(self, source_name: SourceNameType, delay: DelayType):
        """Update the delay for a source"""
        for source in self.sources.values():
            if source.source_info.name == source_name:
                source.update_delay(delay)

    def update_timeshift(self, source_name: SourceNameType, timeshift: TimeshiftType):
        """Update the Timeshift for a source"""
        for source in self.sources.values():
            if source.source_info.name == source_name:
                source.update_timeshift(timeshift)

    def stop(self) -> None:
        """Stops the Sink, closes all handles"""
        logger.debug("[Sink:%s] Stopping PCM thread", self.sink_info.ip)
        self.pcm_thread.stop()
        logger.debug("[Sink:%s] Stopping MP3 Receiver", self.sink_info.ip)
        self.mp3_thread.stop()
        logger.debug("[Sink:%s] Stopping ffmpeg MP3 Converter", self.sink_info.ip)
        #self.mp3_ffmpeg_processor.stop()
        logger.debug("[Sink:%s] Stopping sources", self.sink_info.ip)
        for _, source in self.sources.items():
            logger.debug("[Sink:%s] Stopping source", self.sink_info.ip)
            source.stop()
            logger.debug("[Sink:%s] Stopped source", self.sink_info.ip)
        logger.debug("[Sink:%s] Stopping Audio Controller", self.sink_info.ip)

    def wants_reload(self) -> bool:
        """Returns true of any of the sources want a restart"""
        return self.request_restart
