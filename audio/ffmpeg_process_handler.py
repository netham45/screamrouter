"""Handles the ffmpeg process for each sink"""
import subprocess
import time

import threading

from typing import List

from audio.source_to_ffmpeg_writer import SourceToFFMpegWriter
from audio.stream_info import StreamInfo
from logger import get_logger

logger = get_logger(__name__)

class FFMpegHandler(threading.Thread):
    """Handles an FFMpeg process for a sink"""
    def __init__(self, tag, fifo_in_pcm: str, fifo_in_mp3: str,
                  sources: List[SourceToFFMpegWriter], sink_info: StreamInfo):
        super().__init__(name=f"[Sink:{tag}] ffmpeg Thread")
        self.__fifo_in_pcm: str = fifo_in_pcm
        """Holds the filename for ffmpeg to output PCM to"""
        self.__fifo_in_mp3: str = fifo_in_mp3
        """Holds the filename for ffmpeg to output MP3 to"""
        self.__tag = tag
        """Holds a tag to put on logs"""
        self.__ffmpeg: subprocess.Popen
        """Holds the ffmpeg object"""
        self.__ffmpeg_started: bool = False
        """Holds rather ffmpeg is running"""
        self.__running: bool = True
        """Holds rather we're monitoring ffmpeg to restart it"""
        self.__sources: List[SourceToFFMpegWriter] = sources
        """Holds a list of active sources"""
        self.__sink_info: StreamInfo = sink_info
        """Holds the sink configuration"""
        self.start()
        self.start_ffmpeg()

    def __get_ffmpeg_inputs(self, sources: List[SourceToFFMpegWriter]) -> List[str]:
        """Add an input for each source"""
        ffmpeg_command: List[str] = []
        for source in sources:
            bit_depth = source.stream_attributes.bit_depth
            sample_rate = source.stream_attributes.sample_rate
            channels = source.stream_attributes.channels
            channel_layout = source.stream_attributes.channel_layout
            file_name = source.fifo_file_name
            # This is optimized to reduce latency and initial ffmpeg processing time
            ffmpeg_command.extend(["-max_delay", "0",
                                   "-audio_preload", "0",
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

    def __get_ffmpeg_filters(self, sources: List[SourceToFFMpegWriter]) -> List[str]:
        """Build complex filter"""
        ffmpeg_command_parts: List[str] = []
        full_filter_string: str = ""
        amix_inputs: str = ""

        # For each source IP add a aresample and append it to an input variable for amix
        for idx, value in enumerate(sources):
            full_filter_string = "".join([full_filter_string,
                                          f"[{idx}]volume@volume_{idx}={value.volume},",
                                          f"aresample=isr={value.stream_attributes.sample_rate}:",
                                          f"osr={value.stream_attributes.sample_rate}:",
                                          f"async=500000[a{idx}],"])
            amix_inputs = amix_inputs + f"[a{idx}]"  # amix input
        inputs: int = len(self.__sources)
        combined_filter_string: str = full_filter_string + amix_inputs
        combined_filter_string = combined_filter_string + f"amix=normalize=0:inputs={inputs}"
        ffmpeg_command_parts.extend(["-filter_complex", combined_filter_string])
        return ffmpeg_command_parts

    def __get_ffmpeg_output(self) -> List[str]:
        """Returns the ffmpeg output"""
        ffmpeg_command_parts: List[str] = []
        ffmpeg_command_parts.extend(["-avioflags", "direct",
                                     "-y",
                                     "-f", f"s{self.__sink_info.bit_depth}le", 
                                     "-ac", f"{self.__sink_info.channels}",
                                     "-ar", f"{self.__sink_info.sample_rate}",
                                       f"{self.__fifo_in_pcm}"])  # ffmpeg PCM output
        ffmpeg_command_parts.extend(["-avioflags", "direct",
                                     "-y",
                                     "-f", "mp3",
                                     "-b:a", "320k",
                                     "-ac", "2",
                                      "-ar", f"{self.__sink_info.sample_rate}",
                                      "-reservoir", "0",
                                      f"{self.__fifo_in_mp3}"])  # ffmpeg MP3 output
        return ffmpeg_command_parts

    def __get_ffmpeg_command(self, sources: List[SourceToFFMpegWriter]) -> List[str]:
        """Builds the ffmpeg command"""
        ffmpeg_command_parts: List[str] = ["ffmpeg", "-hide_banner"]  # Build ffmpeg command
        ffmpeg_command_parts.extend(self.__get_ffmpeg_inputs(sources))
        ffmpeg_command_parts.extend(self.__get_ffmpeg_filters(sources))
        ffmpeg_command_parts.extend(self.__get_ffmpeg_output())  # ffmpeg output
        return ffmpeg_command_parts

    def start_ffmpeg(self):
        """Start ffmpeg if it's not running"""
        if self.__running:
            logger.debug("[Sink:%s] ffmpeg started", self.__tag)
            self.__ffmpeg_started = True
            self.__ffmpeg = subprocess.Popen(self.__get_ffmpeg_command(self.__sources),
                                             shell=False,
                                             start_new_session=True,
                                             stdin=subprocess.PIPE,
                                             stdout=subprocess.DEVNULL,
                                             stderr=subprocess.DEVNULL)

    def reset_ffmpeg(self, sources: List[SourceToFFMpegWriter]) -> None:
        """Opens the ffmpeg instance"""
        logger.debug("[Sink:%s] Resetting ffmpeg", self.__tag)
        self.__sources = sources
        if self.__ffmpeg_started:
            self.__ffmpeg.kill()

    def send_ffmpeg_command(self, command: str, command_char: str = "c") -> None:
        """Send ffmpeg a command.
           Commands consist of control character to enter a mode (default c) and a string to run."""
        logger.debug("[Sink:%s] Running ffmpeg command %s %s", self.__tag, command_char, command)
        try:
            if not self.__ffmpeg.stdin is None:
                self.__ffmpeg.stdin.write(command_char.encode())
                self.__ffmpeg.stdin.flush()
                self.__ffmpeg.stdin.write((command + "\n").encode())
                self.__ffmpeg.stdin.flush()
        except BrokenPipeError:
            logger.warning("[Sink:%s] Trying to send a comand to a closed instance of ffmpeg",
                            self.__tag)

    def set_input_volume(self, source: SourceToFFMpegWriter, volumelevel: float):
        """Run an ffmpeg command to set the input volume"""
        index: int = -1
        for idx, _source in enumerate(self.__sources):
            if source.tag == _source.tag:
                index = idx
        if index != -1:
            command: str = f"volume@volume_{index} -1 volume {volumelevel}"
            self.send_ffmpeg_command(command)

    def stop(self) -> None:
        """Stop ffmpeg"""
        self.__running = False
        self.__ffmpeg_started = False
        self.send_ffmpeg_command("", "q")
        self.__ffmpeg.kill()
        self.__ffmpeg_started = False

    def run(self) -> None:
        """This thread monitors ffmpeg and restarts it if it ends or needs restarted"""
        while self.__running:
            if len(self.__sources) == 0:
                time.sleep(.01)
                continue
            if self.__ffmpeg_started:
                self.__ffmpeg.wait()
                logger.debug("[Sink:%s] ffmpeg ended", self.__tag)
                self.start_ffmpeg()
        logger.debug("[Sink:%s] ffmpeg exit", self.__tag)
