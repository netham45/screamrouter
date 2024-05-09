"""This plugin implements an API endpoint to play a URL back over a Sink."""

import os
import select
import signal
import subprocess

from typing import List, Optional, Tuple

from pydantic import BaseModel

from src.audio.scream_header_parser import ScreamHeader, create_stream_info
from src.constants import constants
from src.plugin_manager.screamrouter_plugin import ScreamRouterPlugin
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import SinkNameType, VolumeType
from src.screamrouter_types.configuration import SourceDescription
from src.utils.utils import close_all_pipes

logger = get_logger(__name__)

class PlayURLClass(BaseModel):
    """Holds a URL to be posted to PlayURL"""
    url: str

class PluginPlayURL(ScreamRouterPlugin):
    """This plugin implements an API endpoint to play a URL back over a Sink."""
    def __init__(self):
        super().__init__("Play URL")
        self.original_signal_handler = signal.getsignal(signal.SIGINT)
        """This holds the original signal handler function so it can be
           still called after one is registered to capture SIGCHILD.

           SIGCHILD is sent when ffmpeg ends and is used as a method for
           determing when to play back the next source."""
        signal.signal(signal.SIGCHLD, self.sigchld_handler)
        self.ffmpeg_read: int
        """Descriptor for the pipe for plugin to read from ffmpeg."""
        self.ffmpeg_write: int
        """Descriptor for the pipe for ffmpeg to write to plugin."""
        self.ffmpeg_read, self.ffmpeg_write = os.pipe()
        self.ffmpeg: Optional[subprocess.Popen] = None
        """Holds the ffmpeg process."""
        self.tag = "PlayURL"
        """Plugin tag. Source tags are derived from this."""
        self.stream_info: ScreamHeader = create_stream_info(32, 48000, 2, "stereo")
        """This holds the bytes from a generated Scream Header so they can be
           prepended to data packets"""
        self.queued_url_list: List[Tuple[SinkNameType, str, VolumeType]] = []
        """This holds a list of URLs to play"""
        self.start()
        # Start the run loop in a new process. any variables to be available to run()
        # need to be delcared before this.

    def start_plugin(self):
        """This is called when the plugin is started. API endpoints should
            be added here. Any other startup tasks can be performed too."""
        self.api.post("/sinks/{sink_name}/play_one/{volume}",
                          tags=["Play URL"])(self.queue_url)

    def stop_plugin(self):
        """This is called when the plugin is stopped. You may perform shutdown
           tasks here."""
        os.close(self.ffmpeg_read)
        os.close(self.ffmpeg_write)

    def load_plugin(self):
        """This is called when the configuration is loaded."""

    def unload_plugin(self):
        """This is called when the configuration is unloaded."""

    def sigchld_handler(self, signum, frame):
        """Get a signal when ffmpeg closes"""
        logger.debug("[Plugin URLPlay] Got signal %s", signum)
        self.play_next_url()

        # Call the original signal handler. If this isn't done other plugins that use
        # signal handlers may fail, and ScreamRouter will no longer exit cleanly.
        self.original_signal_handler(signum, frame) # type: ignore

    def play_next_url(self):
        """Plays the next queued up URL.
        Does nothing if there's nothing to queue or if ffmpeg is already playing."""

        play_next: bool = False  # Rather the next entry should be played

        if self.ffmpeg is None:  # If ffmpeg has never ran
            play_next = True
        else:
            if not self.ffmpeg.poll() is None:  # If ffmpeg has ran but ended
                self.ffmpeg.wait()  # Clear ffmpeg from process list
                play_next = True

        if play_next:
            if len(self.queued_url_list) > 0:
                sink_name: SinkNameType
                url: str
                volume: VolumeType
                sink_name, url, volume = self.queued_url_list.pop()
                self.remove_temporary_source(self.tag)  # Remove any old source we may have
                self.add_temporary_source(sink_name, SourceDescription())
                self.run_ffmpeg(url, volume)

    def queue_url(self, url: PlayURLClass, sink_name: SinkNameType, volume: VolumeType):
        """Adds a URL to a queue to be played sequentially"""
        logger.info("[Plugin PlayURL] Adding entry to queue: %s, %s, %s", sink_name, url, volume)
        logger.info("[Plugin PlayURL] Queued entries:")
        for entry in self.queued_url_list:
            logger.info("[Plugin PlayURL] %s", entry)
        self.queued_url_list.append((sink_name, url.url, volume))
        self.play_next_url()

    def run_ffmpeg(self, url: str, volume: VolumeType):
        """Builds the ffmpeg command and runs it"""
        ffmpeg_command_parts: List[str] = []
        ffmpeg_command_parts.extend(["ffmpeg", "-hide_banner"])
        ffmpeg_command_parts.extend(["-re",
                                     "-i", url])
        ffmpeg_command_parts.extend(["-af", f"volume={volume}"])
        ffmpeg_command_parts.extend(["-f", f"s{self.stream_info.bit_depth}le",
                                     "-ac", f"{self.stream_info.channels}",
                                     "-ar", f"{self.stream_info.sample_rate}",
                                    f"pipe:{self.ffmpeg_write}"])
        logger.debug("[PlayURL] ffmpeg command line: %s", ffmpeg_command_parts)

        # None means output to stdout, stderr as normal
        output: Optional[int] = None if constants.SHOW_FFMPEG_OUTPUT else subprocess.DEVNULL

        self.ffmpeg = subprocess.Popen(ffmpeg_command_parts,
                                        shell=False,
                                        start_new_session=True,
                                        pass_fds=[self.ffmpeg_write],
                                        stdin=subprocess.PIPE,
                                        stdout=output,
                                        stderr=output)

    def run(self):
        """This is ran in a new process and must send packets to the host over the queue.
           The multiprocessing variable self.running should be monitored to see if the
           process should end."""
        super().run()
        logger.info("[Plugin] PlayURL started, %s", os.getpid())

        # While the plugin is running check if there's any data available from ffmpeg and
        # write it to ScreamRouter if so.
        while self.running.value:
            ready = select.select([self.ffmpeg_read], [], [], .3)
            if ready[0]:
                data = self.stream_info.header + os.read(self.ffmpeg_read,
                                                    constants.PACKET_DATA_SIZE)
                self.write_data(self.tag, data)
        logger.debug("[Plugin PlayURL] Stopping")
        close_all_pipes()
