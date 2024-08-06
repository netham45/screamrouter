# Scream Audio Streaming

Scream is a virtual audio driver and protocol for streaming audio over a network with very low latency.

## Overview

Scream consists of two main components:

1. A virtual audio driver for Windows that captures audio output 
2. A receiver program that plays the streamed audio on the receiving device. ScreamRouter acts as an intermediary between the driver and receiver to allow mixing, equalization, delays, source copying, etc...

The Scream driver captures raw PCM audio data from Windows and sends it over the network as UDP packets. The receiver listens for these packets and plays them back through the local audio device.

## Scream Protocol

The Scream protocol is very simple, which allows for minimal overhead and very low latency:

- Audio data is sent as raw PCM samples in UDP packets
- A small 5-byte header is prepended to each packet with audio format information.
- Default multicast address is 239.255.77.77, port 4010
- Packet payload is 1152 bytes of audio data

The 5-byte header contains:

1. Sampling rate (byte 1)
2. Sample size in bits (byte 2)  
3. Number of channels (byte 3)
4. Channel mask (bytes 4-5) (WAVEFORMATEX)

## Scream Receiver

The Scream receiver program performs the following key functions:

- Listens on the configured UDP port for incoming Scream audio packets
- Parses the 5-byte header to determine audio format
- Buffers incoming audio data to handle network jitter
- Outputs the raw PCM data to the local audio device for playback

The receivers are designed to be very lightweight and have minimal processing overhead to maintain the low latency of the Scream system.

By using raw PCM data and a simple protocol, Scream achieves very low latency audio streaming over a LAN, typically under 10ms. This makes it suitable for applications like virtual sound cards in virtual machines or streaming audio between rooms in a house.
