# ScreamRouter Technical Overview

ScreamRouter is a multi-process audio routing and mixing system designed to receive audio streams, process them, and output them to multiple destinations. It uses a combination of Python for high-level management and C++ for performance-critical audio processing tasks.

## Key Components

1. Configuration Manager (`configuration_manager.py`)
2. Audio Controller (`audio_controller.py`) 
3. Scream Receiver (`scream_receiver.cpp`)
4. RTP Receiver (`rtp_receiver.cpp`)
5. Source Input Processor (`source_input_processor.cpp`)
6. Sink Output Mixer (`sink_audio_mixer.cpp`)

## Process Management and Relationships

The system uses a multi-process architecture to handle different aspects of audio processing:

1. The Configuration Manager is the main Python process that orchestrates the entire system.

2. The Scream Receiver and RTP Receiver are separate C++ processes that listen for incoming audio streams.

3. For each active source connected to a sink, a Source Input Processor C++ process is created.

4. Each sink has a Sink Output Mixer C++ process to combine audio from multiple sources.

## Data Flow

1. Incoming audio packets are received by the Scream Receiver or RTP Receiver.

2. These receivers tag the packets with the source IP and forward them to the appropriate Source Input Processor(s).

3. Source Input Processors handle audio format conversion, resampling, channel mixing, equalization, and volume adjustment.

4. Processed audio is sent to the Sink Output Mixer.

5. The Sink Output Mixer combines audio from multiple sources and outputs it to the configured destination (UDP, TCP, or MP3 stream).

## Key Features

- Dynamic configuration changes without restarting
- Support for various audio formats and channel layouts
- Real-time audio processing (resampling, mixing, EQ)
- Multiple input (Scream, RTP) and output (UDP, TCP, MP3 stream) methods

## Performance Considerations

- C++ is used for audio processing to minimize latency and maximize throughput
- SIMD instructions (AVX2, SSE2) are used where available for faster processing
- Inter-process communication is done via Unix pipes for efficiency

## Configuration

The system is configured via a YAML file, which defines sources, sinks, and routes. The Configuration Manager reads this file and sets up the necessary processes and connections.

## Extensibility

The plugin system allows for adding custom audio sources or processing modules without modifying the core system.

This architecture allows ScreamRouter to efficiently handle multiple audio streams with low latency while providing flexibility for various use cases and configurations.
