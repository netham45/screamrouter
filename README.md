# **ScreamRouter**

![Screenshot of ScreamRouter](/images/ScreamRouter.png)

### What is ScreamRouter

ScreamRouter is a versatile audio routing and management system with a Python frontend/configuration layer and a C++ backend, designed for network audio streaming. It supports Scream and RTP audio sources, along with Scream receivers and web-based MP3 streamers. 

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
* Home Assistant Custom Component for managing configuration and media playback
* Compatible with PulseAudio RTP streams
* Embedded noVNC for remote computer control
* Docker containers for [Amazon Music](https://github.com/netham45/screamrouter-amazon-music-docker), [Firefox](https://github.com/netham45/screamrouter-firefox-docker), and [Spotify](https://github.com/netham45/screamrouter-spotify-docker)

#### System Management
* Automatic YAML saving on setting changes
* Flexible plugin system for easy addition of new sources
* API and web interface for media control commands to containers

### Use Cases

#### Comprehensive Whole-House Audio Systems

Integrate multiple Scream Sources and Receivers for seamless audio distribution throughout your home.

#### Advanced Volume Management

Implement granular volume control at both group and individual sink levels, ensuring optimal audio balance across your setup.

#### Web-Based Audio Access

Utilize the 'Listen to Sink' feature to enable browser-based audio playback, enhancing accessibility and flexibility.

#### Universal Sink Compatibility

PCM receivers are available for most popular platforms, and for ones that can't receive PCM they can use the MP3 stream.

#### Programmatic Audio Control

Employ the FastAPI interface or Home Assistant integration for automated management of sink activation, deactivation, and volume adjustments.

#### Home Assistant Integration

Seamlessly incorporate sound effects and Text-to-Speech functionality into Home Assistant automations for enhanced smart home experiences.

#### Sound Quality Enhancement

Utilize built-in equalization tools to optimize audio output, particularly beneficial for improving the performance of budget-friendly speakers.

## Documentation

### Install/Configuration

* [Docker ScreamRouter Install Guide](Readme/docker-screamrouter.md)
  - Guide to running ScreamRouter in a Docker container

* [Debian ScreamRouter Install Guide](/Readme/debian-screamrouter.md)
  - Guide to running ScreamRouter on Debian

### General Information

* [Audio Processor](Readme/processor.md)
  - Details on the custom mixer/equalizer/channel layout processor

* [Configuration](Readme/configuration.md)
  - Description of ScreamRouter's routing configuration

### Senders

* [RTP/Linux Source](Readme/rtp-source.md)
  - Guide to using RTP from PulseAudio and Pipewire as an audio source

* [Windows Source](Readme/scream-source.md)
  - Information on configuring a Windows audio source

* [Docker Sources](Readme/docker-sources.md)
  - Information on Docker containers for various streaming services

* [ESP32S3 USB Audio Card and Toslink Senders](Readme/esp32-scream-sender.md)
  - Information on using an ESP32S3 as a USB sound card to stream to Scream, or an ESP32 as a TOSLINK bridge for Scream

* [Raspberry Pi Zero W / Raspberry Pi Zero 2 USB Gadget Sound Card Sender](Readme/raspberry-pi-zero-screamsender.md)
  - Information on configuring a Raspberry Pi Zero W / Raspberry Pi Zero 2 as a USB Gadget Sound Card that forwards to ScreamRouter

#### Docker Containers
* [Amazon Music Docker Container for ScreamRouter](https://github.com/netham45/screamrouter-amazon-music-docker)
  - Repo for running Amazon Music as a Docker container that outputs over RTP to ScreamRouter

* [Firefox Docker Container for ScreamRouter](https://github.com/netham45/screamrouter-firefox-docker)
  - Repo for running Firefox as a Docker container that outputs over RTP to ScreamRouter

* [Spotify Docker Container for ScreamRouter](https://github.com/netham45/screamrouter-spotify-docker)
  - Repo for running Sporify in a Docker container that outputs over RTP to ScreamRouter

### Receivers

* [Windows Receivers](Readme/windows-scream-receiver.md)
  - Information on configuring a Windows audio receiver

* [Raspberry Pi/Linux Receiver](Readme/raspberry-pi-receiver.md)
  - Information on configuring a Raspberry Pi or other Linux system as a Scream audio receiver and have it start on boot

* [ESP32/ESP32s3 spdif/USB UAC 1.0 audio receiver](https://github.com/netham45/esp32-scream-receiver)
  - Repo for an esp32s3 spdif/USB UAC 1.0 audio receiver

* [ESP32 A1S Audiokit Receiver](https://github.com/netham45/esp32-audiokit-screamreader)
  - Repo for an ESP32 A1S Audiokit receiver

* [Android Receiver](https://github.com/martinellimarco/scream-android/)
  - Repo for an Android receiver

* [Python Scream Receiver](https://github.com/netham45/pyscreamreader)
  - Repo for a Python receiver

### UI

* [User Interface Guide](Readme/ui.md)
  - Documentation on using the ScreamRouter web interface

* [Chrome App](Readme/chrome-app-manifest.md)
  - Guide to installing it as an app using Chrome

* [VNC Integration](Readme/vnc.md)
  - Guide to using the embedded noVNC for remote computer control

### API

* [API Documentation](Readme/api.md)
  - Details on the ScreamRouter API endpoints and usage

* [Plugins System](Readme/plugins.md)
  - Documentation on the flexible plugin system for adding new sources

* [Source Command Receiver](Readme/command_receiver.md)
  - Information about the command receiver module for remote control

### Home Assistant

* [Home Assistant Integration](Readme/homeassistant.md)
  - Instructions for integrating ScreamRouter with Home Assistant

## Accessibility

ScreamRouter is aiming to be accessible to those with visual impairments. To this effect the UI has been designed with tooltips and alt tags on every element and can be used from a keyboard.

If you encounter any issues affecting accessibility please file an issue on Github.
