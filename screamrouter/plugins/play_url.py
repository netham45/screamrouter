"""This plugin implements an API endpoint to play a URL back over a Sink."""

import os
import select
import signal
import subprocess
import time
from typing import List, Optional, Tuple

from pydantic import BaseModel

from screamrouter.audio.scream_header_parser import ScreamHeader, create_stream_info
from screamrouter.constants import constants
from screamrouter.plugin_manager.screamrouter_plugin import ScreamRouterPlugin
from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
from screamrouter.screamrouter_types.annotations import SinkNameType, VolumeType
from screamrouter.screamrouter_types.configuration import SourceDescription
from screamrouter.utils.utils import close_all_pipes

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
        if "SIGCHILD" in dir(signal):
            signal.signal(signal.SIGCHLD, self.sigchld_handler)
        self.ffmpeg_read: int
        """Descriptor for the pipe for plugin to read from ffmpeg."""
        self.ffmpeg_write: int
        """Descriptor for the pipe for ffmpeg to write to plugin."""
        self.ffmpeg_read, self.ffmpeg_write = os.pipe()
        self.ffmpeg: Optional[subprocess.Popen] = None
        """Holds the ffmpeg process."""
        self.plugin_instance_tag_base = "PlayURLInstance" # Base for unique instance IDs
        """Plugin tag. Source tags are derived from this."""
        self.current_source_instance_id: Optional[str] = None

        # Default stream info, can be overridden if ffmpeg provides different actuals
        # For ScreamHeader, chlayout1 and chlayout2 are derived from channels.
        # Let's assume create_stream_info handles this.
        # The ScreamHeader object itself is less important now than its constituent parts.
        _default_bit_depth = 32 # Example, ffmpeg output might be s16le or s32le
        _default_sample_rate = 48000
        _default_channels = 2
        _default_channel_layout_str = "stereo" # For create_stream_info

        self.stream_info: ScreamHeader = create_stream_info(
            _default_bit_depth, _default_sample_rate, _default_channels, _default_channel_layout_str
        )
        # Store these for easy access for write_data
        self.current_bit_depth = _default_bit_depth
        self.current_sample_rate = _default_sample_rate
        self.current_channels = _default_channels
        self.current_chlayout1 = self.stream_info.channel_layout[0]
        self.current_chlayout2 = self.stream_info.channel_layout[1]

        """This holds the bytes from a generated Scream Header so they can be
           prepended to data packets"""
        self.queued_url_list: List[Tuple[SinkNameType, str, VolumeType]] = []
        """This holds a list of URLs to play"""
        
        # self.start() # Thread is started by PluginManager after plugin_start is called

    def start_plugin(self):
        """This is called when the plugin is started. API endpoints should
            be added here. Any other startup tasks can be performed too."""
        # Ensure the plugin's base tag for instance ID generation is set
        self.tag = self.plugin_instance_tag_base # Used by add_temporary_source
        
        self.api.post("/sinks/{sink_name}/play_one/{volume}",
                          tags=["Play URL"])(self.queue_url)
        
        # Start the plugin's own processing thread if it has a run() method
        if hasattr(self, 'run') and callable(getattr(self, 'run')):
            if not self.is_alive():
                 logger.info(f"Starting thread for plugin {self.name}")
                 super().start() # Calls threading.Thread.start()

    def stop_plugin(self):
        """This is called when the plugin is stopped. You may perform shutdown
           tasks here."""
        try:
            os.close(self.ffmpeg_read)
        except OSError:
            pass
        try:
            os.close(self.ffmpeg_write)
        except OSError:
            pass

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
                if self.current_source_instance_id:
                    self.remove_temporary_source(self.current_source_instance_id)
                    self.current_source_instance_id = None
                play_next = True

        if play_next:
            if len(self.queued_url_list) > 0:
                sink_name: SinkNameType
                url: str
                volume: VolumeType
                sink_name, url, volume = self.queued_url_list.pop()
                
                # Create a new temporary source and get its unique instance_id
                # The tag passed to SourceDescription is used by C++ to create the SourceInputProcessor instance.
                # self.add_temporary_source now returns this instance_id.
                source_desc = SourceDescription() # Create a default one
                # The tag in SourceDescription is used by C++ to create the SourceInputProcessor instance.
                # The add_temporary_source method in ScreamRouterPlugin now generates a unique tag (instance_id)
                # and sets it on source_desc.tag. This returned tag is what we need.
                self.current_source_instance_id = self.add_temporary_source(sink_name, source_desc)
                
                if not self.current_source_instance_id:
                    logger.error(f"[Plugin PlayURL] Failed to add temporary source for {url}. Cannot play.")
                    self.play_next_url() # Try next if any
                    return

                logger.info(f"[Plugin PlayURL] Playing {url} with source_instance_id: {self.current_source_instance_id}")
                self.run_ffmpeg(url, volume)
            else:
                logger.info("[Plugin PlayURL] Queue is empty.")


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
        # Output format should match what inject_plugin_packet expects for raw PCM
        # For example, 16-bit signed little-endian PCM
        # Update self.current_format based on this if needed.
        # For now, assume ffmpeg outputs s16le, 48kHz, stereo as per typical Scream use.
        # If ffmpeg outputs something else, these values need to be dynamically set.
        self.current_bit_depth = 16 # Example: s16le
        self.current_sample_rate = 48000
        self.current_channels = 2
        # Re-create stream_info to get correct chlayout bytes if format changes
        # Or, ideally, parse them from ffmpeg's output if possible, or have fixed output format.
        temp_stream_info = create_stream_info(self.current_bit_depth, self.current_sample_rate, self.current_channels, "stereo")
        self.current_chlayout1 = self.stream_info.channel_layout[0]
        self.current_chlayout2 = self.stream_info.channel_layout[1]

        ffmpeg_command_parts.extend(["-f", f"s{self.current_bit_depth}le", # e.g., s16le for 16-bit
                                     "-ac", f"{self.current_channels}",
                                     "-ar", f"{self.current_sample_rate}",
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
        while self.running_flag.value: # Use the c_bool flag from base class
            if not self.current_source_instance_id or self.ffmpeg is None or self.ffmpeg.poll() is not None:
                # Not currently playing anything, or ffmpeg ended
                time.sleep(0.1)
                continue

            ready = select.select([self.ffmpeg_read], [], [], 0.1) # Short timeout
            if ready[0]:
                # Read raw PCM data from ffmpeg
                pcm_data = os.read(self.ffmpeg_read, constants.PACKET_DATA_SIZE)
                if not pcm_data: # EOF
                    logger.info(f"[Plugin PlayURL] ffmpeg for {self.current_source_instance_id} sent EOF.")
                    self.play_next_url() # Try to play next
                    continue

                if len(pcm_data) == constants.PACKET_DATA_SIZE:
                    self.write_data(
                        source_instance_id=self.current_source_instance_id,
                        pcm_data=pcm_data,
                        channels=self.current_channels,
                        sample_rate=self.current_sample_rate,
                        bit_depth=self.current_bit_depth,
                        chlayout1=self.current_chlayout1,
                        chlayout2=self.current_chlayout2
                    )
                elif len(pcm_data) > 0:
                     logger.warning(f"[Plugin PlayURL] Received partial packet from ffmpeg for {self.current_source_instance_id}: {len(pcm_data)} bytes. Discarding.")
            else:
                # No data from ffmpeg, check if it has ended
                if self.ffmpeg and self.ffmpeg.poll() is not None:
                    logger.info(f"[Plugin PlayURL] ffmpeg process for {self.current_source_instance_id} ended.")
                    self.play_next_url() # Try to play next
                    
        logger.debug(f"[Plugin PlayURL {self.name}] Thread stopping")
        # Ensure ffmpeg is cleaned up if plugin stops while playing
        if self.ffmpeg and self.ffmpeg.poll() is None:
            logger.info(f"[Plugin PlayURL {self.name}] Killing ffmpeg process on stop.")
            self.ffmpeg.kill()
            self.ffmpeg.wait()
        if self.current_source_instance_id:
            self.remove_temporary_source(self.current_source_instance_id)
        # close_all_pipes() # This is too broad, manage specific pipes (ffmpeg_read/write) in stop_plugin
