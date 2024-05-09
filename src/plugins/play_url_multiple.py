"""This plugin implements an API endpoint to play a URL back over a Sink."""

from ctypes import c_bool
import multiprocessing
import os
import select
import signal
import subprocess
from typing import List, Optional

from pydantic import BaseModel

from src.audio.scream_header_parser import ScreamHeader, create_stream_info
from src.constants import constants
from src.plugin_manager.screamrouter_plugin import ScreamRouterPlugin
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import SinkNameType, VolumeType
from src.screamrouter_types.configuration import SourceDescription
from src.utils.utils import close_all_pipes, close_pipe

logger = get_logger(__name__)

class PlayURLClass(BaseModel):
    """Holds a URL to be posted to PlayURL"""
    url: str

class PluginPlayURLMultiple(ScreamRouterPlugin):
    """This plugin implements an API endpoint to play a URL back over a Sink."""
    def __init__(self):
        super().__init__("Play URL Multiple")
        self.original_signal_handler = signal.getsignal(signal.SIGINT)
        """This holds the original signal handler function so it can be
           still called after one is registered to capture SIGCHILD.

           SIGCHILD is sent when ffmpeg ends and is used as a method for
           determing when to play back the next source."""
        signal.signal(signal.SIGCHLD, self.sigchld_handler)
        self.tag = "PlayURLMultiple"
        """Plugin tag. Source tags are derived from this."""
        self.counter: int = 0
        """Holds a counter so temporary sources can all be unique"""
        self.playing_instances: List[PluginPlayURLInstance] = []
        """Holds a list of playing URLs"""
        self.__mainpid = os.getpid()
        """Holds the main pid so the sigchild interrupt doesn't run on child processes"""
        self.write_lock = multiprocessing.Lock()
        self.start()
        # Start the run loop in a new process. any variables to be available to run()
        # need to be delcared before this.

    def start_plugin(self):
        """This is called when the plugin is started. API endpoints should
            be added here. Any other startup tasks can be performed too."""
        self.api.post("/sinks/{sink_name}/play/{volume}",
                          tags=["Play URL"])(self.play_url)

    def stop_plugin(self):
        """This is called when the plugin is stopped. You may perform shutdown
           tasks here."""
        for instance in self.playing_instances:
            instance.stop()

    def load_plugin(self):
        """This is called when the configuration is loaded."""

    def unload_plugin(self):
        """This is called when the configuration is unloaded."""

    def sigchld_handler(self, signum, frame):
        """Get a signal when ffmpeg closes"""
        #logger.debug("[Plugin PlayURLMultiple] Got signal %s", signum)
        if os.getpid() == self.__mainpid:
            self.check_instances_still_running()
        # Call the original signal handler. If this isn't done other plugins that use
        # signal handlers may fail, and ScreamRouter will no longer exit cleanly.
        self.original_signal_handler(signum, frame) # type: ignore

    def check_instances_still_running(self):
        """Checks if the instances are still running and removes them from the list if not"""
        for index, instance in enumerate(self.playing_instances):
            if instance.check_ffmpeg_done():
                del self.playing_instances[index]

    def play_url(self, url: PlayURLClass, sink_name: SinkNameType, volume: VolumeType):
        """Plays a URL now even if others are playing"""
        logger.info("[Plugin PlayURLMultiple] Playing Entry: %s, %s, %s", sink_name, url, volume)
        self.playing_instances.append(
            PluginPlayURLInstance(self,
                                  sink_name,
                                  url.url,
                                  volume,
                                  f"{self.tag}{self.counter}",
                                  self.screamrouter_write_fd))
        self.counter = self.counter + 1

class PluginPlayURLInstance(multiprocessing.Process):
    """Manages an instance of ffmpeg"""
    def __init__(self, plugin: PluginPlayURLMultiple,
                 sink_name: SinkNameType,
                 url: str,
                 volume: VolumeType,
                 tag: str,
                 screamrouter_write_fd: int):
        """Plays a URL."""
        super().__init__()
        self.url = url
        """Holds the URL to play back"""
        self.tag = tag
        """Holds the tag to put on packets to send in"""
        self.fifo_read: int
        """Descriptor for the pipe for plugin to read from ffmpeg."""
        self.fifo_write: int
        """Descriptor for the pipe for ffmpeg to write to plugin."""
        self.fifo_read, self.fifo_write = os.pipe()
        self.stream_info: ScreamHeader = create_stream_info(32, 48000, 2, "stereo")
        """This holds the bytes from a generated Scream Header so they can be
           prepended to data packets"""
        self.ffmpeg: Optional[subprocess.Popen] = None
        """Holds the ffmpeg process."""
        self.running = multiprocessing.Value(c_bool, True)
        """Rather this instance of URL playback is running"""
        self.screamrouter_write_fd: int = screamrouter_write_fd
        """Queue to write back to ScreamRouter"""
        self.plugin = plugin
        """Holds the plugin to call to add/remove sources"""
        self.plugin.add_temporary_source(sink_name, SourceDescription(tag=self.tag))
        self.start()
        self.run_ffmpeg(url, volume)

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
                                    f"pipe:{self.fifo_write}"])
        logger.debug("[PlayURL] ffmpeg command line: %s", ffmpeg_command_parts)

        # None means output to stdout, stderr as normal
        #output: Optional[int] = None if constants.SHOW_FFMPEG_OUTPUT else subprocess.DEVNULL
        output = None

        self.ffmpeg = subprocess.Popen(ffmpeg_command_parts,
                                        shell=False,
                                        start_new_session=True,
                                        pass_fds=[self.fifo_write],
                                        stdin=subprocess.PIPE,
                                        stdout=output,
                                        stderr=output)

    def stop(self):
        """Stops playback"""
        if not self.ffmpeg is None:
            self.ffmpeg.kill()
            self.ffmpeg.wait()
        self.running.value = c_bool(False)
        os.write(self.fifo_write, bytes([0] * (constants.PACKET_SIZE + constants.TAG_MAX_LENGTH)))
        if constants.KILL_AT_CLOSE:
            self.kill()
        if constants.WAIT_FOR_CLOSES:
            try:
                self.join(5)
            except subprocess.TimeoutExpired:
                logger.warning("Play URL Multiple failed to close")
        close_pipe(self.fifo_read)
        close_pipe(self.fifo_write)

    def check_ffmpeg_done(self) -> bool:
        """Checks if ffmpeg is done"""
        logger.debug("[Plugin PlayURL Multiple %s] Checking if done", self.tag)
        if self.ffmpeg is None:
            logger.debug("[Plugin PlayURL Multiple %s] Haven't started", self.tag)
            return False
        if not self.ffmpeg.poll() is None:
            logger.debug("[Plugin PlayURL Multiple %s] Done", self.tag)
            self.plugin.remove_temporary_source(self.tag)
            self.stop()
            return True
        logger.debug("[Plugin PlayURL Multiple %s] Not done", self.tag)
        return False

    def run(self):
        """This is ran in a new process and must send packets to the host over the queue.
           The multiprocessing variable self.running should be monitored to see if the
           process should end."""
        super().run()
        logger.info("[Plugin] PlayURL started, %s Tag %s ", os.getpid(), self.tag)

        # While the plugin is running check if there's any data available from ffmpeg and
        # write it to ScreamRouter if so.
        while self.running.value:
            ready = select.select([self.fifo_read], [], [], .3)
            if ready[0]:
                data = self.stream_info.header + os.read(self.fifo_read, constants.PACKET_DATA_SIZE)
                if self.plugin.write_lock.acquire(timeout=1):
                    self.plugin.write_data(self.tag, data)
                    self.plugin.write_lock.release()
                else:
                    logger.debug("[Plugin PlayURL Multiple] Couldn't get pipe")
                    break
        logger.debug("[Plugin PlayURL Multiple] Stopping")
        close_all_pipes()
