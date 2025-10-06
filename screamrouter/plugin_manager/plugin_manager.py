"""ScreamRouter Plugin Manager"""
from typing import Any, List  # Added Any for audio_manager_instance

from fastapi import FastAPI

from screamrouter.plugin_manager.screamrouter_plugin import ScreamRouterPlugin
from screamrouter.plugins.play_url import PluginPlayURL
from screamrouter.plugins.play_url_multiple import PluginPlayURLMultiple
from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
from screamrouter.screamrouter_types.annotations import SinkNameType
from screamrouter.screamrouter_types.configuration import SourceDescription

# Attempt to import the C++ audio engine; this might fail if not compiled/installed
try:
    from screamrouter_audio_engine import AudioManager  # type: ignore
except ImportError:
    AudioManager = None # type: ignore
    get_logger(__name__).warning("PluginManager: Failed to import screamrouter_audio_engine. Plugins requiring it may not function correctly.")


logger = get_logger(__name__)

class PluginManager:
    """This implements the plugin manager that starts/stops/loads/unloads plugins
       Start/Stop = ScreamRouter starting/stopping
       load/unload = load/unload for configuration changes"""

    def __init__(self, api: FastAPI, audio_manager_instance: Any = None): # Added audio_manager_instance
        """Initialize the Plugin Manager"""
        self.api: FastAPI = api
        self.audio_manager_instance: Any = audio_manager_instance # Store the instance
        if not self.audio_manager_instance and AudioManager:
            logger.warning("PluginManager initialized without an AudioManager instance, but the module is available. "
                           "Plugins might attempt to use a global/fallback which is not ideal.")
        elif not AudioManager:
            logger.error("PluginManager: AudioManager (screamrouter_audio_engine) not available. "
                         "Plugins relying on direct C++ calls will fail.")

        self.plugin_list: List[ScreamRouterPlugin] = []
        self.register_plugin(PluginPlayURL())
        self.register_plugin(PluginPlayURLMultiple())
        self._wants_reload: bool = False

    def register_plugin(self, plugin: ScreamRouterPlugin):
        """Registers a plugin with the Plugin Manager"""
        self.plugin_list.append(plugin)

    def start_registered_plugins(self):
        """Plugins are started when ScreamRouter is started"""
        for plugin in self.plugin_list:
            # Pass the AudioManager instance to the plugin's start method
            plugin.plugin_start(self.api, self.audio_manager_instance)

    def stop_registered_plugins(self):
        """Plugins are stopped when ScreamRouter is stopped"""
        for plugin in self.plugin_list:
            plugin.stop()

    def load_registered_plugins(self, controller_write_fds: List[int]):
        """Plugins are loaded when the available source list changes"""
        for plugin in self.plugin_list:
            plugin.load(controller_write_fds)

    def unload_registered_plugins(self):
        """Plugins are unloaded when the available source list changes"""
        for plugin in self.plugin_list:
            plugin.unload()

    def get_temporary_sources(self) -> dict[SinkNameType, List[SourceDescription]]:
        """Returns temporary sources from all plugins"""
        return_dict: dict[SinkNameType, List[SourceDescription]] = {}
        for plugin in self.plugin_list:
            for sink_name, sources in plugin.temporary_sink_names_to_sources.items():
                if sink_name in return_dict:
                    return_dict[sink_name].extend(sources)
                else:
                    return_dict[sink_name] = sources
        return return_dict

    def get_permanent_sources(self) -> List[SourceDescription]:
        """Returns permanent sources from all plugins"""
        return_list: List[SourceDescription] = []
        for plugin in self.plugin_list:
            return_list.extend(plugin.permanent_sources)
        return return_list

    def want_reload(self):
        """Sets the plugin manager as wanting a reload"""
        self._wants_reload = True

    def wants_reload(self, reset: bool = True):
        """Returns if any plugins want a reload, resets the reload flag"""
        wants_reload: bool = False
        for plugin in self.plugin_list:
            if plugin.wants_reload:
                if reset:
                    plugin.wants_reload = False
                logger.info("Plugin %s wants reload", plugin.name)
                wants_reload = True
        if self._wants_reload:
            if reset:
                self._wants_reload = False
            wants_reload = True
        if not reset:
            self._wants_reload = wants_reload

        return wants_reload
