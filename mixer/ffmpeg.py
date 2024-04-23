import io
import subprocess
import time                     

import threading

import traceback

from typing import List

from mixer.sourceinfo import SourceInfo

class ffmpeg(threading.Thread):
    def __init__(self, sink_ip, fifo_in: str, sources: List[SourceInfo]):
        super().__init__()
        self.__fifo_in: str = fifo_in
        """Holds the filename for ffmpeg to output to"""
        self.__sink_ip = sink_ip
        """Holds the sink IP that's running ffmpeg"""
        self.__ffmpeg: subprocess.Popen
        """Holds the ffmpeg object"""
        self.__ffmpeg_started: bool = False
        """Holds rather ffmpeg is running"""
        self.__running: bool = True
        """Holds rather we're monitoring ffmpeg to restart it"""
        self.__sources = sources
        """Holds a list of active sources"""

        self.start()

    def __get_ffmpeg_inputs(self, sources: List[SourceInfo]) -> List[str]:
        """Add an input for each source"""
        ffmpeg_command: List[str] = []
        for source in sources:
            bit_depth = source._stream_attributes.bit_depth
            sample_rate = source._stream_attributes.sample_rate
            channels = source._stream_attributes.channels
            file_name = source._fifo_file_name
            ffmpeg_command.extend(['-thread_queue_size', '64',
                                   '-f', f's{bit_depth}le',
                                   '-ac', f'{channels}',
                                   '-ar', f'{sample_rate}',
                                   '-i', f'{file_name}'])
        return ffmpeg_command

    def __get_ffmpeg_filters(self, sources: List[SourceInfo]) -> List[str]:
        """Build complex filter"""
        ffmpeg_command_parts: List[str] = []
        full_filter_string = ""
        amix_inputs = ""

        for idx, value in enumerate(sources):  # For each source IP add an input to aresample async, and append it to an input variable for amix
            full_filter_string = full_filter_string + f"[{idx}]volume@volume_{idx}={value.volume},asetpts='(RTCTIME-RTCSTART)/(TB*1000000)',aresample=async=5000:flags=+res:resampler=soxr[a{idx}],"
            amix_inputs = amix_inputs + f"[a{idx}]"  # amix input
        ffmpeg_command_parts.extend(['-filter_complex', full_filter_string + amix_inputs + f'amix=normalize=0:inputs={len(sources)}'])
        return ffmpeg_command_parts

    def __get_ffmpeg_output(self) -> List[str]:
        """Returns the ffmpeg output"""
        # TODO: Add output bitdepth/channels/sample rate to yaml
        ffmpeg_command_parts: List[str] = []
        ffmpeg_command_parts.extend(['-y', '-f', 's32le', '-ac', '2', '-ar', '48000', f"file:{self.__fifo_in}"])  # ffmpeg output
        return ffmpeg_command_parts

    def __get_ffmpeg_command(self, sources: List[SourceInfo]) -> List[str]:
        """Builds the ffmpeg command"""
        ffmpeg_command_parts: List[str] = ['ffmpeg', '-hide_banner']  # Build ffmpeg command
        ffmpeg_command_parts.extend(self.__get_ffmpeg_inputs(sources))
        ffmpeg_command_parts.extend(self.__get_ffmpeg_filters(sources))
        ffmpeg_command_parts.extend(self.__get_ffmpeg_output())  # ffmpeg output
        return ffmpeg_command_parts
    
    def start_ffmpeg(self):
        self.__ffmpeg_started = True
        self.__ffmpeg = subprocess.Popen(self.__get_ffmpeg_command(self.__sources), shell=False, stdin=subprocess.PIPE)  #, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def reset_ffmpeg(self, sources: List[SourceInfo]) -> None:
        """Opens the ffmpeg instance"""
        self.__sources = sources
        if len(sources) == 0:  # No sources to start
            return
        print(f"[Sink {self.__sink_ip}] Reloading ffmpeg")
        if self.__ffmpeg_started:
            try:
                self.__ffmpeg.kill()
            except:
                print(traceback.format_exc())
                print(f"[Sink {self.__sink_ip}] Failed to close ffmpeg")

    def send_ffmpeg_command(self, command: str) -> None:
        print(f"[Sink {self.__sink_ip}] Running ffmpeg command {command}") 
        self.__ffmpeg.stdin.write("c".encode())  # type: ignore
        self.__ffmpeg.stdin.flush()  # type: ignore
        self.__ffmpeg.stdin.write((command + "\n").encode())  # type: ignore
        self.__ffmpeg.stdin.flush()  # type: ignore

    def set_source_volume(self, source: SourceInfo, volumelevel: float):
        index: int = -1
        for idx, _source in enumerate(self.__sources):
            if source._ip == _source._ip:
                index = idx
        if index != -1:
            command: str = f"volume@volume_{index} -1 volume {volumelevel}"
            self.send_ffmpeg_command(command)
    
    def stop(self) -> None:
        print(f"[Sink {self.__sink_ip}] Stopping ffmperg")
        self.__running = False
        self.__ffmpeg.kill()

    def run(self) -> None:
        """This thread implements listening to self.fifoin and sending it out to dest_ip"""
        while self.__running:
            if len(self.__sources) == 0:
                time.sleep(2)
                continue
            self.start_ffmpeg()
            if self.__ffmpeg_started:
                self.__ffmpeg.wait()
                print(f"[Sink {self.__sink_ip}] ffmpeg ended")
        print(f"[Sink {self.__sink_ip}] ffmpeg exit")