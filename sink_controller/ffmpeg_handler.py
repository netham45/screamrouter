import os
import subprocess
import time                     

import threading

import traceback

from typing import List

from sink_controller.source_info import SourceInfo
from sink_controller.stream_info import StreamInfo

class ffmpeg_handler(threading.Thread):
    def __init__(self, sink_ip, fifo_in_pcm: str, fifo_in_mp3: str, sources: List[SourceInfo], sink_info: StreamInfo):
        super().__init__(name=f"[Sink {sink_ip}] ffmpeg Thread")
        self.__fifo_in_pcm: str = fifo_in_pcm
        """Holds the filename for ffmpeg to output PCM to"""
        self.__fifo_in_mp3: str = fifo_in_mp3
        """Holds the filename for ffmpeg to output MP3 to"""
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
        self.__sink_info: StreamInfo = sink_info
        """Holds the sink configuration"""
        self.start()
        self.start_ffmpeg()
    
    def __get_ffmpeg_inputs(self, sources: List[SourceInfo]) -> List[str]:
        """Add an input for each source"""
        ffmpeg_command: List[str] = []
        for source in sources:
            bit_depth = source._stream_attributes.bit_depth
            sample_rate = source._stream_attributes.sample_rate
            channels = source._stream_attributes.channels
            channel_layout = source._stream_attributes.channel_layout
            file_name = source._fifo_file_name
            ffmpeg_command.extend([
                                   "-max_delay", "0",
                                   "-audio_preload", "1",
                                   "-max_probe_packets", "0",
                                   "-rtbufsize", "0",
                                   "-analyzeduration", "0",
                                   "-probesize", "32",
                                   "-fflags", "discardcorrupt",
                                   "-flags", "low_delay",
                                   "-fflags", "nobuffer",
                                   "-thread_queue_size", "128",
                                   "-channel_layout", f"{channel_layout}",
                                   "-f", f"s{bit_depth}le",
                                   "-ac", f"{channels}",
                                   "-ar", f"{sample_rate}",
                                   "-i", f"{file_name}"])
        return ffmpeg_command

    def __get_ffmpeg_filters(self, sources: List[SourceInfo]) -> List[str]:
        """Build complex filter"""
        ffmpeg_command_parts: List[str] = []
        full_filter_string = ""
        amix_inputs = ""

        for idx, value in enumerate(sources):  # For each source IP add an input to aresample async, and append it to an input variable for amix
            full_filter_string = full_filter_string + f"[{idx}]volume@volume_{idx}={value.volume},aresample=isr={value._stream_attributes.sample_rate}:osr={value._stream_attributes.sample_rate}:async=500000[a{idx}]," # ,adeclick masks dropouts
            amix_inputs = amix_inputs + f"[a{idx}]"  # amix input
        ffmpeg_command_parts.extend(["-filter_complex", full_filter_string + amix_inputs + f"amix=normalize=0:inputs={len(self.__sources)}"])
        return ffmpeg_command_parts

    def __get_ffmpeg_output(self) -> List[str]:
        """Returns the ffmpeg output"""
        ffmpeg_command_parts: List[str] = []
        ffmpeg_command_parts.extend(["-avioflags", "direct", "-y", "-f", f"s{self.__sink_info.bit_depth}le", "-ac", f"{self.__sink_info.channels}", "-ar", f"{self.__sink_info.sample_rate}", f"{self.__fifo_in_pcm}"])  # ffmpeg PCM output
        ffmpeg_command_parts.extend(["-avioflags", "direct", "-y", "-f", "mp3", "-b:a", "320k", "-ac", "2", "-ar", f"{self.__sink_info.sample_rate}", "-reservoir", "0", f"{self.__fifo_in_mp3}"])  # ffmpeg MP3 output
        return ffmpeg_command_parts

    def __get_ffmpeg_command(self, sources: List[SourceInfo]) -> List[str]:
        """Builds the ffmpeg command"""
        ffmpeg_command_parts: List[str] = ["ffmpeg", "-hide_banner"]  # Build ffmpeg command
        ffmpeg_command_parts.extend(self.__get_ffmpeg_inputs(sources))
        ffmpeg_command_parts.extend(self.__get_ffmpeg_filters(sources))
        ffmpeg_command_parts.extend(self.__get_ffmpeg_output())  # ffmpeg output
        return ffmpeg_command_parts
    
    def ffmpeg_preopen_hook(self): 
        """Don't forward signals. It's lifecycle is managed."""
        os.setpgrp()

    def start_ffmpeg(self):
        """Start ffmpeg if it's not running"""
        if (self.__running):
            print(f"[Sink {self.__sink_ip}] ffmpeg started")
            self.__ffmpeg_started = True
            print(self.__get_ffmpeg_command(self.__sources))
            self.__ffmpeg = subprocess.Popen(self.__get_ffmpeg_command(self.__sources), preexec_fn = self.ffmpeg_preopen_hook, shell=False, stdin=subprocess.PIPE)#, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def reset_ffmpeg(self, sources: List[SourceInfo]) -> None:
        """Opens the ffmpeg instance"""
        print(f"[{self.__sink_ip}] Resetting ffmpeg")
        self.__sources = sources
        if self.__ffmpeg_started:
            try:
                self.__ffmpeg.kill()
                self.__ffmpeg.wait()
            except:
                print(traceback.format_exc())
                print(f"[Sink {self.__sink_ip}] Failed to close ffmpeg")

    def send_ffmpeg_command(self, command: str, command_char: str = "c") -> None:
        """Send ffmpeg a command. Commands consist of control character to enter a mode (default 'c') and a string to run."""
        print(f"[Sink {self.__sink_ip}] Running ffmpeg command {command_char} {command}") 
        self.__ffmpeg.stdin.write(command_char.encode())  # type: ignore
        self.__ffmpeg.stdin.flush()  # type: ignore
        self.__ffmpeg.stdin.write((command + "\n").encode())  # type: ignore
        self.__ffmpeg.stdin.flush()  # type: ignore

    def set_input_volume(self, source: SourceInfo, volumelevel: float):
        """Run an ffmpeg command to set the input volume"""
        index: int = -1
        for idx, _source in enumerate(self.__sources):
            if source._ip == _source._ip:
                index = idx
        if index != -1:
            command: str = f"volume@volume_{index} -1 volume {volumelevel}"
            self.send_ffmpeg_command(command)
    
    def stop(self) -> None:
        """Stop ffmpeg"""
        self.__running = False
        self.__ffmpeg_started = False
        try:
            self.send_ffmpeg_command("","q")
        except:
            pass
        try:
            self.__ffmpeg.kill()
        except:
            pass
        self.__ffmpeg_started = False

    def run(self) -> None:
        """This thread implements listening to self.fifoin and sending it out to dest_ip"""
        while self.__running:
            if len(self.__sources) == 0:
                time.sleep(.01)
                continue
            if self.__ffmpeg_started:
                self.__ffmpeg.wait()
                print(f"[Sink {self.__sink_ip}] ffmpeg ended")
                self.start_ffmpeg()
        print(f"[Sink {self.__sink_ip}] ffmpeg exit")