# RTP Audio Streaming with ScreamRouter

This guide explains how to set up and use the RTP audio streaming functionality with ScreamRouter and PulseAudio.

## How it Works

1. ScreamRouter acts as an RTP receiver, listening for incoming audio data.
2. PulseAudio is configured to send audio to ScreamRouter over RTP.
3. ScreamRouter processes the received audio and routes it to the appropriate output.
4. ScreamRouter will listen for incoming RTP packets with the correct format on port 40000.

## Configuring PulseAudio

To send audio from PulseAudio to ScreamRouter via RTP:

1. Load the RTP sender module in PulseAudio:

```bash
pactl load-module module-rtp-send format=s16le source=<source_name> destination=<ScreamRouter_IP> port=<ScreamRouter_Port> mtu=1164
```