import tempfile

from typing import List, Optional

from mixer.ffmpeg_handler import ffmpeg_handler
from mixer.sink_input_queue import SinkInputQueue, SinkInputQueueEntry
from mixer.source_info import SourceInfo
from mixer.stream_info import StreamInfo

from configuration.configuration_controller_types import SourceDescription as ControllerSource

from api.api_webstream import API_Webstream

from mixer.sink_output_threads import sink_mp3_thread, sink_pcm_thread

class SinkController():
    """Handles ffmpeg, keeps a list of it's own sources, sends passed data to the appropriate pipe"""
    def __init__(self, sink_ip: str, sources: List[ControllerSource], websocket: Optional[API_Webstream]):
        """Initialize a sink"""
        
        super().__init__()
        self._sink_ip: str = sink_ip
        """Sink IP"""
        self.__controller_sources: List[ControllerSource] = sources
        """Sources this Sink has"""
        self.__sources: List[SourceInfo] = []
        """Sources this Sink is playing"""
        self.__temp_path: str = tempfile.gettempdir() + f"/scream-{self._sink_ip}-"
        """Per-sink temp path"""
        self.__fifo_in_pcm: str = self.__temp_path + "in-pcm"
        """Input file from ffmpeg PCM output"""
        self.__fifo_in_mp3: str = self.__temp_path + "in-mp3"
        """Input file from ffmpeg MP3 output"""
        self.__ffmpeg: ffmpeg_handler = ffmpeg_handler(self._sink_ip, self.__fifo_in_pcm, self.__fifo_in_mp3, self.__get_open_sources())
        """ffmpeg handler"""
        self.__webstream: Optional[API_Webstream] = websocket
        """Holds the websock object to copy audio to, passed through to MP3 listener thread"""
        self.__pcm_thread: sink_pcm_thread = sink_pcm_thread(self.__fifo_in_pcm, self._sink_ip)
        """Holds the thread to listen to PCM output from ffmpeg"""
        self.__mp3_thread: sink_mp3_thread = sink_mp3_thread(self.__fifo_in_mp3, self._sink_ip, self.__webstream)
        """Holds the thread to listen to MP3 output from ffmpeg"""
        self.__queue_thread: SinkInputQueue = SinkInputQueue(self.process_packet_from_queue, self._sink_ip)
        """Holds the thread to listen to the input queue and send it to ffmpeg"""

        for source in self.__controller_sources:
            self.__sources.append(SourceInfo(source.ip, self.__temp_path + source.ip, self._sink_ip, source.volume))
    
    def update_source_volume(self, controllersource: ControllerSource) -> None:
        """Updates the source volume to the specified volume, does nothing if the source is not playing to this sink."""
        for source in self.__sources:
            if source._ip == controllersource.ip:
                source.volume = controllersource.volume
                self.__ffmpeg.set_input_volume(source, controllersource.volume)

    def __get_open_sources(self) -> List[SourceInfo]:
        """Build a list of active IPs, exclude ones that aren't open"""
        active_sources: List[SourceInfo] = []
        for source in self.__sources:
            if source.is_open():
                active_sources.append(source)
        return active_sources
    
    def __get_source_by_ip(self, ip: str) -> tuple[SourceInfo, bool]:
        """Gets a SourceInfo by IP address"""
        for source in self.__sources:
            if source._ip == ip:
                return (source, True)
        return (None, False)  # type: ignore

    def __check_for_inactive_sources(self) -> None:
        """Looks for old pipes that are open and closes them"""
        for source in self.__sources:
            active_time: int = 200  # Time in milliseconds
            if len(self.__get_open_sources()) == 1:  # Don't close the last pipe until more time has passed
                active_time = 300 * 1000  # Much more time in milliseconds
            if not source.is_active(active_time) and source.is_open():
                print(f"[Sink {self._sink_ip} Source {source._ip}] Closing (Timeout = {active_time}ms)")
                source.close()

    def __update_source_attributes_and_open_source(self, source: SourceInfo, header: bytes) -> None:
        """Verifies the target pipe header matches what we have, updates it if not. Also opens the pipe."""
        try:
            parsed_scream_header = StreamInfo(header)
        except:
            # Got an invalid header. This can just be ignored for now.
            return

        if not source.check_attributes(parsed_scream_header):
            print(f"[Sink {self._sink_ip} Source {source._ip}] Closing (Stream attribute change detected. Was: {source._stream_attributes.bit_depth}-bit at {source._stream_attributes.sample_rate}kHz, is now {parsed_scream_header.bit_depth}-bit at {parsed_scream_header.sample_rate}kHz.)")
            source.set_attributes(parsed_scream_header)
            source.close()
        if not source.is_open():
            source.open()
            self.__ffmpeg.reset_ffmpeg(self.__get_open_sources())

    def process_packet_from_queue(self, entry: SinkInputQueueEntry) -> None:
        source: SourceInfo
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
        self.__queue_thread.queue(SinkInputQueueEntry(source_ip, data))

    def stop(self) -> None:
        """Stops the Sink, closes all handles"""
        print(f"[Sink {self._sink_ip}] Stopping PCM")
        self.__pcm_thread.stop()
        self.__pcm_thread.join()
        print(f"[Sink {self._sink_ip}] Stopping MP3")
        self.__mp3_thread.stop()
        self.__mp3_thread.join()
        print(f"[Sink {self._sink_ip}] Stopping Queue")
        self.__queue_thread.stop()
        self.__queue_thread.join()
        print(f"[Sink {self._sink_ip}] Stopping ffmpeg")
        self.__ffmpeg.stop()
        self.__ffmpeg.join()
        print(f"[Sink {self._sink_ip}] Stopped")