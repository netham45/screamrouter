"""Controls the sink, keeps track of associated sources, and holds the ffmpeg thread object"""
from typing import List, Optional

from audio.ffmpeg_process_handler import FFMpegHandler
from audio.ffmpeg_input_queue import FFMpegInputQueue, FFMpegInputQueueEntry
from audio.source_handler import SourceToFFMpegWriter
from audio.stream_info import StreamInfo, create_stream_info
from audio.ffmpeg_output_threads import FFMpegMP3Thread, FFMpegPCMThread

from configuration.configuration_controller_types import SinkDescription, SourceDescription

from api.api_webstream import APIWebStream

class SinkController():
    """Handles ffmpeg, keeps a list of it's own sources, sends passed data to the appropriate pipe"""
    def __init__(self, sink_info: SinkDescription, sources: List[SourceDescription], websocket: Optional[APIWebStream]):
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
        self.__stream_info: StreamInfo = create_stream_info(self.__bit_depth, self.__sample_rate, self.__channels, self.__channel_layout)
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
        self.__ffmpeg: FFMpegHandler = FFMpegHandler(self.sink_ip, self.__fifo_in_pcm, self.__fifo_in_mp3, self.__get_open_sources(), self.__stream_info)
        """ffmpeg handler"""
        self.__webstream: Optional[APIWebStream] = websocket
        """Holds the websock object to copy audio to, passed through to MP3 listener thread"""
        self.__pcm_thread: FFMpegPCMThread = FFMpegPCMThread(self.__fifo_in_pcm, self.sink_ip, self._sink_port, self.__stream_info)
        """Holds the thread to listen to PCM output from ffmpeg"""
        self.__mp3_thread: FFMpegMP3Thread = FFMpegMP3Thread(self.__fifo_in_mp3, self.sink_ip, self.__webstream)
        """Holds the thread to listen to MP3 output from ffmpeg"""
        self.__queue_thread: FFMpegInputQueue = FFMpegInputQueue(self.process_packet_from_queue, self.sink_ip)
        """Holds the thread to listen to the input queue and send it to ffmpeg"""

        for source in self.__controller_sources:
            self.sources.append(SourceToFFMpegWriter(source.ip, self.__pipe_path + source.ip, self.sink_ip, source.volume))

    def update_source_volume(self, controllersource: SourceDescription) -> None:
        """Updates the source volume to the specified volume, does nothing if the source is not playing to this sink."""
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

    def __get_source_by_ip(self, ip: str) -> tuple[SourceToFFMpegWriter, bool]:
        """Gets a SourceInfo by IP address"""
        for source in self.sources:
            if source.tag == ip:
                return (source, True)
        return (None, False)  # type: ignore

    def __check_for_inactive_sources(self) -> None:
        """Looks for old pipes that are open and closes them"""
        for source in self.sources:
            active_time: int = 100  # Time in milliseconds
            if not source.is_active(active_time) and source.is_open():
                print(f"[Sink {self.sink_ip} Source {source.tag}] Closing (Timeout = {active_time}ms)")
                source.close()
                self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())

    def __update_source_attributes_and_open_source(self, source: SourceToFFMpegWriter, header: bytes) -> None:
        """Verifies the target pipe header matches what we have, updates it if not. Also opens the pipe."""
        parsed_scream_header = StreamInfo(header)
        if not source.check_attributes(parsed_scream_header):
            print([f"[Sink {self.sink_ip} Source {source.tag}] Closing (Stream attribute change detected. Was: {source.stream_attributes.bit_depth}-bit at {source.stream_attributes.sample_rate}kHz ",
                    "{source.stream_attributes.channel_layout} layout is now {parsed_scream_header.bit_depth}-bit at {parsed_scream_header.sample_rate}kHz {parsed_scream_header.channel_layout} layout.)"])
            source.set_attributes(parsed_scream_header)
            source.close()
        if not source.is_open():
            source.open()
            print(self.__get_open_sources())
            self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())

    def process_packet_from_queue(self, entry: FFMpegInputQueueEntry) -> None:
        """Callback for the queue thread to pass packets for processing"""
        source: SourceToFFMpegWriter
        result: bool
        source, result = self.__get_source_by_ip(entry.source_ip)
        if result:
            self.__check_for_inactive_sources()
            self.__update_source_attributes_and_open_source(source, entry.data[:5])
            source.write(entry.data[5:])  # Write the data to the output fifo

    def add_packet_to_queue(self, source_ip: str, data: bytes) -> None:
        """Sends data from the main receiver to ffmpeg after verifying it's on our list
           Scream Source -> Receiver -> Sink Handler -> Sources -> Pipe -> FFMPEG -> Pipe -> Python -> Scream Sink
                                             ^
                                        You are here                   
        """
        self.__queue_thread.queue(FFMpegInputQueueEntry(source_ip, data))

    def stop(self) -> None:
        """Stops the Sink, closes all handles"""
        print(f"[Sink {self.sink_ip}] Stopping PCM thread")
        self.__pcm_thread.stop()
        print(f"[Sink {self.sink_ip}] Stopping MP3 thread")
        self.__mp3_thread.stop()
        print(f"[Sink {self.sink_ip}] Stopping Queue thread")
        self.__queue_thread.stop()
        print(f"[Sink {self.sink_ip}] Stopping ffmpeg thread")
        self.__ffmpeg.stop()
        print(f"[Sink {self.sink_ip}] Stopping sources")
        for source in self.sources:
            source.stop()

    def wait_for_threads_to_stop(self) -> None:
        """Waits for threads to stop"""
        self.__pcm_thread.join()
        print(f"[Sink {self.sink_ip}] PCM thread stopped")
        self.__mp3_thread.join()
        print(f"[Sink {self.sink_ip}] MP3 thread stopped")
        self.__queue_thread.join()
        print(f"[Sink {self.sink_ip}] Queue thread stopped")
        self.__ffmpeg.join()
        print(f"[Sink {self.sink_ip}] ffmpeg thread stopped")
        for source in self.sources:
            source.join()
        print(f"[Sink {self.sink_ip}] sources stopped")
        print(f"[Sink {self.sink_ip}] Stopped")

    def url_playback_done_callback(self, source: SourceToFFMpegWriter):
        """Callback for ffmpeg to clean up when playback is done"""
        self.sources.remove(source)
        self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())
