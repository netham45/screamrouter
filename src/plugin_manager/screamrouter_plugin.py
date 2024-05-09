"""This holds the base class for plugins to extend"""

import fcntl
import multiprocessing
import multiprocessing.sharedctypes
import os
from ctypes import c_bool
import select
from subprocess import TimeoutExpired
from typing import List

from fastapi import FastAPI

import src.constants.constants as constants
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import SinkNameType, SourceNameType
from src.screamrouter_types.configuration import SourceDescription
from src.utils.utils import close_all_pipes, close_pipe, set_process_name

logger = get_logger(__name__)

class ScreamRouterPlugin(multiprocessing.Process):
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
        self.controller_write_fds: List[int]
        self.screamrouter_write_fd: int
        self.screamrouter_read_fd: int
        self.screamrouter_read_fd, self.screamrouter_write_fd = os.pipe()
        logger.info("[Plugin] queue %s", self.screamrouter_write_fd)
        self.running = multiprocessing.Value(c_bool, True)
        self.sender: ScreamRouterPluginSender
        self.api: FastAPI
        self.temporary_sink_names_to_sources: dict[SinkNameType, List[SourceDescription]] = {}
        self.permanent_sources: List[SourceDescription] = []
        self.temporary_source_tag_counter: int = 0
        self.tag: str
        self.has_ran: bool = False
        self.wants_reload: bool = False

    def plugin_start(self, api: FastAPI):
        """Start the plugin"""
        self.api = api
        self.start_plugin()

    def stop(self):
        """Stop the plugin"""
        logger.info("[Plugin] Stopping")
        self.running.value = c_bool(False)
        self.sender.stop()
        self.stop_plugin()

        if constants.KILL_AT_CLOSE:
            self.kill()
        if constants.WAIT_FOR_CLOSES:
            try:
                self.join(5)
            except TimeoutExpired:
                logger.warning("Plugin failed to close")
        logger.info("[Plugin] Stopped")
        close_pipe(self.screamrouter_read_fd)
        close_pipe(self.screamrouter_write_fd)

    def load(self, controller_write_fds: List[int]):
        """Load a new queue list and start sending to ScreamRouter"""
        self.controller_write_fds = controller_write_fds
        if self.has_ran:
            logger.debug("[Plugin] Stopping sender")
            self.sender.stop()
            logger.debug("[Plugin] Sender stopped, making a new one")
        self.sender = ScreamRouterPluginSender(self.name, self.screamrouter_read_fd,
                                               self.screamrouter_write_fd,
                                               self.controller_write_fds)
        self.has_ran = True
        self.load_plugin()

    def unload(self):
        """Unload the queue list and stop sending to ScreamRouter"""
        self.unload_plugin()
        self.sender.stop()

    def add_temporary_source(self, sink_name: SinkNameType,
                             source: SourceDescription) -> str:
        """Adds a temporary source, returns the source tag to put on the packets"""
        logger.info("[Plugin] Adding temporary source %s", source.tag)
        if not sink_name in self.temporary_sink_names_to_sources:
            self.temporary_sink_names_to_sources[sink_name] = []
        if source.tag is None:
            source.tag = f"{self.tag}"
        logger.info("Tag: %s", source.tag)
        source.name = f"{self.tag}"
        self.temporary_sink_names_to_sources[sink_name].append(source)
        self.temporary_source_tag_counter = self.temporary_source_tag_counter + 1
        self.wants_reload = True
        return source.tag

    def remove_temporary_source(self, source_name: SourceNameType):
        """Removes a temporary source, such as when playback is done"""
        logger.info("[Plugin] Removing temporary source %s", source_name)
        for sources in self.temporary_sink_names_to_sources.values():
            for index, source in enumerate(sources):
                if source.name == source_name:
                    del sources[index]
                    self.wants_reload = True
                    return

    def add_permanet_source(self, source: SourceDescription):
        """Adds a permanent source that shows up in the API and gets saved
           If your plugin tag changes the source will break.
           Overwrites existing sources with the same name.
           Permanent sources can be removed from the UI or API once they're no longer
           in use.
           """
        logger.info("[Plugin] Adding permanent source %s", source.name)
        found: bool = False
        for index, permanent_source in enumerate(self.permanent_sources):
            if permanent_source.name == source.name:
                self.permanent_sources[index] = source
                found = True
        if not found:
            self.permanent_sources.append(source)
        self.wants_reload = True

    def write_data(self, tag: str, data:bytes):
        """Writes PCM data to ScreamRouter"""

        # Get a buffer of TAG_MAX_LENGTH with the tag at the start
        tag_data: bytes = tag.encode() + bytes([0] * (constants.TAG_MAX_LENGTH - len(tag)))
        try:
            os.write(self.screamrouter_write_fd, tag_data + data)
        except BlockingIOError:
            pass

    def start_plugin(self):
        """Empty function to be overridden by plugins"""

    def stop_plugin(self):
        """Empty function to be overridden by plugins"""

    def load_plugin(self):
        """Empty function to be overridden by plugins"""

    def unload_plugin(self):
        """Empty function to be overridden by plugins"""

    def run(self):
        """Sets the process name, called by plugins"""
        set_process_name(f"Plugin {self.name}", f"[ScreamRouter Plugin] {self.name}")
        fcntl.fcntl(self.screamrouter_write_fd, fcntl.F_SETFL, os.O_NONBLOCK) 

class ScreamRouterPluginSender(multiprocessing.Process):
    """Handles sending from a plugin to the ScreamRouter sources"""

    def __init__(self, name: str,
                 screamrouter_read_fd: int,
                 screamrouter_write_fd: int,
                 controller_write_fds: List[int]):
        super().__init__(name=f"[Plugin Reader] {name}")
        self.name = name
        """This holds a list of queues listened to by the AudioControllers"""
        self.controller_write_fds: List[int] = controller_write_fds
        """This holds a queue passed to the plugin process, it is monitored
           and sent to the AudioControllers."""
        self.screamrouter_read_fd: int = screamrouter_read_fd
        """Flag if the thread should exit"""
        self.screamrouter_write_fd: int = screamrouter_write_fd
        self.running = multiprocessing.Value(c_bool, True)
        self.start()

    def stop(self):
        """Stop the plugin sender"""
        self.running.value = c_bool(False)
        if constants.KILL_AT_CLOSE:
            self.kill()
        if constants.WAIT_FOR_CLOSES:
            try:
                self.join(5)
            except TimeoutExpired:
                logger.warning("Plugin Sender failed to close")

    def run(self):
        """This thread is created to work around a Multiprocessing limitation where a new queue
           can not be passed to an existing process. When audio processes are reloaded this process
           is reloaded as a way to provide the existing plugin a means of writing to a new
           controller."""
        set_process_name(f"PlgnRdr {self.name}", f"[Plugin Reader] {self.name}")
        logger.debug("[Plugin %s Sender] PID %s",
                     self.name, os.getpid())
        while self.running.value:
            ready = select.select([self.screamrouter_read_fd], [], [], .3)
            if ready[0]:
                data = os.read(self.screamrouter_read_fd, constants.PACKET_SIZE + constants.TAG_MAX_LENGTH)
                for out_queue in self.controller_write_fds:
                    os.write(out_queue, data)
        logger.info("Ending Plugin Sender thread")
        close_all_pipes()
