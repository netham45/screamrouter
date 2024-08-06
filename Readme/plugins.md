# ScreamRouter Plugin System

ScreamRouter supports a flexible plugin system that allows extending its functionality through custom input sources. Plugins are implemented as separate processes that can generate PCM audio data and send it to ScreamRouter for routing and playback.

## Plugin Architecture

The plugin system is built around the following key components:

1. `ScreamRouterPlugin`: The base class for all plugins
2. `PluginManager`: Manages the lifecycle of registered plugins
3. `ScreamRouterPluginSender`: Handles sending audio data from plugins to ScreamRouter

### ScreamRouterPlugin

This is the base class that all plugins should extend. It provides the core functionality and lifecycle methods for plugins.

Key features:
- Runs as a separate process
- Provides methods for adding temporary and permanent sources
- Handles communication with ScreamRouter via pipes

Important methods to implement:
- `start_plugin()`: Called when the plugin is started. Use this to set up API endpoints and initialize resources.
- `stop_plugin()`: Called when the plugin is stopped. Use this for cleanup tasks.
- `load_plugin()`: Called when the configuration is (re)loaded.
- `unload_plugin()`: Called when the configuration is unloaded.

### PluginManager

The PluginManager is responsible for:
- Registering plugins
- Starting and stopping plugins
- Loading and unloading plugins when configuration changes
- Collecting temporary and permanent sources from all plugins

### ScreamRouterPluginSender

This class handles the actual sending of audio data from the plugin to ScreamRouter. It runs in a separate process to work around multiprocessing limitations.

## Plugin Lifecycle

1. Plugins are registered with the PluginManager
2. When ScreamRouter starts, all registered plugins are started
3. Plugins are loaded when the available source list changes
4. Plugins can be unloaded and reloaded as needed
5. When ScreamRouter stops, all plugins are stopped

## Creating a Plugin

To create a plugin:

1. Extend the `ScreamRouterPlugin` class
2. Implement the required lifecycle methods (`start_plugin`, `stop_plugin`, `load_plugin`, `unload_plugin`)
3. Use `add_temporary_source()` or `add_permanent_source()` to add audio sources
4. Use `write_data()` to send audio data to ScreamRouter

## Audio Sources

Plugins can add two types of sources:

1. Temporary sources: Not saved and don't persist across reloads
2. Permanent sources: Appear in the API and are saved

Use the appropriate method to add sources based on your plugin's needs.

## Example Plugins

1. `PluginPlayURL`: Implements an API endpoint to play audio from a URL on a specified sink
2. `PluginPlayURLMultiple`: Extends PluginPlayURL to allow playing multiple URLs simultaneously

These plugins demonstrate how to:
- Set up API endpoints
- Manage external processes (ffmpeg)
- Handle audio streaming
- Interact with ScreamRouter's routing system

## Best Practices

- Use unique tags for your plugin and sources
- Properly handle cleanup in `stop_plugin()` and `unload_plugin()`
- Use the provided logging system for debugging and information
- Be mindful of resource usage, especially when dealing with external processes or streaming
