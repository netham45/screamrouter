# **ScreamRouter**

![Screenshot of ScreamRouter](/images/ScreamRouter.png)

### What is ScreamRouter

ScreamRouter is a versatile audio routing and management system with a Python frontend/configuration layer and a C++ backend, designed for network audio streaming. It supports Scream and RTP audio sources, along with Scream receivers and web-based MP3 streamers. 

### What is Scream

[Scream](https://github.com/duncanthrax/scream) is a virtual audio driver for Windows developed by Tom Kistner. It sends PCM data out over UDP in a low-latency fashion.

### Key Features of ScreamRouter

#### Audio Routing and Configuration
* Configure Sources by IP address
* Set up Routes between Sources and Sinks
* Customize Sinks with Bit Depth, Sample Rate, Channel Layout, IP, and Port
* Group Sources and Sinks for simultaneous control and playback
* Volume control at every level: Source, Route, Sink, and Group

#### Audio Processing and Playback
* Custom mixer/equalizer/channel layout processor for minimal latency
* Adjustable equalization for any sink, route, source, or group
* MP3 stream exposure for browser-based listening of all sinks
* URL playback capability for sinks or sink groups
* Milkdrop Visualizations via [Butterchurn](https://github.com/jberg/butterchurn) project

#### Integration and Compatibility
* Home Assistant Custom Component for managing Sinks and media playback
* PulseAudio stream acceptance
* Embedded noVNC for remote computer control
* Docker containers for [Amazon Music](https://github.com/netham45/screamrouter-amazon-music-docker), [Firefox](https://github.com/netham45/screamrouter-firefox-docker), and [Spotify](https://github.com/netham45/screamrouter-spotify-docker)

#### System Management
* Automatic YAML saving on setting changes
* Flexible plugin system for easy addition of new sources
* API and web interface for media control commands to containers

#### Additional Tools and Receivers
* [ScreamSender](https://github.com/netham45/screamsender/): Linux tool for sending PCM data from the command line
* Playback sinks available for [Windows](https://github.com/duncanthrax/scream/tree/master/Receivers/dotnet-windows/ScreamReader), [Linux](https://github.com/duncanthrax/scream/tree/master/Receivers/unix), and [Android](https://github.com/martinellimarco/scream-android/tree/90d1364ee36dd12ec9d7d2798926150b370030f3)
* Custom receivers:
  - [ESP32 A1S Audiokit Dev boards](https://github.com/netham45/esp32-audiokit-screamreader/)
  - [ESP32/ESP32S with TOSLINK and USB UAC 1.0 output](https://github.com/netham45/esp32-scream-receiver/)
  - [Multi-platform Python receiver](https://github.com/netham45/pyscreamreader)

### Use Cases

ScreamRouter offers a versatile solution for various audio management scenarios, including:

* Comprehensive Whole-House Audio Systems:
   - Integrate multiple Scream Sources and Receivers for seamless audio distribution throughout your home.

* Advanced Volume Management:
   - Implement granular volume control at both group and individual sink levels, ensuring optimal audio balance across your setup.

* Web-Based Audio Access:
   - Utilize the 'Listen to Sink' feature to enable browser-based audio playback, enhancing accessibility and flexibility.

* Universal Sink Compatibility:
   - Leverage the exposed API to incorporate any streaming MP3 player as a sink, expanding your audio output options.

* Programmatic Audio Control:
   - Employ the FastAPI interface or Home Assistant integration for automated management of sink activation, deactivation, and volume adjustments.

* Home Automation Integration:
   - Seamlessly incorporate sound effects and Text-to-Speech functionality into Home Assistant automations for enhanced smart home experiences.

* Sound Quality Enhancement:
   - Utilize built-in equalization tools to optimize audio output, particularly beneficial for improving the performance of budget-friendly speakers.

## Documentation

For comprehensive information on various aspects of ScreamRouter, please refer to the following guides:

* [API Documentation](Readme/api.md)
  - Details on the ScreamRouter API endpoints and usage

* [Source Command Receiver](Readme/command_receiver.md)
  - Information about the command receiver module for remote control

* [Configuration Guide](Readme/configuration.md)
  - Instructions for configuring ScreamRouter and its components
* [Docker ScreamRouter Install Guide](Readme/docker-screamrouter.md)
  - Guide to running ScreamRouter in a Docker container

* [Docker Sources](Readme/docker-sources.md)
  - Information on Docker containers for various streaming services

* [Home Assistant Integration](Readme/homeassistant.md)
  - Instructions for integrating ScreamRouter with Home Assistant

* [Plugins System](Readme/plugins.md)
  - Documentation on the flexible plugin system for adding new sources

* [Audio Processor](Readme/processor.md)
  - Details on the custom mixer/equalizer/channel layout processor

* [RTP Source](Readme/rtp-source.md)
  - Guide to using RTP from PulseAudio as an audio source

* [Scream Source](Readme/scream-source.md)
  - Information on configuring and using Scream as an audio source

* [User Interface Guide](Readme/ui.md)
  - Documentation on using the ScreamRouter web interface

* [VNC Integration](Readme/vnc.md)
  - Guide to using the embedded noVNC for remote computer control

## Relevant Repos

### Docker Containers
* [Amazon Music Docker Container for ScreamRouter](https://github.com/netham45/screamrouter-amazon-music-docker)
* [Firefox Docker Container for ScreamRouter](https://github.com/netham45/screamrouter-firefox-docker)
* [Spotify Docker Container for ScreamRouter](https://github.com/netham45/screamrouter-spotify-docker)

### Receivers
* [ESP32/ESP32s spdif/USB UAC 1.0 audio receiver](https://github.com/netham45/esp32-scream-receiver)
* [ESP32 A1S Audiokit Receiver](https://github.com/netham45/esp32-audiokit-screamreader)
* [Python Scream Receiver](https://github.com/netham45/pyscreamreader)
* [Windows C# Receiver](https://github.com/duncanthrax/scream/tree/master/Receivers/dotnet-windows/ScreamReader)
* [Unix Receiver](https://github.com/duncanthrax/scream/tree/master/Receivers/unix)
* [Android Receiver](https://github.com/martinellimarco/scream-android/)

### Senders
* [Scream Windows Driver](https://github.com/duncanthrax/scream/)
* [Usermode Scream Sender for Windows](https://github.com/netham45/windows-scream-sender)
* [Tool for prepending Scream headers to PCM streams on Linux](https://github.com/netham45/screamsender)

## Installation Guide

See [The ScreamRouter Docker install guide](https://github.com/netham45/screamrouter/blob/main/Readme/docker-screamrouter.md)