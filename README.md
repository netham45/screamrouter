# **ScreamRouter**

![Screenshot of ScreamRouter](/images/ScreamRouter.png)

[New Readme](Readme/readme.md)

### What is it
ScreamRouter is a Python-based audio router for Scream sources and sinks. It allows you to enter an IP address for all of your Scream audio sources and IP address, port, bit depth, sample rate, and channel configuration for all of your sinks. It has a web interface for managing the configuration and listening to sinks.

### What is Scream
[Scream is a virtual audio driver for Windows](https://github.com/duncanthrax/scream) created by Tom Kistner. It operates by sending any received PCM data from Windows's sound engine over the network unprocessed. This allows very low latency. Each packet from Scream is 1152 bytes worth of PCM from Windows's audio engine with a 5 byte header that describes the format of the stream.

The simplicity of Scream allows it to be very easy to work with while retaining low latency.

### Features
* Configure Sources based off of IP address, Configure Routes between Sources and Sinks, Configure Sinks with Bit Depth, Sample Rate, Channel Layout, Sink IP, and Port
* Group together Sources and Sinks to control and play to multiple at a time
* Control the volume for each Source, Route, Sink, and Group the stream passes through
* Exposes an MP3 stream of all sinks so they can be listened to in a browser
* Can play a URL out of a sink or sink group
* Has a Home Assistant Custom Component for managing Sinks and playing media back through Sinks (See: https://github.com/netham45/screamrouter_ha_component )
* Automatically saves to YAML on setting change
* Uses a custom mixer/equalizer/channel layout up/downmixer so latency can be minimized
* Can adjust equalization for any sink, route, source, or group
* Contains a plugin system to easily allow additional sources to be added
* Milkdrop Visualizations thanks to the browser-based [Butterchurn](https://github.com/jberg/butterchurn) project
* There are playback sinks available for common OSes such as [Windows](https://github.com/duncanthrax/scream/tree/master/Receivers/dotnet-windows/ScreamReader), [Linux](https://github.com/duncanthrax/scream/tree/master/Receivers/unix), and [Android](https://github.com/martinellimarco/scream-android/tree/90d1364ee36dd12ec9d7d2798926150b370030f3). I've written one for [ESP32 A1S Audiokit Dev boards](https://github.com/netham45/esp32-audiokit-screamreader/), and [one for esp32/esp32s that does toslink and usb uac 1.0 output](https://github.com/netham45/esp32-scream-receiver/) and I've written a multi-platform receiver in [Python](https://github.com/netham45/pyscreamreader).
* Embedded noVNC for controlling remote computers playing music
* Can accept streams from PulseAudio
* Docker containers to add support for [Amazon Music](https://github.com/netham45/screamrouter-amazon-music-docker), [Firefox](https://github.com/netham45/screamrouter-firefox-docker), [Spotify](https://github.com/netham45/screamrouter-spotify-docker)
* Linux tool for sending PCM data from the command line [ScreamSender](https://github.com/netham45/screamsender/)
* Ability to send media control commands to containers from API and web interface

![Screenshot of ScreamRouter noVNC](/images/noVNC.png)

### Use Cases
* Mixing one or many Scream Sources to one or many Scream Receivers for a whole-house audio setup
* Changing the volume of groups of sinks at once while having each sink also individually leveled
* Use a web browser as a sink via the 'Listen to Sink' feature
* Use any streaming MP3 player as a sink via the exposed API
* Programatically enable/disable sinks, or adjust the volume through the FastAPI API, or through Home Assistant
* Play back sound effects and Text to Speech to arbitrary Sinks from Home Assistant automations
* Line up speakers with differing timings by delaying one
* Adjusting EQ for a cheap speaker so it sounds better

![Screenshot of HA for ScreamRouter](/images/HAMediaPlayer.png)
## Documentation

For comprehensive information on various aspects of ScreamRouter, please refer to the following guides:

* [API Documentation](Readme/api.md): Details on the ScreamRouter API endpoints and usage
* [Source Command Receiver](Readme/command_receiver.md): Information about the command receiver module for remote control
* [Configuration Guide](Readme/configuration.md): Instructions for configuring ScreamRouter and its components
* [Docker ScreamRouter Install Guide](Readme/docker-screamrouter.md): Guide to running ScreamRouter in a Docker container
* [Docker Sources](Readme/docker-sources.md): Information on Docker containers for various streaming services
* [Home Assistant Integration](Readme/homeassistant.md): Instructions for integrating ScreamRouter with Home Assistant
* [Plugins System](Readme/plugins.md): Documentation on the flexible plugin system for adding new sources
* [Audio Processor](Readme/processor.md): Details on the custom mixer/equalizer/channel layout processor
* [RTP Source](Readme/rtp-source.md): Guide to using RTP from PulseAudio as an audio source
* [Scream Source](Readme/scream-source.md): Information on configuring and using Scream as an audio source
* [User Interface Guide](Readme/ui.md): Documentation on using the ScreamRouter web interface
* [VNC Integration](Readme/vnc.md): Guide to using the embedded noVNC for remote computer control

## Prerequisites

- Scream (configured for UDP unicast on port 16401)
- Python requirements (see requirements.txt)
- Linux environment
- SSL certificate (path configurable in src/constants/constants.py)

## Quick Start

1. Install Docker
2. Build the ScreamRouter Docker Container
3. Run the ScreamRouter Docker Container
4. Access the web interface on port 443
5. Add Sources, Sinks, and Routes as needed