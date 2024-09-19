# ScreamRouter Plugin System

ScreamRouter's plugin system allows users to extend its functionality by adding custom input sources. Plugins can generate PCM audio data and send it to ScreamRouter for routing and playback, enabling integration with various audio sources and services.

## Plugin Architecture

The plugin system consists of three main components:

1. `ScreamRouterPlugin`: Base class for all plugins
2. `PluginManager`: Manages plugin lifecycle
3. `ScreamRouterPluginSender`: Handles audio data transmission

### ScreamRouterPlugin

Key features:
- Runs as a separate process
- Provides methods for adding temporary and permanent sources
- Handles communication with ScreamRouter via pipes

Important methods to implement:
- `start_plugin()`: Plugin initialization
- `stop_plugin()`: Cleanup tasks
- `load_plugin()`: Configuration (re)loading
- `unload_plugin()`: Configuration unloading

### PluginManager

Responsibilities:
- Registering, starting, and stopping plugins
- Loading and unloading plugins on configuration changes
- Collecting temporary and permanent sources

### ScreamRouterPluginSender

Handles audio data transmission from plugins to ScreamRouter in a separate process.

## Creating a Plugin

To create a plugin:

1. Extend the `ScreamRouterPlugin` class
2. Implement required lifecycle methods
3. Use `add_temporary_source()` or `add_permanent_source()` to add audio sources
4. Use `write_data()` to send audio data to ScreamRouter

## Audio Sources

Plugins can add two types of sources:
1. Temporary sources: Not saved, don't persist across reloads
2. Permanent sources: Appear in the API and are saved

## Example Plugins

1. `PluginPlayURL`: Plays audio from a URL on a specified sink
2. `PluginPlayURLMultiple`: Plays multiple URLs simultaneously

These plugins demonstrate:
- Setting up API endpoints
- Managing external processes (ffmpeg)
- Handling audio streaming
- Interacting with ScreamRouter's routing system

## Best Practices

- Use unique tags for your plugin and sources
- Handle cleanup properly in `stop_plugin()` and `unload_plugin()`
- Use the provided logging system for debugging
- Be mindful of resource usage, especially with external processes or streaming

## Troubleshooting

If you encounter issues with plugins:

1. Check the ScreamRouter logs for any error messages related to the plugin.
2. Verify that the plugin is correctly installed in the `plugins` directory.
3. Ensure the plugin is properly configured in the ScreamRouter configuration file.
4. If using external dependencies (e.g., ffmpeg), make sure they are installed and accessible.

For more information about ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.
