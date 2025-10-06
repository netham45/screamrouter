"""This holds the base class for plugins to extend"""

import os
import threading
# import select # No longer needed for pipe reading
import time
from ctypes import c_bool
from subprocess import TimeoutExpired
from typing import Any  # Added Any for audio_manager_instance placeholder
from typing import List

from fastapi import FastAPI

import screamrouter.constants.constants as constants
from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
from screamrouter.screamrouter_types.annotations import SinkNameType
from screamrouter.screamrouter_types.configuration import SourceDescription
from screamrouter.utils.utils import (  # close_pipe no longer needed for plugin pipes
    close_all_pipes, set_process_name)

# Attempt to import the C++ audio engine; this might fail if not compiled/installed
try:
    from screamrouter_audio_engine import AudioManager  # type: ignore
except ImportError:
    AudioManager = None
    get_logger(__name__).warning("Failed to import screamrouter_audio_engine. Plugin data writing will be non-functional.")


logger = get_logger(__name__)

class ScreamRouterPlugin(threading.Thread):
    """This class is meant to be extended by plugins.
       
       Notable available functions include:

       add_temporary_source(sink_name: SinkNameType, source: SourceDescription)
       This adds a temporary source to a sink or sink group in ScreamRouter. They are 
       not saved and will not persist across reloads.

       remove_temporary_source(sink_name: SinkNameType)
       This removes a temporary source when it is done playing back.

       add_permanet_source(self, source: SourceDescription)
       This adds a permanent source that will show up in the UI and allow the user to
       create routes based on it.

       def create_stream_info(bit_depth: BitDepthType,
                       sample_rate: SampleRateType,
                       channels: ChannelsType,
                       channel_layout: ChannelLayoutType) -> ScreamHeader:
        This function lives under src.audio.scream_header_parser and handles generating
        Scream headers.

        All options except for Tag for a Source are optional and will be filled in with
        defaults. Names should be provided on permanent Sources and tags should always
        be non-blank. Tags must be unique.
    """
    def __init__(self, name: str):
        super().__init__(name=f"[ScreamRouter Plugin] {name}")
        self.name: str = name
        # self.controller_write_fds: List[int] # No longer used for this purpose
        # self.screamrouter_write_fd: int # Removed
        # self.screamrouter_read_fd: int # Removed
        # self.screamrouter_read_fd, self.screamrouter_write_fd = os.pipe() # Removed
        # logger.info("[Plugin] queue %s", self.screamrouter_write_fd) # Removed
        self.running = True # c_bool might be needed if used across processes, but for thread flag, bool is fine.
                            # Let's keep c_bool for consistency with original stop() logic.
        self.running_flag = c_bool(True) # Use a separate flag for thread loop control

        # self.sender: ScreamRouterPluginSender # Removed
        self.api: FastAPI
        self.temporary_sink_names_to_sources: dict[SinkNameType, List[SourceDescription]] = {}
        self.permanent_sources: List[SourceDescription] = []
        self.temporary_source_tag_counter: int = 0
        self.tag: str # This is the base tag for the plugin, used to generate source_instance_ids
        self.has_ran: bool = False # Kept for load logic, though sender is gone
        self.wants_reload: bool = False
        
        # Placeholder for the C++ AudioManager instance.
        # This needs to be injected by the PluginManager or a similar mechanism.
        self.audio_manager_instance: Any = None # Use Any for now, ideally should be AudioManager type

    def plugin_start(self, api: FastAPI, audio_manager_instance: Any = None):
        """Start the plugin. AudioManager instance should be passed here."""
        self.api = api
        if audio_manager_instance:
            self.audio_manager_instance = audio_manager_instance
        elif AudioManager: # Fallback if AudioManager was imported successfully
             logger.warning(f"Plugin {self.name} started without an explicit AudioManager instance. Attempting to use global if available (not recommended).")
            # This is a placeholder for how a global/singleton might be accessed.
            # Direct instantiation here is likely wrong as there should be one engine instance.
            # self.audio_manager_instance = AudioManager() # This is probably incorrect.
        else:
            logger.error(f"Plugin {self.name} cannot access AudioManager. Data writing will fail.")

        self.start_plugin()

    def stop(self):
        """Stop the plugin"""
        logger.info(f"[Plugin {self.name}] Stopping")
        self.running_flag.value = False # Signal the run loop to exit
        # self.sender.stop() # Removed
        self.stop_plugin()

        # Graceful thread shutdown
        if self.is_alive(): # Check if the thread was started and is alive
            if constants.KILL_AT_CLOSE: # This constant name might be misleading now
                try:
                    # Forcibly stopping threads is generally not safe.
                    # Rely on self.running_flag.value = False and join.
                    # self.kill() # This method doesn't exist on threading.Thread
                    pass
                except AttributeError:
                    pass
            if constants.WAIT_FOR_CLOSES:
                try:
                    self.join(5)
                    if self.is_alive():
                        logger.warning(f"Plugin {self.name} thread failed to close after 5 seconds.")
                except TimeoutExpired: # join() raises RuntimeError on timeout, not TimeoutExpired
                    logger.warning(f"Plugin {self.name} thread failed to close (timeout).")
                except RuntimeError: # Catch timeout from join
                    logger.warning(f"Plugin {self.name} thread failed to close (timeout on join).")
                except AttributeError: # Should not happen if self is a Thread
                    pass
        logger.info(f"[Plugin {self.name}] Stopped")
        # close_pipe(self.screamrouter_read_fd) # Removed
        # close_pipe(self.screamrouter_write_fd) # Removed

    def load(self, controller_write_fds: List[int]): # controller_write_fds is no longer relevant for this
        """Load plugin. controller_write_fds is kept for signature compatibility but not used."""
        # self.controller_write_fds = controller_write_fds # No longer needed
        if self.has_ran:
            logger.debug(f"[Plugin {self.name}] Sender logic removed, no sender to stop/restart.")
            # self.sender.stop() # Removed
        # self.sender = ScreamRouterPluginSender(self.name, self.screamrouter_read_fd, # Removed
        #                                        self.screamrouter_write_fd, # Removed
        #                                        self.controller_write_fds) # Removed
        self.has_ran = True
        self.load_plugin()

    def unload(self):
        """Unload the plugin."""
        self.unload_plugin()
        # self.sender.stop() # Removed

    def add_temporary_source(self, sink_name: SinkNameType,
                             source: SourceDescription) -> str:
        """Adds a temporary source, returns the source tag (instance_id) to put on the packets"""
        # The 'tag' for a temporary source should be unique and is used as source_instance_id
        # The plugin's self.tag is a base name.
        generated_tag = f"{self.tag}_{self.temporary_source_tag_counter}"
        source.tag = generated_tag # This tag will be the source_instance_id
        
        logger.info(f"[Plugin {self.name}] Adding temporary source with instance_id: {source.tag}")
        if not sink_name in self.temporary_sink_names_to_sources:
            self.temporary_sink_names_to_sources[sink_name] = []
        
        source.name = f"{self.name} - {generated_tag}" # Make name more descriptive
        self.temporary_sink_names_to_sources[sink_name].append(source)
        self.temporary_source_tag_counter = self.temporary_source_tag_counter + 1
        self.wants_reload = True
        return source.tag # Return the generated instance_id

    def remove_temporary_source(self, source_instance_id: str):
        """Removes a temporary source by its instance_id (formerly source_tag)."""
        logger.info(f"[Plugin {self.name}] Removing temporary source with instance_id: {source_instance_id}")
        for sources_list in self.temporary_sink_names_to_sources.values():
            for index, source_desc in enumerate(sources_list):
                if source_desc.tag == source_instance_id: # tag is used as instance_id here
                    del sources_list[index]
                    self.wants_reload = True
                    return
        logger.warning(f"[Plugin {self.name}] Temporary source with instance_id {source_instance_id} not found for removal.")


    def add_permanet_source(self, source: SourceDescription):
        """Adds a permanent source that shows up in the API and gets saved
           If your plugin tag changes the source will break.
           Overwrites existing sources with the same name.
           Permanent sources can be removed from the UI or API once they're no longer
           in use.
           The source.tag here should be the unique source_instance_id.
           """
        if not source.tag:
            logger.error(f"[Plugin {self.name}] Permanent source must have a unique tag (source_instance_id).")
            return
            
        logger.info(f"[Plugin {self.name}] Adding permanent source {source.name} with instance_id {source.tag}")
        found: bool = False
        for index, permanent_source in enumerate(self.permanent_sources):
            if permanent_source.tag == source.tag: # Match by tag (instance_id)
                self.permanent_sources[index] = source
                found = True
                break
        if not found:
            self.permanent_sources.append(source)
        self.wants_reload = True

    def write_data(self,
                     source_instance_id: str,
                     pcm_data: bytes,
                     channels: int,
                     sample_rate: int,
                     bit_depth: int,
                     chlayout1: int, # Assuming these are uint8_t in C++
                     chlayout2: int):
        """Writes PCM data to ScreamRouter via the C++ audio engine."""
        if not self.audio_manager_instance:
            logger.error(f"[Plugin {self.name}] AudioManager instance not available. Cannot write data for {source_instance_id}.")
            return
        if AudioManager is None: # Check if the import failed
            logger.error(f"[Plugin {self.name}] screamrouter_audio_engine module not imported. Cannot write data.")
            return

        try:
            # Ensure audio_payload is bytes
            if not isinstance(pcm_data, bytes):
                logger.error(f"[Plugin {self.name}] pcm_data must be bytes for {source_instance_id}.")
                return

            # logger.debug(f"[Plugin {self.name}] Writing data for {source_instance_id}: "
            #              f"{len(pcm_data)} bytes, CH={channels}, SR={sample_rate}, BD={bit_depth}")
            
            # Ensure chlayout1 and chlayout2 are integers for the C++ binding.
            # C++ expects uint8_t, for which pybind11 expects Python integers.
            
            _chlayout1_int = chlayout1
            if isinstance(chlayout1, str) and len(chlayout1) == 1:
                _chlayout1_int = ord(chlayout1)
            elif isinstance(chlayout1, bytes) and len(chlayout1) == 1:
                _chlayout1_int = chlayout1[0]  # This results in an int
            elif not isinstance(chlayout1, int):
                logger.error(f"Plugin {self.name}: Unexpected type for chlayout1: {type(chlayout1)}, value: {chlayout1}. Passing original.")
            
            _chlayout2_int = chlayout2
            if isinstance(chlayout2, str) and len(chlayout2) == 1:
                _chlayout2_int = ord(chlayout2)
            elif isinstance(chlayout2, bytes) and len(chlayout2) == 1:
                _chlayout2_int = chlayout2[0]  # This results in an int
            elif not isinstance(chlayout2, int):
                logger.error(f"Plugin {self.name}: Unexpected type for chlayout2: {type(chlayout2)}, value: {chlayout2}. Passing original.")

            success = self.audio_manager_instance.write_plugin_packet(
                source_instance_id,
                pcm_data,
                channels,
                sample_rate,
                bit_depth,
                _chlayout1_int,
                _chlayout2_int
            )
            if not success:
                logger.warning(f"[Plugin {self.name}] audio_manager.write_plugin_packet for {source_instance_id} reported failure.")
        except Exception as e:
            logger.error(f"[Plugin {self.name}] Error writing data for {source_instance_id} via C++ engine: {e}", exc_info=True)


    def start_plugin(self):
        """Empty function to be overridden by plugins"""

    def stop_plugin(self):
        """Empty function to be overridden by plugins"""

    def load_plugin(self):
        """Empty function to be overridden by plugins"""

    def unload_plugin(self):
        """Empty function to be overridden by plugins"""

    def run(self):
        """Sets the process name, called by plugins.
           The main loop for plugins that need continuous background processing.
           Plugins that are event-driven (e.g., only react to API calls) might not need to override this.
        """
        set_process_name(f"Plugin {self.name}", f"[ScreamRouter Plugin] {self.name}")
        logger.info(f"[Plugin {self.name}] Thread started (PID: {os.getpid()}).")
        while self.running_flag.value: # Use the c_bool flag
            # Most plugins will implement their specific logic here or be event-driven.
            # This base run loop just sleeps. If a plugin needs to do work, it should override run().
            time.sleep(0.5) # Sleep to avoid busy-waiting if not much to do.
        
        # Perform any cleanup specific to the plugin's thread before exiting
        # close_all_pipes() # This was a generic call, plugins should manage their own FDs if any (e.g. ffmpeg pipes)
        logger.info(f"[Plugin {self.name}] Thread stopping (PID: {os.getpid()}).")

# Removed ScreamRouterPluginSender class
