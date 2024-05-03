"""Plugin Manager"""
import multiprocessing
import multiprocessing.connection
import multiprocessing.queues
import threading

from fastapi import FastAPI

from configuration.configuration_manager import ConfigurationManager
from plugin_manager.screamrouter_plugin import ScreamRouterPlugin
from plugins.ffmpeg_play_url import FFMpegPlayURL

class PluginManagerThread(threading.Thread):
    """Manages loading plugins"""
    def __init__(self, api: FastAPI, configuration: ConfigurationManager):
        super().__init__(name="Plugin Manager")
        self.queue: multiprocessing.Queue = multiprocessing.Queue()
        self.api: FastAPI = api
        self.configuration: ConfigurationManager = configuration
        self.last_write_time: float = 0
        """Time of last write"""
        self.load_plugins()
        self.start()

    def load_plugins(self) -> None:
        """Load plugins"""
        plugin: ScreamRouterPlugin = FFMpegPlayURL()
        plugin.configure("FFMpegPlayURL",
                         self.api,
                         self.queue,
                         self.configuration)

    def run(self) -> None:
        """Monitor queues and push any received data to the receiver"""
        while True:
            self.configuration.receiver.add_packet_to_queue("FFMpegPlayURL0",
                                                                self.queue.get())
