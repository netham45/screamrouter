"""One audio controller per sink, handles taking in sources and tracking ffmpeg"""
import threading
from typing import Dict, List, Optional
from pathlib import Path

from api.api_webstream import APIWebStream
from screamrouter_types import SinkDescription, SourceDescription
from audio.ffmpeg_process_handler import FFMpegHandler
from audio.input_queue import InputQueue, FFMpegInputQueueEntry
from audio.source_input_writer import SourceInputThread
from audio.scream_header_parser import ScreamHeader, create_stream_info
from audio.output_threads import MP3OutputThread, PCMOutputThread


from logger import get_logger

logger = get_logger(__name__)

class AudioController():
    """Handles ffmpeg, keeps a list of sources, sends passed data to the appropriate pipe"""
    def __init__(self, sink_info: SinkDescription,
                 sources: List[SourceDescription], websocket: Optional[APIWebStream]):
        """Initialize a sink"""
        super().__init__()
        for source in sources:
            print(f"New Audio Controller {sink_info.name}: {source.name}")
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
        self.__pipe_path_base: str = f"./pipes/scream-{self.sink_info.ip}-"
        """Per-sink pipe path"""
        self.__fifo_in_pcm: Path = Path(self.__pipe_path_base + "in-pcm")
        """Input file from ffmpeg PCM output"""
        self.__fifo_in_mp3: Path = Path(self.__pipe_path_base + "in-mp3")
        """Input file from ffmpeg MP3 output"""
        self.__ffmpeg: FFMpegHandler = FFMpegHandler(self.sink_info.ip,
                                                     Path(self.__fifo_in_pcm),
                                                     Path(self.__fifo_in_mp3),
                                                     self.__get_open_sources(),
                                                     self.__stream_info,
                                                     self.sink_info.equalizer)
        """ffmpeg handler"""
        self.__webstream: Optional[APIWebStream] = websocket
        """Holds the websock object to copy audio to, passed through to MP3 listener thread"""
        self.__pcm_thread: PCMOutputThread = PCMOutputThread(Path(self.__fifo_in_pcm),
                                                             self.sink_info.ip,
                                                             self.sink_info.port,
                                                             self.__stream_info)
        """Holds the thread to listen to PCM output from ffmpeg"""
        self.__mp3_thread: MP3OutputThread = MP3OutputThread(Path(self.__fifo_in_mp3),
                                                             self.sink_info.ip,
                                                             self.__webstream)
        """Holds the thread to listen to MP3 output from ffmpeg"""
        self.__queue_thread: InputQueue = InputQueue(self.process_packet_from_queue,
                                                                 self.sink_info.ip)
        """Holds the thread to listen to the input queue and send it to ffmpeg"""
        self.sources_lock: threading.Lock = threading.Lock()
        """This lock ensures the Sources list is only accessed by one thread at a time"""
        self.count: int = 0

        self.sources_lock.acquire()
        for source in self.__controller_sources:
            pipename: Path = Path(self.__pipe_path_base + str(source.ip))
            self.sources[str(source.ip)] = SourceInputThread(str(source.ip),  # Tag, not IP
                                                     pipename,
                                                     self.sink_info.ip,
                                                     source)
        self.sources_lock.release()

    def update_source_volume(self, controllersource: SourceDescription) -> None:
        """Updates the source volume to the specified volume.
           Does nothing if the source is not playing to this sink."""
        for tag, source in self.sources.items():
            if tag == str(controllersource.ip):
                source.source_info.volume = controllersource.volume
                self.__ffmpeg.set_input_volume(source, controllersource.volume)

    def __get_open_sources(self) -> List[SourceInputThread]:
        """Build a list of active IPs, exclude ones that aren't open"""
        active_sources: List[SourceInputThread] = []
        for _, source in self.sources.items():
            if source.is_open():
                active_sources.append(source)
        return active_sources

    def __get_source_by_tag(self, tag: str) -> Optional[SourceInputThread]:
        """Gets a SourceInfo by IP address"""
        try:
            return self.sources[tag]
        except KeyError:
            pass

    def __check_for_inactive_sources(self) -> None:
        """Looks for old pipes that are open and closes them"""
        if self.sources_lock.acquire(timeout=.2):
            for _, source in self.sources.items():
                if source.is_open() and not source.is_active(100):
                    logger.info("[Sink:%s][Source:%s] Closing (Timeout = 100ms)",
                                self.sink_info.ip,
                                source.tag)
                    source.close()
                    self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())
            self.sources_lock.release()

    def __update_source_attributes_and_open_source(self,
                                                   source: SourceInputThread,
                                                   header: bytes) -> None:
        """Opens and verifies the target pipe header matches what we have, updates it if not."""
        if not source.is_open():
            parsed_scream_header = ScreamHeader(header)
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
            source.open()
            self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())

    def process_packet_from_queue(self, entry: FFMpegInputQueueEntry) -> None:
        """Callback for the queue thread to pass packets for processing"""
        if entry.tag in self.sources:
            source = self.sources[entry.tag]
            self.__check_for_inactive_sources()
            self.__update_source_attributes_and_open_source(source, entry.data[:5])
            source.write(entry.data[5:])  # Write the data to the output fifo

    def add_packet_to_queue(self, tag: str, data: bytes) -> None:
        """Sends data from the main receiver to ffmpeg after verifying it's on our list"""
        source: Optional[SourceInputThread]
        source = self.__get_source_by_tag(tag)
        if not source is None:
            self.__queue_thread.queue(FFMpegInputQueueEntry(tag, data))

    def stop(self) -> None:
        """Stops the Sink, closes all handles"""
        self.sources_lock.acquire()
        logger.debug("[Sink:%s] Stopping PCM thread", self.sink_info.ip)
        self.__pcm_thread.stop()
        logger.debug("[Sink:%s] Stopping MP3 thread", self.sink_info.ip)
        self.__mp3_thread.stop()
        logger.debug("[Sink:%s] Stopping Queue thread", self.sink_info.ip)
        self.__queue_thread.stop()
        logger.debug("[Sink:%s] Stopping ffmpeg thread", self.sink_info.ip)
        self.__ffmpeg.stop()
        logger.debug("[Sink:%s] Stopping sources", self.sink_info.ip)
        for _, source in self.sources.items():
            source.stop()
        self.sources_lock.release()

    def wait_for_threads_to_stop(self) -> None:
        """Waits for threads to stop"""
        self.sources_lock.acquire()
        self.__pcm_thread.join()
        logger.debug("[Sink:%s] PCM thread stopped", self.sink_info.ip)
        self.__mp3_thread.join()
        logger.debug("[Sink:%s] MP3 thread stopped", self.sink_info.ip)
        self.__queue_thread.join()
        logger.debug("[Sink:%s] Queue thread stopped", self.sink_info.ip)
        self.__ffmpeg.join()
        logger.debug("[Sink:%s] ffmpeg thread stopped", self.sink_info.ip)
        for _, source in self.sources.items():
            source.join()
        logger.debug("[Sink:%s] sources stopped", self.sink_info.ip)
        logger.info("[Sink:%s] Stopped", self.sink_info.ip)
        self.sources_lock.release()

    def url_playback_done_callback(self, tag: str):
        """Callback for ffmpeg to clean up when playback is done, tag is the Source tag"""
        if tag in self.sources:
            self.sources_lock.acquire()
            self.sources[tag].close()
            self.sources[tag].stop()
            del self.sources[tag]
            self.sources_lock.release()
            self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())
