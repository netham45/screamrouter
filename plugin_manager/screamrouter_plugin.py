"""Base class for Source plugins to extend"""
import multiprocessing
from typing import List

from fastapi import FastAPI

#from screamrouter_types.configuration import SinkDescription
#from screamrouter_types.annotations import PlaybackURLType, VolumeType
#from audio.receiver_thread import ReceiverThread
#from audio.scream_header_parser import ScreamHeader, create_stream_info
from configuration.configuration_manager import ConfigurationManager
from logger import get_logger
from screamrouter_types.annotations import SinkNameType
from screamrouter_types.configuration import SourceDescription

logger = get_logger(__name__)

class ScreamRouterPlugin():
    """This is the base class for a ScreamRouter plugin to extend"""
    def __init__(self):
        """Base class for ScreamRouterPlugin"""
        self.tag: str
        """This is the tag that will be looked for by the receiver queue parser
           for this plugin"""
        self.api: FastAPI
        """FastAPI object so plugins can add API calls to the /plugins/{plugin_type}/ path"""
        self.__out_queue: multiprocessing.Queue
        """This queue is read by PluginManager and written to the Receiver"""
        self.__configuration: ConfigurationManager
        """ConfigurationManager to call to make changes"""
        self.tag_counter: int = 0
        """Counter so each tag is unique"""

    def configure(self, tag: str,
                  api: FastAPI,
                  out_queue: multiprocessing.Queue,
                  configuration: ConfigurationManager):
        """Called from Plugin Manager, configures and starts the plugin"""
        self.tag = tag
        self.api = api
        self.__out_queue = out_queue
        self.__configuration = configuration
        self.initialize_api()

    def write_bytes(self, data: bytes):
        """Writes a packet to the output queue
           Wants 1152 bytes"""
        # Sample = one channel
        # Frame = samples for all active channels
        self.__out_queue.put(data)


    def initialize_api(self):
        """Blank function to be overriden by plugins.
           This should call FastAPI to add any endpoints you want."""

    def add_one_time_source(self, sinks: List[SinkNameType]) -> str:
        """Adds a one-time source that isn't saved to config files
           This source goes away when the config is reloaded and can't be saved or managed
           through the API"""
        tag: str = f"{self.tag}{self.tag_counter}"
        self.tag_counter = self.tag_counter + 1
        self.__configuration.add_one_time_source(SourceDescription(name=f"{tag}",
                                                                   tag=f"{tag}",
                                                                   has_no_header=True),
                                                                   sinks)
        return tag

    def notify_one_time_source_done(self, tag: str):
        """Notifies the controller a temporary source is done"""
        self.__configuration.remove_one_time_source(tag)
