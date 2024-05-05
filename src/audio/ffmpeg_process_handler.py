"""Handles the ffmpeg process for each audio controller"""
import subprocess
import threading
from typing import List

import src.constants.constants as constants
from src.audio.scream_header_parser import ScreamHeader
from src.audio.source_input_writer import SourceInputThread
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import VolumeType
from src.screamrouter_types.configuration import Equalizer

logger = get_logger(__name__)


class FFMpegHandler:
    """Handles an FFMpeg process for a sink"""
    def __init__(self, tag,
                  fifo_in_pcm: int, fifo_in_mp3: int,
                  sources: List[SourceInputThread],
                  sink_info: ScreamHeader, equalizer: Equalizer):
        self.__fifo_in_pcm: int = fifo_in_pcm
        """Holds the filename for ffmpeg to output PCM to"""
        self.__fifo_in_mp3: int = fifo_in_mp3
        """Holds the filename for ffmpeg to output MP3 to"""
        self.__tag = tag
        """Holds a tag to put on logs"""
        self.__ffmpeg: subprocess.Popen
        """Holds the ffmpeg object"""
        self.__ffmpeg_started: bool = False
        """Holds rather ffmpeg is running"""
        self.__running: bool = True
        """Holds rather we're monitoring ffmpeg to restart it or waiting for the thread to end"""
        self.__sources: List[SourceInputThread] = sources
        """Holds a list of active sources"""
        self.__sink_info: ScreamHeader = sink_info
        """Holds the sink configuration"""
        self.equalizer: Equalizer = equalizer
        """Equalizer"""
        self.ffmpeg_interaction_lock: threading.Lock = threading.Lock()
        """Lock to ensure this ffmpeg instance is only accessed by one thread at a time
           The thread is locked when ffmpeg is not running or being issued a command.
           This is to prevent multiple threads from trying to restart it at once."""
        self.ffmpeg_interaction_lock.acquire()

    def __get_ffmpeg_inputs(self, sources: List[SourceInputThread], fds: List) -> List[str]:
        """Add an input for each source"""
        ffmpeg_command: List[str] = []
        for index, source in enumerate(sources):
            bit_depth = source.stream_attributes.bit_depth
            sample_rate = source.stream_attributes.sample_rate
            channels = source.stream_attributes.channels
            channel_layout = source.stream_attributes.channel_layout
            # This is optimized to reduce latency and initial ffmpeg processing time
            ffmpeg_command.extend(["-blocksize", str(constants.INPUT_BUFFER_SIZE),
                                   "-max_delay", "0",
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
                                   "-i", f"pipe:{fds[index]}"])
        return ffmpeg_command

    def __get_ffmpeg_filters(self, sources: List[SourceInputThread]) -> List[str]:
        """Build complex filter"""
        ffmpeg_command_parts: List[str] = []
        input_filter_string: str = ""
        output_filter_inputs: str = ""

        # For each source IP add a aresample and append it to an input variable for amix
        for index, source in enumerate(sources):
            equalizer_filter: str = "".join([
                 "superequalizer=",
                f"1b={source.source_info.equalizer.b1}:2b={source.source_info.equalizer.b2}:",
                f"3b={source.source_info.equalizer.b3}:4b={source.source_info.equalizer.b4}:",
                f"5b={source.source_info.equalizer.b5}:6b={source.source_info.equalizer.b6}:",
                f"7b={source.source_info.equalizer.b1}:8b={source.source_info.equalizer.b8}:",
                f"9b={source.source_info.equalizer.b7}:10b={source.source_info.equalizer.b10}:",
                f"11b={source.source_info.equalizer.b1}:12b={source.source_info.equalizer.b12}:",
                f"13b={source.source_info.equalizer.b9}:14b={source.source_info.equalizer.b14}:",
                f"15b={source.source_info.equalizer.b11}:16b={source.source_info.equalizer.b16}:",
                f"17b={source.source_info.equalizer.b13}:18b={source.source_info.equalizer.b18}"])
            delay_filter: str = f"adelay=delays={source.source_info.delay}:all=1"
            volume_filter: str = f"volume@volume_{index}={source.source_info.volume}"
            aresample_filter: str = "".join(["aresample=",
                                          f"isr={source.stream_attributes.sample_rate}:",
                                          f"osr={self.__sink_info.sample_rate}:",
                                          "async=500000:",
                                          "flags=+res"])
            input_filter_string = "".join([f"{input_filter_string}",
                                          f"[{index}]",
                                          f"{volume_filter},",
                                          f"{delay_filter},",
                                          f"{aresample_filter},",
                                          f"{equalizer_filter}",
                                          f"[a{index}],"])
            output_filter_inputs = output_filter_inputs + f"[a{index}]"  # amix input
        inputs: int = len(self.__sources)
        output_filter_string = f"{output_filter_inputs}amix=normalize=0:inputs={inputs}"
        complex_filter_string: str = input_filter_string + output_filter_string
        ffmpeg_command_parts.extend(["-filter_complex", complex_filter_string])
        return ffmpeg_command_parts

    def __get_ffmpeg_output(self) -> List[str]:
        """Returns the ffmpeg output"""
        ffmpeg_command_parts: List[str] = []
        ffmpeg_command_parts.extend(["-blocksize", str(constants.PACKET_DATA_SIZE),
                                     "-max_delay", "0",
                                     "-audio_preload", "0",
                                     "-max_probe_packets", "0",
                                     "-rtbufsize", "0",
                                     "-analyzeduration", "0",
                                     "-probesize", "32",
                                     "-fflags", "discardcorrupt",
                                     "-flags", "low_delay",
                                     "-fflags", "nobuffer",
                                     "-avioflags", "direct",
                                     "-y",
                                     "-f", f"s{self.__sink_info.bit_depth}le", 
                                     "-ac", f"{self.__sink_info.channels}",
                                     "-ar", f"{self.__sink_info.sample_rate}",
                                    f"pipe:{self.__fifo_in_pcm}"])  # ffmpeg PCM output
        ffmpeg_command_parts.extend(["-avioflags", "direct",
                                     "-y",
                                     "-f", "mp3",
                                     "-b:a", f"{constants.MP3_STREAM_BITRATE}",
                                     "-ac", "2",
                                      "-ar", f"{constants.MP3_STREAM_SAMPLERATE}",
                                      "-reservoir", "0",
                                      f"pipe:{self.__fifo_in_mp3}"])  # ffmpeg MP3 output
        return ffmpeg_command_parts

    def __get_ffmpeg_command(self, sources: List[SourceInputThread], fds) -> List[str]:
        """Builds the ffmpeg command"""
        ffmpeg_command_parts: List[str] = ["ffmpeg", "-hide_banner"]  # Build ffmpeg command
        ffmpeg_command_parts.extend(self.__get_ffmpeg_inputs(sources, fds))
        ffmpeg_command_parts.extend(self.__get_ffmpeg_filters(sources))
        ffmpeg_command_parts.extend(self.__get_ffmpeg_output())  # ffmpeg output
        logger.debug("[Sink:%s] ffmpeg command %s", self.__tag, ffmpeg_command_parts)
        return ffmpeg_command_parts

    def ffmpeg_preexec(self):
        """Preexec function for ffmpeg"""

    def start_ffmpeg(self):
        """Start ffmpeg if it's not running"""
        if self.__running:
            if not self.__ffmpeg_started:
                logger.debug("[Sink:%s] ffmpeg started", self.__tag)
                self.__ffmpeg_started = True
                fds: List = []
                for source in self.__sources:
                    fds.append(source.fifo_fd_read)
                fds.append(self.__fifo_in_pcm)
                fds.append(self.__fifo_in_mp3)
                if constants.SHOW_FFMPEG_OUTPUT:
                    self.__ffmpeg = subprocess.Popen(self.__get_ffmpeg_command(self.__sources, fds),
                                                    shell=False,
                                                    start_new_session=True,
                                                    pass_fds=fds,
                                                    stdin=subprocess.PIPE,
                                                    )
                else:
                    self.__ffmpeg = subprocess.Popen(self.__get_ffmpeg_command(self.__sources, fds),
                                                    shell=False,
                                                    start_new_session=True,
                                                    pass_fds=fds,
                                                    stdin=subprocess.PIPE,
                                                    stdout=subprocess.DEVNULL,
                                                    stderr=subprocess.DEVNULL)
                # This is where ffmpeg is running and the not running lock can be released
                if self.ffmpeg_interaction_lock.locked():
                    self.ffmpeg_interaction_lock.release()

    def reset_ffmpeg(self, sources: List[SourceInputThread]) -> None:
        """Opens the ffmpeg instance"""       
        if not self.ffmpeg_interaction_lock.locked:
            self.ffmpeg_interaction_lock.acquire()
        logger.debug("[Sink:%s] Resetting ffmpeg", self.__tag)
        # Lock ffmpeg until it can restart
        self.__sources = sources
        self.__ffmpeg_started = False
        try:
            logger.debug("killing ffmpeg thread %s", self.__ffmpeg.pid)
            self.__ffmpeg.kill()
            logger.debug("Killed ffmpeg")
            self.__ffmpeg.wait()
        except AttributeError:
            pass
        self.start_ffmpeg()

    def send_ffmpeg_command(self, command: str, command_char: str = "c") -> None:
        """Send ffmpeg a command.
           Commands consist of control character to enter a mode (default c) and a string to run."""
        logger.debug("[Sink:%s] Running ffmpeg command %s %s", self.__tag, command_char, command)
        # Lock ffmpeg before sending a command
        self.ffmpeg_interaction_lock.acquire()
        try:
            if not self.__ffmpeg.stdin is None:
                self.__ffmpeg.stdin.write(command_char.encode())
                self.__ffmpeg.stdin.flush()
                self.__ffmpeg.stdin.write((command + "\n").encode())
                self.__ffmpeg.stdin.flush()
        except BrokenPipeError:
            logger.warning("[Sink:%s] Trying to send a comand to a closed instance of ffmpeg",
                            self.__tag)
        self.ffmpeg_interaction_lock.release()
        # Release ffmpeg once a command is finished

    def set_input_volume(self, source: SourceInputThread, volume: VolumeType):
        """Run an ffmpeg command to set the input volume"""
        for index, _source in enumerate(self.__sources):
            if source.tag == _source.tag:
                command: str = f"volume@volume_{index} -1 volume {volume}"
                self.send_ffmpeg_command(command)
                return

    def stop(self) -> None:
        """Stop ffmpeg"""
        self.__running = False
        self.__ffmpeg_started = False
        logger.debug("Killing ffmpeg")
        try:
            self.__ffmpeg.kill()
        except AttributeError:
            pass
        logger.debug("Killed ffmpeg")
        try:
            self.__ffmpeg.wait()
        except AttributeError:
            pass
