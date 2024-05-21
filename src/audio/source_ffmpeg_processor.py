"""Handles the ffmpeg process for each audio controller"""
import subprocess
import threading
from typing import List

import src.constants.constants as constants
from src.audio.scream_header_parser import ScreamHeader
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.configuration import SourceDescription

logger = get_logger(__name__)


class SourceFFMpegProcessor:
    """Handles an FFMpeg process for a sink"""
    def __init__(self, tag,
                  ffmpeg_output_pipe: int, ffmpeg_input_pipe: int,
                  source_info: SourceDescription,
                  source_stream_attributes: ScreamHeader,
                  sink_info: ScreamHeader):
        self.__ffmpeg_output_pipe: int = ffmpeg_output_pipe
        """Holds the filename for ffmpeg to output PCM to"""
        self.__ffmpeg_input_pipe: int = ffmpeg_input_pipe
        """Holds the filename for ffmpeg to input PCM from"""
        self.__tag = tag
        """Holds a tag to put on logs"""
        self.__ffmpeg: subprocess.Popen
        """Holds the ffmpeg object"""
        self.__ffmpeg_started: bool = False
        """Holds rather ffmpeg is running"""
        self.__running: bool = True
        """Holds rather we're monitoring ffmpeg to restart it or waiting for the thread to end"""
        self.__source_stream_attributes: ScreamHeader = source_stream_attributes
        self.__source_info: SourceDescription = source_info
        self.__sink_info: ScreamHeader = sink_info
        """Holds the sink configuration"""
        self.ffmpeg_interaction_lock: threading.Lock = threading.Lock()
        """Lock to ensure this ffmpeg instance is only accessed by one thread at a time
           The thread is locked when ffmpeg is not running or being issued a command.
           This is to prevent multiple threads from trying to restart it at once."""
        self.start_ffmpeg()

    def __get_ffmpeg_inputs(self) -> List[str]:
        """Add an input for each source"""
        ffmpeg_command: List[str] = []
        sample_rate = self.__source_stream_attributes.sample_rate
        channels = self.__source_stream_attributes.channels
        channel_layout = self.__source_stream_attributes.channel_layout
        # This is optimized to reduce latency and initial ffmpeg processing time
        ffmpeg_command.extend([
                                "-blocksize", str(constants.PACKET_DATA_SIZE * 4),
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
                                "-f", "s32le",
                                "-ac", f"{channels}",
                                "-ar", f"{sample_rate}",
                                "-i", f"pipe:{self.__ffmpeg_input_pipe}"])
        return ffmpeg_command

    def __get_ffmpeg_filters(self) -> List[str]:
        """Build complex filter"""
        ffmpeg_command_parts: List[str] = []

        # For each source IP add a aresample and append it to an input variable for amix
        equalizer_filter: str = "".join([
                "superequalizer=",
            f"1b={self.__source_info.equalizer.b1}:2b={self.__source_info.equalizer.b2}:",
            f"3b={self.__source_info.equalizer.b3}:4b={self.__source_info.equalizer.b4}:",
            f"5b={self.__source_info.equalizer.b5}:6b={self.__source_info.equalizer.b6}:",
            f"7b={self.__source_info.equalizer.b1}:8b={self.__source_info.equalizer.b8}:",
            f"9b={self.__source_info.equalizer.b7}:10b={self.__source_info.equalizer.b10}:",
            f"11b={self.__source_info.equalizer.b1}:12b={self.__source_info.equalizer.b12}:",
            f"13b={self.__source_info.equalizer.b9}:14b={self.__source_info.equalizer.b14}:",
            f"15b={self.__source_info.equalizer.b11}:16b={self.__source_info.equalizer.b16}:",
            f"17b={self.__source_info.equalizer.b13}:18b={self.__source_info.equalizer.b18}"])
        delay_filter: str = f"adelay=delays={self.__source_info.delay}:all=1"
        aresample_filter: str = "".join(["aresample=",
                                        f"isr={self.__source_stream_attributes.sample_rate}:",
                                        f"osr={self.__sink_info.sample_rate}:",
                                        "async=500000:",
                                        "flags=+res"])
        complex_filter_string = "".join([f"{delay_filter},",
                                         f"{aresample_filter},",
                                         f"{equalizer_filter}"])
        ffmpeg_command_parts.extend(["-filter_complex", complex_filter_string])
        return ffmpeg_command_parts

    def __get_ffmpeg_output(self) -> List[str]:
        """Returns the ffmpeg output"""
        ffmpeg_command_parts: List[str] = []
        ffmpeg_command_parts.extend([
                                     "-blocksize", str(constants.PACKET_DATA_SIZE),
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
                                    f"pipe:{self.__ffmpeg_output_pipe}"])  # ffmpeg PCM output
        return ffmpeg_command_parts

    def __get_ffmpeg_command(self) -> List[str]:
        """Builds the ffmpeg command"""
        ffmpeg_command_parts: List[str] = ["ffmpeg", "-hide_banner"]  # Build ffmpeg command
        ffmpeg_command_parts.extend(self.__get_ffmpeg_inputs())
        ffmpeg_command_parts.extend(self.__get_ffmpeg_filters())
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
                fds.append(self.__ffmpeg_output_pipe)
                fds.append(self.__ffmpeg_input_pipe)
                if constants.SHOW_FFMPEG_OUTPUT:
                    self.__ffmpeg = subprocess.Popen(self.__get_ffmpeg_command(),
                                                    shell=False,
                                                    start_new_session=True,
                                                    pass_fds=fds,
                                                    stdin=subprocess.PIPE,
                                                    )
                else:
                    self.__ffmpeg = subprocess.Popen(self.__get_ffmpeg_command(),
                                                    shell=False,
                                                    start_new_session=True,
                                                    pass_fds=fds,
                                                    stdin=subprocess.PIPE,
                                                    stdout=subprocess.DEVNULL,
                                                    stderr=subprocess.DEVNULL)
                # This is where ffmpeg is running and the not running lock can be released
                if self.ffmpeg_interaction_lock.locked():
                    self.ffmpeg_interaction_lock.release()

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

    def stop(self) -> None:
        """Stop ffmpeg"""
        self.__running = False
        self.__ffmpeg_started = False
        logger.debug("Killing ffmpeg")
        try:
            self.__ffmpeg.terminate()
            self.__ffmpeg.kill()
        except AttributeError as esc:
            logger.debug("Error killing ffmpeg %s", esc)
        finally:
            logger.debug("Killed ffmpeg")
        try:
            self.__ffmpeg.wait()
        except AttributeError as esc:
            logger.debug("Error waiting for ffmpeg %s", esc)
