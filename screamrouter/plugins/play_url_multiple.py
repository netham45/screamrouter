"""This plugin implements an API endpoint to play a URL back over a Sink."""

import os
import select
import signal
import subprocess
import threading
import time
from ctypes import c_bool
from typing import List, Optional

from pydantic import BaseModel

from screamrouter.audio.scream_header_parser import ScreamHeader, create_stream_info
from screamrouter.constants import constants
from screamrouter.plugin_manager.screamrouter_plugin import ScreamRouterPlugin
from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
from screamrouter.screamrouter_types.annotations import (  # BitDepthType, SampleRateType, ChannelsType, ChannelLayoutType
    SinkNameType, VolumeType)
from screamrouter.screamrouter_types.configuration import SourceDescription
from screamrouter.utils.utils import close_all_pipes

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
        if "SIGCHILD" in dir(signal):
            signal.signal(signal.SIGCHLD, self.sigchld_handler)
        self.tag = "PlayURLMultiple"
        """Plugin tag. Source tags are derived from this."""
        self.counter: int = 0
        """Holds a counter so temporary sources can all be unique"""
        self.playing_instances: List[PluginPlayURLInstance] = []
        """Holds a list of playing URLs"""
        self.__mainpid = os.getpid()
        """Holds the main pid so the sigchild interrupt doesn't run on child processes"""
        self.plugin_instance_tag_base = "PlayURLMultiInstance" # Base for unique instance IDs
        
        # self.start() # Thread is started by PluginManager after plugin_start is called

    def start_plugin(self):
        """This is called when the plugin is started. API endpoints should
            be added here. Any other startup tasks can be performed too."""
        # Ensure the plugin's base tag for instance ID generation is set
        self.tag = self.plugin_instance_tag_base # Used by add_temporary_source

        self.api.post("/sinks/{sink_name}/play/{volume}",
                          tags=["Play URL"])(self.play_url)
        
        # This plugin itself doesn't have a run() loop, its instances do.
        # So no super().start() here for the main plugin thread.

    def stop_plugin(self):
        """This is called when the plugin is stopped. You may perform shutdown
           tasks here."""
        logger.info(f"Stopping plugin {self.name}")
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
        logger.info(f"[Plugin PlayURLMultiple] Playing Entry: {sink_name}, {url.url}, {volume}")

        # Create a temporary source and get its unique instance_id
        source_desc = SourceDescription() # Default description
        # add_temporary_source will generate a unique tag (instance_id) like "PlayURLMultiInstance_X"
        # and set it on source_desc.tag. This returned tag is the source_instance_id.
        source_instance_id = self.add_temporary_source(sink_name, source_desc)

        if not source_instance_id:
            logger.error(f"[Plugin PlayURLMultiple] Failed to add temporary source for {url.url}. Cannot play.")
            return

        logger.info(f"[Plugin PlayURLMultiple] Created source_instance_id: {source_instance_id} for {url.url}")
        
        instance = PluginPlayURLInstance(
            plugin=self,
            sink_name=sink_name, # Still needed for context, though source is now identified by instance_id
            url=url.url,
            volume=volume,
            source_instance_id=source_instance_id # Pass the unique ID
            # screamrouter_write_fd is removed
        )
        self.playing_instances.append(instance)
        # self.counter = self.counter + 1 # Counter is managed by ScreamRouterPlugin base for temporary sources

class PluginPlayURLInstance(threading.Thread):
    """Manages an instance of ffmpeg"""
    def __init__(self, plugin: PluginPlayURLMultiple,
                 sink_name: SinkNameType, # Keep for context if needed, not for source ID
                 url: str,
                 volume: VolumeType,
                 source_instance_id: str): # Changed tag to source_instance_id
        """Plays a URL."""
        super().__init__(name=f"PlayURLInstance-{source_instance_id}")
        self.plugin = plugin
        """Holds the plugin to call to add/remove sources"""
        self.url = url
        """Holds the URL to play back"""
        self.source_instance_id = source_instance_id # This is the unique ID for this playback instance
        """Holds the source_instance_id to put on packets to send in"""
        
        self.fifo_read: int
        """Descriptor for the pipe for plugin to read from ffmpeg."""
        self.fifo_write: int
        """Descriptor for the pipe for ffmpeg to write to plugin."""
        self.fifo_read, self.fifo_write = os.pipe()

        # Default stream info, can be overridden if ffmpeg provides different actuals
        _default_bit_depth = 16 # Assuming s16le output from ffmpeg
        _default_sample_rate = 48000
        _default_channels = 2
        _default_channel_layout_str = "stereo"
        self.stream_info: ScreamHeader = create_stream_info(
            _default_bit_depth, _default_sample_rate, _default_channels, _default_channel_layout_str
        )
        # Store these for easy access for write_data
        self.current_bit_depth = _default_bit_depth
        self.current_sample_rate = _default_sample_rate
        self.current_channels = _default_channels
        # Correctly access channel layout bytes as per user feedback
        self.current_chlayout1 = self.stream_info.channel_layout[0]
        self.current_chlayout2 = self.stream_info.channel_layout[1]
        
        self.ffmpeg: Optional[subprocess.Popen] = None
        """Holds the ffmpeg process."""
        self.running_flag = c_bool(True) # For thread loop control
        """Rather this instance of URL playback is running"""
        # self.screamrouter_write_fd: int = screamrouter_write_fd # Removed
        
        # The temporary source was already added by the main plugin before creating this instance.
        # self.plugin.add_temporary_source(sink_name, SourceDescription(tag=self.source_instance_id)) # This is now done in play_url
        
        self.start() # Start the thread for this instance
        self.run_ffmpeg(url, volume)

    def run_ffmpeg(self, url: str, volume: VolumeType):
        """Builds the ffmpeg command and runs it"""
        ffmpeg_command_parts: List[str] = []
        ffmpeg_command_parts.extend(["ffmpeg", "-hide_banner"])
        ffmpeg_command_parts.extend(["-re",
                                     "-i", url])
        ffmpeg_command_parts.extend(["-af", f"volume={volume}"])
        # Output format should match what inject_plugin_packet expects for raw PCM
        # For example, 16-bit signed little-endian PCM
        self.current_bit_depth = 16 # Example: s16le
        self.current_sample_rate = 48000
        self.current_channels = 2
        # Re-create stream_info to get correct chlayout bytes if format changes
        temp_stream_info = create_stream_info(self.current_bit_depth, self.current_sample_rate, self.current_channels, "stereo")
        self.current_chlayout1 = temp_stream_info.channel_layout[0]
        self.current_chlayout2 = temp_stream_info.channel_layout[1]

        ffmpeg_command_parts.extend(["-f", f"s{self.current_bit_depth}le",
                                     "-ac", f"{self.current_channels}",
                                     "-ar", f"{self.current_sample_rate}",
                                    f"pipe:{self.fifo_write}"])
        logger.debug(f"[PlayURLInstance {self.source_instance_id}] ffmpeg command line: {ffmpeg_command_parts}")

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
        if self.ffmpeg and self.ffmpeg.poll() is None: # Check if ffmpeg is running
            logger.info(f"[PlayURLInstance {self.source_instance_id}] Killing ffmpeg process.")
            self.ffmpeg.kill()
            try:
                self.ffmpeg.wait(timeout=2) # Wait for a short period
            except subprocess.TimeoutExpired:
                logger.warning(f"[PlayURLInstance {self.source_instance_id}] ffmpeg did not terminate gracefully after kill.")
        
        self.running_flag.value = False # Signal the run loop to stop
        
        # Write a small amount of data to unblock select in run() if it's waiting
        try:
            os.write(self.fifo_write, b'\0') 
        except OSError: # Pipe might already be closed
            pass

        # Thread joining logic (moved from ScreamRouterPlugin base, as each instance is a thread)
        if self.is_alive():
            if constants.WAIT_FOR_CLOSES:
                try:
                    self.join(5) # Wait for thread to finish
                    if self.is_alive():
                        logger.warning(f"PlayURLInstance {self.source_instance_id} thread failed to close after 5 seconds.")
                except RuntimeError: # join() raises RuntimeError on timeout
                    logger.warning(f"PlayURLInstance {self.source_instance_id} thread failed to close (timeout on join).")
            else: # If not waiting, just log if it's still alive (it might exit quickly)
                if self.is_alive():
                     logger.info(f"PlayURLInstance {self.source_instance_id} thread stop initiated, not waiting for join.")
        
        # Close pipes after attempting to stop/join thread
        try:
            os.close(self.fifo_read)
        except OSError:
            pass
        try:
            os.close(self.fifo_write)
        except OSError:
            pass

    def check_ffmpeg_done(self) -> bool:
        """Checks if ffmpeg is done"""
        # logger.debug(f"[PlayURLInstance {self.source_instance_id}] Checking if ffmpeg is done.")
        if self.ffmpeg is None:
            # logger.debug(f"[PlayURLInstance {self.source_instance_id}] ffmpeg hasn't started yet.")
            return False # Not started, so not done
        
        if self.ffmpeg.poll() is not None: # poll() returns exit code if done, None otherwise
            logger.info(f"[PlayURLInstance {self.source_instance_id}] ffmpeg process has ended.")
            # The main plugin (PluginPlayURLMultiple) is responsible for removing the temporary source
            # by calling self.plugin.remove_temporary_source(self.source_instance_id)
            # This method (check_ffmpeg_done) is called by the main plugin.
            # self.stop() # Call stop to clean up this instance's resources (thread, pipes)
            return True
        # logger.debug(f"[PlayURLInstance {self.source_instance_id}] ffmpeg is still running.")
        return False

    def run(self):
        """This is ran in a new thread and sends packets to the host via the plugin's write_data."""
        # super().run() # Base ScreamRouterPlugin.run() is a simple sleep loop, not needed here.
        logger.info(f"[PlayURLInstance {self.source_instance_id}] Thread started (PID: {os.getpid()}).")

        try:
            while self.running_flag.value:
                if self.ffmpeg is None or self.ffmpeg.poll() is not None:
                    # ffmpeg not running or has ended
                    time.sleep(0.1)
                    if self.ffmpeg and self.ffmpeg.poll() is not None: # If it just ended, break loop
                        logger.info(f"[PlayURLInstance {self.source_instance_id}] ffmpeg ended, exiting run loop.")
                        break
                    continue

                ready = select.select([self.fifo_read], [], [], 0.1) # Short timeout
                if ready[0]:
                    pcm_data = os.read(self.fifo_read, constants.PACKET_DATA_SIZE)
                    if not pcm_data:  # EOF
                        logger.info(f"[PlayURLInstance {self.source_instance_id}] ffmpeg sent EOF.")
                        break # Exit loop on EOF

                    if len(pcm_data) == constants.PACKET_DATA_SIZE:
                        # Use self.plugin.write_data which now calls the C++ engine
                        self.plugin.write_data(
                            source_instance_id=self.source_instance_id,
                            pcm_data=pcm_data,
                            channels=self.current_channels,
                            sample_rate=self.current_sample_rate,
                            bit_depth=self.current_bit_depth,
                            chlayout1=self.current_chlayout1,
                            chlayout2=self.current_chlayout2
                        )
                    elif len(pcm_data) > 0:
                        logger.warning(f"[PlayURLInstance {self.source_instance_id}] Received partial packet from ffmpeg: {len(pcm_data)} bytes. Discarding.")
                else:
                    # No data, check if ffmpeg has ended
                    if self.ffmpeg and self.ffmpeg.poll() is not None:
                        logger.info(f"[PlayURLInstance {self.source_instance_id}] ffmpeg process ended while polling.")
                        break # Exit loop
        except Exception as e:
            logger.error(f"[PlayURLInstance {self.source_instance_id}] Error in run loop: {e}", exc_info=True)
        finally:
            logger.info(f"[PlayURLInstance {self.source_instance_id}] Thread stopping (PID: {os.getpid()}).")
            # Ensure ffmpeg is cleaned up if it was running
            if self.ffmpeg and self.ffmpeg.poll() is None:
                logger.info(f"[PlayURLInstance {self.source_instance_id}] Killing ffmpeg in finally block.")
                self.ffmpeg.kill()
                self.ffmpeg.wait()
            # Close pipes specific to this instance
            try:
                os.close(self.fifo_read)
            except OSError:
                pass
            try:
                os.close(self.fifo_write)
            except OSError:
                pass
            # The main plugin will call remove_temporary_source when check_ffmpeg_done indicates it's finished.
            # And it will also call self.stop() on the instance.
