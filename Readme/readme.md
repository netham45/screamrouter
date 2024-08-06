# ScreamRouter

ScreamRouter is a Python-based audio routing system designed for Scream sources and sinks. It enables efficient management and routing of audio streams across your network through an intuitive web interface.

## Key Features

- Configure and manage Sources, Routes, and Sinks with customizable parameters
- Group Sources and Sinks for easier management
- Fine-tune audio with volume and equalizer controls for each Source, Route, Sink, and Group
- Expose MP3 streams for browser-based listening
- Play audio from URLs through Sinks or Sink Groups
- Seamless Home Assistant integration (Custom Component)
- Automatic YAML configuration saving
- Custom mixer/equalizer/channel layout processor for minimal latency
- Flexible plugin system for easy source additions
- Immersive Milkdrop Visualizations using Butterchurn
- Embedded noVNC for remote computer control
- PulseAudio stream acceptance
- Docker containers for various streaming services
- Linux tool (ScreamSender) for sending PCM data
- Media control commands via API and web interface

## Documentation

For comprehensive information on various aspects of ScreamRouter, please refer to the following guides:

* [API Documentation](api.md): Details on the ScreamRouter API endpoints and usage
* [Source Command Receiver](command_receiver.md): Information about the command receiver module for remote control
* [Configuration Guide](configuration.md): Instructions for configuring ScreamRouter and its components
* [Docker ScreamRouter Install Guide](docker-screamrouter.md): Guide to running ScreamRouter in a Docker container
* [Docker Sources](docker-sources.md): Information on Docker containers for various streaming services
* [Home Assistant Integration](homeassistant.md): Instructions for integrating ScreamRouter with Home Assistant
* [Plugins System](plugins.md): Documentation on the flexible plugin system for adding new sources
* [Audio Processor](processor.md): Details on the custom mixer/equalizer/channel layout processor
* [RTP Source](rtp-source.md): Guide to using RTP from PulseAudio as an audio source
* [Scream Source](scream-source.md): Information on configuring and using Scream as an audio source
* [User Interface Guide](ui.md): Documentation on using the ScreamRouter web interface
* [VNC Integration](vnc.md): Guide to using the embedded noVNC for remote computer control

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