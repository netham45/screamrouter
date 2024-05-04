"""Models for the plugin_manager"""

from pydantic import BaseModel


class SourcePluginDescription(BaseModel):
    """This describes a plugin"""
    name: str
    """Plugin name"""
    description: str
    """Plugin description"""

class PluginManagerInfo(BaseModel):
    """This is passed from the Plugin Manager to a Plugin and contains basic configuration
       information"""
    pipes_dir: str
    """Directory that stores pipes"""
    logs_dir: str
    """Directory that stores logs"""
    plugin_data: dict
    """Dictionary that stores custom plugin data"""

class PluginManagerOutputPacket(BaseModel):
    """Message passed from plugins to PluginManager for each PCM frame"""
    tag: str
    """Plugin Tag"""
    data: bytes
    """PCM Data with a Scream header"""
