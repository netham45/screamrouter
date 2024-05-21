"""Handles the ffmpeg process for each audio controller"""
import subprocess
import threading
from typing import List

import src.constants.constants as constants
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.configuration import SinkDescription

logger = get_logger(__name__)


class MP3FFMpegProcess:
    """Handles an FFMpeg process for a sink"""
    def __init__(self, tag,
                  ffmpeg_output_pipe: int, ffmpeg_input_pipe: int,
                  sink_info: SinkDescription):
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
        self.__sink_info: SinkDescription = sink_info
        """Holds the sink configuration"""
        self.ffmpeg_interaction_lock: threading.Lock = threading.Lock()
        """Lock to ensure this ffmpeg instance is only accessed by one thread at a time
           The thread is locked when ffmpeg is not running or being issued a command.
           This is to prevent multiple threads from trying to restart it at once."""
        self.start_ffmpeg()

    def __get_ffmpeg_input(self) -> List[str]:
        """Add an input for each source"""
        ffmpeg_command: List[str] = []
        sample_rate = self.__sink_info.sample_rate
        channels = self.__sink_info.channels
        channel_layout = self.__sink_info.channel_layout
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

    def __get_ffmpeg_output(self) -> List[str]:
        """Returns the ffmpeg output"""
        ffmpeg_command_parts: List[str] = []
        ffmpeg_command_parts.extend(["-avioflags", "direct",
                                     "-y",
                                     "-f", "mp3",
                                     "-b:a", f"{constants.MP3_STREAM_BITRATE}",
                                     "-ac", "2",
                                      "-ar", f"{constants.MP3_STREAM_SAMPLERATE}",
                                      "-reservoir", "0",
                                     f"pipe:{self.__ffmpeg_output_pipe}"])
        return ffmpeg_command_parts

    def __get_ffmpeg_command(self) -> List[str]:
        """Builds the ffmpeg command"""
        ffmpeg_command_parts: List[str] = ["ffmpeg", "-hide_banner"]  # Build ffmpeg command
        ffmpeg_command_parts.extend(self.__get_ffmpeg_input())
        ffmpeg_command_parts.extend(self.__get_ffmpeg_output())  # ffmpeg output
        logger.debug("[Sink:%s] MP3 ffmpeg command %s", self.__tag, ffmpeg_command_parts)
        return ffmpeg_command_parts

    def ffmpeg_preexec(self):
        """Preexec function for ffmpeg"""

    def start_ffmpeg(self):
        """Start ffmpeg if it's not running"""
        if self.__running:
            if not self.__ffmpeg_started:
                logger.debug("[Sink:%s] mp3 ffmpeg started", self.__tag)
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

    def stop(self) -> None:
        """Stop ffmpeg"""
        self.__running = False
        self.__ffmpeg_started = False
        logger.debug("Killing MP3 ffmpeg")
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
