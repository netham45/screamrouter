"""This holds the base class for plugins to extend"""

import multiprocessing
import multiprocessing.sharedctypes
import queue
from ctypes import c_bool
from typing import List

from fastapi import FastAPI

import src.constants.constants as constants
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import SinkNameType, SourceNameType
from src.screamrouter_types.configuration import SourceDescription
from src.screamrouter_types.packets import InputQueueEntry
from src.utils.utils import set_process_name

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
        set_process_name(f"Plugin {name}", f"[ScreamRouter Plugin] {name}")
        self.name: str = name
        self.out_queue_list: List[multiprocessing.Queue]
        self.screamrouter_in_queue: multiprocessing.Queue = multiprocessing.Queue()
        logger.info("[Plugin] queue %s", self.screamrouter_in_queue)
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
            self.join()
        logger.info("[Plugin] Stopped")

    def load(self, queue_list: List[multiprocessing.Queue]):
        """Load a new queue list and start sending to ScreamRouter"""
        self.out_queue_list = queue_list
        if self.has_ran:
            self.sender.stop(False)
        self.sender = ScreamRouterPluginSender(self.name, self.screamrouter_in_queue,
                                               self.out_queue_list)
        self.has_ran = True
        self.load_plugin()

    def unload(self):
        """Unload the queue list and stop sending to ScreamRouter"""
        self.unload_plugin()
        self.sender.stop()

    def add_temporary_source(self, sink_name: SinkNameType,
                             source: SourceDescription) -> str:
        """Adds a temporary source, returns the source tag to put on the packets"""
        if not sink_name in self.temporary_sink_names_to_sources:
            self.temporary_sink_names_to_sources[sink_name] = []
        source.tag = f"{self.tag}"
        logger.info("Tag: %s", source.tag)
        source.name = f"{self.tag}"
        self.temporary_sink_names_to_sources[sink_name].append(source)
        self.temporary_source_tag_counter = self.temporary_source_tag_counter + 1
        self.wants_reload = True
        logger.info("Adding temporary source %s", source.tag)
        return source.tag


    def remove_temporary_source(self, source_name: SourceNameType):
        """Removes a temporary source, such as when playback is done"""
        for sources in self.temporary_sink_names_to_sources.values():
            for index, source in enumerate(sources):
                if source.name == source_name:
                    del sources[index]
                    self.wants_reload = True
                    logger.info("Removing temporary source %s", source.tag)
                    return

    def add_permanet_source(self, source: SourceDescription):
        """Adds a permanent source that shows up in the API and gets saved
           If your plugin tag changes the source will break.
           Overwrites existing sources with the same name.
           Permanent sources can be removed from the UI or API once they're no longer
           in use.
           """
        found: bool = False
        for index, permanent_source in enumerate(self.permanent_sources):
            if permanent_source.name == source.name:
                self.permanent_sources[index] = source
                found = True
        if not found:
            self.permanent_sources.append(source)
        self.wants_reload = True

    def start_plugin(self):
        """Empty function to be overridden by plugins"""

    def stop_plugin(self):
        """Empty function to be overridden by plugins"""

    def load_plugin(self):
        """Empty function to be overridden by plugins"""

    def unload_plugin(self):
        """Empty function to be overridden by plugins"""

    def run(self):
        """Empty function to be overridden by plugins"""

class ScreamRouterPluginSender(multiprocessing.Process):
    """Handles sending from a plugin to the ScreamRouter sources"""

    def __init__(self, name: str,
                 in_queue: multiprocessing.Queue,
                 out_queue_list: List[multiprocessing.Queue]):
        super().__init__(name=f"[Plugin Reader] {name}")
        set_process_name(f"PlgnRdr {name}", f"[Plugin Reader] {name}")
        self.name = name
        """This holds a list of queues listened to by the AudioControllers"""
        self.out_queue_list: List[multiprocessing.Queue] = out_queue_list
        """This holds a queue passed to the plugin process, it is monitored
           and sent to the AudioControllers."""
        self.in_queue: multiprocessing.Queue = in_queue
        """Flag if the thread should exit"""
        self.running = multiprocessing.Value(c_bool, True)
        self.start()

    def stop(self, force_join: bool = False):
        """Stop the plugin sender"""
        self.running.value = c_bool(False)
        if constants.KILL_AT_CLOSE:
            self.kill()
        if constants.WAIT_FOR_CLOSES or force_join:
            self.join()

    def run(self):
        logger.debug("[Plugin %s Sender] Thread PID %s",
                     self.name, self.out_queue_list)
        while self.running.value:
            try:
                in_packet: InputQueueEntry = self.in_queue.get(timeout=.3)
                for out_queue in self.out_queue_list:
                    out_queue.put(in_packet)
            except queue.Empty:
                pass
        logger.info("Ending Plugin Sender thread")
