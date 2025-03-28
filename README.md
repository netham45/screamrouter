# **ScreamRouter**

![Screenshot of ScreamRouter](/images/ScreamRouter.png)

## Table of Contents
1. [What is ScreamRouter](#what-is-screamrouter)
2. [Key Features](#key-features)
3. [Sources](#sources)
4. [Receivers](#receivers)
5. [Use Cases](#use-cases)
6. [Documentation](#documentation)
7. [Accessibility](#accessibility)
8. [Example Devices](#example-devices)

## What is ScreamRouter

ScreamRouter is a versatile audio routing and management system with a Python frontend/configuration layer and C++ backend, designed for network audio streaming. It supports Scream and RTP audio sources, along with Scream receivers and web-based MP3 streamers.

## Key Features

### Audio Routing and Configuration
- Configure Sources by IP address.
- Set up Routes between Sources and Sinks.
- Customize Sinks with Bit Depth, Sample Rate, Channel Layout, IP, and Port.
- Group Sources and Sinks for simultaneous control and playback.
- Volume control at every level: Source, Route, Sink, and Group.

### Audio Processing and Playback
- Custom mixer/equalizer/channel layout processor for minimal latency, implemented in both Python and C++.
- Adjustable equalization for any sink, route, source, or group.
- MP3 stream exposure for browser-based listening of all sinks.
- URL playback capability for sinks or sink groups.
- Milkdrop Visualizations via [Butterchurn](https://github.com/jberg/butterchurn) project.
- Keeps a rolling buffer of streams that can be used for time shifting/rewinding streams.

### Integration and Compatibility
- Home Assistant Custom Component for managing configuration and media playback.
- Embedded noVNC for remote computer control.

### System Management
- Automatic YAML saving on setting changes.
- Flexible plugin system for easy addition of new sources and functionalities.
- API and web interface for media control commands to containers.
- Advanced configuration management and solving system.
- Comprehensive logging system.

## Sources
- RTP/Linux Source
- Windows Source
- ESP32S3 USB Audio Card and Toslink Senders
- Raspberry Pi Zero W / Raspberry Pi Zero 2 USB Gadget Sound Card Sender
- Amazon Music Docker Container
- Firefox Docker Container
- Spotify Docker Container
- Plugin system, including the ability to play arbitrary URLs out.

## Receivers
- Windows Receivers
- Raspberry Pi/Linux Receiver
- ESP32/ESP32s3 spdif/USB UAC 1.0 audio receiver
- ESP32 A1S Audiokit Receiver
- Android Receiver
- Python Scream Receiver

## Use Cases

### Comprehensive Whole-House Audio Systems
Integrate multiple Scream Sources and Receivers for seamless audio distribution throughout your home.

### Advanced Volume Management
Implement granular volume control at both group and individual sink levels, ensuring optimal audio balance across your setup.

### Web-Based Audio Access
Utilize the 'Listen to Sink' feature to enable browser-based audio playback, enhancing accessibility and flexibility.

### Universal Sink Compatibility
PCM receivers are available for most popular platforms, and for ones that can't receive PCM they can use the MP3 stream.

### Programmatic Audio Control
Employ the FastAPI interface or Home Assistant integration for automated management of sink activation, deactivation, and volume adjustments.

### Home Assistant Integration
Incorporate sound effects and Text-to-Speech functionality into Home Assistant automations and play them from any speaker in your house for an enhanced smart home experience.

### Sound Quality Enhancement
Utilize built-in equalization tools to optimize audio output, particularly beneficial for improving the performance of budget-friendly speakers.

### Time-Shifting and Audio Buffering
Maintains a rolling buffer of the most recent 5 minutes of audio for each stream allowing rewinding to and replaying audio within the buffered timeframe.

## Documentation

All documentation is available at [https://screamrouter.net](https://screamrouter.net)

## Accessibility

ScreamRouter aims to be accessible to those with visual impairments. To this effect, the UI has been designed with tooltips and alt tags on every element and can be used from a keyboard.

If you encounter any issues affecting accessibility, please file an issue on GitHub.

## Example Devices

### ESP32S3 Portable Receiver

![Portable ESP32S3 Scream Receiver](/images/esp32s3_receiver.jpg)
